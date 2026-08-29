//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements conversion from DSDL dialect ops to EmitC constructs.
///
/// The pass lowers dialect-specific control flow and bit operations into EmitC-compatible forms for C code emission.
///
/// The line-building concatenations here carry NOLINT for
/// performance-inefficient-string-concatenation. Each one spells out a line of generated
/// source, and an append sequence would cost the reader the line itself.
///
//===----------------------------------------------------------------------===//

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/ErrorHandling.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/Diagnostics.h>
#include <mlir/IR/DialectRegistry.h>
#include <mlir/IR/Location.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/OperationSupport.h>
#include <mlir/IR/Region.h>
#include <mlir/Pass/PassRegistry.h>
#include <mlir/Transforms/DialectConversion.h>
#include <mlir/Transforms/GreedyPatternRewriteDriver.h>
#include <mlir/Support/LLVM.h>
#include <algorithm>
#include <ranges>
#include <cstdint>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <cstddef>
#include <optional>
#include <cstring>
#include <memory>
#include <utility>

#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/IR/DSDLTypes.h"
#include "llvmdsdl/Transforms/LoweredSerDesContract.h"
#include "llvmdsdl/Transforms/LoweredSerDesContractValidation.h"
#include "llvmdsdl/Transforms/Passes.h"
#include "llvmdsdl/CodeGen/CodegenDiagnosticText.h"
#include "llvmdsdl/CodeGen/SourceWriter.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include <mlir/Dialect/EmitC/IR/EmitC.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/Pass/Pass.h>

