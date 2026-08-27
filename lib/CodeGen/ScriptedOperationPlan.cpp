//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Builds shared scripted operation plans from helper-bound body metadata.
///
/// This component classifies each field into operation categories consumed by
/// TS/Python emitters so orchestration decisions remain centralized.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/ScriptedOperationPlan.h"

#include <cstdint>
#include <llvm/Support/Error.h>
#include <string>
#include <utility>

#include "llvmdsdl/CodeGen/EmitStep.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/CodeGen/RuntimeHelperBindings.h"
#include "llvmdsdl/CodeGen/RuntimeLoweredPlan.h"
#include "llvmdsdl/CodeGen/ScriptedBodyPlan.h"
#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"
#include "llvmdsdl/CodeGen/WireOperationContract.h"
#include "llvmdsdl/Semantics/Model.h"

namespace llvmdsdl
{
namespace
{

const SemanticField* findSemanticFieldByName(const SemanticSection& section, const std::string& fieldName)
{
    for (const auto& field : section.fields)
    {
        if (field.name == fieldName)
        {
            return &field;
        }
    }
    return nullptr;
}

/// @brief A synthesized Pad step for runtime padding entries with no semantic field.
FieldEmitStep synthesizePadStep(const RuntimeFieldPlan& field)
{
    FieldEmitStep step;
    step.kind                = FieldStepKind::Pad;
    step.bits                = field.bitLength;
    step.type.scalarCategory = SemanticScalarCategory::Void;
    step.type.bitLength      = static_cast<std::uint32_t>(field.bitLength);
    return step;
}

ScriptedFieldCardinality classifyFieldCardinality(const RuntimeArrayKind arrayKind)
{
    switch (arrayKind)
    {
    case RuntimeArrayKind::None:
        return ScriptedFieldCardinality::Scalar;
    case RuntimeArrayKind::Fixed:
        return ScriptedFieldCardinality::FixedArray;
    case RuntimeArrayKind::Variable:
        return ScriptedFieldCardinality::VariableArray;
    }
    return ScriptedFieldCardinality::Scalar;
}

ScriptedFieldValueKind classifyFieldValueKind(const RuntimeFieldKind kind)
{
    switch (kind)
    {
    case RuntimeFieldKind::Padding:
        return ScriptedFieldValueKind::Padding;
    case RuntimeFieldKind::Bool:
        return ScriptedFieldValueKind::Bool;
    case RuntimeFieldKind::Unsigned:
        return ScriptedFieldValueKind::Unsigned;
    case RuntimeFieldKind::Signed:
        return ScriptedFieldValueKind::Signed;
    case RuntimeFieldKind::Float:
        return ScriptedFieldValueKind::Float;
    case RuntimeFieldKind::Composite:
        return ScriptedFieldValueKind::Composite;
    }
    return ScriptedFieldValueKind::Padding;
}

}  // namespace

llvm::Expected<ScriptedSectionOperationPlan> buildScriptedSectionOperationPlan(
    const SemanticSection&           section,
    const RuntimeSectionPlan&        runtimePlan,
    const LoweredSectionFacts*       sectionFacts,
    const RuntimeHelperNameResolver& helperNameResolver)
{
    if (auto contractErr = validateRuntimeSectionPlanContract(runtimePlan, "scripted-operation-plan"))
    {
        return std::move(contractErr);
    }

    const auto bodyPlan = buildScriptedSectionBodyPlan(section, runtimePlan, sectionFacts, helperNameResolver);

    ScriptedSectionOperationPlan out;
    out.contractVersion = runtimePlan.contractVersion;
    out.isUnion         = runtimePlan.isUnion;
    out.unionTagBits    = runtimePlan.unionTagBits;
    out.maxBits         = runtimePlan.maxBits;
    out.sectionHelpers  = bodyPlan.sectionHelpers;
    out.fields.reserve(bodyPlan.fields.size());
    for (const auto& bodyField : bodyPlan.fields)
    {
        ScriptedFieldOperationPlan operation;
        operation.body        = bodyField;
        operation.cardinality = classifyFieldCardinality(bodyField.field.arrayKind);
        operation.valueKind   = classifyFieldValueKind(bodyField.field.kind);
        // Attach the shared recursive step trees so scripted emitters render field
        // bodies through renderFieldSteps (single-source sequencing) rather than
        // hand-written per-kind chains.
        if (const auto* const semanticField = findSemanticFieldByName(section, bodyField.field.semanticFieldName))
        {
            const auto* const fieldFacts = findLoweredFieldFacts(sectionFacts, semanticField->name);
            operation.serializeSteps     = buildFieldEmitSteps(semanticField->resolvedType,
                                                               fieldFacts,
                                                               bodyField.arrayPrefixOverride,
                                                               HelperBindingDirection::Serialize);
            operation.deserializeSteps   = buildFieldEmitSteps(semanticField->resolvedType,
                                                               fieldFacts,
                                                               bodyField.arrayPrefixOverride,
                                                               HelperBindingDirection::Deserialize);
        }
        else if (operation.valueKind == ScriptedFieldValueKind::Padding)
        {
            operation.serializeSteps   = synthesizePadStep(bodyField.field);
            operation.deserializeSteps = synthesizePadStep(bodyField.field);
        }
        out.fields.push_back(std::move(operation));
    }
    if (auto contractErr = validateScriptedSectionOperationPlanContract(out, "scripted-operation-plan"))
    {
        return std::move(contractErr);
    }
    return {std::move(out)};
}

llvm::Error validateScriptedSectionOperationPlanContract(const ScriptedSectionOperationPlan& plan,
                                                         const llvm::StringRef               consumerLabel)
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
