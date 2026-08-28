//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements shared native-emitter function skeleton orchestration.
///
/// This utility sequences helper binding emission, helper-contract validation,
/// contract preflight, lowered traversal dispatch, and epilogue emission for
/// native backends.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/NativeFunctionSkeleton.h"

#include "llvmdsdl/CodeGen/LoweredRenderIR.h"
#include "llvmdsdl/CodeGen/NativeEmitterTraversal.h"
#include "llvmdsdl/CodeGen/NativeHelperContract.h"
#include "llvm/Support/Error.h"
#include <string>
#include <utility>

namespace llvmdsdl
{

bool emitNativeFunctionSkeleton(const SemanticSection&                 section,
                                const LoweredSectionFacts* const       sectionFacts,
                                const HelperBindingDirection           direction,
                                const NativeFunctionSkeletonCallbacks& callbacks)
{
    const auto renderIR = buildLoweredBodyRenderIR(section, sectionFacts, direction);
    if (auto contractErr = validateLoweredBodyRenderIRContract(renderIR, "native-function-skeleton"))
    {
        if (callbacks.emitMissingHelperContract)
        {
            callbacks.emitMissingHelperContract(llvm::toString(std::move(contractErr)));
        }
        return false;
    }
    if (callbacks.emitHelperBindings)
    {
        callbacks.emitHelperBindings(renderIR.helperBindings);
    }

    std::string missingHelperRequirement;
    if (!validateNativeSectionHelperContract(section,
                                             sectionFacts,
                                             direction,
                                             renderIR.helperBindings,
                                             &missingHelperRequirement))
    {
        if (callbacks.emitMissingHelperContract)
        {
            callbacks.emitMissingHelperContract(missingHelperRequirement);
        }
        return false;
    }

    if (callbacks.emitContractPreflight)
    {
        callbacks.emitContractPreflight(renderIR.helperBindings);
    }
    if (callbacks.makeTraversalCallbacks)
    {
        const auto traversalCallbacks = callbacks.makeTraversalCallbacks(renderIR);
        forEachNativeEmitterRenderStep(renderIR, traversalCallbacks);
    }
    if (callbacks.emitEpilogue)
    {
        callbacks.emitEpilogue();
    }
    return true;
}

}  // namespace llvmdsdl