namespace llvmdsdl
{
namespace
{

SourceWriter makeEmitCWriter(std::ostringstream& out)
{
    return SourceWriter{out, IndentPolicy::spaces(2)};
}

std::int64_t nonNegative(std::int64_t value)
{
    return std::max<std::int64_t>(value, 0);
}

bool isVariableArrayKind(llvm::StringRef arrayKind)
{
    return arrayKind == "variable_inclusive" || arrayKind == "variable_exclusive";
}

bool isSupportedArrayKind(llvm::StringRef arrayKind)
{
    return arrayKind == "none" || arrayKind == "fixed" || isVariableArrayKind(arrayKind);
}

std::int64_t ioStepBits(mlir::Operation* ioOp)
{
    if (auto bits = ioOp->getAttrOfType<mlir::IntegerAttr>("lowered_bits"))
    {
        return nonNegative(bits.getInt());
    }
    if (auto bits = ioOp->getAttrOfType<mlir::IntegerAttr>("max_bits"))
    {
        return nonNegative(bits.getInt());
    }
    return 0;
}

enum class PlanStepKind
{
    Align,
    Padding,
    Field
};

struct PlanStep final
{
    PlanStepKind kind{PlanStepKind::Field};
    std::int64_t bits{0};
    std::string  name;
    std::string  cName;
    std::string  scalarCategory;
    std::string  castMode;
    std::string  arrayKind;
    std::int64_t bitLength{0};
    std::int64_t arrayCapacity{0};
    std::int64_t arrayLengthPrefixBits{0};
    std::int64_t alignmentBits{1};
    std::int64_t unionOptionIndex{0};
    std::int64_t unionTagBits{0};
    std::string  compositeCTypeName;
    std::string  serUnsignedHelper;
    std::string  deserUnsignedHelper;
    std::string  serSignedHelper;
    std::string  deserSignedHelper;
    std::string  serFloatHelper;
    std::string  deserFloatHelper;
    std::string  serArrayLengthPrefixHelper;
    std::string  deserArrayLengthPrefixHelper;
    std::string  arrayLengthValidateHelper;
    std::string  delimiterValidateHelper;
    bool         compositeSealed{true};
    std::int64_t compositeExtentBits{0};
};

std::vector<PlanStep> collectPlanSteps(mlir::Operation* plan)
{
    std::vector<PlanStep> steps;
    if (plan->getNumRegions() == 0 || plan->getRegion(0).empty())
    {
        return steps;
    }
    for (mlir::Operation& op : plan->getRegion(0).front())
    {
        if (op.getName().getStringRef() == "dsdl.align")
        {
            const auto bits = op.getAttrOfType<mlir::IntegerAttr>("bits");
            PlanStep   alignStep;
            alignStep.kind = PlanStepKind::Align;
            alignStep.bits = bits ? nonNegative(bits.getInt()) : 1;
            steps.push_back(std::move(alignStep));
            continue;
        }
        if (op.getName().getStringRef() != "dsdl.io")
        {
            continue;
        }

        const std::int64_t bits     = ioStepBits(&op);
        const auto         kindAttr = op.getAttrOfType<mlir::StringAttr>("kind");
        const auto         nameAttr = op.getAttrOfType<mlir::StringAttr>("name");
        const auto         kind     = kindAttr ? kindAttr.getValue() : llvm::StringRef("field");
        if (kind == "padding")
        {
            steps.push_back(
                PlanStep{PlanStepKind::Padding,
                         bits,
                         nameAttr ? nameAttr.getValue().str() : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("c_name")
                             ? op.getAttrOfType<mlir::StringAttr>("c_name").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("scalar_category")
                             ? op.getAttrOfType<mlir::StringAttr>("scalar_category").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("cast_mode")
                             ? op.getAttrOfType<mlir::StringAttr>("cast_mode").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("array_kind")
                             ? op.getAttrOfType<mlir::StringAttr>("array_kind").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::IntegerAttr>("bit_length")
                             ? op.getAttrOfType<mlir::IntegerAttr>("bit_length").getInt()
                             : 0,
                         op.getAttrOfType<mlir::IntegerAttr>("array_capacity")
                             ? op.getAttrOfType<mlir::IntegerAttr>("array_capacity").getInt()
                             : 0,
                         op.getAttrOfType<mlir::IntegerAttr>("array_length_prefix_bits")
                             ? op.getAttrOfType<mlir::IntegerAttr>("array_length_prefix_bits").getInt()
                             : 0,
                         op.getAttrOfType<mlir::IntegerAttr>("alignment_bits")
                             ? op.getAttrOfType<mlir::IntegerAttr>("alignment_bits").getInt()
                             : 1,
                         op.getAttrOfType<mlir::IntegerAttr>("union_option_index")
                             ? op.getAttrOfType<mlir::IntegerAttr>("union_option_index").getInt()
                             : 0,
                         op.getAttrOfType<mlir::IntegerAttr>("union_tag_bits")
                             ? op.getAttrOfType<mlir::IntegerAttr>("union_tag_bits").getInt()
                             : 0,
                         op.getAttrOfType<mlir::StringAttr>("composite_c_type_name")
                             ? op.getAttrOfType<mlir::StringAttr>("composite_c_type_name").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("lowered_ser_unsigned_helper")
                             ? op.getAttrOfType<mlir::StringAttr>("lowered_ser_unsigned_helper").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("lowered_deser_unsigned_helper")
                             ? op.getAttrOfType<mlir::StringAttr>("lowered_deser_unsigned_helper").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("lowered_ser_signed_helper")
                             ? op.getAttrOfType<mlir::StringAttr>("lowered_ser_signed_helper").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("lowered_deser_signed_helper")
                             ? op.getAttrOfType<mlir::StringAttr>("lowered_deser_signed_helper").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("lowered_ser_float_helper")
                             ? op.getAttrOfType<mlir::StringAttr>("lowered_ser_float_helper").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("lowered_deser_float_helper")
                             ? op.getAttrOfType<mlir::StringAttr>("lowered_deser_float_helper").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("lowered_ser_array_length_prefix_helper")
                             ? op.getAttrOfType<mlir::StringAttr>("lowered_ser_array_length_prefix_helper")
                                   .getValue()
                                   .str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("lowered_deser_array_length_prefix_helper")
                             ? op.getAttrOfType<mlir::StringAttr>("lowered_deser_array_length_prefix_helper")
                                   .getValue()
                                   .str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("lowered_array_length_validate_helper")
                             ? op.getAttrOfType<mlir::StringAttr>("lowered_array_length_validate_helper")
                                   .getValue()
                                   .str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("lowered_delimiter_validate_helper")
                             ? op.getAttrOfType<mlir::StringAttr>("lowered_delimiter_validate_helper").getValue().str()
                             : std::string{}});
            if (auto sealed = op.getAttrOfType<mlir::BoolAttr>("composite_sealed"))
            {
                steps.back().compositeSealed = sealed.getValue();
            }
            if (auto extent = op.getAttrOfType<mlir::IntegerAttr>("composite_extent_bits"))
            {
                steps.back().compositeExtentBits = nonNegative(extent.getInt());
            }
        }
        else
        {
            steps.push_back(
                PlanStep{PlanStepKind::Field,
                         bits,
                         nameAttr ? nameAttr.getValue().str() : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("c_name")
                             ? op.getAttrOfType<mlir::StringAttr>("c_name").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("scalar_category")
                             ? op.getAttrOfType<mlir::StringAttr>("scalar_category").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("cast_mode")
                             ? op.getAttrOfType<mlir::StringAttr>("cast_mode").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("array_kind")
                             ? op.getAttrOfType<mlir::StringAttr>("array_kind").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::IntegerAttr>("bit_length")
                             ? op.getAttrOfType<mlir::IntegerAttr>("bit_length").getInt()
                             : 0,
                         op.getAttrOfType<mlir::IntegerAttr>("array_capacity")
                             ? op.getAttrOfType<mlir::IntegerAttr>("array_capacity").getInt()
                             : 0,
                         op.getAttrOfType<mlir::IntegerAttr>("array_length_prefix_bits")
                             ? op.getAttrOfType<mlir::IntegerAttr>("array_length_prefix_bits").getInt()
                             : 0,
                         op.getAttrOfType<mlir::IntegerAttr>("alignment_bits")
                             ? op.getAttrOfType<mlir::IntegerAttr>("alignment_bits").getInt()
                             : 1,
                         op.getAttrOfType<mlir::IntegerAttr>("union_option_index")
                             ? op.getAttrOfType<mlir::IntegerAttr>("union_option_index").getInt()
                             : 0,
                         op.getAttrOfType<mlir::IntegerAttr>("union_tag_bits")
                             ? op.getAttrOfType<mlir::IntegerAttr>("union_tag_bits").getInt()
                             : 0,
                         op.getAttrOfType<mlir::StringAttr>("composite_c_type_name")
                             ? op.getAttrOfType<mlir::StringAttr>("composite_c_type_name").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("lowered_ser_unsigned_helper")
                             ? op.getAttrOfType<mlir::StringAttr>("lowered_ser_unsigned_helper").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("lowered_deser_unsigned_helper")
                             ? op.getAttrOfType<mlir::StringAttr>("lowered_deser_unsigned_helper").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("lowered_ser_signed_helper")
                             ? op.getAttrOfType<mlir::StringAttr>("lowered_ser_signed_helper").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("lowered_deser_signed_helper")
                             ? op.getAttrOfType<mlir::StringAttr>("lowered_deser_signed_helper").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("lowered_ser_float_helper")
                             ? op.getAttrOfType<mlir::StringAttr>("lowered_ser_float_helper").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("lowered_deser_float_helper")
                             ? op.getAttrOfType<mlir::StringAttr>("lowered_deser_float_helper").getValue().str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("lowered_ser_array_length_prefix_helper")
                             ? op.getAttrOfType<mlir::StringAttr>("lowered_ser_array_length_prefix_helper")
                                   .getValue()
                                   .str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("lowered_deser_array_length_prefix_helper")
                             ? op.getAttrOfType<mlir::StringAttr>("lowered_deser_array_length_prefix_helper")
                                   .getValue()
                                   .str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("lowered_array_length_validate_helper")
                             ? op.getAttrOfType<mlir::StringAttr>("lowered_array_length_validate_helper")
                                   .getValue()
                                   .str()
                             : std::string{},
                         op.getAttrOfType<mlir::StringAttr>("lowered_delimiter_validate_helper")
                             ? op.getAttrOfType<mlir::StringAttr>("lowered_delimiter_validate_helper").getValue().str()
                             : std::string{}});
            if (auto sealed = op.getAttrOfType<mlir::BoolAttr>("composite_sealed"))
            {
                steps.back().compositeSealed = sealed.getValue();
            }
            if (auto extent = op.getAttrOfType<mlir::IntegerAttr>("composite_extent_bits"))
            {
                steps.back().compositeExtentBits = nonNegative(extent.getInt());
            }
        }
    }
    return steps;
}

std::string renderGenericSerializeFunction(llvm::StringRef              functionName,
                                           llvm::StringRef              cTypeName,
                                           llvm::StringRef              cSerializeSymbol,
                                           llvm::StringRef              fullName,
                                           llvm::StringRef              sectionName,
                                           std::int64_t                 minBits,
                                           std::int64_t                 maxBits,
                                           const std::vector<PlanStep>& steps,
                                           llvm::StringRef              capacityCheckSymbol)
{
    const std::string  functionNameText     = functionName.str();
    const std::string  cTypeNameText        = cTypeName.str();
    const std::string  cSerializeSymbolText = cSerializeSymbol.str();
    const std::string  fullNameText         = fullName.str();
    const std::string  sectionNameText      = sectionName.str();
    std::ostringstream out;
    SourceWriter       w = makeEmitCWriter(out);
    if (cTypeNameText.empty())
    {
        out << "int8_t " << functionNameText
            << "(const void* obj, uint8_t* buffer, size_t* const "
               "inout_buffer_size_bytes)\n";
    }
    else
    {
        out << "int8_t " << functionNameText << "(const " << cTypeNameText
            << "* const obj, uint8_t* buffer, size_t* const inout_buffer_size_bytes)\n";
    }
    w.open("{");
    out << "  // IR section: " << fullNameText;
    if (!sectionNameText.empty())
    {
        out << " (" << sectionNameText << ")";
    }
    out << ", min_bits=" << minBits << ", max_bits=" << maxBits << ".\n";
    if (!cSerializeSymbolText.empty())
    {
        out << "  // Public C API symbol: " << cSerializeSymbolText << "\n";
    }
    out << "  // Generic bitstream mapping: non-padding fields are packed in\n";
    out << "  // declaration order from/to object memory as contiguous bits.\n";
    out << "  if ((obj == NULL) || (buffer == NULL) || (inout_buffer_size_bytes == "
           "NULL)) {\n";
    out << "    return -(int8_t)DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;\n";
    out << "  }\n";
    out << "  const uint8_t* const obj_bytes = (const uint8_t*)obj;\n";
    out << "  const size_t capacity_bits = (*inout_buffer_size_bytes) * 8U;\n";
    out << "  const int8_t _err_capacity = " << capacityCheckSymbol.str() << "((int64_t)capacity_bits);\n";
    out << "  if (_err_capacity < 0) {\n";
    out << "    return _err_capacity;\n";
    out << "  }\n";
    out << "  size_t offset_bits = 0U;\n";
    out << "  size_t obj_offset_bits = 0U;\n";
    out << "  (void)obj_bytes;\n";
    out << "  (void)obj_offset_bits;\n";
    for (std::size_t index = 0; index < steps.size(); ++index)
    {
        const auto& step = steps[index];
        if (step.kind == PlanStepKind::Align)
        {
            if (step.bits > 1)
            {
                out << "  offset_bits = ((offset_bits + " << (step.bits - 1) << "U) / " << step.bits << "U) * "
                    << step.bits << "U;\n";
            }
            continue;
        }

        out << "  {\n";
        out << "    const size_t bits_" << index << " = " << nonNegative(step.bits) << "U;\n";
        if (!step.name.empty())
        {
            out << "    /* " << step.name << " */\n";
        }
        out << "    if (bits_" << index << " > 0U) {\n";
        out << "      if (offset_bits + bits_" << index << " > capacity_bits) {\n";
        out << "        return "
               "-(int8_t)DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL;\n";
        out << "      }\n";
        if (step.kind == PlanStepKind::Padding)
        {
            out << "      for (size_t bit_" << index << " = 0U; bit_" << index << " < bits_" << index << "; ++bit_"
                << index << ") {\n";
            out << "        dsdl_runtime_set_bit(buffer, *inout_buffer_size_bytes, "
                   "offset_bits + bit_"
                << index << ", false);\n";
            out << "      }\n";
        }
        else
        {
            out << "      dsdl_runtime_copy_bits(buffer, offset_bits, bits_" << index
                << ", obj_bytes, obj_offset_bits);\n";
            out << "      obj_offset_bits += bits_" << index << ";\n";
        }
        out << "      offset_bits += bits_" << index << ";\n";
        out << "    }\n";
        out << "  }\n";
    }
    out << "  *inout_buffer_size_bytes = (offset_bits + 7U) / 8U;\n";
    out << "  return (int8_t)DSDL_RUNTIME_SUCCESS;\n";
    out << "}\n";
    return out.str();
}

std::string renderGenericDeserializeFunction(llvm::StringRef              functionName,
                                             llvm::StringRef              cTypeName,
                                             llvm::StringRef              cDeserializeSymbol,
                                             llvm::StringRef              fullName,
                                             llvm::StringRef              sectionName,
                                             std::int64_t                 minBits,
                                             std::int64_t                 maxBits,
                                             const std::vector<PlanStep>& steps)
{
    const std::string  functionNameText       = functionName.str();
    const std::string  cTypeNameText          = cTypeName.str();
    const std::string  cDeserializeSymbolText = cDeserializeSymbol.str();
    const std::string  fullNameText           = fullName.str();
    const std::string  sectionNameText        = sectionName.str();
    std::ostringstream out;
    SourceWriter       w = makeEmitCWriter(out);
    if (cTypeNameText.empty())
    {
        out << "int8_t " << functionNameText
            << "(void* out_obj, const uint8_t* buffer, size_t* const "
               "inout_buffer_size_bytes)\n";
    }
    else
    {
        out << "int8_t " << functionNameText << "(" << cTypeNameText
            << "* const out_obj, const uint8_t* buffer, size_t* const "
               "inout_buffer_size_bytes)\n";
    }
    w.open("{");
    out << "  // IR section: " << fullNameText;
    if (!sectionNameText.empty())
    {
        out << " (" << sectionNameText << ")";
    }
    out << ", min_bits=" << minBits << ", max_bits=" << maxBits << ".\n";
    if (!cDeserializeSymbolText.empty())
    {
        out << "  // Public C API symbol: " << cDeserializeSymbolText << "\n";
    }
    out << "  // Generic bitstream mapping: non-padding fields are unpacked in\n";
    out << "  // declaration order into object memory as contiguous bits.\n";
    out << "  if ((out_obj == NULL) || (buffer == NULL) || (inout_buffer_size_bytes "
           "== NULL)) {\n";
    out << "    return -(int8_t)DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;\n";
    out << "  }\n";
    out << "  uint8_t* const obj_bytes = (uint8_t*)out_obj;\n";
    out << "  const size_t capacity_bits = (*inout_buffer_size_bytes) * 8U;\n";
    out << "  const size_t required_bits = " << nonNegative(maxBits) << "U;\n";
    out << "  const size_t obj_capacity_bytes = (required_bits + 7U) / 8U;\n";
    out << "  size_t offset_bits = 0U;\n";
    out << "  size_t obj_offset_bits = 0U;\n";
    out << "  (void)obj_bytes;\n";
    out << "  (void)obj_capacity_bytes;\n";
    out << "  (void)obj_offset_bits;\n";
    for (std::size_t index = 0; index < steps.size(); ++index)
    {
        const auto& step = steps[index];
        if (step.kind == PlanStepKind::Align)
        {
            if (step.bits > 1)
            {
                out << "  offset_bits = ((offset_bits + " << (step.bits - 1) << "U) / " << step.bits << "U) * "
                    << step.bits << "U;\n";
            }
            continue;
        }

        out << "  {\n";
        out << "    const size_t bits_" << index << " = " << nonNegative(step.bits) << "U;\n";
        if (!step.name.empty())
        {
            out << "    /* " << step.name << " */\n";
        }
        out << "    if (bits_" << index << " > 0U) {\n";
        if (step.kind == PlanStepKind::Field)
        {
            out << "      const size_t available_bits_" << index
                << " = (offset_bits < capacity_bits) ? (capacity_bits - offset_bits) : "
                   "0U;\n";
            out << "      const size_t copy_bits_" << index << " = (available_bits_" << index << " < bits_" << index
                << ") ? available_bits_" << index << " : bits_" << index << ";\n";
            out << "      if (copy_bits_" << index << " > 0U) {\n";
            out << "        dsdl_runtime_copy_bits(obj_bytes, obj_offset_bits, "
                   "copy_bits_"
                << index << ", buffer, offset_bits);\n";
            out << "      }\n";
            out << "      if (copy_bits_" << index << " < bits_" << index << ") {\n";
            out << "        const size_t zero_bits_" << index << " = bits_" << index << " - copy_bits_" << index
                << ";\n";
            out << "        for (size_t bit_" << index << " = 0U; bit_" << index << " < zero_bits_" << index
                << "; ++bit_" << index << ") {\n";
            out << "          dsdl_runtime_set_bit(obj_bytes, obj_capacity_bytes, "
                   "obj_offset_bits + copy_bits_"
                << index << " + bit_" << index << ", false);\n";
            out << "        }\n";
            out << "      }\n";
            out << "      obj_offset_bits += bits_" << index << ";\n";
        }
        out << "      offset_bits += bits_" << index << ";\n";
        out << "    }\n";
        out << "  }\n";
    }
    out << "  const size_t consumed_bits = (offset_bits < capacity_bits) "
           "? offset_bits : capacity_bits;\n";
    out << "  *inout_buffer_size_bytes = consumed_bits / 8U;\n";
    out << "  return (int8_t)DSDL_RUNTIME_SUCCESS;\n";
    out << "}\n";
    return out.str();
}

void emitMalformedCategoryComment(SourceWriter& w, const std::string& category)
{
    w.line("/* " + category + " */");
}

bool supportsTypedFieldStep(const PlanStep& step)
{
    if (step.kind != PlanStepKind::Field)
    {
        return true;
    }
    if (step.cName.empty())
    {
        return false;
    }

    if (!isSupportedArrayKind(step.arrayKind))
    {
        return false;
    }
    if (step.arrayKind != "none")
    {
        if (step.arrayCapacity < 0)
        {
            return false;
        }
        if (isVariableArrayKind(step.arrayKind) && (step.arrayLengthPrefixBits <= 0 || step.arrayLengthPrefixBits > 64))
        {
            return false;
        }
    }

    if (step.scalarCategory == "void")
    {
        return false;
    }

    if (step.scalarCategory == "composite")
    {
        return !step.compositeCTypeName.empty();
    }
    if (step.scalarCategory == "bool")
    {
        return step.bitLength == 1;
    }
    if (step.scalarCategory == "byte" || step.scalarCategory == "utf8")
    {
        return step.bitLength == 8;
    }
    if (step.scalarCategory == "unsigned" || step.scalarCategory == "signed")
    {
        return step.bitLength >= 1 && step.bitLength <= 64;
    }
    if (step.scalarCategory == "float")
    {
        return step.bitLength == 16 || step.bitLength == 32 || step.bitLength == 64;
    }
    return false;
}

bool supportsTypedLowering(const std::vector<PlanStep>& steps, const bool isUnion, const std::int64_t unionTagBits)
{
    if (isUnion && (unionTagBits <= 0 || unionTagBits > 64))
    {
        return false;
    }

    std::set<std::int64_t> unionOptions;
    for (const auto& step : steps)
    {
        if (!supportsTypedFieldStep(step))
        {
            return false;
        }
        if (step.kind == PlanStepKind::Align && step.bits <= 0)
        {
            return false;
        }
        if (isUnion && step.kind == PlanStepKind::Field)
        {
            unionOptions.insert(step.unionOptionIndex);
        }
    }
    return !(isUnion && unionOptions.empty());
}

void emitDeserializeAlign(SourceWriter& w, const std::int64_t alignmentBits)
{
    if (alignmentBits <= 1)
    {
        return;
    }
    w.line("offset_bits = ((offset_bits + " + std::to_string(alignmentBits - 1) + "U) / " +
           std::to_string(alignmentBits) + "U) * " + std::to_string(alignmentBits) + "U;");
}

void emitSerializeAlign(SourceWriter& w, const std::int64_t alignmentBits, const std::string& tag)
{
    if (alignmentBits <= 1)
    {
        return;
    }
    const std::string alignedName = "_aligned_offset_bits_" + tag;
    const std::string padBitName  = "_pad_bit_" + tag;
    const std::string errName     = "_err_align_" + tag;
    w.line("const size_t " + alignedName + " = ((offset_bits + " + std::to_string(alignmentBits - 1) + "U) / " +
           std::to_string(alignmentBits) + "U) * " + std::to_string(alignmentBits) + "U;");
    w.open("for (size_t " + padBitName + " = offset_bits; " + padBitName + " < " + alignedName + "; ++" + padBitName +
           ") {");
    w.line("const int8_t " + errName + " = dsdl_runtime_set_bit(buffer, capacity_bytes, " + padBitName + ", false);");
    w.open("if (" + errName + " < 0) {");
    w.line("return " + errName + ";");
    w.close("}");
    w.close("}");
    w.line("offset_bits = " + alignedName + ";");
}

std::string unsignedGetterForBits(const std::int64_t bits)
{
    if (bits <= 8)
    {
        return "dsdl_runtime_get_u8";
    }
    if (bits <= 16)
    {
        return "dsdl_runtime_get_u16";
    }
    if (bits <= 32)
    {
        return "dsdl_runtime_get_u32";
    }
    return "dsdl_runtime_get_u64";
}

/// @brief The C fixed-width unsigned storage type that holds `bits` (union tag width, etc.).
std::string unsignedStorageTypeForBits(const std::int64_t bits)
{
    if (bits <= 8)
    {
        return "uint8_t";
    }
    if (bits <= 16)
    {
        return "uint16_t";
    }
    if (bits <= 32)
    {
        return "uint32_t";
    }
    return "uint64_t";
}

std::string signedGetterForBits(const std::int64_t bits)
{
    if (bits <= 8)
    {
        return "dsdl_runtime_get_i8";
    }
    if (bits <= 16)
    {
        return "dsdl_runtime_get_i16";
    }
    if (bits <= 32)
    {
        return "dsdl_runtime_get_i32";
    }
    return "dsdl_runtime_get_i64";
}

void emitSerializePadding(SourceWriter& w, const std::size_t index, const std::int64_t bits)
{
    w.open("for (size_t bit_" + std::to_string(index) + " = 0U; bit_" + std::to_string(index) + " < " +
           std::to_string(nonNegative(bits)) + "U; ++bit_" + std::to_string(index) + ") {");
    w.line("const int8_t _err_pad_" + std::to_string(index) +
           " = dsdl_runtime_set_bit(buffer, capacity_bytes, offset_bits + "
           "bit_" +
           std::to_string(index) + ", false);");
    w.open("if (_err_pad_" + std::to_string(index) + " < 0) {");
    w.line("return _err_pad_" + std::to_string(index) + ";");
    w.close("}");
    w.close("}");
    w.line("offset_bits += " + std::to_string(nonNegative(bits)) + "U;");
}

bool emitSerializeField(SourceWriter& w, const PlanStep& step, const std::string& expr, std::size_t index);
bool emitDeserializeField(SourceWriter& w, const PlanStep& step, const std::string& expr, std::size_t index);

bool emitSerializeArrayField(SourceWriter& w, const PlanStep& step, const std::string& expr, const std::size_t index)
{
    const bool variable     = isVariableArrayKind(step.arrayKind);
    const auto capacityExpr = std::to_string(nonNegative(step.arrayCapacity)) + "U";

    if (variable)
    {
        if (step.serArrayLengthPrefixHelper.empty())
        {
            return false;
        }
        if (!step.arrayLengthValidateHelper.empty())
        {
            w.line("const int8_t _err_lenchk_" + std::to_string(index) + " = " + step.arrayLengthValidateHelper +
                   "((int64_t)(" + expr + ".count));");
            w.open("if (_err_lenchk_" + std::to_string(index) + " < 0) {");
            w.line("return _err_lenchk_" + std::to_string(index) + ";");
            w.close("}");
        }
        else
        {
            w.open("if (" + expr + ".count > " + capacityExpr + ") {");
            emitMalformedCategoryComment(w, codegen_diagnostic_text::malformedArrayLengthCategory());
            w.line("return -(int8_t)DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH;");
            w.close("}");
        }
        w.line("const uint64_t _wire_len_" + std::to_string(index) + " = (uint64_t)" + step.serArrayLengthPrefixHelper +
               "((int64_t)(" + expr + ".count));");
        w.line("const int8_t _err_len_" + std::to_string(index) +
               " = dsdl_runtime_set_uxx(buffer, capacity_bytes, offset_bits, "
               "_wire_len_" +
               std::to_string(index) + ", (uint8_t)" + std::to_string(nonNegative(step.arrayLengthPrefixBits)) + "U);");
        w.open("if (_err_len_" + std::to_string(index) + " < 0) {");
        w.line("return _err_len_" + std::to_string(index) + ";");
        w.close("}");
        w.line("offset_bits += " + std::to_string(nonNegative(step.arrayLengthPrefixBits)) + "U;");
    }

    const auto countExpr = variable ? (expr + ".count") : capacityExpr;
    if (step.scalarCategory == "bool")
    {
        const auto sourceExpr = variable ? ("&" + expr + ".bitpacked[0]") : ("&" + expr + "[0]");
        w.line("dsdl_runtime_copy_bits(&buffer[0], offset_bits, " + countExpr + ", " + sourceExpr + ", 0U);");
        w.line("offset_bits += " + countExpr + ";");
        return true;
    }

    const auto loopIndex    = "_i_" + std::to_string(index);
    const auto accessPrefix = variable ? (expr + ".elements") : expr;
    w.open("for (size_t " + loopIndex + " = 0U; " + loopIndex + " < " + countExpr + "; ++" + loopIndex + ") {");
    auto elementStep                  = step;
    elementStep.arrayKind             = "none";
    elementStep.arrayCapacity         = 0;
    elementStep.arrayLengthPrefixBits = 0;
    if (!emitSerializeField(w, elementStep, accessPrefix + "[" + loopIndex + "]", index))
    {
        return false;
    }
    w.close("}");
    return true;
}

bool emitDeserializeArrayField(SourceWriter& w, const PlanStep& step, const std::string& expr, const std::size_t index)
{
    const bool variable     = isVariableArrayKind(step.arrayKind);
    const auto capacityExpr = std::to_string(nonNegative(step.arrayCapacity)) + "U";

    if (variable)
    {
        if (step.deserArrayLengthPrefixHelper.empty())
        {
            return false;
        }
        w.line("const uint64_t _wire_len_" + std::to_string(index) + " = (uint64_t)" +
               unsignedGetterForBits(step.arrayLengthPrefixBits) + "(buffer, capacity_bytes, offset_bits, (uint8_t)" +
               std::to_string(nonNegative(step.arrayLengthPrefixBits)) + "U);");
        w.line(expr + ".count = (size_t)" + step.deserArrayLengthPrefixHelper + "((int64_t)_wire_len_" +
               std::to_string(index) + ");");
        w.line("offset_bits += " + std::to_string(nonNegative(step.arrayLengthPrefixBits)) + "U;");
        if (!step.arrayLengthValidateHelper.empty())
        {
            w.line("const int8_t _err_lenchk_" + std::to_string(index) + " = " + step.arrayLengthValidateHelper +
                   "((int64_t)(" + expr + ".count));");
            w.open("if (_err_lenchk_" + std::to_string(index) + " < 0) {");
            w.line("return _err_lenchk_" + std::to_string(index) + ";");
            w.close("}");
        }
        else
        {
            w.open("if (" + expr + ".count > " + capacityExpr + ") {");
            emitMalformedCategoryComment(w, codegen_diagnostic_text::malformedArrayLengthCategory());
            w.line("return -(int8_t)DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH;");
            w.close("}");
        }
    }

    const auto countExpr = variable ? (expr + ".count") : capacityExpr;
    if (step.scalarCategory == "bool")
    {
        const auto targetExpr = variable ? ("&" + expr + ".bitpacked[0]") : ("&" + expr + "[0]");
        w.line("dsdl_runtime_get_bits(" + targetExpr + ", &buffer[0], capacity_bytes, offset_bits, " + countExpr +
               ");");
        w.line("offset_bits += " + countExpr + ";");
        return true;
    }

    const auto loopIndex    = "_i_" + std::to_string(index);
    const auto accessPrefix = variable ? (expr + ".elements") : expr;
    w.open("for (size_t " + loopIndex + " = 0U; " + loopIndex + " < " + countExpr + "; ++" + loopIndex + ") {");
    auto elementStep                  = step;
    elementStep.arrayKind             = "none";
    elementStep.arrayCapacity         = 0;
    elementStep.arrayLengthPrefixBits = 0;
    if (!emitDeserializeField(w, elementStep, accessPrefix + "[" + loopIndex + "]", index))
    {
        return false;
    }
    w.close("}");
    return true;
}

bool emitSerializeField(SourceWriter& w, const PlanStep& step, const std::string& expr, const std::size_t index)
{
    if (step.arrayKind != "none")
    {
        return emitSerializeArrayField(w, step, expr, index);
    }

    if (step.scalarCategory == "composite")
    {
        if (!step.compositeSealed)
        {
            w.line("const size_t _delim_start_bytes_" + std::to_string(index) + " = offset_bits / 8U;");
            w.line("offset_bits += 32U;");
            w.line("const size_t _remaining_bytes_" + std::to_string(index) +
                   " = capacity_bytes - dsdl_runtime_choose_min(offset_bits / 8U, "
                   "capacity_bytes);");
            w.line("size_t _size_bytes_" + std::to_string(index) +
                   " = capacity_bytes - dsdl_runtime_choose_min(offset_bits / 8U, "
                   "capacity_bytes);");
            w.line("const int8_t _err_" + std::to_string(index) + " = " + step.compositeCTypeName + "__serialize_(&" +
                   expr + ", &buffer[offset_bits / 8U], &_size_bytes_" + std::to_string(index) + ");");
            w.open("if (_err_" + std::to_string(index) + " < 0) {");
            w.line("return _err_" + std::to_string(index) + ";");
            w.close("}");
            if (step.delimiterValidateHelper.empty())
            {
                return false;
            }
            w.line("const int8_t _delim_chk_" + std::to_string(index) + " = " + step.delimiterValidateHelper +
                   "((int64_t)_size_bytes_" + std::to_string(index) + ", (int64_t)_remaining_bytes_" +
                   std::to_string(index) + ");");
            w.open("if (_delim_chk_" + std::to_string(index) + " < 0) {");
            emitMalformedCategoryComment(w, codegen_diagnostic_text::malformedDelimiterHeaderCategory());
            w.line("return _delim_chk_" + std::to_string(index) + ";");
            w.close("}");
            w.line("offset_bits += _size_bytes_" + std::to_string(index) + " * 8U;");
            w.line("const int8_t _hdr_err_" + std::to_string(index) +
                   " = dsdl_runtime_set_uxx(buffer, capacity_bytes, "
                   "_delim_start_bytes_" +
                   std::to_string(index) + " * 8U, (uint64_t)_size_bytes_" + std::to_string(index) + ", 32U);");
            w.open("if (_hdr_err_" + std::to_string(index) + " < 0) {");
            w.line("return _hdr_err_" + std::to_string(index) + ";");
            w.close("}");
        }
        else
        {
            w.line("size_t _size_bytes_" + std::to_string(index) +
                   " = capacity_bytes - dsdl_runtime_choose_min(offset_bits / 8U, "
                   "capacity_bytes);");
            w.line("const int8_t _err_" + std::to_string(index) + " = " + step.compositeCTypeName + "__serialize_(&" +
                   expr + ", &buffer[offset_bits / 8U], &_size_bytes_" + std::to_string(index) + ");");
            w.open("if (_err_" + std::to_string(index) + " < 0) {");
            w.line("return _err_" + std::to_string(index) + ";");
            w.close("}");
            w.line("offset_bits += _size_bytes_" + std::to_string(index) + " * 8U;");
        }
        return true;
    }

    if (step.scalarCategory == "bool")
    {
        w.line("const int8_t _err_" + std::to_string(index) +
               " = dsdl_runtime_set_bit(buffer, capacity_bytes, offset_bits, " + expr + ");");
        w.open("if (_err_" + std::to_string(index) + " < 0) {");
        w.line("return _err_" + std::to_string(index) + ";");
        w.close("}");
        w.line("offset_bits += 1U;");
        return true;
    }

    if (step.scalarCategory == "byte" || step.scalarCategory == "utf8" || step.scalarCategory == "unsigned")
    {
        std::string valueExpr = "(uint64_t)(" + expr + ")";
        if (step.serUnsignedHelper.empty())
        {
            return false;
        }
        const auto normName = "_norm_" + std::to_string(index);
        w.line("const uint64_t " + normName + " = (uint64_t)" + step.serUnsignedHelper + "((int64_t)(" + valueExpr +
               "));");
        valueExpr = normName;
        w.line("const int8_t _err_" + std::to_string(index) +
               " = dsdl_runtime_set_uxx(buffer, capacity_bytes, offset_bits, " + valueExpr + ", (uint8_t)" +
               std::to_string(nonNegative(step.bitLength)) + "U);");
        w.open("if (_err_" + std::to_string(index) + " < 0) {");
        w.line("return _err_" + std::to_string(index) + ";");
        w.close("}");
        w.line("offset_bits += " + std::to_string(nonNegative(step.bitLength)) + "U;");
        return true;
    }

    if (step.scalarCategory == "signed")
    {
        std::string valueExpr = "(int64_t)(" + expr + ")";
        if (step.serSignedHelper.empty())
        {
            return false;
        }
        const auto normName = "_norms_" + std::to_string(index);
        w.line("const int64_t " + normName + " = (int64_t)" + step.serSignedHelper + "((int64_t)(" + valueExpr + "));");
        valueExpr = normName;
        w.line("const int8_t _err_" + std::to_string(index) +
               " = dsdl_runtime_set_ixx(buffer, capacity_bytes, offset_bits, " + valueExpr + ", (uint8_t)" +
               std::to_string(nonNegative(step.bitLength)) + "U);");
        w.open("if (_err_" + std::to_string(index) + " < 0) {");
        w.line("return _err_" + std::to_string(index) + ";");
        w.close("}");
        w.line("offset_bits += " + std::to_string(nonNegative(step.bitLength)) + "U;");
        return true;
    }

    if (step.scalarCategory == "float")
    {
        std::string setter;
        std::string castType;
        if (step.bitLength == 16 || step.bitLength == 32)
        {
            castType = "float";
            setter   = (step.bitLength == 16) ? "dsdl_runtime_set_f16" : "dsdl_runtime_set_f32";
        }
        else if (step.bitLength == 64)
        {
            castType = "double";
            setter   = "dsdl_runtime_set_f64";
        }
        else
        {
            return false;
        }
        if (step.serFloatHelper.empty())
        {
            return false;
        }
        // Keep the value in its native storage width (float for 16/32-bit,
        // double for 64-bit) through the identity normalization helper so a NaN
        // payload is not canonicalized by a float->double->float round-trip.
        const auto normName = "_normf_" + std::to_string(index);
        w.line("const " + castType + " " + normName + " = " + step.serFloatHelper + "((" + castType + ")(" + expr +
               "));");
        std::string const& valueExpr = normName;
        w.line("const int8_t _err_" + std::to_string(index) + " = " + setter +
               "(buffer, capacity_bytes, offset_bits, " + valueExpr + ");");
        w.open("if (_err_" + std::to_string(index) + " < 0) {");
        w.line("return _err_" + std::to_string(index) + ";");
        w.close("}");
        w.line("offset_bits += " + std::to_string(nonNegative(step.bitLength)) + "U;");
        return true;
    }

    return false;
}

bool emitDeserializeField(SourceWriter& w, const PlanStep& step, const std::string& expr, const std::size_t index)
{
    if (step.arrayKind != "none")
    {
        return emitDeserializeArrayField(w, step, expr, index);
    }

    if (step.scalarCategory == "composite")
    {
        if (!step.compositeSealed)
        {
            w.line("size_t _size_bytes_" + std::to_string(index) +
                   " = (size_t)dsdl_runtime_get_u32(buffer, capacity_bytes, "
                   "offset_bits, 32U);");
            w.line("offset_bits += 32U;");
            w.line("const size_t _remaining_bytes_" + std::to_string(index) +
                   " = capacity_bytes - dsdl_runtime_choose_min(offset_bits / 8U, "
                   "capacity_bytes);");
            if (step.delimiterValidateHelper.empty())
            {
                return false;
            }
            w.line("const int8_t _delim_chk_" + std::to_string(index) + " = " + step.delimiterValidateHelper +
                   "((int64_t)_size_bytes_" + std::to_string(index) + ", (int64_t)_remaining_bytes_" +
                   std::to_string(index) + ");");
            w.open("if (_delim_chk_" + std::to_string(index) + " < 0) {");
            emitMalformedCategoryComment(w, codegen_diagnostic_text::malformedDelimiterHeaderCategory());
            w.line("return _delim_chk_" + std::to_string(index) + ";");
            w.close("}");
            // The nested deserialize writes back how many bytes it actually consumed, which for a
            // delimited field may be FEWER than the header declares (a newer peer appended fields this
            // reader does not understand). Capture that in a separate variable and advance the outer
            // offset by the header-declared size (`_size_bytes_`) -- advancing by the consumed count
            // would misplace every subsequent field, breaking delimited forward compatibility.
            w.line("size_t _consumed_bytes_" + std::to_string(index) + " = _size_bytes_" + std::to_string(index) + ";");
            w.line("const int8_t _err_" + std::to_string(index) + " = " + step.compositeCTypeName + "__deserialize_(&" +
                   expr + ", &buffer[offset_bits / 8U], &_consumed_bytes_" + std::to_string(index) + ");");
            w.open("if (_err_" + std::to_string(index) + " < 0) {");
            w.line("return _err_" + std::to_string(index) + ";");
            w.close("}");
            w.line("offset_bits += _size_bytes_" + std::to_string(index) + " * 8U;");
        }
        else
        {
            w.line("size_t _size_bytes_" + std::to_string(index) +
                   " = capacity_bytes - dsdl_runtime_choose_min(offset_bits / 8U, "
                   "capacity_bytes);");
            w.line("const int8_t _err_" + std::to_string(index) + " = " + step.compositeCTypeName + "__deserialize_(&" +
                   expr + ", &buffer[offset_bits / 8U], &_size_bytes_" + std::to_string(index) + ");");
            w.open("if (_err_" + std::to_string(index) + " < 0) {");
            w.line("return _err_" + std::to_string(index) + ";");
            w.close("}");
            w.line("offset_bits += _size_bytes_" + std::to_string(index) + " * 8U;");
        }
        return true;
    }

    if (step.scalarCategory == "bool")
    {
        w.line(expr + " = dsdl_runtime_get_bit(buffer, capacity_bytes, offset_bits);");
        w.line("offset_bits += 1U;");
        return true;
    }

    if (step.scalarCategory == "byte" || step.scalarCategory == "utf8" || step.scalarCategory == "unsigned")
    {
        const auto rawName = "_raw_" + std::to_string(index);
        w.line("const uint64_t " + rawName + " = (uint64_t)" + unsignedGetterForBits(step.bitLength) +
               "(buffer, capacity_bytes, offset_bits, (uint8_t)" + std::to_string(nonNegative(step.bitLength)) + "U);");
        if (step.deserUnsignedHelper.empty())
        {
            return false;
        }
        w.line(expr + " = (uint64_t)" + step.deserUnsignedHelper + "((int64_t)" + rawName + ");");
        w.line("offset_bits += " + std::to_string(nonNegative(step.bitLength)) + "U;");
        return true;
    }

    if (step.scalarCategory == "signed")
    {
        const auto rawName = "_raws_" + std::to_string(index);
        w.line("const int64_t " + rawName + " = (int64_t)" + signedGetterForBits(step.bitLength) +
               "(buffer, capacity_bytes, offset_bits, (uint8_t)" + std::to_string(nonNegative(step.bitLength)) + "U);");
        if (step.deserSignedHelper.empty())
        {
            return false;
        }
        w.line(expr + " = (int64_t)" + step.deserSignedHelper + "((int64_t)" + rawName + ");");
        w.line("offset_bits += " + std::to_string(nonNegative(step.bitLength)) + "U;");
        return true;
    }

    if (step.scalarCategory == "float")
    {
        std::string getter;
        if (step.bitLength == 16)
        {
            getter = "dsdl_runtime_get_f16";
        }
        else if (step.bitLength == 32)
        {
            getter = "dsdl_runtime_get_f32";
        }
        else if (step.bitLength == 64)
        {
            getter = "dsdl_runtime_get_f64";
        }
        else
        {
            return false;
        }
        std::string const castType = (step.bitLength == 64) ? "double" : "float";
        const auto        rawName  = "_rawf_" + std::to_string(index);
        // Read into the native storage width (get_f16/get_f32 return float,
        // get_f64 returns double) and run the identity normalization helper at
        // that width, so a NaN payload survives without float->double->float
        // canonicalization.
        w.line("const " + castType + " " + rawName + " = " + getter + "(buffer, capacity_bytes, offset_bits);");
        if (step.deserFloatHelper.empty())
        {
            return false;
        }
        w.line(expr + " = (" + castType + ")(" + step.deserFloatHelper + "(" + rawName + "));");
        w.line("offset_bits += " + std::to_string(nonNegative(step.bitLength)) + "U;");
        return true;
    }

    return false;
}

std::string renderTypedSerializeFunction(llvm::StringRef              functionName,
                                         llvm::StringRef              cTypeName,
                                         llvm::StringRef              cSerializeSymbol,
                                         llvm::StringRef              fullName,
                                         llvm::StringRef              sectionName,
                                         std::int64_t                 minBits,
                                         std::int64_t                 maxBits,
                                         const std::vector<PlanStep>& steps,
                                         const bool                   isUnion,
                                         const std::int64_t           unionTagBits,
                                         llvm::StringRef              capacityCheckSymbol,
                                         llvm::StringRef              unionTagValidateSymbol,
                                         llvm::StringRef              unionTagSerializeSymbol)
{
    const std::string  functionNameText     = functionName.str();
    const std::string  cTypeNameText        = cTypeName.str();
    const std::string  cSerializeSymbolText = cSerializeSymbol.str();
    const std::string  fullNameText         = fullName.str();
    const std::string  sectionNameText      = sectionName.str();
    std::ostringstream out;
    SourceWriter       w = makeEmitCWriter(out);
    if (cTypeNameText.empty())
    {
        out << "int8_t " << functionNameText
            << "(const void* obj, uint8_t* buffer, size_t* const "
               "inout_buffer_size_bytes)\n";
    }
    else
    {
        out << "int8_t " << functionNameText << "(const " << cTypeNameText
            << "* const obj, uint8_t* buffer, size_t* const inout_buffer_size_bytes)\n";
    }
    w.open("{");
    w.line("// IR section: " + fullNameText +
           (sectionNameText.empty() ? std::string{} : (" (" + sectionNameText + ")")) +
           ", min_bits=" + std::to_string(minBits) + ", max_bits=" + std::to_string(maxBits) + ".");
    if (!cSerializeSymbolText.empty())
    {
        w.line("// Public C API symbol: " + cSerializeSymbolText);
    }
    w.line("// Typed IR lowering path.");
    w.open("if ((obj == NULL) || (buffer == NULL) || (inout_buffer_size_bytes == "
           "NULL)) {");
    w.line("return -(int8_t)DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
    w.close("}");
    w.line("const size_t capacity_bytes = *inout_buffer_size_bytes;");
    w.line("const int8_t _err_capacity = " + capacityCheckSymbol.str() + "((int64_t)(capacity_bytes * 8U));");
    w.open("if (_err_capacity < 0) {");
    w.line("return _err_capacity;");
    w.close("}");
    w.line("size_t offset_bits = 0U;");

    if (isUnion)
    {
        const auto tagBits = nonNegative(unionTagBits);
        w.line("const uint64_t _tag_value = (uint64_t)" + unionTagSerializeSymbol.str() + "((int64_t)(obj->_tag_));");
        w.line("const int8_t _err_union_tag = " + unionTagValidateSymbol.str() + "((int64_t)_tag_value);");
        w.open("if (_err_union_tag < 0) {");
        emitMalformedCategoryComment(w, codegen_diagnostic_text::malformedUnionTagCategory());
        w.line("return _err_union_tag;");
        w.close("}");
        w.line("const int8_t _err_tag_ = dsdl_runtime_set_uxx(buffer, "
               "capacity_bytes, offset_bits, _tag_value, (uint8_t)" +
               std::to_string(tagBits) + "U);");
        w.open("if (_err_tag_ < 0) {");
        w.line("return _err_tag_;");
        w.close("}");
        w.line("offset_bits += " + std::to_string(tagBits) + "U;");

        std::vector<const PlanStep*> unionFields;
        for (const auto& step : steps)
        {
            if (step.kind == PlanStepKind::Field)
            {
                unionFields.push_back(&step);
            }
        }
        std::ranges::sort(unionFields, [](const PlanStep* lhs, const PlanStep* rhs) {
            return lhs->unionOptionIndex < rhs->unionOptionIndex;
        });

        bool first = true;
        for (std::size_t i = 0; i < unionFields.size(); ++i)
        {
            const auto& step = *unionFields[i];
            w.open(std::string(first ? "if" : "else if") +
                   " (obj->_tag_ == " + std::to_string(nonNegative(step.unionOptionIndex)) + "U) {");
            first = false;
            emitSerializeAlign(w, step.alignmentBits, "u" + std::to_string(i));
            if (!emitSerializeField(w, step, "obj->" + step.cName, i))
            {
                w.line("return -(int8_t)DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
            }
            w.close("}");
        }
        w.open("else {");
        emitMalformedCategoryComment(w, codegen_diagnostic_text::malformedUnionTagCategory());
        w.line("return -(int8_t)DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG;");
        w.close("}");
    }
    else
    {
        for (std::size_t i = 0; i < steps.size(); ++i)
        {
            const auto& step = steps[i];
            if (step.kind == PlanStepKind::Align)
            {
                emitSerializeAlign(w, step.bits, "a" + std::to_string(i));
                continue;
            }
            if (step.kind == PlanStepKind::Padding)
            {
                emitSerializePadding(w, i, step.bits);
                continue;
            }
            if (!emitSerializeField(w, step, "obj->" + step.cName, i))
            {
                w.line("return -(int8_t)DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
            }
        }
    }

    emitSerializeAlign(w, 8, "final");
    w.line("*inout_buffer_size_bytes = (size_t)(offset_bits / 8U);");
    w.line("return (int8_t)DSDL_RUNTIME_SUCCESS;");
    w.close("}");
    return out.str();
}

std::string renderTypedDeserializeFunction(llvm::StringRef              functionName,
                                           llvm::StringRef              cTypeName,
                                           llvm::StringRef              cDeserializeSymbol,
                                           llvm::StringRef              fullName,
                                           llvm::StringRef              sectionName,
                                           std::int64_t                 minBits,
                                           std::int64_t                 maxBits,
                                           const std::vector<PlanStep>& steps,
                                           const bool                   isUnion,
                                           const std::int64_t           unionTagBits,
                                           llvm::StringRef              unionTagValidateSymbol,
                                           llvm::StringRef              unionTagDeserializeSymbol)
{
    const std::string  functionNameText       = functionName.str();
    const std::string  cTypeNameText          = cTypeName.str();
    const std::string  cDeserializeSymbolText = cDeserializeSymbol.str();
    const std::string  fullNameText           = fullName.str();
    const std::string  sectionNameText        = sectionName.str();
    std::ostringstream out;
    SourceWriter       w = makeEmitCWriter(out);
    if (cTypeNameText.empty())
    {
        out << "int8_t " << functionNameText
            << "(void* out_obj, const uint8_t* buffer, size_t* const "
               "inout_buffer_size_bytes)\n";
    }
    else
    {
        out << "int8_t " << functionNameText << "(" << cTypeNameText
            << "* const out_obj, const uint8_t* buffer, size_t* const "
               "inout_buffer_size_bytes)\n";
    }
    w.open("{");
    w.line("// IR section: " + fullNameText +
           (sectionNameText.empty() ? std::string{} : (" (" + sectionNameText + ")")) +
           ", min_bits=" + std::to_string(minBits) + ", max_bits=" + std::to_string(maxBits) + ".");
    if (!cDeserializeSymbolText.empty())
    {
        w.line("// Public C API symbol: " + cDeserializeSymbolText);
    }
    w.line("// Typed IR lowering path.");
    w.open("if ((out_obj == NULL) || (inout_buffer_size_bytes == NULL) || "
           "((buffer == NULL) && (0U != *inout_buffer_size_bytes))) {");
    w.line("return -(int8_t)DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
    w.close("}");
    w.open("if (buffer == NULL) {");
    w.line("buffer = (const uint8_t*)\"\";");
    w.close("}");
    w.line("const size_t capacity_bytes = *inout_buffer_size_bytes;");
    w.line("const size_t capacity_bits = capacity_bytes * 8U;");
    w.line("size_t offset_bits = 0U;");

    if (isUnion)
    {
        const auto tagBits = nonNegative(unionTagBits);
        w.line("const uint64_t _tag_wire = " + unsignedGetterForBits(tagBits) +
               "(buffer, capacity_bytes, offset_bits, (uint8_t)" + std::to_string(tagBits) + "U);");
        w.line("const uint64_t _tag_value = (uint64_t)" + unionTagDeserializeSymbol.str() + "((int64_t)_tag_wire);");
        w.line("const int8_t _err_union_tag = " + unionTagValidateSymbol.str() + "((int64_t)_tag_value);");
        w.open("if (_err_union_tag < 0) {");
        emitMalformedCategoryComment(w, codegen_diagnostic_text::malformedUnionTagCategory());
        w.line("return _err_union_tag;");
        w.close("}");
        // Store the tag in its true width; a hardcoded (uint8_t) truncates a wide tag
        // (>256-option unions get a 16-bit tag) and corrupts the decoded object / round-trip.
        w.line("out_obj->_tag_ = (" + unsignedStorageTypeForBits(tagBits) + ")_tag_value;");
        w.line("offset_bits += " + std::to_string(tagBits) + "U;");

        std::vector<const PlanStep*> unionFields;
        for (const auto& step : steps)
        {
            if (step.kind == PlanStepKind::Field)
            {
                unionFields.push_back(&step);
            }
        }
        std::ranges::sort(unionFields, [](const PlanStep* lhs, const PlanStep* rhs) {
            return lhs->unionOptionIndex < rhs->unionOptionIndex;
        });

        bool first = true;
        for (std::size_t i = 0; i < unionFields.size(); ++i)
        {
            const auto& step = *unionFields[i];
            w.open(std::string(first ? "if" : "else if") +
                   " (_tag_value == " + std::to_string(nonNegative(step.unionOptionIndex)) + "U) {");
            first = false;
            emitDeserializeAlign(w, step.alignmentBits);
            if (!emitDeserializeField(w, step, "out_obj->" + step.cName, i))
            {
                w.line("return -(int8_t)DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
            }
            w.close("}");
        }
        w.open("else {");
        emitMalformedCategoryComment(w, codegen_diagnostic_text::malformedUnionTagCategory());
        w.line("return -(int8_t)DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG;");
        w.close("}");
    }
    else
    {
        for (std::size_t i = 0; i < steps.size(); ++i)
        {
            const auto& step = steps[i];
            if (step.kind == PlanStepKind::Align)
            {
                emitDeserializeAlign(w, step.bits);
                continue;
            }
            if (step.kind == PlanStepKind::Padding)
            {
                w.line("offset_bits += " + std::to_string(nonNegative(step.bits)) + "U;");
                continue;
            }
            if (!emitDeserializeField(w, step, "out_obj->" + step.cName, i))
            {
                w.line("return -(int8_t)DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
            }
        }
    }

    emitDeserializeAlign(w, 8);
    w.line("*inout_buffer_size_bytes = (size_t)(dsdl_runtime_choose_min(offset_bits, "
           "capacity_bits) / 8U);");
    w.line("return (int8_t)DSDL_RUNTIME_SUCCESS;");
    w.close("}");
    return out.str();
}

/// @brief Name of the runtime bit-copy primitive both bit ops call.
///
/// The primitive is `static inline` in `dsdl_runtime.h`, which the emitted translation unit
/// already includes, so the call needs no declaration of its own.
constexpr llvm::StringRef kRuntimeCopyBits = "dsdl_runtime_copy_bits";

/// @brief `DSDL_RUNTIME_ERROR_INVALID_ARGUMENT`, returned negated as the runtime does.
constexpr std::int64_t kRuntimeErrorInvalidArgument = 2;

/// @brief Width of the length that precedes a delimited nested composite.
constexpr std::int64_t kDelimiterHeaderBits = 32;

/// @brief Converts the dialect's pointer onto the C path's pointer.
///
/// The plan body is built once on `!dsdl.ptr`; this is the half of that bargain the C backend
/// pays. Object emission converts the same type to `!llvm.ptr`.
///
/// The conversion has to reach function signatures, not just operands, because `!dsdl.ptr` is
/// how a plan states what it was handed. An operand-only rewrite would leave the argument type
/// behind and no EmitC operation accepts it.
mlir::TypeConverter makeBitCopyTypeConverter()
{
    mlir::TypeConverter converter;
    converter.addConversion([](mlir::Type type) { return type; });
    converter.addConversion([](mlir::dsdl::OpaqueType named) -> mlir::Type {
        return mlir::emitc::OpaqueType::get(named.getContext(), named.getName());
    });
    converter.addConversion([&converter](mlir::dsdl::PtrType ptr) -> mlir::Type {
        return mlir::emitc::PointerType::get(converter.convertType(ptr.getPointee()));
    });
    return converter;
}

/// @brief Rewrites a bulk bit copy into `dsdl_runtime_copy_bits`.
struct BitWriteLowering final : public mlir::OpConversionPattern<mlir::dsdl::BitWriteOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::BitWriteOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::BitWriteOp           op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        rewriter.replaceOpWithNewOp<mlir::emitc::CallOpaqueOp>(op,
                                                               mlir::TypeRange{},
                                                               rewriter.getStringAttr(kRuntimeCopyBits),
                                                               mlir::ValueRange{adaptor.getDestination(),
                                                                                adaptor.getDestinationBitOffset(),
                                                                                adaptor.getWidth(),
                                                                                adaptor.getSource(),
                                                                                adaptor.getSourceBitOffset()});
        return mlir::success();
    }
};

/// @brief Rewrites a bulk bit read into `dsdl_runtime_get_bits`, which zero-extends a run
///        reaching past the buffer rather than refusing it.
struct BitReadLowering final : public mlir::OpConversionPattern<mlir::dsdl::BitReadOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::BitReadOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::BitReadOp            op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        rewriter.replaceOpWithNewOp<mlir::emitc::CallOpaqueOp>(op,
                                                               mlir::TypeRange{},
                                                               rewriter.getStringAttr("dsdl_runtime_get_bits"),
                                                               mlir::ValueRange{adaptor.getDestination(),
                                                                                adaptor.getBuffer(),
                                                                                adaptor.getBufferSizeBytes(),
                                                                                adaptor.getBitOffset(),
                                                                                adaptor.getWidth()});
        return mlir::success();
    }
};

struct IsNullLowering final : public mlir::OpConversionPattern<mlir::dsdl::IsNullOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::IsNullOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::IsNullOp             op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Type pointerType = adaptor.getPointer().getType();
        auto             null        = mlir::emitc::ConstantOp::create(rewriter,
                                                          op.getLoc(),
                                                          pointerType,
                                                          mlir::emitc::OpaqueAttr::get(rewriter.getContext(), "NULL"));
        rewriter.replaceOpWithNewOp<mlir::emitc::CmpOp>(op,
                                                        rewriter.getI1Type(),
                                                        mlir::emitc::CmpPredicate::eq,
                                                        adaptor.getPointer(),
                                                        null);
        return mlir::success();
    }
};

/// @brief Addresses `pointer[0]`, which is how EmitC spells a dereference that can be assigned.
///
/// `emitc.apply "*"` yields an rvalue and cannot be written through, so both directions go via
/// a zero subscript.
mlir::Value scalarSlot(mlir::ConversionPatternRewriter& rewriter, mlir::Location loc, mlir::Value pointer)
{
    auto indexType = mlir::emitc::OpaqueType::get(rewriter.getContext(), "size_t");
    auto zero      = mlir::emitc::ConstantOp::create(rewriter,
                                                loc,
                                                indexType,
                                                mlir::emitc::OpaqueAttr::get(rewriter.getContext(), "0"));
    return mlir::emitc::SubscriptOp::create(rewriter,
                                            loc,
                                            mlir::cast<mlir::TypedValue<mlir::emitc::PointerType>>(pointer),
                                            zero);
}

struct BufferOrEmptyLowering final : public mlir::OpConversionPattern<mlir::dsdl::BufferOrEmptyOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::BufferOrEmptyOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::BufferOrEmptyOp      op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc         = op.getLoc();
        const mlir::Type     pointerType = adaptor.getBuffer().getType();
        auto                 null        = mlir::emitc::ConstantOp::create(rewriter,
                                                          loc,
                                                          pointerType,
                                                          mlir::emitc::OpaqueAttr::get(rewriter.getContext(), "NULL"));
        auto isNull = mlir::emitc::CmpOp::create(rewriter,
                                                 loc,
                                                 rewriter.getI1Type(),
                                                 mlir::emitc::CmpPredicate::eq,
                                                 adaptor.getBuffer(),
                                                 null);
        // A string literal is the shortest expression of "somewhere readable holding no
        // bytes" that C guarantees; the pointer is never dereferenced past its terminator
        // because the capacity travelling with it is zero.
        auto empty = mlir::emitc::ConstantOp::create(
            rewriter,
            loc,
            pointerType,
            mlir::emitc::OpaqueAttr::get(rewriter.getContext(), "(const uint8_t*)\"\""));
        rewriter.replaceOpWithNewOp<mlir::emitc::ConditionalOp>(op,
                                                                pointerType,
                                                                isNull,
                                                                empty,
                                                                adaptor.getBuffer());
        return mlir::success();
    }
};

