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

#include <llvm/ADT/StringRef.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/Support/LLVM.h>

#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/Transforms/LoweredSerDesContract.h"
#include <mlir/Dialect/Func/IR/FuncOps.h>

namespace llvmdsdl
{
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
                                                                             mlir::Operation* operation)
{
    auto plan = mlir::dyn_cast<mlir::dsdl::SerializationPlanOp>(operation);
    if (!plan)
    {
        return LoweredPlanContractViolation{operation, "not a serialization plan"};
    }
    if (!plan.getLowered())
    {
        return LoweredPlanContractViolation{operation,
                                            "missing lowered marker attribute '" + plan.getLoweredAttrName().str() +
                                                "'"};
    }
    const auto minBits      = plan.getLoweredMinBits();
    const auto maxBits      = plan.getLoweredMaxBits();
    const auto stepCount    = plan.getLoweredStepCount();
    const auto fieldCount   = plan.getLoweredFieldCount();
    const auto paddingCount = plan.getLoweredPaddingCount();
    const auto alignCount   = plan.getLoweredAlignCount();
    if (!minBits || !maxBits || !stepCount || !fieldCount || !paddingCount || !alignCount)
    {
        return LoweredPlanContractViolation{operation, "missing required lowered plan metadata"};
    }
    if (*minBits < 0 || *maxBits < *minBits || *stepCount < 0 || *fieldCount < 0 || *paddingCount < 0 ||
        *alignCount < 0)
    {
        return LoweredPlanContractViolation{operation, "invalid lowered plan metadata values"};
    }

    const auto capacityCheckHelper = plan.getLoweredCapacityCheckHelper();
    if (!capacityCheckHelper || capacityCheckHelper->empty())
    {
        return LoweredPlanContractViolation{operation,
                                            "missing lowered capacity-check helper attribute '" +
                                                plan.getLoweredCapacityCheckHelperAttrName().str() + "'"};
    }
    if (!module.lookupSymbol<mlir::func::FuncOp>(*capacityCheckHelper))
    {
        return LoweredPlanContractViolation{operation,
                                            "missing lowered capacity-check helper symbol: " +
                                                capacityCheckHelper->str()};
    }

    if (plan.getIsUnion())
    {
        const auto unionTagBits              = plan.getUnionTagBits();
        const auto unionOptionCount          = plan.getUnionOptionCount();
        const auto unionTagValidateHelper    = plan.getLoweredUnionTagValidateHelper();
        const auto unionTagSerializeHelper   = plan.getLoweredSerUnionTagHelper();
        const auto unionTagDeserializeHelper = plan.getLoweredDeserUnionTagHelper();
        if (!unionTagBits || !unionOptionCount || !unionTagValidateHelper || !unionTagSerializeHelper ||
            !unionTagDeserializeHelper)
        {
            return LoweredPlanContractViolation{operation, "missing required lowered union metadata"};
        }
        if (*unionTagBits <= 0 || *unionTagBits > 64 || *unionOptionCount <= 0)
        {
            return LoweredPlanContractViolation{operation, "invalid lowered union metadata values"};
        }
        if (!module.lookupSymbol<mlir::func::FuncOp>(*unionTagValidateHelper) ||
            !module.lookupSymbol<mlir::func::FuncOp>(*unionTagSerializeHelper) ||
            !module.lookupSymbol<mlir::func::FuncOp>(*unionTagDeserializeHelper))
        {
            return LoweredPlanContractViolation{operation, "missing lowered union-tag helper symbol body"};
        }
    }

    if (plan.getBody().empty())
    {
        return LoweredPlanContractViolation{operation, "must contain a non-empty lowered plan body"};
    }

    std::int64_t           observedStepCount    = 0;
    std::int64_t           observedFieldCount   = 0;
    std::int64_t           observedPaddingCount = 0;
    std::int64_t           observedAlignCount   = 0;
    std::set<std::int64_t> seenStepIndexes;
    for (mlir::Operation& stepOp : plan.getBody().front())
    {
        if (auto align = mlir::dyn_cast<mlir::dsdl::AlignOp>(stepOp))
        {
            const auto stepIndex = align.getStepIndex();
            if (!stepIndex)
            {
                return LoweredPlanContractViolation{&stepOp, "missing lowered align metadata"};
            }
            if (align.getBits() <= 1)
            {
                return LoweredPlanContractViolation{&stepOp, "unexpected no-op alignment in lowered plan"};
            }
            if (!seenStepIndexes.insert(*stepIndex).second)
            {
                return LoweredPlanContractViolation{&stepOp, "duplicate lowered step_index"};
            }
            ++observedStepCount;
            ++observedAlignCount;
            continue;
        }
        auto step = mlir::dyn_cast<mlir::dsdl::IOOp>(stepOp);
        if (!step)
        {
            return LoweredPlanContractViolation{&stepOp, "unsupported lowered plan operation"};
        }

        const auto loweredBits = step.getLoweredBits();
        const auto stepIndex   = step.getStepIndex();
        if (!loweredBits || !stepIndex)
        {
            return LoweredPlanContractViolation{&stepOp, "missing required lowered step metadata"};
        }
        if (*loweredBits < 0 || *loweredBits != step.getMaxBits())
        {
            return LoweredPlanContractViolation{&stepOp, "invalid lowered step metadata values"};
        }
        if (!seenStepIndexes.insert(*stepIndex).second)
        {
            return LoweredPlanContractViolation{&stepOp, "duplicate lowered step_index"};
        }
        ++observedStepCount;

        const bool isPadding = step.isPadding();
        if (isPadding)
        {
            ++observedPaddingCount;
        }
        else
        {
            ++observedFieldCount;
        }
        auto requireStepHelperSymbol =
            [&](const std::optional<llvm::StringRef> helper,
                const llvm::StringRef                attrName,
                const llvm::StringRef                helperLabel) -> std::optional<LoweredPlanContractViolation> {
            if (!helper || helper->empty())
            {
                return LoweredPlanContractViolation{&stepOp,
                                                    "missing lowered " + helperLabel.str() + " helper attribute '" +
                                                        attrName.str() + "'"};
            }
            if (!module.lookupSymbol<mlir::func::FuncOp>(*helper))
            {
                return LoweredPlanContractViolation{&stepOp,
                                                    "missing lowered " + helperLabel.str() +
                                                        " helper symbol: " + helper->str()};
            }
            return std::nullopt;
        };

        const bool variableArray = step.isVariableArray();
        if (variableArray && (step.getArrayLengthPrefixBits() <= 0 || step.getArrayLengthPrefixBits() > 64))
        {
            return LoweredPlanContractViolation{&stepOp, "missing valid array-length prefix width"};
        }
        if (!isPadding && variableArray)
        {
            if (const auto violation = requireStepHelperSymbol(step.getLoweredSerArrayLengthPrefixHelper(),
                                                               step.getLoweredSerArrayLengthPrefixHelperAttrName(),
                                                               "array-length-prefix"))
            {
                return violation;
            }
            if (const auto violation = requireStepHelperSymbol(step.getLoweredDeserArrayLengthPrefixHelper(),
                                                               step.getLoweredDeserArrayLengthPrefixHelperAttrName(),
                                                               "array-length-prefix"))
            {
                return violation;
            }
            if (const auto violation = requireStepHelperSymbol(step.getLoweredArrayLengthValidateHelper(),
                                                               step.getLoweredArrayLengthValidateHelperAttrName(),
                                                               "array-length-validate"))
            {
                return violation;
            }
        }

        if (!isPadding)
        {
            const auto category = step.getScalarCategory();
            if (category == "unsigned" || category == "byte" || category == "utf8")
            {
                if (const auto violation = requireStepHelperSymbol(step.getLoweredSerUnsignedHelper(),
                                                                   step.getLoweredSerUnsignedHelperAttrName(),
                                                                   "scalar-unsigned"))
                {
                    return violation;
                }
                if (const auto violation = requireStepHelperSymbol(step.getLoweredDeserUnsignedHelper(),
                                                                   step.getLoweredDeserUnsignedHelperAttrName(),
                                                                   "scalar-unsigned"))
                {
                    return violation;
                }
            }
            else if (category == "signed")
            {
                if (const auto violation = requireStepHelperSymbol(step.getLoweredSerSignedHelper(),
                                                                   step.getLoweredSerSignedHelperAttrName(),
                                                                   "scalar-signed"))
                {
                    return violation;
                }
                if (const auto violation = requireStepHelperSymbol(step.getLoweredDeserSignedHelper(),
                                                                   step.getLoweredDeserSignedHelperAttrName(),
                                                                   "scalar-signed"))
                {
                    return violation;
                }
            }
            else if (category == "float")
            {
                if (const auto violation = requireStepHelperSymbol(step.getLoweredSerFloatHelper(),
                                                                   step.getLoweredSerFloatHelperAttrName(),
                                                                   "scalar-float"))
                {
                    return violation;
                }
                if (const auto violation = requireStepHelperSymbol(step.getLoweredDeserFloatHelper(),
                                                                   step.getLoweredDeserFloatHelperAttrName(),
                                                                   "scalar-float"))
                {
                    return violation;
                }
            }
        }

        if (!isPadding && step.isComposite() && !step.getCompositeSealed().value_or(true))
        {
            if (const auto violation = requireStepHelperSymbol(step.getLoweredDelimiterValidateHelper(),
                                                               step.getLoweredDelimiterValidateHelperAttrName(),
                                                               "delimiter-validate"))
            {
                return violation;
            }
        }
    }

    if (observedStepCount != *stepCount || observedFieldCount != *fieldCount || observedPaddingCount != *paddingCount ||
        observedAlignCount != *alignCount)
    {
        return LoweredPlanContractViolation{operation, "lowered plan counts do not match plan body"};
    }
    for (const auto stepIndex : seenStepIndexes)
    {
        if (stepIndex < 0 || stepIndex >= *stepCount)
        {
            return LoweredPlanContractViolation{operation, "step_index out of lowered plan bounds"};
        }
    }

    return std::nullopt;
}

}  // namespace llvmdsdl
