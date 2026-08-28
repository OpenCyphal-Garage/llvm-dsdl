//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements shared lowered-step traversal helpers for native emitters.
///
/// The traversal wraps generic lowered render-step dispatch and adds alignment
/// callback sequencing for field and padding steps.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/NativeEmitterTraversal.h"

#include "llvmdsdl/CodeGen/LoweredRenderIR.h"
#include "llvmdsdl/CodeGen/SerDesStatementPlan.h"
#include "llvmdsdl/Semantics/Model.h"

namespace llvmdsdl
{

void forEachNativeEmitterRenderStep(const LoweredBodyRenderIR&             renderIR,
                                    const NativeEmitterTraversalCallbacks& callbacks)
{
    LoweredRenderStepCallbacks loweredCallbacks;
    loweredCallbacks.onUnionDispatch = callbacks.onUnionDispatch;
    loweredCallbacks.onField         = [&callbacks](const PlannedFieldStep& step) {
        if (step.field == nullptr)
        {
            return;
        }

        if (callbacks.onFieldAlignment)
        {
            callbacks.onFieldAlignment(step.field->resolvedType.alignmentBits);
        }
        if (callbacks.onField)
        {
            callbacks.onField(step);
        }
    };
    loweredCallbacks.onPadding = [&callbacks](const PlannedFieldStep& step) {
        if (step.field == nullptr)
        {
            return;
        }

        if (callbacks.onPaddingAlignment)
        {
            callbacks.onPaddingAlignment(step.field->resolvedType.alignmentBits);
        }
        if (callbacks.onPadding)
        {
            callbacks.onPadding(step);
        }
    };
    forEachLoweredRenderStep(renderIR, loweredCallbacks);
}

}  // namespace llvmdsdl