struct LoadScalarLowering final : public mlir::OpConversionPattern<mlir::dsdl::LoadScalarOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::LoadScalarOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::LoadScalarOp         op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc  = op.getLoc();
        const mlir::Value    slot = scalarSlot(rewriter, loc, adaptor.getPointer());
        const mlir::Type     stored =
            mlir::cast<mlir::emitc::LValueType>(slot.getType()).getValueType();
        mlir::Value loaded = mlir::emitc::LoadOp::create(rewriter, loc, stored, slot);
        if (stored != op.getValue().getType())
        {
            loaded = mlir::emitc::CastOp::create(rewriter, loc, op.getValue().getType(), loaded);
        }
        rewriter.replaceOp(op, loaded);
        return mlir::success();
    }
};

struct StoreScalarLowering final : public mlir::OpConversionPattern<mlir::dsdl::StoreScalarOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::StoreScalarOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::StoreScalarOp        op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc  = op.getLoc();
        const mlir::Value    slot = scalarSlot(rewriter, loc, adaptor.getPointer());
        const mlir::Type     stored =
            mlir::cast<mlir::emitc::LValueType>(slot.getType()).getValueType();
        mlir::Value value = adaptor.getValue();
        if (stored != value.getType())
        {
            value = mlir::emitc::CastOp::create(rewriter, loc, stored, value);
        }
        rewriter.replaceOpWithNewOp<mlir::emitc::AssignOp>(op, slot, value);
        return mlir::success();
    }
};

/// @brief Names the runtime primitive a scalar access lowers to.
///
/// The runtime spells one primitive per value shape rather than one generic call, so the
/// selection is by value type, width and signedness together. Widths that are not a standard
/// integer size go through the `xx` primitives, which take the width as an argument.
std::string runtimePrimitiveName(const bool write, mlir::Type valueType, const std::int64_t width, const bool isSigned)
{
    if (mlir::isa<mlir::FloatType>(valueType))
    {
        // Selected by the field's width, not the value's. A float16 field travels as a C
        // `float` and is written by set_f16; choosing on the carrier would write 32 bits into
        // a 16-bit slot and report the buffer too small.
        return std::string(write ? "dsdl_runtime_set_f" : "dsdl_runtime_get_f") + std::to_string(width);
    }
    if (width == 1 && !isSigned)
    {
        return write ? "dsdl_runtime_set_bit" : "dsdl_runtime_get_bit";
    }
    if (write)
    {
        return isSigned ? "dsdl_runtime_set_ixx" : "dsdl_runtime_set_uxx";
    }
    // Reads answer in a concrete width, so the primitive is chosen by the smallest standard
    // integer that holds the field rather than by the field width itself.
    const unsigned holder = (width <= 8) ? 8U : (width <= 16) ? 16U : (width <= 32) ? 32U : 64U;
    return std::string(isSigned ? "dsdl_runtime_get_i" : "dsdl_runtime_get_u") + std::to_string(holder);
}

/// @brief Materialises an lvalue holding @p pointer.
///
/// `emitc.member_of_ptr` takes an lvalue of pointer type, and a function parameter is an
/// rvalue, so a member access needs the pointer parked in a variable first.
mlir::Value pointerSlot(mlir::ConversionPatternRewriter& rewriter, mlir::Location loc, mlir::Value pointer)
{
    auto slotType = mlir::emitc::LValueType::get(pointer.getType());
    auto slot     = mlir::emitc::VariableOp::create(rewriter,
                                                loc,
                                                slotType,
                                                mlir::emitc::OpaqueAttr::get(rewriter.getContext(), ""));
    mlir::emitc::AssignOp::create(rewriter, loc, slot, pointer);
    return slot;
}

struct WriteBitsLowering final : public mlir::OpConversionPattern<mlir::dsdl::WriteBitsOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::WriteBitsOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::WriteBitsOp          op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc      = op.getLoc();
        const std::int64_t   width    = op.getWidth();
        const bool           isSigned = op.getIsSigned();
        const std::string    callee   = runtimePrimitiveName(true, op.getValue().getType(), width, isSigned);

        mlir::SmallVector<mlir::Value, 5> args{adaptor.getBuffer(), adaptor.getBufferSizeBytes(),
                                               adaptor.getBitOffset(), adaptor.getValue()};
        // Only the width-carrying integer primitives take a length; the bit and float ones
        // encode it in the name they were selected by.
        if (callee == "dsdl_runtime_set_uxx" || callee == "dsdl_runtime_set_ixx")
        {
            args.push_back(mlir::emitc::ConstantOp::create(rewriter,
                                                           loc,
                                                           rewriter.getIntegerType(8),
                                                           rewriter.getI8IntegerAttr(
                                                               static_cast<std::int8_t>(width))));
        }
        rewriter.replaceOpWithNewOp<mlir::emitc::CallOpaqueOp>(op,
                                                               mlir::TypeRange{rewriter.getIntegerType(8)},
                                                               rewriter.getStringAttr(callee),
                                                               args);
        return mlir::success();
    }
};

struct ReadBitsLowering final : public mlir::OpConversionPattern<mlir::dsdl::ReadBitsOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::ReadBitsOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::ReadBitsOp           op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc    = op.getLoc();
        const std::int64_t   width  = op.getWidth();
        const std::string    callee = runtimePrimitiveName(false, op.getValue().getType(), width, op.getIsSigned());

        mlir::SmallVector<mlir::Value, 4> args{adaptor.getBuffer(),
                                               adaptor.getBufferSizeBytes(),
                                               adaptor.getBitOffset()};
        if (callee.find("_get_u") != std::string::npos || callee.find("_get_i") != std::string::npos)
        {
            args.push_back(mlir::emitc::ConstantOp::create(rewriter,
                                                           loc,
                                                           rewriter.getIntegerType(8),
                                                           rewriter.getI8IntegerAttr(
                                                               static_cast<std::int8_t>(width))));
        }
        rewriter.replaceOpWithNewOp<mlir::emitc::CallOpaqueOp>(op,
                                                               mlir::TypeRange{op.getValue().getType()},
                                                               rewriter.getStringAttr(callee),
                                                               args);
        return mlir::success();
    }
};

