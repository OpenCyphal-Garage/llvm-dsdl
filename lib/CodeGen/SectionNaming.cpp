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

namespace
{

/// @brief True when @p language declares a section's fields and constants into one region.
///
/// C++ puts both in the struct body. Go, Rust, TypeScript and Python declare constants outside the
/// type, and C emits them as macros carrying the type name as a prefix, so in those five a field and
/// a constant that project onto one identifier are two different identifiers.
bool constantsShareTheFieldScope(const CodegenNamingLanguage language)
{
    return language == CodegenNamingLanguage::Cpp;
}

/// @brief Declares @p section's non-padding fields into @p scope, in DSDL order.
void declareFields(NamingScope& scope, const SemanticSection& section)
{
    for (const auto& field : section.fields)
    {
        if (!field.isPadding)
        {
            (void) scope.declare(IdentifierRole::FieldName, field.name);
        }
    }
}

/// @brief Declares @p section's constants into @p scope, in DSDL order.
void declareConstants(NamingScope& scope, const SemanticSection& section)
{
    for (const auto& constant : section.constants)
    {
        (void) scope.declare(IdentifierRole::ConstantName, constant.name);
    }
}

}  // namespace

NamingScope makeSectionFieldScope(const CodegenNamingLanguage language, const SemanticSection& section)
{
    NamingScope scope(language);
    declareFields(scope, section);
    if (constantsShareTheFieldScope(language))
    {
        declareConstants(scope, section);
    }
    return scope;
}

NamingScope makeSectionConstantScope(const CodegenNamingLanguage language, const SemanticSection& section)
{
    if (constantsShareTheFieldScope(language))
    {
        return makeSectionFieldScope(language, section);
    }
    NamingScope scope(language);
    declareConstants(scope, section);
    return scope;
}

}  // namespace llvmdsdl
