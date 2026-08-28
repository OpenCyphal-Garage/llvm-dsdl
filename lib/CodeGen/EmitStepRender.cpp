//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared union-prologue render template (see EmitStep.h).
///
//===----------------------------------------------------------------------===//
#include "llvmdsdl/CodeGen/EmitStep.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "llvmdsdl/CodeGen/ArrayWirePlan.h"
#include "llvmdsdl/CodeGen/EmitTrace.h"
#include "llvmdsdl/CodeGen/HelperSymbolResolver.h"
#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"
#include "llvmdsdl/CodeGen/TypeStorage.h"
#include "llvmdsdl/Semantics/Model.h"

namespace llvmdsdl
{

std::vector<UnionEmitStep> buildUnionSectionSteps(const EmitTraceDirection direction, const std::size_t caseCount)
{
    std::vector<UnionEmitStep> steps;
    steps.reserve(6U + (3U * caseCount));
    if (direction == EmitTraceDirection::Serialize)
    {
        steps.push_back(UnionEmitStep{UnionStepKind::SerializeValidateTag, -1});
        steps.push_back(UnionEmitStep{UnionStepKind::SerializeWriteMaskedTag, -1});
    }
    else
    {
        steps.push_back(UnionEmitStep{UnionStepKind::DeserializeReadMaskStoreTag, -1});
        steps.push_back(UnionEmitStep{UnionStepKind::DeserializeValidateTag, -1});
    }
    steps.push_back(UnionEmitStep{UnionStepKind::AdvanceTag, -1});
    steps.push_back(UnionEmitStep{UnionStepKind::BeginDispatch, -1});
    for (std::size_t i = 0; i < caseCount; ++i)
    {
        const auto ordinal = static_cast<std::int32_t>(i);
        steps.push_back(UnionEmitStep{UnionStepKind::BeginCase, ordinal});
        steps.push_back(UnionEmitStep{UnionStepKind::CaseBody, ordinal});
        steps.push_back(UnionEmitStep{UnionStepKind::EndCase, ordinal});
    }
    steps.push_back(UnionEmitStep{UnionStepKind::BadTagDefault, -1});
    steps.push_back(UnionEmitStep{UnionStepKind::EndDispatch, -1});
    return steps;
}

void renderUnionSection(const EmitTraceDirection            direction,
                        const std::vector<UnionCaseRender>& cases,
                        UnionSectionSpelling&               spelling)
{
    for (const auto& step : buildUnionSectionSteps(direction, cases.size()))
    {
        switch (step.kind)
        {
        case UnionStepKind::SerializeValidateTag:
            spelling.spellSerializeValidateTag();
            break;
        case UnionStepKind::SerializeWriteMaskedTag:
            spelling.spellSerializeWriteMaskedTag();
            break;
        case UnionStepKind::DeserializeReadMaskStoreTag:
            spelling.spellDeserializeReadMaskStoreTag();
            break;
        case UnionStepKind::DeserializeValidateTag:
            spelling.spellDeserializeValidateTag();
            break;
        case UnionStepKind::AdvanceTag:
            spelling.spellAdvanceTag();
            break;
        case UnionStepKind::BeginDispatch:
            spelling.spellBeginDispatch();
            break;
        case UnionStepKind::BeginCase:
            assert(step.caseOrdinal >= 0);
            spelling.spellBeginCase(cases[static_cast<std::size_t>(step.caseOrdinal)].optionIndex,
                                    step.caseOrdinal == 0);
            break;
        case UnionStepKind::CaseBody:
            assert(step.caseOrdinal >= 0);
            cases[static_cast<std::size_t>(step.caseOrdinal)].renderBody();
            break;
        case UnionStepKind::EndCase:
            spelling.spellEndCase();
            break;
        case UnionStepKind::BadTagDefault:
            spelling.spellBadTagDefault();
            break;
        case UnionStepKind::EndDispatch:
            spelling.spellEndDispatch();
            break;
        }
    }
}

FieldEmitStep buildFieldEmitSteps(const SemanticFieldType&           type,
                                  const LoweredFieldFacts* const     fieldFacts,
                                  const std::optional<std::uint32_t> prefixBitsOverride,
                                  const HelperBindingDirection       direction)
{
    FieldEmitStep step;
    step.type = type;

    if (type.arrayKind != ArrayKind::None)
    {
        const auto arrayPlan = buildArrayWirePlan(type, fieldFacts, prefixBitsOverride, direction);
        step.kind            = arrayPlan.variable ? FieldStepKind::VariableArray : FieldStepKind::FixedArray;
        step.capacity        = type.arrayCapacity;
        step.prefixBits      = arrayPlan.prefixBits;
        step.arrayHelpers    = arrayPlan.descriptor;
        // The element subtree: nesting is shared here, not re-written per backend.
        step.children.push_back(buildFieldEmitSteps(arrayElementType(type), fieldFacts, std::nullopt, direction));
        return step;
    }

    switch (type.scalarCategory)
    {
    case SemanticScalarCategory::Void:
        step.kind = FieldStepKind::Pad;
        step.bits = type.bitLength;
        return step;
    case SemanticScalarCategory::Bool:
        step.kind = FieldStepKind::ScalarBool;
        step.bits = 1;
        return step;
    case SemanticScalarCategory::Byte:
    case SemanticScalarCategory::Utf8:
    case SemanticScalarCategory::UnsignedInt:
        step.kind = FieldStepKind::ScalarUint;
        break;
    case SemanticScalarCategory::SignedInt:
        step.kind = FieldStepKind::ScalarSint;
        break;
    case SemanticScalarCategory::Float:
        step.kind = FieldStepKind::ScalarFloat;
        break;
    case SemanticScalarCategory::Composite:
        step.kind = FieldStepKind::Composite;
        if (!type.compositeSealed)
        {
            step.delimiterValidateSymbol = resolveDelimiterValidateHelperSymbol(type, fieldFacts);
        }
        return step;
    }

    step.bits               = type.bitLength;
    step.scalarHelperSymbol = resolveScalarHelperSymbol(type, fieldFacts, direction);
    return step;
}

void renderFieldSteps(const FieldEmitStep&         step,
                      const std::string&           expr,
                      const HelperBindingDirection direction,
                      FieldStepSpelling&           spelling)
{
    const bool serialize = direction == HelperBindingDirection::Serialize;
    switch (step.kind)
    {
    case FieldStepKind::Pad:
        spelling.spellPad(step);
        return;
    case FieldStepKind::ScalarBool:
    case FieldStepKind::ScalarUint:
    case FieldStepKind::ScalarSint:
    case FieldStepKind::ScalarFloat:
        if (serialize)
        {
            spelling.spellScalarSerialize(step, expr);
        }
        else
        {
            spelling.spellScalarDeserialize(step, expr);
        }
        return;
    case FieldStepKind::Composite:
        if (serialize)
        {
            spelling.spellCompositeSerialize(step, expr);
        }
        else
        {
            spelling.spellCompositeDeserialize(step, expr);
        }
        return;
    case FieldStepKind::FixedArray: {
        assert(step.children.size() == 1U);
        if (serialize)
        {
            spelling.spellFixedArrayLenCheck(step, expr);                   // D2 interface point
            if (spelling.trySpellArrayBulkFastPath(step, expr, direction))  // D3 interface point
            {
                return;
            }
            const auto elemExpr = spelling.spellBeginElemLoopSerialize(step, expr);
            renderFieldSteps(step.children.front(), elemExpr, direction, spelling);
            spelling.spellEndElemLoopSerialize(step, expr);
        }
        else
        {
            const auto countExpr = spelling.spellFixedArrayCountDeserialize(step, expr);
            if (spelling.trySpellArrayBulkFastPath(step, expr, direction))  // D3 interface point
            {
                return;
            }
            const auto elemExpr = spelling.spellBeginElemLoopDeserialize(step, expr, countExpr);
            renderFieldSteps(step.children.front(), elemExpr, direction, spelling);
            spelling.spellEndElemLoopDeserialize(step, expr);
        }
        return;
    }
    case FieldStepKind::VariableArray: {
        assert(step.children.size() == 1U);
        if (serialize)
        {
            spelling.spellVariableArrayLenSerialize(step, expr);
            const auto elemExpr = spelling.spellBeginElemLoopSerialize(step, expr);
            renderFieldSteps(step.children.front(), elemExpr, direction, spelling);
            spelling.spellEndElemLoopSerialize(step, expr);
        }
        else
        {
            const auto countExpr = spelling.spellVariableArrayLenDeserialize(step, expr);
            const auto elemExpr  = spelling.spellBeginElemLoopDeserialize(step, expr, countExpr);
            renderFieldSteps(step.children.front(), elemExpr, direction, spelling);
            spelling.spellEndElemLoopDeserialize(step, expr);
        }
        return;
    }
    }
}

}  // namespace llvmdsdl
