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

}  // namespace llvmdsdl
