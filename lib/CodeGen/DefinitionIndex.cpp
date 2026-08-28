//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements shared semantic-definition lookup index for code generation.
///
/// This component centralizes keyed definition lookups used by emitter contexts.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/DefinitionIndex.h"

#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/Semantics/Model.h"

namespace llvmdsdl
{

DefinitionIndex::DefinitionIndex(const SemanticModule& semantic)
{
    for (const auto& def : semantic.definitions)
    {
        byKey_.emplace(loweredTypeKey(def.info.fullName, def.info.majorVersion, def.info.minorVersion), &def);
    }
}

const SemanticDefinition* DefinitionIndex::find(const SemanticTypeRef& ref) const
{
    const auto it = byKey_.find(loweredTypeKey(ref.fullName, ref.majorVersion, ref.minorVersion));
    if (it == byKey_.end())
    {
        return nullptr;
    }
    return it->second;
}

}  // namespace llvmdsdl
