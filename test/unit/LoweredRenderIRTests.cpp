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
#include <functional>
#include <optional>
#include <unordered_map>

#include "llvmdsdl/CodeGen/LoweredRenderIR.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"
#include "llvmdsdl/CodeGen/SerDesHelperDescriptors.h"
#include "llvmdsdl/CodeGen/SerDesStatementPlan.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include "UnitTests.h"

bool runLoweredRenderIRTests()
{
    {
        llvmdsdl::SemanticSection section;
        section.serializationBufferSizeBits = 32;

        llvmdsdl::SemanticField scalar;
        scalar.name                        = "scalar";
        scalar.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        scalar.resolvedType.bitLength      = 8;
        section.fields.push_back(scalar);

        llvmdsdl::SemanticField pad;
        pad.name                        = "_pad0";
        pad.isPadding                   = true;
        pad.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::Void;
        pad.resolvedType.bitLength      = 1;
        section.fields.push_back(pad);

        llvmdsdl::LoweredSectionFacts facts;
        facts.capacityCheckHelper              = "cap";
        facts.fieldsByName["scalar"].stepIndex = 5;
        facts.fieldsByName["_pad0"].stepIndex  = 10;

        const auto renderIR =
            llvmdsdl::buildLoweredBodyRenderIR(section, &facts, llvmdsdl::HelperBindingDirection::Serialize);

        if (!renderIR.helperBindings.capacityCheck || renderIR.helperBindings.capacityCheck->symbol != "cap")
        {
            std::cerr << "lowered render ir missing capacity helper\n";
            return false;
        }
        if (renderIR.contractVersion != llvmdsdl::kWireOperationContractVersion)
        {
            std::cerr << "lowered render ir contract-version mismatch\n";
            return false;
        }
        if (renderIR.steps.size() != 2)
        {
            std::cerr << "lowered render ir non-union step count mismatch\n";
            return false;
        }
        if (renderIR.steps[0].kind != llvmdsdl::LoweredRenderStepKind::Field ||
            renderIR.steps[0].fieldStep.field == nullptr || renderIR.steps[0].fieldStep.field->name != "scalar")
        {
            std::cerr << "lowered render ir field step mismatch\n";
            return false;
        }
        if (renderIR.steps[1].kind != llvmdsdl::LoweredRenderStepKind::Padding ||
            renderIR.steps[1].fieldStep.field == nullptr || !renderIR.steps[1].fieldStep.field->isPadding)
        {
            std::cerr << "lowered render ir padding step mismatch\n";
            return false;
        }

        std::vector<std::string>             visited;
        llvmdsdl::LoweredRenderStepCallbacks callbacks;
        callbacks.onField = [&visited](const llvmdsdl::PlannedFieldStep& step) {
            visited.push_back("field:" + step.field->name);
        };
        callbacks.onPadding = [&visited](const llvmdsdl::PlannedFieldStep& step) {
            visited.push_back("padding:" + step.field->name);
        };
        callbacks.onUnionDispatch = [&visited](const std::vector<llvmdsdl::PlannedFieldStep>&) {
            visited.emplace_back("union");
        };
        llvmdsdl::forEachLoweredRenderStep(renderIR, callbacks);
        if (visited.size() != 2U || visited[0] != "field:scalar" || visited[1] != "padding:_pad0")
        {
            std::cerr << "lowered render step traversal mismatch for non-union plan\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticSection section;
        section.isUnion                     = true;
        section.serializationBufferSizeBits = 16;

        llvmdsdl::SemanticField alpha;
        alpha.name                        = "alpha";
        alpha.unionOptionIndex            = 2;
        alpha.unionTagBits                = 2;
        alpha.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        alpha.resolvedType.bitLength      = 8;
        section.fields.push_back(alpha);

        llvmdsdl::SemanticField beta;
        beta.name                        = "beta";
        beta.unionOptionIndex            = 0;
        beta.unionTagBits                = 2;
        beta.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        beta.resolvedType.bitLength      = 8;
        section.fields.push_back(beta);

        llvmdsdl::LoweredSectionFacts facts;
        facts.fieldsByName["alpha"].stepIndex = 20;
        facts.fieldsByName["beta"].stepIndex  = 10;
        facts.deserUnionTagHelper             = "tag_des";

        const auto renderIR =
            llvmdsdl::buildLoweredBodyRenderIR(section, &facts, llvmdsdl::HelperBindingDirection::Deserialize);

        if (renderIR.steps.size() != 1 || renderIR.steps.front().kind != llvmdsdl::LoweredRenderStepKind::UnionDispatch)
        {
            std::cerr << "lowered render ir union dispatch step mismatch\n";
            return false;
        }
        if (renderIR.steps.front().unionBranches.size() != 2 ||
            renderIR.steps.front().unionBranches[0].field == nullptr ||
            renderIR.steps.front().unionBranches[0].field->name != "beta" ||
            renderIR.steps.front().unionBranches[1].field == nullptr ||
            renderIR.steps.front().unionBranches[1].field->name != "alpha")
        {
            std::cerr << "lowered render ir union branch ordering mismatch\n";
            return false;
        }
        if (!renderIR.helperBindings.unionTagMask || renderIR.helperBindings.unionTagMask->symbol != "tag_des")
        {
            std::cerr << "lowered render ir union helper mismatch\n";
            return false;
        }

        std::vector<std::string>             visited;
        llvmdsdl::LoweredRenderStepCallbacks callbacks;
        callbacks.onUnionDispatch = [&visited](const std::vector<llvmdsdl::PlannedFieldStep>& branches) {
            visited.emplace_back("union");
            for (const auto& branch : branches)
            {
                visited.push_back("branch:" + branch.field->name);
            }
        };
        callbacks.onField   = [&visited](const llvmdsdl::PlannedFieldStep&) { visited.emplace_back("field"); };
        callbacks.onPadding = [&visited](const llvmdsdl::PlannedFieldStep&) { visited.emplace_back("padding"); };
        llvmdsdl::forEachLoweredRenderStep(renderIR, callbacks);
        if (visited.size() != 3U || visited[0] != "union" || visited[1] != "branch:beta" ||
            visited[2] != "branch:alpha")
        {
            std::cerr << "lowered render step traversal mismatch for union plan\n";
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

        std::size_t                          fieldCalls   = 0U;
        std::size_t                          paddingCalls = 0U;
        llvmdsdl::LoweredRenderStepCallbacks callbacks;
        callbacks.onField   = [&fieldCalls](const llvmdsdl::PlannedFieldStep&) { ++fieldCalls; };
        callbacks.onPadding = [&paddingCalls](const llvmdsdl::PlannedFieldStep&) { ++paddingCalls; };
        llvmdsdl::forEachLoweredRenderStep(renderIR, callbacks);
        if (fieldCalls != 0U || paddingCalls != 0U)
        {
            std::cerr << "lowered render traversal should skip null field/padding steps\n";
            return false;
        }
    }

    {
        llvmdsdl::LoweredBodyRenderIR invalidRenderIR;
        invalidRenderIR.contractVersion = llvmdsdl::kWireOperationContractVersion + 1;
        auto err = llvmdsdl::validateLoweredBodyRenderIRContract(invalidRenderIR, "lowered-render-ir-tests");
        if (!err)
        {
            std::cerr << "lowered render ir should reject unsupported wire-operation major version\n";
            return false;
        }
        const std::string errText = llvm::toString(std::move(err));
        if (!llvm::StringRef(errText).contains("unsupported wire-operation contract major version"))
        {
            std::cerr << "unexpected lowered render ir contract error text: " << errText << "\n";
            return false;
        }
    }

    return true;
}
