//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Builds backend-neutral runtime lowering plans from lowered render IR.
///
/// The planning utilities convert generic render steps into runtime execution
/// primitives shared by scripted emitters.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/RuntimeLoweredPlan.h"

#include <algorithm>
#include <set>
#include <string>
#include <cstddef>
#include <utility>

#include "llvmdsdl/CodeGen/LoweredRenderIR.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"
#include "llvmdsdl/CodeGen/SerDesStatementPlan.h"
#include "llvmdsdl/Semantics/BitLengthSet.h"
#include "llvm/Support/Error.h"

namespace llvmdsdl
{
namespace
{

std::size_t expectedStepCount(const SemanticSection& section)
{
    if (!section.isUnion)
    {
        return section.fields.size();
    }
    std::size_t count = 0;
    for (const auto& field : section.fields)
    {
        if (!field.isPadding)
        {
            ++count;
        }
    }
    return count;
}

}  // namespace

llvm::Expected<std::vector<RuntimeOrderedFieldStep>> buildRuntimeOrderedFieldSteps(
    const SemanticSection&           section,
    const LoweredSectionFacts* const sectionFacts)
{
    if (sectionFacts == nullptr)
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "missing lowered section facts required for runtime planning");
    }

    const auto renderIR = buildLoweredBodyRenderIR(section, sectionFacts, HelperBindingDirection::Serialize);
    if (auto contractErr = validateLoweredBodyRenderIRContract(renderIR, "runtime-lowered-plan"))
    {
        return std::move(contractErr);
    }
    std::vector<RuntimeOrderedFieldStep> out;

    if (section.isUnion)
    {
        const auto* unionStep = renderIR.steps.empty() ? nullptr : &renderIR.steps.front();
        if (unionStep == nullptr || unionStep->kind != LoweredRenderStepKind::UnionDispatch)
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "lowered render IR missing union-dispatch step");
        }
        if (renderIR.steps.size() != 1U)
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "lowered union render IR unexpectedly contains non-dispatch steps");
        }
        out.reserve(unionStep->unionBranches.size());
        for (const auto& branch : unionStep->unionBranches)
        {
            if (branch.field == nullptr)
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "lowered union render IR contains null field branch");
            }
            const auto* const facts = findLoweredFieldFacts(sectionFacts, branch.field->name);
            if (facts == nullptr || !facts->stepIndex)
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "missing lowered step index for union field '%s'",
                                               branch.field->name.c_str());
            }
            out.push_back(RuntimeOrderedFieldStep{branch.field, branch.arrayLengthPrefixBits});
        }
    }
    else
    {
        out.reserve(renderIR.steps.size());
        for (const auto& step : renderIR.steps)
        {
            if (step.kind != LoweredRenderStepKind::Field && step.kind != LoweredRenderStepKind::Padding)
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "lowered struct render IR contains unsupported step kind");
            }
            if (step.fieldStep.field == nullptr)
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "lowered struct render IR contains null field step");
            }
            const auto* const facts = findLoweredFieldFacts(sectionFacts, step.fieldStep.field->name);
            if (facts == nullptr || !facts->stepIndex)
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "missing lowered step index for field '%s'",
                                               step.fieldStep.field->name.c_str());
            }
            out.push_back(RuntimeOrderedFieldStep{step.fieldStep.field, step.fieldStep.arrayLengthPrefixBits});
        }
    }

    if (out.size() != expectedStepCount(section))
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "lowered render IR step count does not match semantic field count");
    }
    std::set<const SemanticField*> uniqueness;
    for (const auto& step : out)
    {
        if (!uniqueness.insert(step.field).second)
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "lowered render IR contains duplicate field references");
        }
    }

    return out;
}

