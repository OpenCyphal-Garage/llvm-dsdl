//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements the shared per-section identifier scopes.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/SectionNaming.h"

namespace llvmdsdl
{

NamingScope makeSectionFieldScope(const CodegenNamingLanguage language, const SemanticSection& section)
{
    NamingScope scope(language);
    for (const auto& field : section.fields)
    {
        if (!field.isPadding)
        {
            (void) scope.declare(IdentifierRole::FieldName, field.name);
        }
    }
    return scope;
}

NamingScope makeSectionConstantScope(const CodegenNamingLanguage language, const SemanticSection& section)
{
    NamingScope scope(language);
    for (const auto& constant : section.constants)
    {
        (void) scope.declare(IdentifierRole::ConstantName, constant.name);
    }
    return scope;
}

}  // namespace llvmdsdl