/// @brief Walks a member path, answering the lvalue the last name designates.
///
/// The first hop leaves a pointer and uses `member_of_ptr`; the rest are within a value and
/// use `member`. Every hop but the last lands on a struct whose type this pass does not know,
/// so the intermediate lvalues are opaque -- C resolves them, and nothing here has to.
mlir::Value walkMemberPath(mlir::ConversionPatternRewriter& rewriter,
                           mlir::Location                   loc,
                           mlir::Value                      object,
                           mlir::ArrayAttr                  path,
                           mlir::Type                       leafType)
{
    mlir::Value cursor = pointerSlot(rewriter, loc, object);
    for (std::size_t hop = 0; hop < path.size(); ++hop)
    {
        const bool       last     = (hop + 1 == path.size());
        const mlir::Type hopType  = last ? leafType
                                         : mlir::emitc::OpaqueType::get(rewriter.getContext(), "struct");
        auto             member   = mlir::cast<mlir::StringAttr>(path[hop]);
        if (hop == 0)
        {
            cursor = mlir::emitc::MemberOfPtrOp::create(rewriter,
                                                        loc,
                                                        mlir::emitc::LValueType::get(hopType),
                                                        member,
                                                        cursor);
        }
        else
        {
            cursor = mlir::emitc::MemberOp::create(rewriter,
                                                   loc,
                                                   mlir::emitc::LValueType::get(hopType),
                                                   member,
                                                   cursor);
        }
    }
    return cursor;
}

struct LoadMemberLowering final : public mlir::OpConversionPattern<mlir::dsdl::LoadMemberOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::LoadMemberOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::LoadMemberOp         op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Value slot =
            walkMemberPath(rewriter, op.getLoc(), adaptor.getObject(), op.getPath(), op.getValue().getType());
        rewriter.replaceOpWithNewOp<mlir::emitc::LoadOp>(op, op.getValue().getType(), slot);
        return mlir::success();
    }
};

struct StoreMemberLowering final : public mlir::OpConversionPattern<mlir::dsdl::StoreMemberOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::StoreMemberOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::StoreMemberOp        op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Value slot = walkMemberPath(rewriter,
                                                op.getLoc(),
                                                adaptor.getObject(),
                                                op.getPath(),
                                                adaptor.getValue().getType());
        rewriter.replaceOpWithNewOp<mlir::emitc::AssignOp>(op, slot, adaptor.getValue());
        return mlir::success();
    }
};

/// @brief Addresses one element of the array storage a path reaches.
mlir::Value elementSlot(mlir::ConversionPatternRewriter& rewriter,
                        mlir::Location                   loc,
                        mlir::Value                      object,
                        mlir::ArrayAttr                  path,
                        mlir::Value                      index,
                        llvm::StringRef                  elementTypeName)
{
    // The member is reached at whatever qualification it is declared with -- a serializer
    // holds the object by pointer-to-const -- and then the pointer is taken unqualified. The
    // element read out of it is assigned to a variable, and EmitC declares its variables
    // before assigning them, which a const-qualified declaration does not survive.
    auto*             ctx        = rewriter.getContext();
    const std::string bare       = elementTypeName.starts_with("const ")
                                       ? elementTypeName.drop_front(std::strlen("const ")).str()
                                       : elementTypeName.str();
    auto              declared   = mlir::emitc::PointerType::get(mlir::emitc::OpaqueType::get(ctx, elementTypeName));
    auto              unqualified = mlir::emitc::PointerType::get(mlir::emitc::OpaqueType::get(ctx, bare));

    auto        storageSlot = walkMemberPath(rewriter, loc, object, path, declared);
    mlir::Value storage     = mlir::emitc::LoadOp::create(rewriter, loc, declared, storageSlot);
    if (declared != unqualified)
    {
        storage = mlir::emitc::CastOp::create(rewriter, loc, unqualified, storage);
    }
    return mlir::emitc::SubscriptOp::create(rewriter,
                                            loc,
                                            mlir::cast<mlir::TypedValue<mlir::emitc::PointerType>>(storage),
                                            index);
}

struct LoadElementLowering final : public mlir::OpConversionPattern<mlir::dsdl::LoadElementOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::LoadElementOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::LoadElementOp        op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc  = op.getLoc();
        const mlir::Value    slot = elementSlot(rewriter,
                                             loc,
                                             adaptor.getObject(),
                                             op.getPath(),
                                             adaptor.getIndex(),
                                             op.getElementType());
        const mlir::Type stored = mlir::cast<mlir::emitc::LValueType>(slot.getType()).getValueType();
        mlir::Value      loaded = mlir::emitc::LoadOp::create(rewriter, loc, stored, slot);
        if (stored != op.getValue().getType())
        {
            loaded = mlir::emitc::CastOp::create(rewriter, loc, op.getValue().getType(), loaded);
        }
        rewriter.replaceOp(op, loaded);
        return mlir::success();
    }
};

struct StoreElementLowering final : public mlir::OpConversionPattern<mlir::dsdl::StoreElementOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::StoreElementOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::StoreElementOp       op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc  = op.getLoc();
        const mlir::Value    slot = elementSlot(rewriter,
                                             loc,
                                             adaptor.getObject(),
                                             op.getPath(),
                                             adaptor.getIndex(),
                                             op.getElementType());
        const mlir::Type stored = mlir::cast<mlir::emitc::LValueType>(slot.getType()).getValueType();
        mlir::Value      value  = adaptor.getValue();
        if (stored != value.getType())
        {
            value = mlir::emitc::CastOp::create(rewriter, loc, stored, value);
        }
        rewriter.replaceOpWithNewOp<mlir::emitc::AssignOp>(op, slot, value);
        return mlir::success();
    }
};

//===----------------------------------------------------------------------===//
// Building a plan body as operations
//===----------------------------------------------------------------------===//

/// @brief What a step carries forward: how far into the wire it got, and what went wrong.
///
/// Both travel as values rather than as constants folded at build time. A variable-length
/// array makes the offset depend on a count read from the object, so no offset after the
/// first array is known here.
struct PlanCursor final
{
    mlir::Value bitOffset;
    mlir::Value error;
};

/// @brief Whether a plan's steps can be built as operations yet.
///
/// Scalars and arrays of scalars. Composites and unions each need their own shape and are
/// still rendered as text, so a plan containing one falls back whole rather than in part: a
/// function is one body, and it is either operations or a string.
/// @brief Whether one field step can be built as operations.
///
/// Shared by the two shapes a plan comes in. A union is one field per option and a struct is
/// a sequence of them, but what a single field needs is the same either way, and having the
/// two disagree is how an option gets accepted that the arm builder cannot emit.
bool supportsFieldStep(const PlanStep& step)
{
    const bool isArray = !step.arrayKind.empty() && (step.arrayKind != "none");
    if (!step.compositeCTypeName.empty())
    {
        // An array of delimited composites needs a length header per element, which the
        // element loop does not write.
        return !(isArray && !step.compositeSealed);
    }
    if ((step.scalarCategory != "unsigned") && (step.scalarCategory != "signed") &&
        (step.scalarCategory != "float") && (step.scalarCategory != "bool"))
    {
        return false;
    }

    if ((step.bitLength <= 0) || (step.bitLength > 64))
    {
        return false;
    }
    if (isArray)
    {
        if (isVariableArrayKind(step.arrayKind))
        {
            return (step.arrayLengthPrefixBits > 0) && (step.arrayLengthPrefixBits <= 64);
        }
        // A fixed array's length is its declaration, so an absent one leaves nothing to loop
        // over.
        return step.arrayCapacity > 0;
    }
    return true;
}

bool supportsOperationLowering(const std::vector<PlanStep>& steps, const bool isUnion)
{
    if (steps.empty())
    {
        // A type with no fields, or a service section with none, encodes nothing. The body is
        // its prologue and epilogue, which the builders emit regardless. A union with no
        // options is a different matter and is rejected below.
        return !isUnion;
    }
    if (isUnion)
    {
        // A union is one field per option, each aligned on its own through its
        // alignmentBits. Any alignment step in the list is not a member of the union and is
        // passed over the same way the option collection passes over it.
        return std::all_of(steps.begin(), steps.end(), [](const PlanStep& step) {
            if (step.kind == PlanStepKind::Align)
            {
                return step.bits > 0;
            }
            return (step.kind == PlanStepKind::Field) && supportsFieldStep(step);
        });
    }
    for (const auto& step : steps)
    {
        if ((step.kind == PlanStepKind::Align) || (step.kind == PlanStepKind::Padding))
        {
            // An alignment is a jump to the next boundary and a void field is a run of
            // reserved bits; both are widths, and a width of nothing is not one.
            if (step.bits <= 0)
            {
                return false;
            }
            continue;
        }
        if ((step.kind != PlanStepKind::Field) || !supportsFieldStep(step))
        {
            return false;
        }
    }
    return true;
}



mlir::Value constantI64(mlir::OpBuilder& b, mlir::Location loc, const std::int64_t value)
{
    return mlir::arith::ConstantOp::create(b, loc, b.getI64IntegerAttr(value));
}

mlir::Value constantI8(mlir::OpBuilder& b, mlir::Location loc, const std::int64_t value)
{
    return mlir::arith::ConstantOp::create(b, loc, b.getI8IntegerAttr(static_cast<std::int8_t>(value)));
}

mlir::Value isHealthy(mlir::OpBuilder& b, mlir::Location loc, mlir::Value error)
{
    return mlir::arith::CmpIOp::create(b, loc, mlir::arith::CmpIPredicate::eq, error, constantI8(b, loc, 0));
}

bool stepIsComposite(const PlanStep& step)
{
    return !step.compositeCTypeName.empty();
}

bool stepIsBitpackedArray(const PlanStep& step);

/// A bool array moves as one run of bits, and an array of composites element by element; both
/// are defined below, beside the other nesting builders, and reached from the array builders
/// above them.
PlanCursor buildBitpackedArray(mlir::OpBuilder&   b,
                               mlir::Location     loc,
                               const PlanStep&    step,
                               const std::int64_t memberIndex,
                               mlir::Value        object,
                               mlir::Value      buffer,
                               mlir::Value      capacityBytes,
                               PlanCursor       cursor,
                               mlir::Value      count,
                               bool             writing);

PlanCursor buildCompositeElementLoop(mlir::OpBuilder&   b,
                                     mlir::Location     loc,
                                     const PlanStep&    step,
                                     const std::int64_t memberIndex,
                                     mlir::Value        object,
                                     mlir::Value      buffer,
                                     mlir::Value      capacityBytes,
                                     PlanCursor       cursor,
                                     mlir::Value      count,
                                     bool             writing);

bool stepIsArray(const PlanStep& step)
{
    return !step.arrayKind.empty() && (step.arrayKind != "none");
}

/// @brief The value type a step's saturation helper and wire access are expressed in.
mlir::Type stepValueType(mlir::OpBuilder& b, const PlanStep& step)
{
    if (step.scalarCategory == "float")
    {
        return (step.bitLength <= 32) ? b.getF32Type() : b.getF64Type();
    }
    return b.getIntegerType(64);
}

/// @brief The member path reaching a step's storage.
///
/// A scalar field is one name. An array's count and elements are two, because the generated
/// struct holds them in a nested member of its own.
mlir::ArrayAttr stepPath(mlir::OpBuilder& b, const PlanStep& step, llvm::StringRef leaf)
{
    if (leaf.empty())
    {
        return b.getStrArrayAttr({step.cName});
    }
    return b.getStrArrayAttr({step.cName, leaf});
}

/// @brief The C spelling of one array element's storage.
std::string elementTypeName(const PlanStep& step)
{
    if (step.scalarCategory == "float")
    {
        return (step.bitLength <= 32) ? "float" : "double";
    }
    const unsigned holder = (step.bitLength <= 8)    ? 8U
                            : (step.bitLength <= 16) ? 16U
                            : (step.bitLength <= 32) ? 32U
                                                     : 64U;
    return std::string(step.scalarCategory == "signed" ? "int" : "uint") + std::to_string(holder) + "_t";
}

bool stepIsFixedArray(const PlanStep& step)
{
    return stepIsArray(step) && !isVariableArrayKind(step.arrayKind);
}

/// @brief The path to a step's element storage.
///
/// A variable-length array is a member holding a count and the elements beside it. A fixed one
/// is the elements: it has no count to hold, its length being in its declaration.
/// @brief Marks an access whose member is signed, which decides how a load widens it.
///
/// The C path gets this from the member's declared type; an object lowering has only the
/// struct, where a width carries no sign.
void markSigned(mlir::Operation* op, const PlanStep& step)
{
    if (step.scalarCategory == "signed")
    {
        op->setAttr("llvmdsdl.is_signed", mlir::UnitAttr::get(op->getContext()));
    }
}

mlir::ArrayAttr elementPath(mlir::OpBuilder& b, const PlanStep& step)
{
    return stepIsFixedArray(step) ? b.getStrArrayAttr({step.cName})
                                  : b.getStrArrayAttr({step.cName, "elements"});
}

mlir::DenseI64ArrayAttr elementIndices(mlir::OpBuilder& b, const PlanStep& step, const std::int64_t memberIndex)
{
    // A fixed array is the member; a variable one holds its elements in the first position of
    // the pair the member is, its count in the second.
    return stepIsFixedArray(step) ? b.getDenseI64ArrayAttr({memberIndex})
                                  : b.getDenseI64ArrayAttr({memberIndex, 0});
}

std::string serHelperFor(const PlanStep& step)
{
    return (step.scalarCategory == "float")    ? step.serFloatHelper
           : (step.scalarCategory == "signed") ? step.serSignedHelper
                                               : step.serUnsignedHelper;
}

std::string deserHelperFor(const PlanStep& step)
{
    return (step.scalarCategory == "float")    ? step.deserFloatHelper
           : (step.scalarCategory == "signed") ? step.deserSignedHelper
                                               : step.deserUnsignedHelper;
}

mlir::Value applyHelper(mlir::OpBuilder& b, mlir::Location loc, llvm::StringRef helper, mlir::Value value)
{
    if (helper.empty())
    {
        return value;
    }
    auto call = mlir::func::CallOp::create(b,
                                           loc,
                                           mlir::SymbolRefAttr::get(b.getContext(), helper),
                                           mlir::TypeRange{value.getType()},
                                           mlir::ValueRange{value});
    return call.getResult(0);
}

/// @brief Keeps the first error. A later check does not get to overwrite an earlier failure.
///
/// The plan stops at the first thing that goes wrong, so a check that runs after one has
/// already failed reports what was already known rather than what it makes of state the
/// failure left behind.
mlir::Value foldError(mlir::OpBuilder& b, mlir::Location loc, mlir::Value existing, mlir::Value next)
{
    return mlir::arith::SelectOp::create(b, loc, isHealthy(b, loc, existing), next, existing);
}

/// @brief Calls a helper that answers an error code.
mlir::Value callErrorHelper(mlir::OpBuilder&  b,
                            mlir::Location    loc,
                            llvm::StringRef   symbol,
                            mlir::ValueRange  arguments)
{
    auto call = mlir::func::CallOp::create(b,
                                           loc,
                                           mlir::SymbolRefAttr::get(b.getContext(), symbol),
                                           mlir::TypeRange{b.getIntegerType(8)},
                                           arguments);
    return call.getResult(0);
}

/// @brief Writes one already-normalised value and advances the cursor past it.
PlanCursor emitWrite(mlir::OpBuilder&   b,
                     mlir::Location     loc,
                     mlir::Value        buffer,
                     mlir::Value        capacityBytes,
                     PlanCursor         cursor,
                     mlir::Value        value,
                     const std::int64_t width,
                     const bool         isSigned)
{
    auto write = mlir::dsdl::WriteBitsOp::create(b,
                                                 loc,
                                                 b.getIntegerType(8),
                                                 buffer,
                                                 capacityBytes,
                                                 cursor.bitOffset,
                                                 value,
                                                 b.getI64IntegerAttr(width),
                                                 isSigned ? b.getUnitAttr() : nullptr);
    return PlanCursor{mlir::arith::AddIOp::create(b, loc, cursor.bitOffset, constantI64(b, loc, width)),
                      write.getError()};
}

/// @brief Runs @p body only while nothing has failed, threading the cursor either way.
template <typename BodyFn>
PlanCursor guarded(mlir::OpBuilder& b, mlir::Location loc, PlanCursor cursor, BodyFn body)
{
    const mlir::SmallVector<mlir::Type, 2> types{b.getIntegerType(64), b.getIntegerType(8)};
    auto guard = mlir::scf::IfOp::create(b, loc, types, isHealthy(b, loc, cursor.error), true);
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        b.setInsertionPointToStart(guard.thenBlock());
        const PlanCursor next = body(cursor);
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{next.bitOffset, next.error});
    }
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        b.setInsertionPointToStart(guard.elseBlock());
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{cursor.bitOffset, cursor.error});
    }
    return PlanCursor{guard.getResult(0), guard.getResult(1)};
}

/// @brief Serializes one scalar field.
PlanCursor buildScalarWrite(mlir::OpBuilder&   b,
                            mlir::Location     loc,
                            const PlanStep&    step,
                            const std::int64_t memberIndex,
                            mlir::Value        object,
                            mlir::Value      buffer,
                            mlir::Value      capacityBytes,
                            PlanCursor       cursor)
{
    return guarded(b, loc, cursor, [&](PlanCursor inner) {
        const mlir::Type valueType = stepValueType(b, step);
        mlir::Value      member    = mlir::dsdl::LoadMemberOp::create(b,
                                                                 loc,
                                                                 valueType,
                                                                 object,
                                                                 stepPath(b, step, {}),
                                                                 b.getDenseI64ArrayAttr(llvm::ArrayRef<std::int64_t>{memberIndex}));
        markSigned(member.getDefiningOp(), step);
        member = applyHelper(b, loc, serHelperFor(step), member);
        return emitWrite(b,
                         loc,
                         buffer,
                         capacityBytes,
                         inner,
                         member,
                         step.bitLength,
                         step.scalarCategory == "signed");
    });
}

/// @brief Serializes one variable-length array: a validated count, its prefix, then elements.
PlanCursor buildArrayWrite(mlir::OpBuilder&   b,
                           mlir::Location     loc,
                           const PlanStep&    step,
                           const std::int64_t memberIndex,
                           mlir::Value        object,
                           mlir::Value      buffer,
                           mlir::Value      capacityBytes,
                           PlanCursor       cursor)
{
    return guarded(b, loc, cursor, [&](PlanCursor inner) {
        auto i64Ty = b.getIntegerType(64);
        const bool fixed = stepIsFixedArray(step);

        // A fixed array's length is in its declaration, so there is nothing to read from the
        // object, nothing that could be out of range, and nothing to announce on the wire.
        mlir::Value count = constantI64(b, loc, step.arrayCapacity);
        if (!fixed)
        {
            count = mlir::dsdl::LoadMemberOp::create(b,
                                                     loc,
                                                     i64Ty,
                                                     object,
                                                     stepPath(b, step, "count"),
                                                     b.getDenseI64ArrayAttr(llvm::ArrayRef<std::int64_t>{memberIndex, 1}));

            // A count past the declared capacity is the plan's error to report, not the wire's.
            if (!step.arrayLengthValidateHelper.empty())
            {
                inner.error = callErrorHelper(b, loc, step.arrayLengthValidateHelper, mlir::ValueRange{count});
            }
        }

        return guarded(b, loc, inner, [&](PlanCursor afterCheck) {
            PlanCursor afterPrefix = afterCheck;
            if (!fixed)
            {
                mlir::Value wireLength = applyHelper(b, loc, step.serArrayLengthPrefixHelper, count);
                afterPrefix =
                    emitWrite(b, loc, buffer, capacityBytes, afterCheck, wireLength, step.arrayLengthPrefixBits, false);
            }

            if (stepIsComposite(step))
            {
                return buildCompositeElementLoop(
                    b, loc, step, memberIndex, object, buffer, capacityBytes, afterPrefix, count, true);
            }
            if (stepIsBitpackedArray(step))
            {
                return buildBitpackedArray(
                    b, loc, step, memberIndex, object, buffer, capacityBytes, afterPrefix, count, true);
            }

            // Driven by the offset rather than by a separate index. An scf.while's results
            // are exactly the values its condition forwards, so a carried index would also
            // be a result, and nothing after the loop reads it -- which the emitted C
            // declares and never uses. The index is recoverable from the offset, the
            // element width being fixed.
            const mlir::Value start = afterPrefix.bitOffset;
            const mlir::Value width = constantI64(b, loc, step.bitLength);
            const mlir::Value end   = mlir::arith::AddIOp::create(
                b, loc, start, mlir::arith::MulIOp::create(b, loc, count, width));

            const mlir::SmallVector<mlir::Type, 2> loopTypes{i64Ty, b.getIntegerType(8)};
            auto loop = mlir::scf::WhileOp::create(
                b, loc, loopTypes, mlir::ValueRange{start, afterPrefix.error});

            {
                mlir::OpBuilder::InsertionGuard const g(b);
                mlir::Block* before =
                    b.createBlock(&loop.getBefore(), {}, {i64Ty, b.getIntegerType(8)}, {loc, loc});
                b.setInsertionPointToStart(before);
                const mlir::Value more = mlir::arith::CmpIOp::create(b,
                                                                     loc,
                                                                     mlir::arith::CmpIPredicate::ult,
                                                                     before->getArgument(0),
                                                                     end);
                const mlir::Value keep = mlir::arith::AndIOp::create(
                    b, loc, more, isHealthy(b, loc, before->getArgument(1)));
                mlir::scf::ConditionOp::create(b, loc, keep, before->getArguments());
            }
            {
                mlir::OpBuilder::InsertionGuard const g(b);
                mlir::Block* after =
                    b.createBlock(&loop.getAfter(), {}, {i64Ty, b.getIntegerType(8)}, {loc, loc});
                b.setInsertionPointToStart(after);
                const mlir::Value offset   = after->getArgument(0);
                const mlir::Value incoming = after->getArgument(1);
                const mlir::Value index    = mlir::arith::DivUIOp::create(
                    b, loc, mlir::arith::SubIOp::create(b, loc, offset, start), width);

                const mlir::Type valueType = stepValueType(b, step);
                mlir::Value      element   = mlir::dsdl::LoadElementOp::create(
                    b,
                    loc,
                    valueType,
                    object,
                    elementPath(b, step),
                    elementIndices(b, step, memberIndex),
                    index,
                    b.getStringAttr("const " + elementTypeName(step)));
                markSigned(element.getDefiningOp(), step);
                element = applyHelper(b, loc, serHelperFor(step), element);

                const PlanCursor written = emitWrite(b,
                                                     loc,
                                                     buffer,
                                                     capacityBytes,
                                                     PlanCursor{offset, incoming},
                                                     element,
                                                     step.bitLength,
                                                     step.scalarCategory == "signed");
                const mlir::Value carried = mlir::arith::SelectOp::create(b,
                                                                          loc,
                                                                          isHealthy(b, loc, incoming),
                                                                          written.error,
                                                                          incoming);
                mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{written.bitOffset, carried});
            }
            return PlanCursor{loop.getResult(0), loop.getResult(1)};
        });
    });
}

