//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The steps of a serialization plan, read once for every target.
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

namespace mlir
{
class Operation;
}  // namespace mlir

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

/// @brief One step of a serialization plan, with every fact the lowering stamped on it.
struct PlanStep final
{
    /// @brief What the step does.
    PlanStepKind kind{PlanStepKind::Field};
    /// @brief Alignment or padding width; the field's own width lives in @ref bitLength.
    std::int64_t bits{0};
    /// @brief The DSDL field name.
    std::string name;
    /// @brief The C member name the backend stamped.
    std::string cName;
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
    /// @brief The C type of a nested composite.
    std::string compositeCTypeName;
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
/// @param[in] plan A `dsdl.serialization_plan` operation.
/// @return Its steps.
std::vector<PlanStep> collectPlanSteps(mlir::Operation* plan);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_TRANSFORMS_PLAN_STEPS_H
