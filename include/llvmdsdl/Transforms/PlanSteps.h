//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The steps of a serialisation plan, read once for every target.
///
/// A `dsdl.serialization_plan` is a sequence of `dsdl.align` and `dsdl.io` operations carrying
/// their facts as attributes. This is the view the body builder and the conversions read them
/// through, so that no two of them read an attribute differently.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_TRANSFORMS_PLAN_STEPS_H
#define LLVMDSDL_TRANSFORMS_PLAN_STEPS_H

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <llvm/ADT/StringRef.h>

#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/Support/DefinitionNaming.h"

namespace llvmdsdl
{

/// @brief Clamps @p value at zero.
inline std::int64_t nonNegative(const std::int64_t value)
{
    return std::max<std::int64_t>(value, 0);
}

/// @brief The narrowest standard integer width that holds @p bits.
inline unsigned holderWidthFor(const std::int64_t bits)
{
    if (bits <= 8)
    {
        return 8U;
    }
    if (bits <= 16)
    {
        return 16U;
    }
    if (bits <= 32)
    {
        return 32U;
    }
    return 64U;
}

/// @brief Whether @p arrayKind names a variable-length array.
inline bool isVariableArrayKind(const llvm::StringRef arrayKind)
{
    return arrayKind == "variable_inclusive" || arrayKind == "variable_exclusive";
}

/// @brief Whether @p arrayKind is one the plan vocabulary defines.
inline bool isSupportedArrayKind(const llvm::StringRef arrayKind)
{
    return arrayKind == "none" || arrayKind == "fixed" || isVariableArrayKind(arrayKind);
}

/// @brief What one step of a plan does to the wire.
enum class PlanStepKind
{
    /// @brief Rounds the offset up to a boundary.
    Align,
    /// @brief Reserves bits that carry no member.
    Padding,
    /// @brief Encodes or decodes a member.
    Field
};

/// @brief One step of a serialisation plan, with every fact the lowering stamped on it.
struct PlanStep final
{
    /// @brief What the step does.
    PlanStepKind kind{PlanStepKind::Field};
    /// @brief Alignment or padding width; the field's own width lives in @ref bitLength.
    std::int64_t bits{0};
    /// @brief The DSDL field name.
    std::string name;
    /// @brief The scalar category: `bool`, `unsigned`, `signed`, `float`, `composite`.
    std::string scalarCategory;
    /// @brief `saturated` or `truncated`.
    std::string castMode;
    /// @brief `none`, `fixed`, `variable_inclusive` or `variable_exclusive`.
    std::string arrayKind;
    /// @brief Width of one scalar element, in bits.
    std::int64_t bitLength{0};
    /// @brief Capacity of an array.
    std::int64_t arrayCapacity{0};
    /// @brief Width of a variable-length array's length prefix, in bits.
    std::int64_t arrayLengthPrefixBits{0};
    /// @brief Alignment the field requires, in bits.
    std::int64_t alignmentBits{1};
    /// @brief Which option this field is, within a union.
    std::int64_t unionOptionIndex{0};
    /// @brief Width of the union tag, in bits.
    std::int64_t unionTagBits{0};
    /// @brief Full name of the composite a composite step holds, else empty.
    std::string compositeFullName;

    /// @brief Version of that composite.
    std::int64_t compositeMajor{0};
    std::int64_t compositeMinor{0};
    /// @brief Lowered helper symbol.
    std::string serUnsignedHelper;
    /// @brief Lowered helper symbol.
    std::string deserUnsignedHelper;
    /// @brief Lowered helper symbol.
    std::string serSignedHelper;
    /// @brief Lowered helper symbol.
    std::string deserSignedHelper;
    /// @brief Lowered helper symbol.
    std::string serFloatHelper;
    /// @brief Lowered helper symbol.
    std::string deserFloatHelper;
    /// @brief Lowered helper symbol.
    std::string serArrayLengthPrefixHelper;
    /// @brief Lowered helper symbol.
    std::string deserArrayLengthPrefixHelper;
    /// @brief Lowered helper symbol.
    std::string arrayLengthValidateHelper;
    /// @brief Lowered helper symbol.
    std::string delimiterValidateHelper;
    /// @brief Whether a nested composite is sealed rather than delimited.
    bool compositeSealed{true};
    /// @brief Extent of a delimited nested composite, in bits.
    std::int64_t compositeExtentBits{0};
};

/// @brief Reads the steps of @p plan, in order.
/// @param[in] plan The plan.
/// @return Its steps.
std::vector<PlanStep> collectPlanSteps(mlir::dsdl::SerializationPlanOp plan);

/// @brief The identity of one plan's object: full name, version, and the section of a service.
///
/// `!dsdl.object` carries it, and every target resolves it to the struct or class it declares.
inline std::string planIdentity(const llvm::StringRef fullName,
                                const std::int64_t    major,
                                const std::int64_t    minor,
                                const llvm::StringRef section)
{
    std::string identity = fullName.str() + "." + std::to_string(major) + "." + std::to_string(minor);
    if (!section.empty())
    {
        identity += "/" + section.str();
    }
    return identity;
}

inline std::string planIdentity(mlir::dsdl::SchemaOp schema, mlir::dsdl::SerializationPlanOp plan)
{
    return planIdentity(schema.getFullName(),
                        schema.getMajor(),
                        schema.getMinor(),
                        plan.getSection().value_or(llvm::StringRef{}));
}

/// @brief The symbol `build-dsdl-plan-bodies` gives one plan's body in one direction.
inline std::string planBodySymbol(const llvm::StringRef fullName,
                                  const std::int64_t    major,
                                  const std::int64_t    minor,
                                  const llvm::StringRef section,
                                  const bool            serialize)
{
    return renderDefinitionSymbolBase(fullName, static_cast<std::uint32_t>(major), static_cast<std::uint32_t>(minor)) +
           renderSectionSymbolSuffix(section) + (serialize ? "__serialize_ir_" : "__deserialize_ir_");
}

}  // namespace llvmdsdl

#endif  // LLVMDSDL_TRANSFORMS_PLAN_STEPS_H