/// @brief Pads to the next byte boundary, one zero bit at a time.
///
/// The count is not known here once an array is involved, so this is the loop the text form
/// writes rather than the unrolled writes a fixed layout would allow.
/// @brief Writes zero bits from the cursor up to @p end.
PlanCursor buildZeroBitsTo(mlir::OpBuilder& b,
                           mlir::Location   loc,
                           mlir::Value      buffer,
                           mlir::Value      capacityBytes,
                           PlanCursor       cursor,
                           mlir::Value      end)
{
    auto i64Ty = b.getIntegerType(64);
    auto i8Ty  = b.getIntegerType(8);

    auto loop = mlir::scf::WhileOp::create(b,
                                           loc,
                                           mlir::TypeRange{i64Ty, i8Ty},
                                           mlir::ValueRange{cursor.bitOffset, cursor.error});
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        mlir::Block* before = b.createBlock(&loop.getBefore(), {}, {i64Ty, i8Ty}, {loc, loc});
        b.setInsertionPointToStart(before);
        const mlir::Value more = mlir::arith::CmpIOp::create(b,
                                                             loc,
                                                             mlir::arith::CmpIPredicate::ult,
                                                             before->getArgument(0),
                                                             end);
        const mlir::Value keep =
            mlir::arith::AndIOp::create(b, loc, more, isHealthy(b, loc, before->getArgument(1)));
        mlir::scf::ConditionOp::create(b, loc, keep, before->getArguments());
    }
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        mlir::Block* after = b.createBlock(&loop.getAfter(), {}, {i64Ty, i8Ty}, {loc, loc});
        b.setInsertionPointToStart(after);
        const mlir::Value zeroBit  = mlir::arith::ConstantOp::create(b, loc, b.getBoolAttr(false));
        const mlir::Value incoming = after->getArgument(1);
        const PlanCursor  written  = emitWrite(b,
                                             loc,
                                             buffer,
                                             capacityBytes,
                                             PlanCursor{after->getArgument(0), incoming},
                                             zeroBit,
                                             1,
                                             false);
        // Every value a loop carries has to be read in its body. The condition already
        // guarantees this one is clear, so the select always takes the write's own result --
        // but binding it and dropping it leaves a declaration the emitted C never uses.
        const mlir::Value carried = mlir::arith::SelectOp::create(b,
                                                                  loc,
                                                                  isHealthy(b, loc, incoming),
                                                                  written.error,
                                                                  incoming);
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{written.bitOffset, carried});
    }
    return PlanCursor{loop.getResult(0), loop.getResult(1)};
}

/// @brief Encodes or skips a void field of @p bits.
///
/// Reserved bits are written as zeros and read as nothing: a decoder has no name to put them
/// under, so it only steps over them.
PlanCursor buildPaddingStep(mlir::OpBuilder&   b,
                            mlir::Location     loc,
                            mlir::Value        buffer,
                            mlir::Value        capacityBytes,
                            PlanCursor         cursor,
                            const std::int64_t bits,
                            const bool         writing)
{
    const mlir::Value end =
        mlir::arith::AddIOp::create(b, loc, cursor.bitOffset, constantI64(b, loc, bits));
    if (!writing)
    {
        return PlanCursor{end, cursor.error};
    }
    return guarded(b, loc, cursor, [&](PlanCursor inner) {
        return buildZeroBitsTo(b, loc, buffer, capacityBytes, inner, end);
    });
}

PlanCursor buildFinalPadding(mlir::OpBuilder&            b,
                             mlir::Location              loc,
                             mlir::Value                 buffer,
                             mlir::Value                 capacityBytes,
                             PlanCursor                  cursor,
                             std::optional<std::int64_t> staticBitOffset)
{
    // A plan of fixed-width scalars ends at a known offset, so the padding is a known number
    // of writes and needs no loop. Only a variable-length array makes the end unknown.
    if (staticBitOffset.has_value())
    {
        const std::int64_t aligned = ((*staticBitOffset + 7) / 8) * 8;
        if (aligned == *staticBitOffset)
        {
            // Already on a boundary, so there is nothing to pad and the running offset
            // stands as it is.
            return cursor;
        }
        for (std::int64_t bit = *staticBitOffset; bit < aligned; ++bit)
        {
            cursor = guarded(b, loc, cursor, [&](PlanCursor inner) {
                const mlir::Value zeroBit = mlir::arith::ConstantOp::create(b, loc, b.getBoolAttr(false));
                return emitWrite(b, loc, buffer, capacityBytes, inner, zeroBit, 1, false);
            });
        }
        return PlanCursor{constantI64(b, loc, aligned), cursor.error};
    }

    const mlir::Value seven   = constantI64(b, loc, 7);
    const mlir::Value eight   = constantI64(b, loc, 8);
    const mlir::Value rounded = mlir::arith::AddIOp::create(b, loc, cursor.bitOffset, seven);
    const mlir::Value bytes   = mlir::arith::DivUIOp::create(b, loc, rounded, eight);
    const mlir::Value aligned = mlir::arith::MulIOp::create(b, loc, bytes, eight);
    return buildZeroBitsTo(b, loc, buffer, capacityBytes, cursor, aligned);
}



/// @brief Whether a step leaves the plan on a byte boundary, given it began on one.
///
/// A composite does: it consumes whole bytes, whatever it contains. That is what makes the
/// alignment before the next one a no-op, and it holds without knowing any offset.
bool stepPreservesByteAlignment(const PlanStep& step)
{
    if ((step.kind == PlanStepKind::Align) || stepIsComposite(step))
    {
        return true;
    }
    if (step.kind == PlanStepKind::Padding)
    {
        return (step.bits % 8) == 0;
    }
    if ((step.bitLength % 8) != 0)
    {
        return false;
    }
    return !stepIsArray(step) || ((step.arrayLengthPrefixBits % 8) == 0);
}

/// @brief Rounds the running offset up to a byte boundary.
///
/// Serializing has to write the bits it skips, because the buffer is the output. Reading does
/// not: the offset simply moves.
PlanCursor buildAlignment(mlir::OpBuilder& b,
                          mlir::Location   loc,
                          mlir::Value      buffer,
                          mlir::Value      capacityBytes,
                          PlanCursor       cursor,
                          const bool       writing,
                          const bool       alreadyAligned)
{
    if (alreadyAligned)
    {
        // Nothing to skip to. Worth knowing statically rather than emitting a loop that
        // never runs: every field before this one occupied whole bytes, or a nested
        // composite left the plan on a boundary by construction.
        return cursor;
    }
    const mlir::Value seven   = constantI64(b, loc, 7);
    const mlir::Value eight   = constantI64(b, loc, 8);
    const mlir::Value rounded = mlir::arith::AddIOp::create(b, loc, cursor.bitOffset, seven);
    const mlir::Value bytes   = mlir::arith::DivUIOp::create(b, loc, rounded, eight);
    const mlir::Value aligned = mlir::arith::MulIOp::create(b, loc, bytes, eight);
    if (!writing)
    {
        return PlanCursor{aligned, cursor.error};
    }
    return buildFinalPadding(b, loc, buffer, capacityBytes, cursor, std::nullopt);
}

/// @brief The space the wire buffer has left at the plan's current position.
mlir::Value remainingBytes(mlir::OpBuilder& b, mlir::Location loc, mlir::Value capacityBytes, mlir::Value bitOffset)
{
    const mlir::Value used  = mlir::arith::DivUIOp::create(b, loc, bitOffset, constantI64(b, loc, 8));
    const mlir::Value fits  = mlir::arith::CmpIOp::create(b, loc, mlir::arith::CmpIPredicate::ult, used, capacityBytes);
    const mlir::Value taken = mlir::arith::SelectOp::create(b, loc, fits, used, capacityBytes);
    return mlir::arith::SubIOp::create(b, loc, capacityBytes, taken);
}

/// @brief Encodes or decodes one sealed nested composite through its own entry point.
///
/// The nested type is handed the member, the point the container reached, and the space
/// left; it answers with what it used, and the container advances by that. The container
/// does not know the nested layout and does not need to.
PlanCursor buildCompositeStep(mlir::OpBuilder&   b,
                              mlir::Location     loc,
                              const PlanStep&    step,
                              const std::int64_t memberIndex,
                              mlir::Value        object,
                              mlir::Value        buffer,
                              mlir::Value        capacityBytes,
                              PlanCursor         cursor,
                              const bool         writing)
{
    return guarded(b, loc, cursor, [&](PlanCursor inner) {
        auto* ctx    = b.getContext();
        auto  i64Ty  = b.getIntegerType(64);
        auto  sizePtr = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, "size_t"));

        const std::string qualifier   = writing ? "const " : "";
        auto              nestedPtr   = mlir::dsdl::PtrType::get(
            ctx, mlir::dsdl::OpaqueType::get(ctx, qualifier + step.compositeCTypeName));
        auto bufferPtr = mlir::dsdl::PtrType::get(
            ctx, mlir::dsdl::OpaqueType::get(ctx, writing ? "uint8_t" : "const uint8_t"));

        const mlir::Value available = remainingBytes(b, loc, capacityBytes, inner.bitOffset);
        const mlir::Value sizeSlot  = mlir::dsdl::LocalOp::create(b, loc, sizePtr, available);
        const mlir::Value memberPtr = mlir::dsdl::MemberAddrOp::create(b,
                                                                       loc,
                                                                       nestedPtr,
                                                                       object,
                                                                       b.getStrArrayAttr({step.cName}),
                                                                       b.getDenseI64ArrayAttr({memberIndex}));
        const mlir::Value byteOffset =
            mlir::arith::DivUIOp::create(b, loc, inner.bitOffset, constantI64(b, loc, 8));
        const mlir::Value at = mlir::dsdl::BufferAtOp::create(b, loc, bufferPtr, buffer, byteOffset);

        const std::string callee = step.compositeCTypeName + (writing ? "__serialize_" : "__deserialize_");
        auto              call   = mlir::dsdl::CallSerdesOp::create(b,
                                                    loc,
                                                    b.getIntegerType(8),
                                                    b.getStringAttr(callee),
                                                    memberPtr,
                                                    at,
                                                    sizeSlot);

        // Read back before branching on the error: the nested call reports what it used in
        // the same place either way, and the guard below decides whether it counts.
        const mlir::Value used = mlir::dsdl::LoadScalarOp::create(b, loc, i64Ty, sizeSlot);
        const mlir::Value advanced =
            mlir::arith::AddIOp::create(b,
                                        loc,
                                        inner.bitOffset,
                                        mlir::arith::MulIOp::create(b, loc, used, constantI64(b, loc, 8)));
        const mlir::Value ok      = isHealthy(b, loc, call.getError());
        const mlir::Value nextOff = mlir::arith::SelectOp::create(b, loc, ok, advanced, inner.bitOffset);
        return PlanCursor{nextOff, call.getError()};
    });
}


/// @brief The union's options, one field each, in tag order.
std::vector<const PlanStep*> unionOptionsOf(const std::vector<PlanStep>& steps)
{
    std::vector<const PlanStep*> options;
    for (const auto& step : steps)
    {
        if (step.kind == PlanStepKind::Field)
        {
            options.push_back(&step);
        }
    }
    std::ranges::sort(options, [](const PlanStep* lhs, const PlanStep* rhs) {
        return lhs->unionOptionIndex < rhs->unionOptionIndex;
    });
    return options;
}

/// @brief Runs one option's steps when the tag selects it, and passes the cursor through
///        untouched otherwise.
///
/// A union encodes exactly one of its options, so the chain of tests is the plan: whichever
/// arm the tag selects contributes, and the rest contribute nothing. An option that the tag
/// did not select must not advance the offset, which is why each arm yields the cursor it
/// was given rather than a merged one.
template <typename StepFn>
PlanCursor buildUnionOption(mlir::OpBuilder& b,
                            mlir::Location   loc,
                            mlir::Value      tag,
                            const PlanStep&  option,
                            PlanCursor       cursor,
                            StepFn           emitStep)
{
    const mlir::Value selected = mlir::arith::CmpIOp::create(
        b, loc, mlir::arith::CmpIPredicate::eq, tag, constantI64(b, loc, option.unionOptionIndex));

    const mlir::SmallVector<mlir::Type, 2> types{b.getIntegerType(64), b.getIntegerType(8)};
    auto arm = mlir::scf::IfOp::create(b, loc, types, selected, true);
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        b.setInsertionPointToStart(arm.thenBlock());
        const PlanCursor next = emitStep(cursor);
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{next.bitOffset, next.error});
    }
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        b.setInsertionPointToStart(arm.elseBlock());
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{cursor.bitOffset, cursor.error});
    }
    return PlanCursor{arm.getResult(0), arm.getResult(1)};
}


/// @brief Encodes or decodes one delimited nested composite.
///
/// A delimited nested type is preceded by its own length in bytes, so that a reader which
/// does not know the type can step over it. That is the whole point of the header, and it is
/// why the decoder advances by the length it was told rather than by what the nested decode
/// consumed: a newer sender may have written fields this reader has no name for, and skipping
/// only what was understood would leave the cursor inside them.
PlanCursor buildDelimitedCompositeStep(mlir::OpBuilder&   b,
                                       mlir::Location     loc,
                                       const PlanStep&    step,
                                       const std::int64_t memberIndex,
                                       mlir::Value        object,
                                       mlir::Value        buffer,
                                       mlir::Value        capacityBytes,
                                       PlanCursor         cursor,
                                       const bool         writing)
{
    return guarded(b, loc, cursor, [&](PlanCursor inner) {
        auto* ctx     = b.getContext();
        auto  i64Ty   = b.getIntegerType(64);
        auto  sizePtr = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, "size_t"));
        auto  nestedPtr = mlir::dsdl::PtrType::get(
            ctx, mlir::dsdl::OpaqueType::get(ctx, (writing ? "const " : "") + step.compositeCTypeName));
        auto bufferPtr = mlir::dsdl::PtrType::get(
            ctx, mlir::dsdl::OpaqueType::get(ctx, writing ? "uint8_t" : "const uint8_t"));

        const mlir::Value eight        = constantI64(b, loc, 8);
        const mlir::Value headerOffset = inner.bitOffset;

        mlir::Value declared;
        if (!writing)
        {
            declared = mlir::dsdl::ReadBitsOp::create(b,
                                                      loc,
                                                      i64Ty,
                                                      buffer,
                                                      capacityBytes,
                                                      headerOffset,
                                                      b.getI64IntegerAttr(kDelimiterHeaderBits),
                                                      nullptr);
        }
        const mlir::Value afterHeader =
            mlir::arith::AddIOp::create(b, loc, headerOffset, constantI64(b, loc, kDelimiterHeaderBits));
        const mlir::Value remaining = remainingBytes(b, loc, capacityBytes, afterHeader);

        // Serializing does not know the length until the nested type reports it, so the header
        // is reserved here and written once the encoding below has run.
        const mlir::Value sizeInit = writing ? remaining : declared;
        const mlir::Value sizeSlot = mlir::dsdl::LocalOp::create(b, loc, sizePtr, sizeInit);

        mlir::Value error = inner.error;
        if (!writing && !step.delimiterValidateHelper.empty())
        {
            error = foldError(
                b,
                loc,
                error,
                callErrorHelper(b, loc, step.delimiterValidateHelper, mlir::ValueRange{declared, remaining}));
        }

        return guarded(b, loc, PlanCursor{afterHeader, error}, [&](PlanCursor ready) {
            const mlir::Value memberPtr = mlir::dsdl::MemberAddrOp::create(b,
                                                                           loc,
                                                                           nestedPtr,
                                                                           object,
                                                                           b.getStrArrayAttr({step.cName}),
                                                                           b.getDenseI64ArrayAttr({memberIndex}));
            const mlir::Value at = mlir::dsdl::BufferAtOp::create(
                b, loc, bufferPtr, buffer, mlir::arith::DivUIOp::create(b, loc, ready.bitOffset, eight));

            const std::string callee = step.compositeCTypeName + (writing ? "__serialize_" : "__deserialize_");
            auto              call   = mlir::dsdl::CallSerdesOp::create(b,
                                                        loc,
                                                        b.getIntegerType(8),
                                                        b.getStringAttr(callee),
                                                        memberPtr,
                                                        at,
                                                        sizeSlot);

            mlir::Value err = call.getError();

            // The length the reader will be told. Serializing learns it from the nested type,
            // reading it back out of the slot the callee wrote. Decoding was told it up front
            // and steps that far regardless of what the nested decode consumed, so it never
            // reads the slot back at all.
            const mlir::Value span =
                writing ? mlir::dsdl::LoadScalarOp::create(b, loc, i64Ty, sizeSlot) : declared;

            if (writing && !step.delimiterValidateHelper.empty())
            {
                err = foldError(
                    b,
                    loc,
                    err,
                    callErrorHelper(b, loc, step.delimiterValidateHelper, mlir::ValueRange{span, remaining}));
            }

            return guarded(b, loc, PlanCursor{ready.bitOffset, err}, [&](PlanCursor done) {
                mlir::Value after = mlir::arith::AddIOp::create(
                    b, loc, done.bitOffset, mlir::arith::MulIOp::create(b, loc, span, eight));
                mlir::Value outcome = done.error;
                if (writing)
                {
                    auto header = mlir::dsdl::WriteBitsOp::create(b,
                                                                  loc,
                                                                  b.getIntegerType(8),
                                                                  buffer,
                                                                  capacityBytes,
                                                                  headerOffset,
                                                                  span,
                                                                  b.getI64IntegerAttr(kDelimiterHeaderBits),
                                                                  nullptr);
                    outcome = foldError(b, loc, outcome, header.getError());
                }
                return PlanCursor{after, outcome};
            });
        });
    });
}



bool stepIsBitpackedArray(const PlanStep& step)
{
    return stepIsArray(step) && (step.scalarCategory == "bool");
}

/// @brief Moves a bool array, which is stored bitpacked rather than as elements.
///
/// One run of bits rather than a loop: the storage already has the layout the wire wants, so
/// the whole array travels in a single copy whose length is the array's count.
PlanCursor buildBitpackedArray(mlir::OpBuilder&   b,
                               mlir::Location     loc,
                               const PlanStep&    step,
                               const std::int64_t memberIndex,
                               mlir::Value        object,
                               mlir::Value      buffer,
                               mlir::Value      capacityBytes,
                               PlanCursor       cursor,
                               mlir::Value      count,
                               const bool       writing)
{
    auto*      ctx       = b.getContext();
    const auto qualifier = writing ? std::string("const ") : std::string();
    auto       bytePtr   = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, qualifier + "uint8_t"));

    // A variable-length bool array keeps its bits in a `bitpacked` member beside the count; a
    // fixed one has no count, so the member is the storage.
    const bool fixed = stepIsFixedArray(step);
    const mlir::Value packed =
        mlir::dsdl::ElementAddrOp::create(b,
                                          loc,
                                          bytePtr,
                                          object,
                                          fixed ? b.getStrArrayAttr({step.cName})
                                                : b.getStrArrayAttr({step.cName, "bitpacked"}),
                                          fixed ? b.getDenseI64ArrayAttr({memberIndex})
                                                : b.getDenseI64ArrayAttr({memberIndex, 0}),
                                          constantI64(b, loc, 0),
                                          b.getStringAttr(qualifier + "uint8_t"));
    if (writing)
    {
        mlir::dsdl::BitWriteOp::create(b,
                                       loc,
                                       buffer,
                                       cursor.bitOffset,
                                       count,
                                       packed,
                                       constantI64(b, loc, 0));
    }
    else
    {
        mlir::dsdl::BitReadOp::create(b, loc, packed, buffer, capacityBytes, cursor.bitOffset, count);
    }
    return PlanCursor{mlir::arith::AddIOp::create(b, loc, cursor.bitOffset, count), cursor.error};
}

/// @brief Encodes or decodes an array of nested composites, one element at a time.
///
/// Each element goes through the nested type's own entry point and reports its own length, so
/// unlike an array of scalars the stride is not known and the loop cannot be driven by the
/// offset. It is counted instead, with `scf.for`, whose induction variable is not among its
/// results -- an `scf.while` would make the index a result nothing reads, and that reaches
/// the emitted C as a variable nothing uses.
PlanCursor buildCompositeElementLoop(mlir::OpBuilder&   b,
                                     mlir::Location     loc,
                                     const PlanStep&    step,
                                     const std::int64_t memberIndex,
                                     mlir::Value        object,
                                     mlir::Value      buffer,
                                     mlir::Value      capacityBytes,
                                     PlanCursor       cursor,
                                     mlir::Value      count,
                                     const bool       writing)
{
    auto*      ctx       = b.getContext();
    auto       i64Ty     = b.getIntegerType(64);
    auto       i8Ty      = b.getIntegerType(8);
    auto       indexTy   = b.getIndexType();
    auto       sizePtr   = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, "size_t"));
    const auto qualifier = writing ? std::string("const ") : std::string();
    auto       nestedPtr =
        mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, qualifier + step.compositeCTypeName));
    auto bufferPtr =
        mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, writing ? "uint8_t" : "const uint8_t"));

    const mlir::Value zero  = mlir::arith::ConstantIndexOp::create(b, loc, 0);
    const mlir::Value one   = mlir::arith::ConstantIndexOp::create(b, loc, 1);
    const mlir::Value bound = mlir::arith::IndexCastOp::create(b, loc, indexTy, count);
    const mlir::Value eight = constantI64(b, loc, 8);

    auto loop = mlir::scf::ForOp::create(b,
                                         loc,
                                         zero,
                                         bound,
                                         one,
                                         mlir::ValueRange{cursor.bitOffset, cursor.error});
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        b.setInsertionPointToStart(loop.getBody());
        const mlir::Value index =
            mlir::arith::IndexCastOp::create(b, loc, i64Ty, loop.getInductionVar());
        const PlanCursor carried{loop.getRegionIterArg(0), loop.getRegionIterArg(1)};

        const PlanCursor next = guarded(b, loc, carried, [&](PlanCursor inner) {
            const mlir::Value available = remainingBytes(b, loc, capacityBytes, inner.bitOffset);
            const mlir::Value sizeSlot  = mlir::dsdl::LocalOp::create(b, loc, sizePtr, available);
            const mlir::Value elementPtr =
                mlir::dsdl::ElementAddrOp::create(b,
                                                  loc,
                                                  nestedPtr,
                                                  object,
                                                  elementPath(b, step),
                                                  elementIndices(b, step, memberIndex),
                                                  index,
                                                  b.getStringAttr(qualifier + step.compositeCTypeName));
            const mlir::Value at = mlir::dsdl::BufferAtOp::create(
                b, loc, bufferPtr, buffer, mlir::arith::DivUIOp::create(b, loc, inner.bitOffset, eight));

            const std::string callee = step.compositeCTypeName + (writing ? "__serialize_" : "__deserialize_");
            auto              call   = mlir::dsdl::CallSerdesOp::create(b,
                                                        loc,
                                                        i8Ty,
                                                        b.getStringAttr(callee),
                                                        elementPtr,
                                                        at,
                                                        sizeSlot);
            const mlir::Value used = mlir::dsdl::LoadScalarOp::create(b, loc, i64Ty, sizeSlot);
            const mlir::Value advanced =
                mlir::arith::AddIOp::create(b,
                                            loc,
                                            inner.bitOffset,
                                            mlir::arith::MulIOp::create(b, loc, used, eight));
            const mlir::Value ok = isHealthy(b, loc, call.getError());
            return PlanCursor{mlir::arith::SelectOp::create(b, loc, ok, advanced, inner.bitOffset),
                              call.getError()};
        });
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{next.bitOffset, next.error});
    }
    return PlanCursor{loop.getResult(0), loop.getResult(1)};
}


