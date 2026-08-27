//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Builds render-oriented IR steps from lowered statement plans.
///
/// The render IR normalizes union dispatch and linear field processing so backend emitters can share traversal logic.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/LoweredRenderIR.h"

#include <llvm/ADT/StringRef.h>
#include <utility>

#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"
#include "llvmdsdl/CodeGen/SerDesStatementPlan.h"
#include "llvmdsdl/CodeGen/WireOperationContract.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvm/Support/Error.h"

namespace llvmdsdl
{
struct LoweredSectionFacts;

LoweredBodyRenderIR buildLoweredBodyRenderIR(const SemanticSection&       section,
                                             const LoweredSectionFacts*   sectionFacts,
                                             const HelperBindingDirection direction)
{
    LoweredBodyRenderIR out;
    out.contractVersion      = kWireOperationContractVersion;
    const auto statementPlan = buildSectionStatementPlan(section, sectionFacts);
    out.helperBindings       = buildSectionHelperBindingPlan(section, sectionFacts, direction);

    if (section.isUnion)
    {
        LoweredRenderStep unionStep;
        unionStep.kind          = LoweredRenderStepKind::UnionDispatch;
        unionStep.unionBranches = statementPlan.unionBranches;
        out.steps.push_back(std::move(unionStep));
        return out;
    }

    out.steps.reserve(statementPlan.orderedFields.size());
    for (const auto& fieldStep : statementPlan.orderedFields)
    {
        LoweredRenderStep step;
        if (fieldStep.field && fieldStep.field->isPadding)
        {
            step.kind = LoweredRenderStepKind::Padding;
        }
        else
        {
            step.kind = LoweredRenderStepKind::Field;
        }
        step.fieldStep = fieldStep;
        out.steps.push_back(std::move(step));
    }
    return out;
}

llvm::Error validateLoweredBodyRenderIRContract(const LoweredBodyRenderIR& renderIR,
                                                const llvm::StringRef      consumerLabel)
{
    if (isSupportedWireOperationContractVersion(renderIR.contractVersion))
    {
        return llvm::Error::success();
    }
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "unsupported wire-operation contract major version for %s: %s",
                                   consumerLabel.str().c_str(),
                                   wireOperationUnsupportedMajorVersionDiagnosticDetail(renderIR.contractVersion)
                                       .c_str());
}

void forEachLoweredRenderStep(const LoweredBodyRenderIR& renderIR, const LoweredRenderStepCallbacks& callbacks)
{
    for (const auto& step : renderIR.steps)
    {
        switch (step.kind)
        {
        case LoweredRenderStepKind::UnionDispatch:
            if (callbacks.onUnionDispatch)
            {
                callbacks.onUnionDispatch(step.unionBranches);
            }
            break;
        case LoweredRenderStepKind::Field:
            if (step.fieldStep.field != nullptr && callbacks.onField)
            {
                callbacks.onField(step.fieldStep);
            }
            break;
        case LoweredRenderStepKind::Padding:
            if (step.fieldStep.field != nullptr && callbacks.onPadding)
            {
                callbacks.onPadding(step.fieldStep);
            }
            break;
        }
    }
}

}  // namespace llvmdsdl
