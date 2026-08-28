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
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include <string>

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

/// @brief True when @p language emits the per-array-field metadata constants.
///
/// Only C and C++ do. The other four expose an array's capacity through the container it is declared
/// as, so they have no such constant and nothing to allocate a name for.
bool emitsArrayMetadata(const CodegenNamingLanguage language)
{
    return language == CodegenNamingLanguage::C || language == CodegenNamingLanguage::Cpp;
}

/// @brief Declares @p section's array-metadata constants into @p scope, in DSDL field order.
void declareArrayMetadata(NamingScope& scope, const SemanticSection& section, const CodegenNamingLanguage language)
{
    for (const auto& field : section.fields)
    {
        if (field.isPadding || (field.resolvedType.arrayKind == ArrayKind::None))
        {
            continue;
        }
        for (const auto kind : {ArrayMetadataKind::Capacity, ArrayMetadataKind::IsVariableLength})
        {
            (void) scope.declare(IdentifierRole::MacroName, arrayMetadataName(language, field.name, kind));
        }
    }
}

/// @brief Declares everything @p language puts in one region with @p section's constants.
///
/// The order is what decides which name moves when two collide, and it runs from least to most
/// willing to move. Fields are first because a field's identifier is the ABI a caller writes against
/// and has to be predictable from the DSDL alone; the generated array metadata is next; DSDL
/// constants are last, being the only one of the three a author can rename without changing the wire
/// format or breaking a field access.
void declareConstantRegion(NamingScope& scope, const SemanticSection& section, const CodegenNamingLanguage language)
{
    if (emitsArrayMetadata(language))
    {
        declareArrayMetadata(scope, section, language);
    }
    declareConstants(scope, section);
}

}  // namespace

std::string arrayMetadataName(const CodegenNamingLanguage language,
                              const llvm::StringRef       fieldName,
                              const ArrayMetadataKind     kind)
{
    return fieldName.str() + ((kind == ArrayMetadataKind::Capacity) ? "_ARRAY_CAPACITY" : "_ARRAY_IS_VARIABLE_LENGTH") +
           ((language == CodegenNamingLanguage::C) ? "_" : "");
}

NamingScope makeSectionFieldScope(const CodegenNamingLanguage language, const SemanticSection& section)
{
    NamingScope scope(language);
    declareFields(scope, section);
    if (constantsShareTheFieldScope(language))
    {
        declareConstantRegion(scope, section, language);
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
    declareConstantRegion(scope, section, language);
    return scope;
}

}  // namespace llvmdsdl