/// @brief Where a step's field sits among the generated struct's members.
///
/// The struct is the non-padding fields in declaration order: a `void` field reserves wire
/// bits and has nothing to hold, so it takes no member and no position. A union lists its
/// options and then `_tag_`, which is why the tag's index is the option count.
///
/// The C path never reads this -- it has the member's name -- and object emission has nothing
/// else to go on, so it is the one place the two targets are told apart by more than spelling.
std::vector<std::int64_t> memberIndicesFor(const std::vector<PlanStep>& steps)
{
    std::vector<std::int64_t> indices(steps.size(), -1);
    std::int64_t              next = 0;
    for (std::size_t i = 0; i < steps.size(); ++i)
    {
        if (steps[i].kind == PlanStepKind::Field)
        {
            indices[i] = next++;
        }
    }
    return indices;
}

/// @brief The index of a union's `_tag_`, which the struct places after the options.
std::int64_t unionTagMemberIndex(const std::vector<PlanStep>& steps)
{
    return static_cast<std::int64_t>(unionOptionsOf(steps).size());
}

/// @brief Builds a typed serialize body as operations.
///
/// The published header declares this symbol, so the parameter spellings are the header's.
/// Control flow is structured, because the C path has no branch-graph conversion, so what the
/// hand-written text says with an early return this says by carrying an error through the
/// cursor.
mlir::LogicalResult buildTypedSerializeBody(mlir::OpBuilder&             builder,
                                            mlir::ModuleOp               module,
                                            mlir::Location               loc,
                                            llvm::StringRef              functionName,
                                            llvm::StringRef              cTypeName,
                                            const std::vector<PlanStep>& steps,
                                            llvm::StringRef              capacityCheckSymbol,
                                            const bool                   isUnion,
                                            const std::int64_t           unionTagBits,
                                            llvm::StringRef              unionTagValidateSymbol,
                                            llvm::StringRef              unionTagHelper)
{
    if (capacityCheckSymbol.empty() || !module.lookupSymbol<mlir::func::FuncOp>(capacityCheckSymbol))
    {
        return mlir::failure();
    }

    mlir::OpBuilder::InsertionGuard const outer(builder);
    builder.setInsertionPointToEnd(&module.getBodyRegion().front());

    auto* ctx    = builder.getContext();
    auto  objTy  = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, ("const " + cTypeName).str()));
    auto  bufTy  = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, "uint8_t"));
    auto  sizeTy = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, "size_t"));
    auto  i8Ty   = builder.getIntegerType(8);
    auto  i64Ty  = builder.getIntegerType(64);
    auto  fnType = builder.getFunctionType(mlir::TypeRange{objTy, bufTy, sizeTy}, mlir::TypeRange{i8Ty});
    auto  fn     = mlir::func::FuncOp::create(builder, loc, functionName, fnType);
    fn->setAttr("llvmdsdl.plan_origin", builder.getStringAttr(kLoweredSerDesContractProducer));

    mlir::Block* entry = fn.addEntryBlock();
    builder.setInsertionPointToStart(entry);
    const mlir::Value object  = entry->getArgument(0);
    const mlir::Value buffer  = entry->getArgument(1);
    const mlir::Value sizePtr = entry->getArgument(2);

    mlir::Value anyNull = mlir::dsdl::IsNullOp::create(builder, loc, builder.getI1Type(), object);
    for (const mlir::Value pointer : {buffer, sizePtr})
    {
        auto next = mlir::dsdl::IsNullOp::create(builder, loc, builder.getI1Type(), pointer);
        anyNull   = mlir::arith::OrIOp::create(builder, loc, anyNull, next);
    }

    auto outerIf = mlir::scf::IfOp::create(builder, loc, mlir::TypeRange{i8Ty}, anyNull, true);
    {
        mlir::OpBuilder::InsertionGuard const g(builder);
        builder.setInsertionPointToStart(outerIf.thenBlock());
        mlir::scf::YieldOp::create(
            builder,
            loc,
            mlir::ValueRange{constantI8(builder, loc, -kRuntimeErrorInvalidArgument)});
    }
    {
        mlir::OpBuilder::InsertionGuard const g(builder);
        builder.setInsertionPointToStart(outerIf.elseBlock());

        const mlir::Value capacityBytes = mlir::dsdl::LoadScalarOp::create(builder, loc, i64Ty, sizePtr);
        const mlir::Value capacityBits =
            mlir::arith::MulIOp::create(builder, loc, capacityBytes, constantI64(builder, loc, 8));
        const mlir::Value capacityError = callErrorHelper(builder, loc, capacityCheckSymbol, mlir::ValueRange{capacityBits});

        const std::vector<std::int64_t> members = memberIndicesFor(steps);
        bool                            byteAligned = true;
        PlanCursor cursor{constantI64(builder, loc, 0), capacityError};

        if (isUnion)
        {
            // The tag comes off the object, is normalised, and is validated before anything
            // is written: a tag naming no option selects nothing, and the plan stops there.
            const mlir::Value rawTag = mlir::dsdl::LoadMemberOp::create(builder,
                                                                        loc,
                                                                        i64Ty,
                                                                        object,
                                                                        builder.getStrArrayAttr({"_tag_"}),
                                                                        builder.getDenseI64ArrayAttr(
                                                                            llvm::ArrayRef<std::int64_t>{
                                                                                unionTagMemberIndex(steps)}));
            const mlir::Value tagValue = applyHelper(builder, loc, unionTagHelper, rawTag);
            if (!unionTagValidateSymbol.empty())
            {
                cursor.error = foldError(builder,
                                         loc,
                                         cursor.error,
                                         callErrorHelper(builder, loc, unionTagValidateSymbol, mlir::ValueRange{tagValue}));
            }
            cursor = guarded(builder, loc, cursor, [&](PlanCursor inner) {
                return emitWrite(builder, loc, buffer, capacityBytes, inner, tagValue, unionTagBits, false);
            });

            for (const PlanStep* option : unionOptionsOf(steps))
            {
                cursor = buildUnionOption(builder, loc, rawTag, *option, cursor, [&](PlanCursor arm) {
                    if (option->alignmentBits > 1)
                    {
                        arm = buildAlignment(builder, loc, buffer, capacityBytes, arm, true, false);
                    }
                    if (stepIsArray(*option))
                    {
                        return buildArrayWrite(builder, loc, *option, option->unionOptionIndex, object, buffer, capacityBytes, arm);
                    }
                    if (stepIsComposite(*option))
                    {
                        const auto emit = option->compositeSealed ? buildCompositeStep : buildDelimitedCompositeStep;
                        return emit(builder, loc, *option, option->unionOptionIndex, object, buffer, capacityBytes, arm, true);
                    }
                    return buildScalarWrite(builder, loc, *option, option->unionOptionIndex, object, buffer, capacityBytes, arm);
                });
            }
            byteAligned = false;
        }
        for (std::size_t index = 0; isUnion ? false : (index < steps.size()); ++index)
        {
            const PlanStep&    step    = steps[index];
            const std::int64_t memberIndex = members[index];
            const bool         aligned = byteAligned;
            byteAligned             = byteAligned && stepPreservesByteAlignment(step);
            if (step.kind == PlanStepKind::Align)
            {
                cursor = buildAlignment(builder, loc, buffer, capacityBytes, cursor, true, aligned);
            }
            else if (step.kind == PlanStepKind::Padding)
            {
                cursor = buildPaddingStep(builder, loc, buffer, capacityBytes, cursor, step.bits, true);
            }
            else if (stepIsArray(step))
            {
                cursor = buildArrayWrite(builder, loc, step, memberIndex, object, buffer, capacityBytes, cursor);
            }
            else if (stepIsComposite(step))
            {
                const auto emit = step.compositeSealed ? buildCompositeStep : buildDelimitedCompositeStep;
                cursor = emit(builder,
                              loc,
                              step,
                              memberIndex,
                              object,
                              buffer,
                              capacityBytes,
                              cursor,
                              true);
            }
            else
            {
                cursor = buildScalarWrite(builder, loc, step, memberIndex, object, buffer, capacityBytes, cursor);
            }
        }
        // Where the plan ends is known when nothing varies, and known to be byte-aligned
        // whenever every width is a whole number of bytes -- an array of them lands on a
        // boundary whatever its count. Either way the trailing padding is not a loop.
        std::optional<std::int64_t> staticEnd;
        // A nested composite's length is its own to decide, so anything after one is as
        // unknown as anything after an array.
        const bool anyVariable = std::any_of(steps.begin(), steps.end(), [](const PlanStep& step) {
            return stepIsArray(step) || stepIsComposite(step) || (step.kind == PlanStepKind::Align);
        });
        if (!anyVariable)
        {
            std::int64_t total = 0;
            for (const auto& step : steps)
            {
                total += step.bitLength;
            }
            staticEnd = total;
        }
        else
        {
            const bool wholeBytes = std::all_of(steps.begin(), steps.end(), [](const PlanStep& step) {
                if ((step.kind == PlanStepKind::Align) || stepIsComposite(step))
                {
                    // Both land the plan on a byte boundary by construction.
                    return true;
                }
                const bool widthWhole  = (step.bitLength % 8) == 0;
                const bool prefixWhole = !stepIsArray(step) || ((step.arrayLengthPrefixBits % 8) == 0);
                return widthWhole && prefixWhole;
            });
            if (wholeBytes || byteAligned)
            {
                staticEnd = 0;
            }
        }
        cursor = buildFinalPadding(builder, loc, buffer, capacityBytes, cursor, staticEnd);

        auto epilogue = mlir::scf::IfOp::create(builder,
                                                loc,
                                                mlir::TypeRange{i8Ty},
                                                isHealthy(builder, loc, cursor.error),
                                                true);
        {
            mlir::OpBuilder::InsertionGuard const g3(builder);
            builder.setInsertionPointToStart(epilogue.thenBlock());
            mlir::dsdl::StoreScalarOp::create(
                builder,
                loc,
                sizePtr,
                mlir::arith::DivUIOp::create(builder, loc, cursor.bitOffset, constantI64(builder, loc, 8)));
            mlir::scf::YieldOp::create(builder, loc, mlir::ValueRange{constantI8(builder, loc, 0)});
        }
        {
            mlir::OpBuilder::InsertionGuard const g3(builder);
            builder.setInsertionPointToStart(epilogue.elseBlock());
            mlir::scf::YieldOp::create(builder, loc, mlir::ValueRange{cursor.error});
        }
        mlir::scf::YieldOp::create(builder, loc, mlir::ValueRange{epilogue.getResult(0)});
    }

    builder.setInsertionPointToEnd(entry);
    mlir::func::ReturnOp::create(builder, loc, mlir::ValueRange{outerIf.getResult(0)});
    return mlir::success();
}

/// @brief Deserializes one scalar field, advancing the offset past it.
///
/// No guard, unlike the serialize side. A read cannot fail: the runtime answers a short
/// buffer by zero-extending, which is the tolerance a deserializer is required to have.
PlanCursor buildScalarRead(mlir::OpBuilder&   b,
                           mlir::Location     loc,
                           const PlanStep&    step,
                           const std::int64_t memberIndex,
                           mlir::Value        object,
                           mlir::Value      buffer,
                           mlir::Value      capacityBytes,
                           PlanCursor       cursor)
{
    const mlir::Value bitOffset = cursor.bitOffset;
    const mlir::Type valueType = stepValueType(b, step);
    mlir::Value      raw       = mlir::dsdl::ReadBitsOp::create(b,
                                                     loc,
                                                     valueType,
                                                     buffer,
                                                     capacityBytes,
                                                     bitOffset,
                                                     b.getI64IntegerAttr(step.bitLength),
                                                     (step.scalarCategory == "signed") ? b.getUnitAttr() : nullptr);
    raw = applyHelper(b, loc, deserHelperFor(step), raw);
    markSigned(mlir::dsdl::StoreMemberOp::create(b,
                                                 loc,
                                                 object,
                                                 stepPath(b, step, {}),
                                                 b.getDenseI64ArrayAttr(llvm::ArrayRef<std::int64_t>{memberIndex}),
                                                 raw),
               step);
    return PlanCursor{mlir::arith::AddIOp::create(b, loc, bitOffset, constantI64(b, loc, step.bitLength)),
                      cursor.error};
}

/// @brief Deserializes one variable-length array.
///
/// The count comes off the wire and is clamped to the declared capacity before it is used to
/// bound the loop: a length prefix is attacker-controlled, and a decoder that trusted it would
/// write past the elements it has.
/// @brief Reads @p count elements into the object, advancing past them.
PlanCursor buildArrayElementReads(mlir::OpBuilder&   b,
                                  mlir::Location     loc,
                                  const PlanStep&    step,
                                  const std::int64_t memberIndex,
                                  mlir::Value        object,
                                  mlir::Value      buffer,
                                  mlir::Value      capacityBytes,
                                  PlanCursor       cursor,
                                  mlir::Value      count)
{
    if (stepIsComposite(step))
    {
        return buildCompositeElementLoop(b, loc, step, memberIndex, object, buffer, capacityBytes, cursor, count, false);
    }
    if (stepIsBitpackedArray(step))
    {
        return buildBitpackedArray(b, loc, step, memberIndex, object, buffer, capacityBytes, cursor, count, false);
    }
    auto              i64Ty  = b.getIntegerType(64);
    const mlir::Value offset = cursor.bitOffset;
    // Driven by the offset alone, for the same reason as the serialize side: a carried index
    // would also be a loop result, and nothing after the loop reads it.
    const mlir::Value start = offset;
    const mlir::Value width = constantI64(b, loc, step.bitLength);
    const mlir::Value end =
        mlir::arith::AddIOp::create(b, loc, start, mlir::arith::MulIOp::create(b, loc, count, width));

    auto loop = mlir::scf::WhileOp::create(b, loc, mlir::TypeRange{i64Ty}, mlir::ValueRange{start});
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        mlir::Block* before = b.createBlock(&loop.getBefore(), {}, {i64Ty}, {loc});
        b.setInsertionPointToStart(before);
        const mlir::Value more = mlir::arith::CmpIOp::create(b,
                                                             loc,
                                                             mlir::arith::CmpIPredicate::ult,
                                                             before->getArgument(0),
                                                             end);
        mlir::scf::ConditionOp::create(b, loc, more, before->getArguments());
    }
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        mlir::Block* after = b.createBlock(&loop.getAfter(), {}, {i64Ty}, {loc});
        b.setInsertionPointToStart(after);
        const mlir::Value at    = after->getArgument(0);
        const mlir::Value index = mlir::arith::DivUIOp::create(
            b, loc, mlir::arith::SubIOp::create(b, loc, at, start), width);

        const mlir::Type valueType = stepValueType(b, step);
        mlir::Value      element   = mlir::dsdl::ReadBitsOp::create(b,
                                                             loc,
                                                             valueType,
                                                             buffer,
                                                             capacityBytes,
                                                             at,
                                                             b.getI64IntegerAttr(step.bitLength),
                                                             (step.scalarCategory == "signed")
                                                                 ? b.getUnitAttr()
                                                                 : nullptr);
        element = applyHelper(b, loc, deserHelperFor(step), element);
        markSigned(mlir::dsdl::StoreElementOp::create(b,
                                                      loc,
                                                      object,
                                                      elementPath(b, step),
                                                      elementIndices(b, step, memberIndex),
                                                      index,
                                                      element,
                                                      b.getStringAttr(elementTypeName(step))),
                   step);
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{mlir::arith::AddIOp::create(b, loc, at, width)});
    }
    return PlanCursor{loop.getResult(0), cursor.error};
}

PlanCursor buildArrayRead(mlir::OpBuilder&   b,
                          mlir::Location     loc,
                          const PlanStep&    step,
                          const std::int64_t memberIndex,
                          mlir::Value        object,
                          mlir::Value      buffer,
                          mlir::Value      capacityBytes,
                          PlanCursor       cursor)
{
    // The whole read is guarded, not just the element loop. A step before this one may
    // already have failed -- a nested union rejecting its tag, say -- and the reference
    // returns at that point. Running on regardless would replace the error it reported with
    // whatever this array makes of bytes that were never meant to be an array.
    return guarded(b, loc, cursor, [&](PlanCursor outer) {
    const mlir::Value bitOffset = outer.bitOffset;
    auto              i64Ty     = b.getIntegerType(64);
    const bool        fixed     = stepIsFixedArray(step);

    if (fixed)
    {
        // No prefix, no count member, and no length to judge: the declaration says how many.
        return guarded(b, loc, outer, [&](PlanCursor inner) {
            const mlir::Value count = constantI64(b, loc, step.arrayCapacity);
            return buildArrayElementReads(b, loc, step, memberIndex, object, buffer, capacityBytes, inner, count);
        });
    }

    mlir::Value wireLength = mlir::dsdl::ReadBitsOp::create(b,
                                                            loc,
                                                            i64Ty,
                                                            buffer,
                                                            capacityBytes,
                                                            bitOffset,
                                                            b.getI64IntegerAttr(step.arrayLengthPrefixBits),
                                                            nullptr);
    wireLength = applyHelper(b, loc, step.deserArrayLengthPrefixHelper, wireLength);
    mlir::Value offset =
        mlir::arith::AddIOp::create(b, loc, bitOffset, constantI64(b, loc, step.arrayLengthPrefixBits));

    // The length off the wire is stored as it was read and then validated, not clamped to
    // the declared capacity. A prefix longer than the array can hold is malformed input, and
    // a decoder that quietly truncated it would accept a message the sender did not send.
    mlir::dsdl::StoreMemberOp::create(b,
                                      loc,
                                      object,
                                      stepPath(b, step, "count"),
                                      b.getDenseI64ArrayAttr(llvm::ArrayRef<std::int64_t>{memberIndex, 1}),
                                      wireLength);
    const mlir::Value count = wireLength;
    mlir::Value       error = outer.error;
    if (!step.arrayLengthValidateHelper.empty())
    {
        error = callErrorHelper(b, loc, step.arrayLengthValidateHelper, mlir::ValueRange{count});
    }

    return guarded(b, loc, PlanCursor{offset, error}, [&](PlanCursor inner) {
        return buildArrayElementReads(b, loc, step, memberIndex, object, buffer, capacityBytes, inner, count);
    });
    });
}

