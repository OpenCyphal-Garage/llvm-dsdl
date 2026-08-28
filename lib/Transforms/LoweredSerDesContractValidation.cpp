//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared lowered-serdes contract validation helpers.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Transforms/LoweredSerDesContractValidation.h"

#include <cstdint>
#include <optional>
#include <set>
#include <string>

#include <llvm/ADT/StringRef.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/BuiltinOps.h>

#include "llvmdsdl/Transforms/LoweredSerDesContract.h"
#include <mlir/Dialect/Func/IR/FuncOps.h>

namespace llvmdsdl
{
namespace
{

bool isVariableArrayKind(const llvm::StringRef arrayKind)
{
    return arrayKind == "variable_inclusive" || arrayKind == "variable_exclusive";
}

bool isSupportedArrayKind(const llvm::StringRef arrayKind)
{
    return arrayKind == "none" || arrayKind == "fixed" || isVariableArrayKind(arrayKind);
}

}  // namespace

std::optional<LoweredContractEnvelopeViolation> findLoweredContractEnvelopeViolation(mlir::Operation* operation)
{
    const auto contractVersion = operation->getAttrOfType<mlir::IntegerAttr>(kLoweredSerDesContractVersionAttr);
    if (!contractVersion)
    {
        return LoweredContractEnvelopeViolation{LoweredContractEnvelopeViolationKind::MissingVersion, 0};
    }
    if (!isSupportedLoweredSerDesContractVersion(contractVersion.getInt()))
    {
        return LoweredContractEnvelopeViolation{LoweredContractEnvelopeViolationKind::UnsupportedMajorVersion,
                                                contractVersion.getInt()};
    }
    const auto contractProducer = operation->getAttrOfType<mlir::StringAttr>(kLoweredSerDesContractProducerAttr);
    if (!contractProducer || contractProducer.getValue() != kLoweredSerDesContractProducer)
    {
        return LoweredContractEnvelopeViolation{LoweredContractEnvelopeViolationKind::ProducerMismatch, 0};
    }
    return std::nullopt;
}

std::optional<LoweredPlanContractViolation> findLoweredPlanContractViolation(mlir::ModuleOp   module,
                                                                             mlir::Operation* plan)
{
    if (!plan->hasAttr(kLoweredPlanMarkerAttr))
    {
        return LoweredPlanContractViolation{plan,
                                            "missing lowered marker attribute '" + std::string(kLoweredPlanMarkerAttr) +
                                                "'"};
    }
    const auto minBits      = plan->getAttrOfType<mlir::IntegerAttr>(kLoweredMinBitsAttr);
    const auto maxBits      = plan->getAttrOfType<mlir::IntegerAttr>(kLoweredMaxBitsAttr);
    const auto stepCount    = plan->getAttrOfType<mlir::IntegerAttr>(kLoweredStepCountAttr);
    const auto fieldCount   = plan->getAttrOfType<mlir::IntegerAttr>(kLoweredFieldCountAttr);
    const auto paddingCount = plan->getAttrOfType<mlir::IntegerAttr>(kLoweredPaddingCountAttr);
    const auto alignCount   = plan->getAttrOfType<mlir::IntegerAttr>(kLoweredAlignCountAttr);
    if (!minBits || !maxBits || !stepCount || !fieldCount || !paddingCount || !alignCount)
    {
        return LoweredPlanContractViolation{plan, "missing required lowered plan metadata"};
    }
    if (minBits.getInt() < 0 || maxBits.getInt() < minBits.getInt() || stepCount.getInt() < 0 ||
        fieldCount.getInt() < 0 || paddingCount.getInt() < 0 || alignCount.getInt() < 0)
    {
        return LoweredPlanContractViolation{plan, "invalid lowered plan metadata values"};
    }

    const auto capacityCheckHelper = plan->getAttrOfType<mlir::StringAttr>(kLoweredCapacityCheckHelperAttr);
    if (!capacityCheckHelper || capacityCheckHelper.getValue().empty())
    {
        return LoweredPlanContractViolation{plan,
                                            "missing lowered capacity-check helper attribute '" +
                                                std::string(kLoweredCapacityCheckHelperAttr) + "'"};
    }
    if (!module.lookupSymbol<mlir::func::FuncOp>(capacityCheckHelper.getValue()))
    {
        return LoweredPlanContractViolation{plan,
                                            "missing lowered capacity-check helper symbol: " +
                                                capacityCheckHelper.getValue().str()};
    }

    if (plan->hasAttr("is_union"))
    {
        const auto unionTagBits            = plan->getAttrOfType<mlir::IntegerAttr>("union_tag_bits");
        const auto unionOptionCount        = plan->getAttrOfType<mlir::IntegerAttr>("union_option_count");
        const auto unionTagValidateHelper  = plan->getAttrOfType<mlir::StringAttr>(kLoweredUnionTagValidateHelperAttr);
        const auto unionTagSerializeHelper = plan->getAttrOfType<mlir::StringAttr>(kLoweredSerUnionTagHelperAttr);
        const auto unionTagDeserializeHelper = plan->getAttrOfType<mlir::StringAttr>(kLoweredDeserUnionTagHelperAttr);
        if (!unionTagBits || !unionOptionCount || !unionTagValidateHelper || !unionTagSerializeHelper ||
            !unionTagDeserializeHelper)
        {
            return LoweredPlanContractViolation{plan, "missing required lowered union metadata"};
        }
        if (unionTagBits.getInt() <= 0 || unionTagBits.getInt() > 64 || unionOptionCount.getInt() <= 0)
        {
            return LoweredPlanContractViolation{plan, "invalid lowered union metadata values"};
        }
        if (!module.lookupSymbol<mlir::func::FuncOp>(unionTagValidateHelper.getValue()) ||
            !module.lookupSymbol<mlir::func::FuncOp>(unionTagSerializeHelper.getValue()) ||
            !module.lookupSymbol<mlir::func::FuncOp>(unionTagDeserializeHelper.getValue()))
        {
            return LoweredPlanContractViolation{plan, "missing lowered union-tag helper symbol body"};
        }
    }

    if (plan->getNumRegions() == 0 || plan->getRegion(0).empty())
    {
        return LoweredPlanContractViolation{plan, "must contain a non-empty lowered plan body"};
    }

    std::int64_t           observedStepCount    = 0;
    std::int64_t           observedFieldCount   = 0;
    std::int64_t           observedPaddingCount = 0;
    std::int64_t           observedAlignCount   = 0;
    std::set<std::int64_t> seenStepIndexes;
    for (mlir::Operation& step : plan->getRegion(0).front())
    {
        const auto stepName = step.getName().getStringRef();
        if (stepName == "dsdl.align")
        {
            const auto bits      = step.getAttrOfType<mlir::IntegerAttr>("bits");
            const auto stepIndex = step.getAttrOfType<mlir::IntegerAttr>("step_index");
            if (!bits || !stepIndex)
            {
                return LoweredPlanContractViolation{&step, "missing lowered align metadata"};
            }
            if (bits.getInt() <= 1)
            {
                return LoweredPlanContractViolation{&step, "unexpected no-op alignment in lowered plan"};
            }
            if (!seenStepIndexes.insert(stepIndex.getInt()).second)
            {
                return LoweredPlanContractViolation{&step, "duplicate lowered step_index"};
            }
            ++observedStepCount;
            ++observedAlignCount;
            continue;
        }
        if (stepName != "dsdl.io")
        {
            return LoweredPlanContractViolation{&step, "unsupported lowered plan operation"};
        }

        const auto stepMinBits    = step.getAttrOfType<mlir::IntegerAttr>("min_bits");
        const auto stepMaxBits    = step.getAttrOfType<mlir::IntegerAttr>("max_bits");
        const auto loweredBits    = step.getAttrOfType<mlir::IntegerAttr>("lowered_bits");
        const auto stepIndex      = step.getAttrOfType<mlir::IntegerAttr>("step_index");
        const auto scalarCategory = step.getAttrOfType<mlir::StringAttr>("scalar_category");
        const auto arrayKind      = step.getAttrOfType<mlir::StringAttr>("array_kind");
        const auto kind           = step.getAttrOfType<mlir::StringAttr>("kind");
        const auto bitLength      = step.getAttrOfType<mlir::IntegerAttr>("bit_length");
        const auto alignmentBits  = step.getAttrOfType<mlir::IntegerAttr>("alignment_bits");
        if (!stepMinBits || !stepMaxBits || !loweredBits || !stepIndex || !scalarCategory || !arrayKind || !kind ||
            !bitLength || !alignmentBits)
        {
            return LoweredPlanContractViolation{&step, "missing required lowered step metadata"};
        }
        if (stepMinBits.getInt() < 0 || stepMaxBits.getInt() < stepMinBits.getInt() || loweredBits.getInt() < 0 ||
            loweredBits.getInt() != stepMaxBits.getInt() || bitLength.getInt() < 0 || alignmentBits.getInt() <= 0)
        {
            return LoweredPlanContractViolation{&step, "invalid lowered step metadata values"};
        }
        if (!isSupportedArrayKind(arrayKind.getValue()))
        {
            return LoweredPlanContractViolation{&step, "unsupported array kind in lowered step metadata"};
        }
        if (kind.getValue() != "field" && kind.getValue() != "padding")
        {
            return LoweredPlanContractViolation{&step, "unsupported step kind in lowered step metadata"};
        }
        if (!seenStepIndexes.insert(stepIndex.getInt()).second)
        {
            return LoweredPlanContractViolation{&step, "duplicate lowered step_index"};
        }
        ++observedStepCount;

        const bool isPadding = kind.getValue() == "padding";
        if (isPadding)
        {
            ++observedPaddingCount;
        }
        else
        {
            ++observedFieldCount;
        }
        auto requireStepHelperSymbol =
            [&](const llvm::StringRef attrName,
                const llvm::StringRef helperLabel) -> std::optional<LoweredPlanContractViolation> {
            const auto helper = step.getAttrOfType<mlir::StringAttr>(attrName);
            if (!helper || helper.getValue().empty())
            {
                return LoweredPlanContractViolation{&step,
                                                    "missing lowered " + helperLabel.str() + " helper attribute '" +
                                                        attrName.str() + "'"};
            }
            if (!module.lookupSymbol<mlir::func::FuncOp>(helper.getValue()))
            {
                return LoweredPlanContractViolation{&step,
                                                    "missing lowered " + helperLabel.str() +
                                                        " helper symbol: " + helper.getValue().str()};
            }
            return std::nullopt;
        };

        const bool variableArray   = isVariableArrayKind(arrayKind.getValue());
        const auto arrayPrefixBits = step.getAttrOfType<mlir::IntegerAttr>("array_length_prefix_bits");
        if (variableArray && (!arrayPrefixBits || arrayPrefixBits.getInt() <= 0 || arrayPrefixBits.getInt() > 64))
        {
            return LoweredPlanContractViolation{&step, "missing valid array-length prefix width"};
        }
        if (!isPadding && variableArray)
        {
            if (const auto violation =
                    requireStepHelperSymbol("lowered_ser_array_length_prefix_helper", "array-length-prefix"))
            {
                return violation;
            }
            if (const auto violation =
                    requireStepHelperSymbol("lowered_deser_array_length_prefix_helper", "array-length-prefix"))
            {
                return violation;
            }
            if (const auto violation =
                    requireStepHelperSymbol("lowered_array_length_validate_helper", "array-length-validate"))
            {
                return violation;
            }
        }

        if (!isPadding)
        {
            const auto category = scalarCategory.getValue();
            if (category == "unsigned" || category == "byte" || category == "utf8")
            {
                if (const auto violation = requireStepHelperSymbol("lowered_ser_unsigned_helper", "scalar-unsigned"))
                {
                    return violation;
                }
                if (const auto violation = requireStepHelperSymbol("lowered_deser_unsigned_helper", "scalar-unsigned"))
                {
                    return violation;
                }
            }
            else if (category == "signed")
            {
                if (const auto violation = requireStepHelperSymbol("lowered_ser_signed_helper", "scalar-signed"))
                {
                    return violation;
                }
                if (const auto violation = requireStepHelperSymbol("lowered_deser_signed_helper", "scalar-signed"))
                {
                    return violation;
                }
            }
            else if (category == "float")
            {
                if (const auto violation = requireStepHelperSymbol("lowered_ser_float_helper", "scalar-float"))
                {
                    return violation;
                }
                if (const auto violation = requireStepHelperSymbol("lowered_deser_float_helper", "scalar-float"))
                {
                    return violation;
                }
            }
        }

        if (!isPadding && scalarCategory.getValue() == "composite")
        {
            const auto compositeSealed = step.getAttrOfType<mlir::BoolAttr>("composite_sealed");
            if (compositeSealed && !compositeSealed.getValue())
            {
                if (!step.getAttrOfType<mlir::IntegerAttr>("composite_extent_bits"))
                {
                    return LoweredPlanContractViolation{&step,
                                                        "delimited composite missing composite_extent_bits metadata"};
                }
                if (const auto violation =
                        requireStepHelperSymbol("lowered_delimiter_validate_helper", "delimiter-validate"))
                {
                    return violation;
                }
            }
        }
    }

    if (observedStepCount != stepCount.getInt() || observedFieldCount != fieldCount.getInt() ||
        observedPaddingCount != paddingCount.getInt() || observedAlignCount != alignCount.getInt())
    {
        return LoweredPlanContractViolation{plan, "lowered plan counts do not match plan body"};
    }
    for (const auto stepIndex : seenStepIndexes)
    {
        if (stepIndex < 0 || stepIndex >= stepCount.getInt())
        {
            return LoweredPlanContractViolation{plan, "step_index out of lowered plan bounds"};
        }
    }

    return std::nullopt;
}

}  // namespace llvmdsdl