llvm::Expected<RuntimeSectionPlan> buildRuntimeSectionPlan(const SemanticSection&           section,
                                                           const LoweredSectionFacts* const sectionFacts)
{
    RuntimeSectionPlan plan;
    plan.contractVersion                = kWireOperationContractVersion;
    plan.isUnion                        = section.isUnion;
    std::int64_t                maxBits = 0;
    std::optional<std::int64_t> unionTagBits;
    std::set<std::uint32_t>     unionOptionIndexes;
    auto                        orderedStepsOrErr = buildRuntimeOrderedFieldSteps(section, sectionFacts);
    if (!orderedStepsOrErr)
    {
        return orderedStepsOrErr.takeError();
    }
    const auto& orderedSteps = *orderedStepsOrErr;
    for (const auto& orderedStep : orderedSteps)
    {
        if (orderedStep.field == nullptr)
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "lowered runtime section plan contains null field");
        }
        const auto& field     = *orderedStep.field;
        auto        fieldBits = static_cast<std::int64_t>(field.resolvedType.bitLength);
        if (fieldBits < 0)
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "field '%s' has invalid negative bit length",
                                           field.name.c_str());
        }

        RuntimeArrayKind arrayKind             = RuntimeArrayKind::None;
        std::int64_t     arrayCapacity         = 0;
        std::int64_t     arrayLengthPrefixBits = 0;
        if (field.resolvedType.arrayKind == ArrayKind::None)
        {
            arrayKind = RuntimeArrayKind::None;
        }
        else if (field.resolvedType.arrayKind == ArrayKind::Fixed)
        {
            if (field.resolvedType.arrayCapacity <= 0)
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "field '%s' has invalid fixed-array capacity",
                                               field.name.c_str());
            }
            arrayKind     = RuntimeArrayKind::Fixed;
            arrayCapacity = field.resolvedType.arrayCapacity;
        }
        else if (field.resolvedType.arrayKind == ArrayKind::VariableInclusive ||
                 field.resolvedType.arrayKind == ArrayKind::VariableExclusive)
        {
            const auto prefixBits = orderedStep.arrayLengthPrefixBits
                                        ? static_cast<std::int64_t>(*orderedStep.arrayLengthPrefixBits)
                                        : field.resolvedType.arrayLengthPrefixBits;
            if (field.resolvedType.arrayCapacity < 0 || prefixBits <= 0)
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "field '%s' has invalid variable-array capacity or prefix width",
                                               field.name.c_str());
            }
            arrayKind             = RuntimeArrayKind::Variable;
            arrayCapacity         = field.resolvedType.arrayCapacity;
            arrayLengthPrefixBits = prefixBits;
        }
        else
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "field '%s' has unsupported array kind",
                                           field.name.c_str());
        }

        RuntimeFieldKind kind = RuntimeFieldKind::Unsigned;
        switch (field.resolvedType.scalarCategory)
        {
        case SemanticScalarCategory::Bool:
            if (fieldBits != 1)
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "field '%s' bool bit length must be 1",
                                               field.name.c_str());
            }
            kind = RuntimeFieldKind::Bool;
            break;
        case SemanticScalarCategory::Byte:
        case SemanticScalarCategory::Utf8:
        case SemanticScalarCategory::UnsignedInt:
            kind = RuntimeFieldKind::Unsigned;
            break;
        case SemanticScalarCategory::SignedInt:
            kind = RuntimeFieldKind::Signed;
            break;
        case SemanticScalarCategory::Float:
            if (fieldBits != 16 && fieldBits != 32 && fieldBits != 64)
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "field '%s' has unsupported float bit length %lld",
                                               field.name.c_str(),
                                               static_cast<long long>(fieldBits));
            }
            kind = RuntimeFieldKind::Float;
            break;
        case SemanticScalarCategory::Composite:
            if (!field.resolvedType.compositeType)
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "field '%s' is composite but missing target type metadata",
                                               field.name.c_str());
            }
            if (fieldBits <= 0)
            {
                fieldBits = field.resolvedType.compositeExtentBits;
            }
            if (fieldBits <= 0)
            {
                fieldBits = field.resolvedType.bitLengthSet.max();
            }
            kind = RuntimeFieldKind::Composite;
            break;
        case SemanticScalarCategory::Void:
            if (arrayKind != RuntimeArrayKind::None)
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "padding field '%s' cannot be an array",
                                               field.name.c_str());
            }
            kind = RuntimeFieldKind::Padding;
            break;
        }

        if (kind == RuntimeFieldKind::Composite || kind == RuntimeFieldKind::Padding)
        {
            if (fieldBits < 0)
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "field '%s' has invalid bit-length metadata",
                                               field.name.c_str());
            }
        }
        else
        {
            if (fieldBits <= 0 || fieldBits > 64)
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "field '%s' has unsupported scalar bit length %lld",
                                               field.name.c_str(),
                                               static_cast<long long>(fieldBits));
            }
        }

        if (plan.isUnion && kind == RuntimeFieldKind::Padding)
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "union section cannot contain padding runtime fields");
        }

        RuntimeFieldPlan fieldPlan;
        fieldPlan.semanticFieldName = field.name;
        fieldPlan.fieldName         = field.name;
        fieldPlan.kind              = kind;
        fieldPlan.castMode          = field.resolvedType.castMode;
        fieldPlan.bitLength         = fieldBits;
        fieldPlan.alignmentBits =
            std::max<std::int64_t>(1, static_cast<std::int64_t>(field.resolvedType.alignmentBits));
        fieldPlan.useBigInt =
            (kind == RuntimeFieldKind::Unsigned || kind == RuntimeFieldKind::Signed) && (fieldBits > 53);
        fieldPlan.compositeType           = field.resolvedType.compositeType;
        fieldPlan.compositeSealed         = field.resolvedType.compositeSealed;
        fieldPlan.compositePayloadMaxBits = field.resolvedType.bitLengthSet.max();
        fieldPlan.unionOptionIndex        = field.unionOptionIndex;
        fieldPlan.arrayKind               = arrayKind;
        fieldPlan.arrayCapacity           = arrayCapacity;
        fieldPlan.arrayLengthPrefixBits   = arrayLengthPrefixBits;
        if (fieldPlan.compositePayloadMaxBits < 0)
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "composite field '%s' has invalid payload max bits",
                                           field.name.c_str());
        }
        plan.fields.push_back(fieldPlan);

        if (plan.isUnion)
        {
            const auto tagBits = static_cast<std::int64_t>(field.unionTagBits);
            if (tagBits <= 0 || tagBits > 53)
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "union field '%s' has invalid union-tag width %lld",
                                               field.name.c_str(),
                                               static_cast<long long>(tagBits));
            }
            if (unionTagBits && *unionTagBits != tagBits)
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "union section has inconsistent union-tag widths");
            }
            unionTagBits = tagBits;
            unionOptionIndexes.insert(field.unionOptionIndex);
        }

        if (arrayKind == RuntimeArrayKind::None)
        {
            maxBits += fieldBits;
        }
        else if (arrayKind == RuntimeArrayKind::Fixed)
        {
            maxBits += fieldBits * arrayCapacity;
        }
        else
        {
            maxBits += arrayLengthPrefixBits + (fieldBits * arrayCapacity);
        }
    }

    if (plan.isUnion)
    {
        if (plan.fields.empty() || !unionTagBits || unionOptionIndexes.size() != plan.fields.size())
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "union runtime plan metadata is incomplete or inconsistent");
        }
        std::ranges::sort(plan.fields, [](const RuntimeFieldPlan& lhs, const RuntimeFieldPlan& rhs) {
            return lhs.unionOptionIndex < rhs.unionOptionIndex;
        });
        plan.unionTagBits          = *unionTagBits;
        std::int64_t maxOptionBits = 0;
        for (const auto& fieldPlan : plan.fields)
        {
            std::int64_t optionBits = fieldPlan.bitLength;
            if (fieldPlan.arrayKind == RuntimeArrayKind::Fixed)
            {
                optionBits = fieldPlan.bitLength * fieldPlan.arrayCapacity;
            }
            else if (fieldPlan.arrayKind == RuntimeArrayKind::Variable)
            {
                optionBits = fieldPlan.arrayLengthPrefixBits + (fieldPlan.bitLength * fieldPlan.arrayCapacity);
            }
            maxOptionBits = std::max(maxOptionBits, optionBits);
        }
        const auto fallbackMaxBits = plan.unionTagBits + maxOptionBits;
        plan.maxBits               = std::max(section.maxBitLength, fallbackMaxBits);
    }
    else
    {
        plan.maxBits = std::max(section.maxBitLength, maxBits);
    }
    if (auto contractErr = validateRuntimeSectionPlanContract(plan, "runtime-lowered-plan"))
    {
        return std::move(contractErr);
    }
    return plan;
}

llvm::Error validateRuntimeSectionPlanContract(const RuntimeSectionPlan& plan, const llvm::StringRef consumerLabel)
{
    if (isSupportedWireOperationContractVersion(plan.contractVersion))
    {
        return llvm::Error::success();
    }
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "unsupported wire-operation contract major version for %s: %s",
                                   consumerLabel.str().c_str(),
                                   wireOperationUnsupportedMajorVersionDiagnosticDetail(plan.contractVersion).c_str());
}

}  // namespace llvmdsdl