/// @brief Builds a typed deserialize body as operations.
///
/// The argument check is not the serialize one reversed, and its order is load-bearing. A null
/// buffer is legal when the declared size is zero, and C reaches that clause by short-circuit,
/// having already established the size pointer is non-null. Reading the size eagerly would
/// dereference null on exactly the call the check exists to reject.
mlir::LogicalResult buildTypedDeserializeBody(mlir::OpBuilder&             builder,
                                              mlir::ModuleOp               module,
                                              mlir::Location               loc,
                                              llvm::StringRef              functionName,
                                              llvm::StringRef              cTypeName,
                                              const std::vector<PlanStep>& steps,
                                              const bool                   isUnion,
                                              const std::int64_t           unionTagBits,
                                              llvm::StringRef              unionTagValidateSymbol,
                                              llvm::StringRef              unionTagHelper)
{
    mlir::OpBuilder::InsertionGuard const outer(builder);
    builder.setInsertionPointToEnd(&module.getBodyRegion().front());

    auto* ctx    = builder.getContext();
    auto  objTy  = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, cTypeName));
    auto  bufTy  = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, "const uint8_t"));
    auto  sizeTy = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, "size_t"));
    auto  i8Ty   = builder.getIntegerType(8);
    auto  i64Ty  = builder.getIntegerType(64);
    auto  fnType = builder.getFunctionType(mlir::TypeRange{objTy, bufTy, sizeTy}, mlir::TypeRange{i8Ty});
    auto  fn     = mlir::func::FuncOp::create(builder, loc, functionName, fnType);
    fn->setAttr("llvmdsdl.plan_origin", builder.getStringAttr(kLoweredSerDesContractProducer));

    mlir::Block* entry = fn.addEntryBlock();
    builder.setInsertionPointToStart(entry);
    const mlir::Value object  = entry->getArgument(0);
    const mlir::Value buffer  = entry->getArgument(1);
    const mlir::Value sizePtr = entry->getArgument(2);

    const mlir::Value objNull    = mlir::dsdl::IsNullOp::create(builder, loc, builder.getI1Type(), object);
    const mlir::Value sizeNull   = mlir::dsdl::IsNullOp::create(builder, loc, builder.getI1Type(), sizePtr);
    const mlir::Value bufNull    = mlir::dsdl::IsNullOp::create(builder, loc, builder.getI1Type(), buffer);
    const mlir::Value cannotRead = mlir::arith::OrIOp::create(builder, loc, objNull, sizeNull);

    auto rejected =
        mlir::scf::IfOp::create(builder, loc, mlir::TypeRange{builder.getI1Type()}, cannotRead, true);
    {
        mlir::OpBuilder::InsertionGuard const g(builder);
        builder.setInsertionPointToStart(rejected.thenBlock());
        mlir::scf::YieldOp::create(
            builder,
            loc,
            mlir::ValueRange{mlir::arith::ConstantOp::create(builder, loc, builder.getBoolAttr(true))});
    }
    {
        mlir::OpBuilder::InsertionGuard const g(builder);
        builder.setInsertionPointToStart(rejected.elseBlock());
        const mlir::Value capacity = mlir::dsdl::LoadScalarOp::create(builder, loc, i64Ty, sizePtr);
        const mlir::Value nonEmpty = mlir::arith::CmpIOp::create(builder,
                                                                 loc,
                                                                 mlir::arith::CmpIPredicate::ne,
                                                                 capacity,
                                                                 constantI64(builder, loc, 0));
        mlir::scf::YieldOp::create(
            builder,
            loc,
            mlir::ValueRange{mlir::arith::AndIOp::create(builder, loc, bufNull, nonEmpty)});
    }

    auto outerIf =
        mlir::scf::IfOp::create(builder, loc, mlir::TypeRange{i8Ty}, rejected.getResult(0), true);
    {
        mlir::OpBuilder::InsertionGuard const g(builder);
        builder.setInsertionPointToStart(outerIf.thenBlock());
        mlir::scf::YieldOp::create(
            builder,
            loc,
            mlir::ValueRange{constantI8(builder, loc, -kRuntimeErrorInvalidArgument)});
    }
    {
        mlir::OpBuilder::InsertionGuard const g(builder);
        builder.setInsertionPointToStart(outerIf.elseBlock());

        const mlir::Value capacityBytes = mlir::dsdl::LoadScalarOp::create(builder, loc, i64Ty, sizePtr);
        const mlir::Value readable      = mlir::dsdl::BufferOrEmptyOp::create(builder, loc, bufTy, buffer);

        const std::vector<std::int64_t> members = memberIndicesFor(steps);
        bool                            byteAligned = true;
        PlanCursor cursor{constantI64(builder, loc, 0), constantI8(builder, loc, 0)};

        if (isUnion)
        {
            const mlir::Value rawTag = mlir::dsdl::ReadBitsOp::create(builder,
                                                                      loc,
                                                                      i64Ty,
                                                                      readable,
                                                                      capacityBytes,
                                                                      cursor.bitOffset,
                                                                      builder.getI64IntegerAttr(unionTagBits),
                                                                      nullptr);
            const mlir::Value tagValue = applyHelper(builder, loc, unionTagHelper, rawTag);
            if (!unionTagValidateSymbol.empty())
            {
                cursor.error = foldError(builder,
                                         loc,
                                         cursor.error,
                                         callErrorHelper(builder, loc, unionTagValidateSymbol, mlir::ValueRange{tagValue}));
            }
            cursor = guarded(builder, loc, cursor, [&](PlanCursor inner) {
                mlir::dsdl::StoreMemberOp::create(builder,
                                                  loc,
                                                  object,
                                                  builder.getStrArrayAttr({"_tag_"}),
                                                  builder.getDenseI64ArrayAttr(
                                                      llvm::ArrayRef<std::int64_t>{unionTagMemberIndex(steps)}),
                                                  tagValue);
                return PlanCursor{mlir::arith::AddIOp::create(builder,
                                                              loc,
                                                              inner.bitOffset,
                                                              constantI64(builder, loc, unionTagBits)),
                                  inner.error};
            });

            for (const PlanStep* option : unionOptionsOf(steps))
            {
                cursor = buildUnionOption(builder, loc, tagValue, *option, cursor, [&](PlanCursor arm) {
                    if (option->alignmentBits > 1)
                    {
                        arm = buildAlignment(builder, loc, readable, capacityBytes, arm, false, false);
                    }
                    if (stepIsArray(*option))
                    {
                        return buildArrayRead(builder, loc, *option, option->unionOptionIndex, object, readable, capacityBytes, arm);
                    }
                    if (stepIsComposite(*option))
                    {
                        const auto emit = option->compositeSealed ? buildCompositeStep : buildDelimitedCompositeStep;
                        return emit(builder, loc, *option, option->unionOptionIndex, object, readable, capacityBytes, arm, false);
                    }
                    return buildScalarRead(builder, loc, *option, option->unionOptionIndex, object, readable, capacityBytes, arm);
                });
            }
            byteAligned = false;
        }
        for (std::size_t index = 0; isUnion ? false : (index < steps.size()); ++index)
        {
            const PlanStep&    step    = steps[index];
            const std::int64_t memberIndex = members[index];
            const bool         aligned = byteAligned;
            byteAligned             = byteAligned && stepPreservesByteAlignment(step);
            if (step.kind == PlanStepKind::Align)
            {
                cursor = buildAlignment(builder, loc, readable, capacityBytes, cursor, false, aligned);
            }
            else if (step.kind == PlanStepKind::Padding)
            {
                cursor = buildPaddingStep(builder, loc, readable, capacityBytes, cursor, step.bits, false);
            }
            else if (stepIsArray(step))
            {
                cursor = buildArrayRead(builder, loc, step, memberIndex, object, readable, capacityBytes, cursor);
            }
            else if (stepIsComposite(step))
            {
                const auto emit = step.compositeSealed ? buildCompositeStep : buildDelimitedCompositeStep;
                cursor = emit(builder,
                              loc,
                              step,
                              memberIndex,
                              object,
                              readable,
                              capacityBytes,
                              cursor,
                              false);
            }
            else
            {
                cursor = buildScalarRead(builder, loc, step, memberIndex, object, readable, capacityBytes, cursor);
            }
        }
        const mlir::Value offset = cursor.bitOffset;

        const mlir::Value seven   = constantI64(builder, loc, 7);
        const mlir::Value eight   = constantI64(builder, loc, 8);
        const mlir::Value rounded = mlir::arith::AddIOp::create(builder, loc, offset, seven);
        const mlir::Value bytes   = mlir::arith::DivUIOp::create(builder, loc, rounded, eight);
        const mlir::Value aligned = mlir::arith::MulIOp::create(builder, loc, bytes, eight);

        // What was consumed, clamped to what was there: a truncated input is decoded as far
        // as it went rather than rejected.
        const mlir::Value capacityBits = mlir::arith::MulIOp::create(builder, loc, capacityBytes, eight);
        const mlir::Value fits         = mlir::arith::CmpIOp::create(builder,
                                                             loc,
                                                             mlir::arith::CmpIPredicate::ult,
                                                             aligned,
                                                             capacityBits);
        const mlir::Value clamped      = mlir::arith::SelectOp::create(builder, loc, fits, aligned, capacityBits);

        // A nested type can refuse what it was given, and then nothing was consumed to
        // report: the size is written only on the path that succeeded.
        auto epilogue = mlir::scf::IfOp::create(builder,
                                                loc,
                                                mlir::TypeRange{i8Ty},
                                                isHealthy(builder, loc, cursor.error),
                                                true);
        {
            mlir::OpBuilder::InsertionGuard const g2(builder);
            builder.setInsertionPointToStart(epilogue.thenBlock());
            mlir::dsdl::StoreScalarOp::create(
                builder,
                loc,
                sizePtr,
                mlir::arith::DivUIOp::create(builder, loc, clamped, eight));
            mlir::scf::YieldOp::create(builder, loc, mlir::ValueRange{constantI8(builder, loc, 0)});
        }
        {
            mlir::OpBuilder::InsertionGuard const g2(builder);
            builder.setInsertionPointToStart(epilogue.elseBlock());
            mlir::scf::YieldOp::create(builder, loc, mlir::ValueRange{cursor.error});
        }
        mlir::scf::YieldOp::create(builder, loc, mlir::ValueRange{epilogue.getResult(0)});
    }

    builder.setInsertionPointToEnd(entry);
    mlir::func::ReturnOp::create(builder, loc, mlir::ValueRange{outerIf.getResult(0)});
    return mlir::success();
}

/// @brief Takes the address of an lvalue, which is how a plan hands storage to a callee.
mlir::Value addressOf(mlir::ConversionPatternRewriter& rewriter,
                      mlir::Location                   loc,
                      mlir::Value                      lvalue,
                      mlir::Type                       pointerType)
{
    return mlir::emitc::ApplyOp::create(rewriter, loc, pointerType, "&", lvalue);
}

struct MemberAddrLowering final : public mlir::OpConversionPattern<mlir::dsdl::MemberAddrOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::MemberAddrOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::MemberAddrOp         op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        auto pointerType = mlir::cast<mlir::emitc::PointerType>(
            getTypeConverter()->convertType(op.getAddress().getType()));
        const mlir::Value slot = walkMemberPath(rewriter,
                                                op.getLoc(),
                                                adaptor.getObject(),
                                                op.getPath(),
                                                pointerType.getPointee());
        rewriter.replaceOp(op, addressOf(rewriter, op.getLoc(), slot, pointerType));
        return mlir::success();
    }
};

struct ElementAddrLowering final : public mlir::OpConversionPattern<mlir::dsdl::ElementAddrOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::ElementAddrOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::ElementAddrOp        op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc = op.getLoc();
        auto pointerType         = mlir::cast<mlir::emitc::PointerType>(
            getTypeConverter()->convertType(op.getAddress().getType()));
        const mlir::Value slot = elementSlot(rewriter,
                                             loc,
                                             adaptor.getObject(),
                                             op.getPath(),
                                             adaptor.getIndex(),
                                             op.getElementType());
        rewriter.replaceOp(op, addressOf(rewriter, loc, slot, pointerType));
        return mlir::success();
    }
};

struct BufferAtLowering final : public mlir::OpConversionPattern<mlir::dsdl::BufferAtOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::BufferAtOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::BufferAtOp           op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc = op.getLoc();
        auto pointerType         = mlir::cast<mlir::emitc::PointerType>(
            getTypeConverter()->convertType(op.getAddress().getType()));
        auto element = mlir::emitc::SubscriptOp::create(
            rewriter,
            loc,
            mlir::cast<mlir::TypedValue<mlir::emitc::PointerType>>(adaptor.getBuffer()),
            adaptor.getByteOffset());
        rewriter.replaceOp(op, addressOf(rewriter, loc, element, pointerType));
        return mlir::success();
    }
};

struct LocalLowering final : public mlir::OpConversionPattern<mlir::dsdl::LocalOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::LocalOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::LocalOp              op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc = op.getLoc();
        auto pointerType         = mlir::cast<mlir::emitc::PointerType>(
            getTypeConverter()->convertType(op.getAddress().getType()));
        const mlir::Type stored = pointerType.getPointee();

        auto slot = mlir::emitc::VariableOp::create(rewriter,
                                                    loc,
                                                    mlir::emitc::LValueType::get(stored),
                                                    mlir::emitc::OpaqueAttr::get(rewriter.getContext(), ""));
        mlir::Value init = adaptor.getInit();
        if (init.getType() != stored)
        {
            init = mlir::emitc::CastOp::create(rewriter, loc, stored, init);
        }
        mlir::emitc::AssignOp::create(rewriter, loc, slot, init);
        rewriter.replaceOp(op, addressOf(rewriter, loc, slot, pointerType));
        return mlir::success();
    }
};

struct CallSerdesLowering final : public mlir::OpConversionPattern<mlir::dsdl::CallSerdesOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::CallSerdesOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::CallSerdesOp         op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        rewriter.replaceOpWithNewOp<mlir::emitc::CallOpaqueOp>(
            op,
            mlir::TypeRange{rewriter.getIntegerType(8)},
            op.getCalleeAttr(),
            mlir::ValueRange{adaptor.getObject(), adaptor.getBuffer(), adaptor.getSize()});
        return mlir::success();
    }
};


/// @brief Builds serialization plan bodies as operations, before a target is chosen.
///
/// This ran inside convert-dsdl-to-emitc while C was the only consumer. It is its own pass so
/// that object emission can take the same bodies: what a plan does is not a property of the
/// language it will be written in, and a pass that lowers to one target is the wrong place to
/// decide it.
///
/// A plan the builders decline is left alone, and convert-dsdl-to-emitc renders it as text.
/// Whether a body was built is asked of the module by symbol rather than recorded in state,
/// so the two passes cannot come to disagree about it.
struct BuildDSDLPlanBodiesPass
    : public mlir::PassWrapper<BuildDSDLPlanBodiesPass, mlir::OperationPass<mlir::ModuleOp>>
{
    llvm::StringRef getArgument() const final
    {
        return "build-dsdl-plan-bodies";
    }
    llvm::StringRef getDescription() const final
    {
        return "Build DSDL serialization plan bodies as dialect operations";
    }
    void getDependentDialects(mlir::DialectRegistry& registry) const override
    {
        registry.insert<mlir::dsdl::DSDLDialect, mlir::func::FuncDialect, mlir::arith::ArithDialect,
                        mlir::scf::SCFDialect>();
    }

    // NOLINTNEXTLINE(misc-override-with-different-visibility) -- MLIR declares passes this way.
    void runOnOperation() override
    {
        auto module = getOperation();
        if (!module->hasAttr("llvmdsdl.names_final"))
        {
            // Lowering stamps the spelling it can guess at, and a body built over those would
            // name members and call symbols no backend emits. The generic renderer takes the
            // plan until a backend has said what it calls things.
            return;
        }

        mlir::SmallVector<mlir::func::FuncOp, 16> built;
        for (mlir::Operation& schema : module.getBodyRegion().front())
        {
            if (schema.getName().getStringRef() != "dsdl.schema")
            {
                continue;
            }
            const auto symNameAttr = schema.getAttrOfType<mlir::StringAttr>("sym_name");
            if (!symNameAttr || (schema.getNumRegions() == 0) || schema.getRegion(0).empty())
            {
                continue;
            }
            if (schema.hasAttr("llvmdsdl.layout_only"))
            {
                // Present so that a member of this type can be addressed. Its serialisation is
                // its own object's to define.
                continue;
            }
            for (mlir::Operation& child : schema.getRegion(0).front())
            {
                if (child.getName().getStringRef() != "dsdl.serialization_plan")
                {
                    continue;
                }
                if (findLoweredContractEnvelopeViolation(&child) || findLoweredPlanContractViolation(module, &child))
                {
                    // Malformed input is convert-dsdl-to-emitc's to report; declining here
                    // leaves it to say so rather than failing twice in different words.
                    continue;
                }

                const auto        sectionAttr = child.getAttrOfType<mlir::StringAttr>("section");
                const std::string section     = sectionAttr ? sectionAttr.getValue().str() : std::string{};
                const std::string fnStem      = symNameAttr.getValue().str() + renderSectionSymbolSuffix(section);
                const auto        cTypeNameAttr = child.getAttrOfType<mlir::StringAttr>("c_type_name");
                const std::string cTypeName     = cTypeNameAttr ? cTypeNameAttr.getValue().str() : std::string{};
                if (cTypeName.empty())
                {
                    continue;
                }
                const auto        capacityAttr = child.getAttrOfType<mlir::StringAttr>(kLoweredCapacityCheckHelperAttr);
                const std::string capacityCheckSymbol = capacityAttr ? capacityAttr.getValue().str() : std::string{};

                const bool         isUnion          = child.hasAttr("is_union");
                const auto         unionTagBitsAttr = child.getAttrOfType<mlir::IntegerAttr>("union_tag_bits");
                const std::int64_t unionTagBits = unionTagBitsAttr ? nonNegative(unionTagBitsAttr.getInt()) : 0;
                const auto serTagAttr   = child.getAttrOfType<mlir::StringAttr>(kLoweredSerUnionTagHelperAttr);
                const auto deserTagAttr = child.getAttrOfType<mlir::StringAttr>(kLoweredDeserUnionTagHelperAttr);
                const auto validateAttr = child.getAttrOfType<mlir::StringAttr>(kLoweredUnionTagValidateHelperAttr);
                const std::string unionTagSerializeHelper = serTagAttr ? serTagAttr.getValue().str() : std::string{};
                const std::string unionTagDeserializeHelper =
                    deserTagAttr ? deserTagAttr.getValue().str() : std::string{};
                const std::string unionTagValidateSymbol =
                    (isUnion && validateAttr) ? validateAttr.getValue().str() : std::string{};

                const auto steps = collectPlanSteps(&child);
                if (!supportsTypedLowering(steps, isUnion, unionTagBits) ||
                    !supportsOperationLowering(steps, isUnion))
                {
                    continue;
                }
                for (const auto& step : steps)
                {
                    if ((step.kind == PlanStepKind::Field) && step.cName.empty())
                    {
                        continue;
                    }
                }

                mlir::OpBuilder builder(&getContext());
                const auto remember = [&](llvm::StringRef name) {
                    if (auto fn = module.lookupSymbol<mlir::func::FuncOp>(name))
                    {
                        built.push_back(fn);
                    }
                };
                (void) buildTypedSerializeBody(builder,
                                               module,
                                               child.getLoc(),
                                               fnStem + "__serialize_ir_",
                                               cTypeName,
                                               steps,
                                               capacityCheckSymbol,
                                               isUnion,
                                               unionTagBits,
                                               unionTagValidateSymbol,
                                               unionTagSerializeHelper);
                (void) buildTypedDeserializeBody(builder,
                                                 module,
                                                 child.getLoc(),
                                                 fnStem + "__deserialize_ir_",
                                                 cTypeName,
                                                 steps,
                                                 isUnion,
                                                 unionTagBits,
                                                 unionTagValidateSymbol,
                                                 unionTagDeserializeHelper);
                remember(fnStem + "__serialize_ir_");
                remember(fnStem + "__deserialize_ir_");
            }
        }

        // Canonicalise what was built, here rather than downstream. A loop carries values its
        // body may not read and a branch may prove constant; those are dead results while they
        // are still scf, and become variables a target cannot remove once they are not.
        //
        // Applied to the built functions rather than the module. A serialization plan holds a
        // region, has no results and declares no memory effects, so it is trivially dead to a
        // module-wide sweep -- which would delete the plans the next pass still has to read.
        mlir::RewritePatternSet cleanup(&getContext());
        for (mlir::RegisteredOperationName name : getContext().getRegisteredOperations())
        {
            name.getCanonicalizationPatterns(cleanup, &getContext());
        }
        const mlir::FrozenRewritePatternSet frozen(std::move(cleanup));
        for (mlir::func::FuncOp fn : built)
        {
            if (mlir::failed(mlir::applyPatternsGreedily(fn, frozen)))
            {
                fn.emitError("failed to canonicalize built plan body");
                signalPassFailure();
                return;
            }
        }
    }
};

struct ConvertDSDLToEmitCPass : public mlir::PassWrapper<ConvertDSDLToEmitCPass, mlir::OperationPass<mlir::ModuleOp>>
{
    llvm::StringRef getArgument() const final
    {
        return "convert-dsdl-to-emitc";
    }
    llvm::StringRef getDescription() const final
    {
        return "Lower DSDL dialect schema ops into Func/Arith ops for EmitC lowering";
    }
    void getDependentDialects(mlir::DialectRegistry& registry) const override
    {
        registry.insert<mlir::emitc::EmitCDialect>();
    }

    /// @brief Converts plan operations into their C spellings.
    ///
    /// Runs before the schema walk, for a module that already carries a plan body, and again
    /// after it, for the bodies the walk builds. The second run is a no-op when the first
    /// already emptied the module of them.
    mlir::LogicalResult lowerPlanOperations(mlir::ModuleOp module)
    {
        mlir::TypeConverter     converter = makeBitCopyTypeConverter();
        mlir::RewritePatternSet patterns(&getContext());
        patterns.add<BitWriteLowering,
                     BitReadLowering,
                     WriteBitsLowering,
                     ReadBitsLowering,
                     LoadMemberLowering,
                     StoreMemberLowering,
                     LoadElementLowering,
                     StoreElementLowering,
                     MemberAddrLowering,
                     BufferAtLowering,
                     ElementAddrLowering,
                     LocalLowering,
                     CallSerdesLowering,
                     IsNullLowering,
                     BufferOrEmptyLowering,
                     LoadScalarLowering,
                     StoreScalarLowering>(converter, &getContext());
        mlir::populateFunctionOpInterfaceTypeConversionPattern<mlir::func::FuncOp>(patterns, converter);

        mlir::ConversionTarget target(getContext());
        target.addLegalDialect<mlir::emitc::EmitCDialect,
                               mlir::arith::ArithDialect,
                               mlir::func::FuncDialect,
                               mlir::scf::SCFDialect>();
        target.addLegalDialect<mlir::dsdl::DSDLDialect>();
        target.addIllegalOp<mlir::dsdl::BitWriteOp,
                            mlir::dsdl::BitReadOp,
                            mlir::dsdl::WriteBitsOp,
                            mlir::dsdl::ReadBitsOp,
                            mlir::dsdl::LoadMemberOp,
                            mlir::dsdl::StoreMemberOp,
                            mlir::dsdl::LoadElementOp,
                            mlir::dsdl::StoreElementOp,
                            mlir::dsdl::MemberAddrOp,
                            mlir::dsdl::BufferAtOp,
                            mlir::dsdl::ElementAddrOp,
                            mlir::dsdl::LocalOp,
                            mlir::dsdl::CallSerdesOp,
                            mlir::dsdl::IsNullOp,
                            mlir::dsdl::BufferOrEmptyOp,
                            mlir::dsdl::LoadScalarOp,
                            mlir::dsdl::StoreScalarOp>();
        target.addDynamicallyLegalOp<mlir::func::FuncOp>([&converter](mlir::func::FuncOp fn) {
            return converter.isSignatureLegal(fn.getFunctionType());
        });

        if (mlir::failed(mlir::applyPartialConversion(module, target, std::move(patterns))))
        {
            module.emitError("failed to lower dsdl plan operations to emitc");
            signalPassFailure();
            return mlir::failure();
        }
        return mlir::success();
    }

