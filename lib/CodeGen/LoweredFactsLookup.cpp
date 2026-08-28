//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements shared lookup helpers for lowered MLIR section facts.
///
/// Backends use this component to avoid duplicating lowered-facts lookup logic.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/LoweredFactsLookup.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/Semantics/Model.h"
#include <llvm/ADT/StringRef.h>

namespace llvmdsdl
{

const LoweredSectionFacts* lookupLoweredSectionFacts(const LoweredFactsMap&    loweredFacts,
                                                     const SemanticDefinition& def,
                                                     const llvm::StringRef     sectionKey)
{
    const auto definitionIt =
        loweredFacts.find(loweredTypeKey(def.info.fullName, def.info.majorVersion, def.info.minorVersion));
    if (definitionIt == loweredFacts.end())
    {
        return nullptr;
    }
    const auto sectionIt = definitionIt->second.find(sectionKey.str());
    if (sectionIt == definitionIt->second.end())
    {
        return nullptr;
    }
    return &sectionIt->second;
}

}  // namespace llvmdsdl
