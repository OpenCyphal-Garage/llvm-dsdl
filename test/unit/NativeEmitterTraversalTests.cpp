//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <string>
#include <vector>
#include <cstddef>
#include <optional>
#include <unordered_map>

#include "llvmdsdl/CodeGen/LoweredRenderIR.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/CodeGen/NativeEmitterTraversal.h"
#include "llvmdsdl/Semantics/Model.h"

#include "UnitTests.h"

bool runNativeEmitterTraversalTests()
{
    {
        llvmdsdl::SemanticSection section;

        llvmdsdl::SemanticField scalar;
        scalar.name                        = "scalar";
        scalar.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        scalar.resolvedType.bitLength      = 8;
        scalar.resolvedType.alignmentBits  = 8;
        section.fields.push_back(scalar);

        llvmdsdl::SemanticField padding;
        padding.name                        = "_pad0";
        padding.isPadding                   = true;
        padding.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::Void;
        padding.resolvedType.bitLength      = 3;
        padding.resolvedType.alignmentBits  = 4;
        section.fields.push_back(padding);

        llvmdsdl::LoweredSectionFacts facts;
        facts.fieldsByName["scalar"].stepIndex = 10;
        facts.fieldsByName["_pad0"].stepIndex  = 20;

        const auto renderIR =
            llvmdsdl::buildLoweredBodyRenderIR(section, &facts, llvmdsdl::HelperBindingDirection::Serialize);

        std::vector<std::string>                  events;
        llvmdsdl::NativeEmitterTraversalCallbacks callbacks;
        callbacks.onUnionDispatch = [&events](const std::vector<llvmdsdl::PlannedFieldStep>&) {
            events.emplace_back("union");
        };
        callbacks.onFieldAlignment = [&events](const std::int64_t alignmentBits) {
            events.push_back("field-align:" + std::to_string(alignmentBits));
        };
        callbacks.onField = [&events](const llvmdsdl::PlannedFieldStep& step) {
            events.push_back("field:" + step.field->name);
        };
        callbacks.onPaddingAlignment = [&events](const std::int64_t alignmentBits) {
            events.push_back("padding-align:" + std::to_string(alignmentBits));
        };
        callbacks.onPadding = [&events](const llvmdsdl::PlannedFieldStep& step) {
            events.push_back("padding:" + step.field->name);
        };

        llvmdsdl::forEachNativeEmitterRenderStep(renderIR, callbacks);
        if (events.size() != 4U || events[0] != "field-align:8" || events[1] != "field:scalar" ||
            events[2] != "padding-align:4" || events[3] != "padding:_pad0")
        {
            std::cerr << "native emitter traversal non-union callback sequencing mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticSection section;
        section.isUnion = true;

        llvmdsdl::SemanticField alpha;
        alpha.name                        = "alpha";
        alpha.unionOptionIndex            = 2;
        alpha.unionTagBits                = 2;
        alpha.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        alpha.resolvedType.bitLength      = 8;
        alpha.resolvedType.alignmentBits  = 8;
        section.fields.push_back(alpha);

        llvmdsdl::SemanticField beta;
        beta.name                        = "beta";
        beta.unionOptionIndex            = 0;
        beta.unionTagBits                = 2;
        beta.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        beta.resolvedType.bitLength      = 8;
        beta.resolvedType.alignmentBits  = 8;
        section.fields.push_back(beta);

        llvmdsdl::LoweredSectionFacts facts;
        facts.fieldsByName["alpha"].stepIndex = 20;
        facts.fieldsByName["beta"].stepIndex  = 10;

        const auto renderIR =
            llvmdsdl::buildLoweredBodyRenderIR(section, &facts, llvmdsdl::HelperBindingDirection::Deserialize);

        std::vector<std::string>                  events;
        llvmdsdl::NativeEmitterTraversalCallbacks callbacks;
        callbacks.onUnionDispatch = [&events](const std::vector<llvmdsdl::PlannedFieldStep>& branches) {
            events.emplace_back("union");
            for (const auto& branch : branches)
            {
                events.push_back("branch:" + branch.field->name);
            }
        };
        callbacks.onFieldAlignment = [&events](const std::int64_t alignmentBits) {
            events.push_back("field-align:" + std::to_string(alignmentBits));
        };
        callbacks.onField = [&events](const llvmdsdl::PlannedFieldStep& step) {
            events.push_back("field:" + step.field->name);
        };
        callbacks.onPaddingAlignment = [&events](const std::int64_t alignmentBits) {
            events.push_back("padding-align:" + std::to_string(alignmentBits));
        };
        callbacks.onPadding = [&events](const llvmdsdl::PlannedFieldStep& step) {
            events.push_back("padding:" + step.field->name);
        };

        llvmdsdl::forEachNativeEmitterRenderStep(renderIR, callbacks);
        if (events.size() != 3U || events[0] != "union" || events[1] != "branch:beta" || events[2] != "branch:alpha")
        {
            std::cerr << "native emitter traversal union dispatch mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::LoweredBodyRenderIR renderIR;
        llvmdsdl::LoweredRenderStep   nullFieldStep;
        nullFieldStep.kind = llvmdsdl::LoweredRenderStepKind::Field;
        renderIR.steps.push_back(nullFieldStep);
        llvmdsdl::LoweredRenderStep nullPaddingStep;
        nullPaddingStep.kind = llvmdsdl::LoweredRenderStepKind::Padding;
        renderIR.steps.push_back(nullPaddingStep);

        std::size_t fieldAlignCalls   = 0U;
        std::size_t fieldCalls        = 0U;
        std::size_t paddingAlignCalls = 0U;
        std::size_t paddingCalls      = 0U;

        llvmdsdl::NativeEmitterTraversalCallbacks callbacks;
        callbacks.onFieldAlignment   = [&fieldAlignCalls](const std::int64_t) { ++fieldAlignCalls; };
        callbacks.onField            = [&fieldCalls](const llvmdsdl::PlannedFieldStep&) { ++fieldCalls; };
        callbacks.onPaddingAlignment = [&paddingAlignCalls](const std::int64_t) { ++paddingAlignCalls; };
        callbacks.onPadding          = [&paddingCalls](const llvmdsdl::PlannedFieldStep&) { ++paddingCalls; };

        llvmdsdl::forEachNativeEmitterRenderStep(renderIR, callbacks);
        if (fieldAlignCalls != 0U || fieldCalls != 0U || paddingAlignCalls != 0U || paddingCalls != 0U)
        {
            std::cerr << "native emitter traversal should skip null field/padding steps\n";
            return false;
        }
    }

    return true;
}