    // NOLINTNEXTLINE(misc-override-with-different-visibility) -- MLIR declares passes this way.
    void runOnOperation() override
    {
        auto       module               = getOperation();
        const bool headersAvailable     = module->hasAttr("llvmdsdl.headers_available");
        const bool requireTypedLowering = module->hasAttr("llvmdsdl.require_typed_lowering");
        if (requireTypedLowering && !headersAvailable)
        {
            module.emitError("typed lowering requires header availability");
            signalPassFailure();
            return;
        }
        auto& body = module.getBodyRegion().front();

        if (mlir::failed(lowerPlanOperations(module)))
        {
            return;
        }

        std::vector<mlir::Operation*> schemaOps;
        for (mlir::Operation& op : body)
        {
            if (op.getName().getStringRef() == "dsdl.schema")
            {
                schemaOps.push_back(&op);
            }
        }
        if (schemaOps.empty())
        {
            return;
        }
        if (const auto envelopeViolation = findLoweredContractEnvelopeViolation(module.getOperation()))
        {
            switch (envelopeViolation->kind)
            {
            case LoweredContractEnvelopeViolationKind::MissingVersion:
                module.emitError("lowered SerDes contract missing module attribute '" +
                                 std::string(kLoweredSerDesContractVersionAttr) +
                                 "'; run lower-dsdl-exec before "
                                 "convert-dsdl-to-emitc");
                break;
            case LoweredContractEnvelopeViolationKind::UnsupportedMajorVersion:
                module.emitError(
                    "unsupported lowered SerDes contract major version: " +
                    loweredSerDesUnsupportedMajorVersionDiagnosticDetail(envelopeViolation->encodedVersion) +
                    "; run matching lower-dsdl-exec before convert-dsdl-to-emitc");
                break;
            case LoweredContractEnvelopeViolationKind::ProducerMismatch:
                module.emitError("lowered SerDes contract producer mismatch: expected '" +
                                 std::string(kLoweredSerDesContractProducer) +
                                 "'; run lower-dsdl-exec before "
                                 "convert-dsdl-to-emitc");
                break;
            }
            signalPassFailure();
            return;
        }

        std::vector<std::string> emittedFunctions;
        emittedFunctions.reserve(schemaOps.size() * 4U);
        std::set<std::string> forwardDeclaredTypes;
        std::set<std::string> capacityCheckSymbols;
        std::set<std::string> unionTagValidateSymbols;
        std::set<std::string> unionTagIoHelperSymbols;
        std::set<std::string> scalarUnsignedHelperSymbols;
        std::set<std::string> scalarSignedHelperSymbols;
        std::set<std::string> scalarFloatHelperSymbols;
        std::set<std::string> arrayLengthPrefixHelperSymbols;
        std::set<std::string> arrayLengthValidateSymbols;
        std::set<std::string> delimiterValidateSymbols;
        std::set<std::string> typedHeaders;

        for (mlir::Operation* schema : schemaOps)
        {
            const auto symNameAttr = schema->getAttrOfType<mlir::StringAttr>("sym_name");
            if (!symNameAttr)
            {
                continue;
            }
            const auto        fullNameAttr = schema->getAttrOfType<mlir::StringAttr>("full_name");
            const std::string fullName = fullNameAttr ? fullNameAttr.getValue().str() : symNameAttr.getValue().str();
            const auto        headerPathAttr = schema->getAttrOfType<mlir::StringAttr>("header_path");
            const std::string headerPath     = headerPathAttr ? headerPathAttr.getValue().str() : std::string{};

            if (schema->getNumRegions() == 0 || schema->getRegion(0).empty())
            {
                continue;
            }

            for (mlir::Operation& child : schema->getRegion(0).front())
            {
                if (child.getName().getStringRef() != "dsdl.serialization_plan")
                {
                    continue;
                }
                if (const auto envelopeViolation = findLoweredContractEnvelopeViolation(&child))
                {
                    switch (envelopeViolation->kind)
                    {
                    case LoweredContractEnvelopeViolationKind::MissingVersion:
                        child.emitOpError("missing lowered contract version; run "
                                          "lower-dsdl-exec before convert-dsdl-to-emitc");
                        break;
                    case LoweredContractEnvelopeViolationKind::UnsupportedMajorVersion:
                        child.emitOpError(
                            "unsupported lowered contract major version: " +
                            loweredSerDesUnsupportedMajorVersionDiagnosticDetail(envelopeViolation->encodedVersion) +
                            "; run matching lower-dsdl-exec before convert-dsdl-to-emitc");
                        break;
                    case LoweredContractEnvelopeViolationKind::ProducerMismatch:
                        child.emitOpError("missing lowered contract producer marker; run "
                                          "lower-dsdl-exec before convert-dsdl-to-emitc");
                        break;
                    }
                    signalPassFailure();
                    return;
                }
                if (const auto violation = findLoweredPlanContractViolation(module, &child))
                {
                    violation->operation->emitOpError(violation->message);
                    signalPassFailure();
                    return;
                }
                const auto        sectionAttr = child.getAttrOfType<mlir::StringAttr>("section");
                const std::string section     = sectionAttr ? sectionAttr.getValue().str() : std::string{};
                const std::string fnStem      = symNameAttr.getValue().str() + renderSectionSymbolSuffix(section);
                const auto capacityCheckAttr  = child.getAttrOfType<mlir::StringAttr>(kLoweredCapacityCheckHelperAttr);
                const std::string capacityCheckSymbol =
                    capacityCheckAttr ? capacityCheckAttr.getValue().str() : std::string{};
                const std::int64_t minBits =
                    nonNegative(child.getAttrOfType<mlir::IntegerAttr>(kLoweredMinBitsAttr).getInt());
                const std::int64_t maxBits =
                    nonNegative(child.getAttrOfType<mlir::IntegerAttr>(kLoweredMaxBitsAttr).getInt());
                const auto cTypeNameAttr          = child.getAttrOfType<mlir::StringAttr>("c_type_name");
                const auto cSerializeSymbolAttr   = child.getAttrOfType<mlir::StringAttr>("c_serialize_symbol");
                const auto cDeserializeSymbolAttr = child.getAttrOfType<mlir::StringAttr>("c_deserialize_symbol");
                const std::string cTypeName       = cTypeNameAttr ? cTypeNameAttr.getValue().str() : std::string{};
                if (!cTypeName.empty())
                {
                    forwardDeclaredTypes.insert(cTypeName);
                }
                const auto steps = collectPlanSteps(&child);
                for (const auto& step : steps)
                {
                    // C member names are the C backend's to decide, not lowering's: it stamps
                    // `c_name` onto its own clone of the schema so that the struct declaration and
                    // the references below cannot disagree. Reaching here without one means the
                    // schema came from somewhere that did not, and every member reference this plan
                    // emits would name nothing.
                    if ((step.kind == PlanStepKind::Field) && step.cName.empty())
                    {
                        child.emitOpError("field step '" + step.name + "' has no 'c_name' attribute");
                        signalPassFailure();
                        return;
                    }
                    if (!step.serUnsignedHelper.empty())
                    {
                        if (!module.lookupSymbol<mlir::func::FuncOp>(step.serUnsignedHelper))
                        {
                            child.emitOpError("missing lowered scalar helper symbol: " + step.serUnsignedHelper);
                            signalPassFailure();
                            return;
                        }
                        scalarUnsignedHelperSymbols.insert(step.serUnsignedHelper);
                    }
                    if (!step.deserUnsignedHelper.empty())
                    {
                        if (!module.lookupSymbol<mlir::func::FuncOp>(step.deserUnsignedHelper))
                        {
                            child.emitOpError("missing lowered scalar helper symbol: " + step.deserUnsignedHelper);
                            signalPassFailure();
                            return;
                        }
                        scalarUnsignedHelperSymbols.insert(step.deserUnsignedHelper);
                    }
                    if (!step.serSignedHelper.empty())
                    {
                        if (!module.lookupSymbol<mlir::func::FuncOp>(step.serSignedHelper))
                        {
                            child.emitOpError("missing lowered scalar helper symbol: " + step.serSignedHelper);
                            signalPassFailure();
                            return;
                        }
                        scalarSignedHelperSymbols.insert(step.serSignedHelper);
                    }
                    if (!step.deserSignedHelper.empty())
                    {
                        if (!module.lookupSymbol<mlir::func::FuncOp>(step.deserSignedHelper))
                        {
                            child.emitOpError("missing lowered scalar helper symbol: " + step.deserSignedHelper);
                            signalPassFailure();
                            return;
                        }
                        scalarSignedHelperSymbols.insert(step.deserSignedHelper);
                    }
                    if (!step.serFloatHelper.empty())
                    {
                        if (!module.lookupSymbol<mlir::func::FuncOp>(step.serFloatHelper))
                        {
                            child.emitOpError("missing lowered scalar helper symbol: " + step.serFloatHelper);
                            signalPassFailure();
                            return;
                        }
                        scalarFloatHelperSymbols.insert(step.serFloatHelper);
                    }
                    if (!step.deserFloatHelper.empty())
                    {
                        if (!module.lookupSymbol<mlir::func::FuncOp>(step.deserFloatHelper))
                        {
                            child.emitOpError("missing lowered scalar helper symbol: " + step.deserFloatHelper);
                            signalPassFailure();
                            return;
                        }
                        scalarFloatHelperSymbols.insert(step.deserFloatHelper);
                    }
                    if (!step.serArrayLengthPrefixHelper.empty())
                    {
                        if (!module.lookupSymbol<mlir::func::FuncOp>(step.serArrayLengthPrefixHelper))
                        {
                            child.emitOpError("missing lowered array-length-prefix helper symbol: " +
                                              step.serArrayLengthPrefixHelper);
                            signalPassFailure();
                            return;
                        }
                        arrayLengthPrefixHelperSymbols.insert(step.serArrayLengthPrefixHelper);
                    }
                    if (!step.deserArrayLengthPrefixHelper.empty())
                    {
                        if (!module.lookupSymbol<mlir::func::FuncOp>(step.deserArrayLengthPrefixHelper))
                        {
                            child.emitOpError("missing lowered array-length-prefix helper symbol: " +
                                              step.deserArrayLengthPrefixHelper);
                            signalPassFailure();
                            return;
                        }
                        arrayLengthPrefixHelperSymbols.insert(step.deserArrayLengthPrefixHelper);
                    }
                    if (!step.arrayLengthValidateHelper.empty())
                    {
                        if (!module.lookupSymbol<mlir::func::FuncOp>(step.arrayLengthValidateHelper))
                        {
                            child.emitOpError("missing lowered array-length helper symbol: " +
                                              step.arrayLengthValidateHelper);
                            signalPassFailure();
                            return;
                        }
                        arrayLengthValidateSymbols.insert(step.arrayLengthValidateHelper);
                    }
                    if (!step.delimiterValidateHelper.empty())
                    {
                        if (!module.lookupSymbol<mlir::func::FuncOp>(step.delimiterValidateHelper))
                        {
                            child.emitOpError("missing lowered delimiter helper symbol: " +
                                              step.delimiterValidateHelper);
                            signalPassFailure();
                            return;
                        }
                        delimiterValidateSymbols.insert(step.delimiterValidateHelper);
                    }
                }
                const bool         isUnion          = child.hasAttr("is_union");
                const auto         unionTagBitsAttr = child.getAttrOfType<mlir::IntegerAttr>("union_tag_bits");
                const std::int64_t unionTagBits     = unionTagBitsAttr ? nonNegative(unionTagBitsAttr.getInt()) : 0;
                const auto unionTagSerHelperAttr = child.getAttrOfType<mlir::StringAttr>(kLoweredSerUnionTagHelperAttr);
                const auto unionTagDeserHelperAttr =
                    child.getAttrOfType<mlir::StringAttr>(kLoweredDeserUnionTagHelperAttr);
                const std::string unionTagSerializeHelper =
                    unionTagSerHelperAttr ? unionTagSerHelperAttr.getValue().str() : std::string{};
                const std::string unionTagDeserializeHelper =
                    unionTagDeserHelperAttr ? unionTagDeserHelperAttr.getValue().str() : std::string{};
                std::string unionTagValidateSymbol;
                if (isUnion)
                {
                    const auto unionTagValidateAttr =
                        child.getAttrOfType<mlir::StringAttr>(kLoweredUnionTagValidateHelperAttr);
                    unionTagValidateSymbol =
                        unionTagValidateAttr ? unionTagValidateAttr.getValue().str() : std::string{};
                    unionTagValidateSymbols.insert(unionTagValidateSymbol);
                    unionTagIoHelperSymbols.insert(unionTagSerializeHelper);
                    unionTagIoHelperSymbols.insert(unionTagDeserializeHelper);
                }
                const bool useTyped = headersAvailable && supportsTypedLowering(steps, isUnion, unionTagBits);
                if (requireTypedLowering && !useTyped)
                {
                    std::string reason = "typed lowering is required for this module";
                    if (!headersAvailable)
                    {
                        reason += " but generated headers are not available";
                    }
                    else
                    {
                        reason += " but the serialization plan contains unsupported constructs";
                    }
                    if (!section.empty())
                    {
                        reason += " (section: " + section + ")";
                    }
                    child.emitOpError(reason);
                    signalPassFailure();
                    return;
                }
                if (useTyped)
                {
                    if (isUnion && (unionTagSerializeHelper.empty() || unionTagDeserializeHelper.empty()))
                    {
                        child.emitOpError("typed lowering requires lowered union-tag IO helpers; run "
                                          "lower-dsdl-serialization before convert-dsdl-to-emitc");
                        signalPassFailure();
                        return;
                    }
                    for (const auto& step : steps)
                    {
                        if (step.kind != PlanStepKind::Field)
                        {
                            continue;
                        }
                        if (isVariableArrayKind(step.arrayKind) && step.arrayLengthValidateHelper.empty())
                        {
                            child.emitOpError("typed lowering requires lowered array-length validation helper "
                                              "for variable array field '" +
                                              step.cName +
                                              "'; run lower-dsdl-exec before "
                                              "convert-dsdl-to-emitc");
                            signalPassFailure();
                            return;
                        }
                        if (isVariableArrayKind(step.arrayKind) &&
                            (step.serArrayLengthPrefixHelper.empty() || step.deserArrayLengthPrefixHelper.empty()))
                        {
                            child.emitOpError("typed lowering requires lowered array-length-prefix IO "
                                              "helpers for variable array field '" +
                                              step.cName +
                                              "'; run lower-dsdl-exec before "
                                              "convert-dsdl-to-emitc");
                            signalPassFailure();
                            return;
                        }
                        if (step.scalarCategory == "unsigned" || step.scalarCategory == "byte" ||
                            step.scalarCategory == "utf8")
                        {
                            if (step.serUnsignedHelper.empty() || step.deserUnsignedHelper.empty())
                            {
                                child.emitOpError("typed lowering requires lowered unsigned scalar helpers for "
                                                  "field '" +
                                                  step.cName +
                                                  "'; run lower-dsdl-exec before "
                                                  "convert-dsdl-to-emitc");
                                signalPassFailure();
                                return;
                            }
                        }
                        else if (step.scalarCategory == "signed")
                        {
                            if (step.serSignedHelper.empty() || step.deserSignedHelper.empty())
                            {
                                child.emitOpError("typed lowering requires lowered signed scalar helpers for "
                                                  "field '" +
                                                  step.cName +
                                                  "'; run lower-dsdl-exec before "
                                                  "convert-dsdl-to-emitc");
                                signalPassFailure();
                                return;
                            }
                        }
                        else if (step.scalarCategory == "float")
                        {
                            if (step.serFloatHelper.empty() || step.deserFloatHelper.empty())
                            {
                                child.emitOpError("typed lowering requires lowered float scalar helpers for "
                                                  "field '" +
                                                  step.cName +
                                                  "'; run lower-dsdl-exec before "
                                                  "convert-dsdl-to-emitc");
                                signalPassFailure();
                                return;
                            }
                        }
                        else if (step.scalarCategory == "composite" && !step.compositeSealed)
                        {
                            if (step.delimiterValidateHelper.empty())
                            {
                                child.emitOpError("typed lowering requires lowered delimiter-header validation "
                                                  "helper for delimited composite field '" +
                                                  step.cName +
                                                  "'; run lower-dsdl-exec before "
                                                  "convert-dsdl-to-emitc");
                                signalPassFailure();
                                return;
                            }
                        }
                    }
                }
                if (useTyped)
                {
                    if (!headerPath.empty())
                    {
                        typedHeaders.insert(headerPath);
                    }
                }
                capacityCheckSymbols.insert(capacityCheckSymbol);

                if (useTyped)
                {
                    // Built already, by build-dsdl-plan-bodies, or not built at all: asking
                    // the module by symbol keeps the two passes from disagreeing about it.
                    const bool builtSerialize =
                        module.lookupSymbol<mlir::func::FuncOp>(fnStem + "__serialize_ir_") != nullptr;
                    if (!builtSerialize)
                    {
                    emittedFunctions.push_back(renderTypedSerializeFunction(fnStem + "__serialize_ir_",
                                                                            cTypeName,
                                                                            cSerializeSymbolAttr
                                                                                ? cSerializeSymbolAttr.getValue()
                                                                                : llvm::StringRef{},
                                                                            fullName,
                                                                            section,
                                                                            minBits,
                                                                            maxBits,
                                                                            steps,
                                                                            isUnion,
                                                                            unionTagBits,
                                                                            capacityCheckSymbol,
                                                                            unionTagValidateSymbol,
                                                                            unionTagSerializeHelper));
                    }
                    const bool builtDeserialize =
                        module.lookupSymbol<mlir::func::FuncOp>(fnStem + "__deserialize_ir_") != nullptr;
                    if (!builtDeserialize)
                    {
                    emittedFunctions.push_back(renderTypedDeserializeFunction(fnStem + "__deserialize_ir_",
                                                                              cTypeName,
                                                                              cDeserializeSymbolAttr
                                                                                  ? cDeserializeSymbolAttr.getValue()
                                                                                  : llvm::StringRef{},
                                                                              fullName,
                                                                              section,
                                                                              minBits,
                                                                              maxBits,
                                                                              steps,
                                                                              isUnion,
                                                                              unionTagBits,
                                                                              unionTagValidateSymbol,
                                                                              unionTagDeserializeHelper));
                    }
                }
                else
                {
                    emittedFunctions.push_back(renderGenericSerializeFunction(fnStem + "__serialize_ir_",
                                                                              cTypeName,
                                                                              cSerializeSymbolAttr
                                                                                  ? cSerializeSymbolAttr.getValue()
                                                                                  : llvm::StringRef{},
                                                                              fullName,
                                                                              section,
                                                                              minBits,
                                                                              maxBits,
                                                                              steps,
                                                                              capacityCheckSymbol));
                    emittedFunctions.push_back(renderGenericDeserializeFunction(fnStem + "__deserialize_ir_",
                                                                                cTypeName,
                                                                                cDeserializeSymbolAttr
                                                                                    ? cDeserializeSymbolAttr.getValue()
                                                                                    : llvm::StringRef{},
                                                                                fullName,
                                                                                section,
                                                                                minBits,
                                                                                maxBits,
                                                                                steps));
                }
            }
        }

        mlir::OpBuilder builder(module.getContext());
        builder.setInsertionPointToStart(&body);
        const mlir::Location loc = builder.getUnknownLoc();
        mlir::emitc::VerbatimOp::create(builder, loc, "#include <stddef.h>");
        mlir::emitc::VerbatimOp::create(builder, loc, "#include <stdint.h>");
        mlir::emitc::VerbatimOp::create(builder, loc, "#include \"dsdl_runtime.h\"");
        if (headersAvailable)
        {
            for (const auto& headerPath : typedHeaders)
            {
                mlir::emitc::VerbatimOp::create(builder, loc, "#include \"" + headerPath + "\"");
            }
        }
        mlir::emitc::VerbatimOp::create(builder, loc, "/* Generated from DSDL IR by convert-dsdl-to-emitc. */");
        for (const auto& typeName : forwardDeclaredTypes)
        {
            // NOLINTNEXTLINE(performance-inefficient-string-concatenation)
            mlir::emitc::VerbatimOp::create(builder, loc, "typedef struct " + typeName + " " + typeName + ";");
        }
        for (const auto& symbol : capacityCheckSymbols)
        {
            mlir::emitc::VerbatimOp::create(builder, loc, "int8_t " + symbol + "(int64_t);");
        }
        for (const auto& symbol : unionTagValidateSymbols)
        {
            mlir::emitc::VerbatimOp::create(builder, loc, "int8_t " + symbol + "(int64_t);");
        }
        for (const auto& symbol : unionTagIoHelperSymbols)
        {
            mlir::emitc::VerbatimOp::create(builder, loc, "int64_t " + symbol + "(int64_t);");
        }
        for (const auto& symbol : scalarUnsignedHelperSymbols)
        {
            mlir::emitc::VerbatimOp::create(builder, loc, "int64_t " + symbol + "(int64_t);");
        }
        for (const auto& symbol : scalarSignedHelperSymbols)
        {
            mlir::emitc::VerbatimOp::create(builder, loc, "int64_t " + symbol + "(int64_t);");
        }
        for (const auto& symbol : scalarFloatHelperSymbols)
        {
            // The scalar float helper is width-matched (float for 16/32-bit
            // fields, double for 64-bit); mirror its actual signature in the
            // forward declaration so the prototype matches the lowered definition.
            std::string ctype = "double";
            if (auto fn = module.lookupSymbol<mlir::func::FuncOp>(symbol))
            {
                if (fn.getFunctionType().getNumInputs() == 1 && fn.getFunctionType().getInput(0).isF32())
                {
                    ctype = "float";
                }
            }
            // NOLINTNEXTLINE(performance-inefficient-string-concatenation)
            mlir::emitc::VerbatimOp::create(builder, loc, ctype + " " + symbol + "(" + ctype + ");");
        }
        for (const auto& symbol : arrayLengthPrefixHelperSymbols)
        {
            mlir::emitc::VerbatimOp::create(builder, loc, "int64_t " + symbol + "(int64_t);");
        }
        for (const auto& symbol : arrayLengthValidateSymbols)
        {
            mlir::emitc::VerbatimOp::create(builder, loc, "int8_t " + symbol + "(int64_t);");
        }
        for (const auto& symbol : delimiterValidateSymbols)
        {
            mlir::emitc::VerbatimOp::create(builder, loc, "int8_t " + symbol + "(int64_t, int64_t);");
        }
        for (const auto& fn : emittedFunctions)
        {
            mlir::emitc::VerbatimOp::create(builder, loc, fn);
        }

        for (mlir::Operation* op : schemaOps)
        {
            op->erase();
        }

        // Before the conversion, not after. A loop carries an induction variable its caller
        // does not read, and here that is a dead scf result the canonicaliser drops; once it
        // is an emitc.variable it has memory effects and stays, reaching the consumer as a
        // set-but-unused declaration and their -Werror build.
        //
        // Applied as patterns rather than through a pass manager, because this is already
        // inside a pass.
        {
            mlir::RewritePatternSet cleanup(&getContext());
            for (mlir::RegisteredOperationName name : getContext().getRegisteredOperations())
            {
                name.getCanonicalizationPatterns(cleanup, &getContext());
            }
            if (mlir::failed(mlir::applyPatternsGreedily(module, std::move(cleanup))))
            {
                module.emitError("failed to canonicalize built plan bodies");
                signalPassFailure();
                return;
            }
        }

        if (getenv("LLVMDSDL_DUMP_IR") != nullptr)
        {
            llvm::errs() << "=== BEFORE CONVERSION ===\n";
            module.print(llvm::errs());
        }

        if (mlir::failed(lowerPlanOperations(module)))
        {
            return;
        }

        if (getenv("LLVMDSDL_DUMP_IR") != nullptr)
        {
            llvm::errs() << "=== AFTER CONVERSION ===\n";
            module.print(llvm::errs());
        }

        // Again after the conversion: it introduces values of its own, and a declaration the
        // emitted C never reads is a diagnostic in the consumer's build.
        {
            mlir::RewritePatternSet cleanup(&getContext());
            for (mlir::RegisteredOperationName name : getContext().getRegisteredOperations())
            {
                name.getCanonicalizationPatterns(cleanup, &getContext());
            }
            if (mlir::failed(mlir::applyPatternsGreedily(module, std::move(cleanup))))
            {
                module.emitError("failed to canonicalize converted plan bodies");
                signalPassFailure();
                return;
            }
        }
    }
};

}  // namespace

std::unique_ptr<mlir::Pass> createConvertDSDLToEmitCPass()
{
    return std::make_unique<ConvertDSDLToEmitCPass>();
}

std::unique_ptr<mlir::Pass> createBuildDSDLPlanBodiesPass()
{
    return std::make_unique<BuildDSDLPlanBodiesPass>();
}

void registerDSDLConvertPasses()
{
    static bool once = false;
    if (once)
    {
        return;
    }
    once = true;
    static mlir::PassRegistration<ConvertDSDLToEmitCPass> const reg;
    static mlir::PassRegistration<BuildDSDLPlanBodiesPass> const buildReg;
}

}  // namespace llvmdsdl
