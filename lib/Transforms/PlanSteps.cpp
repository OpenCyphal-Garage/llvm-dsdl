//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Reads a serialization plan's steps out of its operations.
///
//===----------------------------------------------------------------------===//
#include "llvmdsdl/Transforms/PlanSteps.h"

#include <llvm/ADT/StringRef.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/Region.h>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace llvmdsdl
{
namespace
{

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

}  // namespace

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

}  // namespace llvmdsdl
