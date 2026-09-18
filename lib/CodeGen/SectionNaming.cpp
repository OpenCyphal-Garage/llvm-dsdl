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

#include <array>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
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

/// @brief Declares @p section's union option tag constants into @p scope, in tag order.
void declareUnionOptionTags(NamingScope& scope, const SemanticSection& section, const CodegenNamingLanguage language)
{
    if (!section.isUnion)
    {
        return;
    }
    for (const auto& field : section.fields)
    {
        if (!field.isPadding)
        {
            (void) scope.declare(IdentifierRole::MacroName, unionOptionTagName(language, field.name));
        }
    }
}

/// @brief The names a Python or TypeScript module declares for the definition itself.
///
/// They sit in the module's scope, which is also where a section's constants are declared, so a
/// type whose constant prefix is `DSDL` or `LLVMDSDL` can reach them.
llvm::ArrayRef<llvm::StringRef> moduleMetadataNames(const CodegenNamingLanguage language)
{
    static constexpr std::array<llvm::StringRef, 19> kNames = {"LLVMDSDL_GENERATOR_VERSION",
                                                               "DSDL_FULL_NAME",
                                                               "DSDL_IS_DEPRECATED",
                                                               "DSDL_VERSION_MAJOR",
                                                               "DSDL_VERSION_MINOR",
                                                               "DSDL_HAS_FIXED_PORT_ID",
                                                               "DSDL_FIXED_PORT_ID",
                                                               "DSDL_WIRE_FLAT",
                                                               "DSDL_WIRE_FLAT_REASON",
                                                               "DSDL_HOST_IMAGE",
                                                               "DSDL_HOST_IMAGE_REASON",
                                                               "DSDL_REQUEST_WIRE_FLAT",
                                                               "DSDL_REQUEST_WIRE_FLAT_REASON",
                                                               "DSDL_REQUEST_HOST_IMAGE",
                                                               "DSDL_REQUEST_HOST_IMAGE_REASON",
                                                               "DSDL_RESPONSE_WIRE_FLAT",
                                                               "DSDL_RESPONSE_WIRE_FLAT_REASON",
                                                               "DSDL_RESPONSE_HOST_IMAGE",
                                                               "DSDL_RESPONSE_HOST_IMAGE_REASON"};
    static constexpr std::array<llvm::StringRef, 0>  kNone  = {};
    const bool                                       moduleScoped =
        (language == CodegenNamingLanguage::Python) || (language == CodegenNamingLanguage::TypeScript);
    return moduleScoped ? llvm::ArrayRef<llvm::StringRef>(kNames) : llvm::ArrayRef<llvm::StringRef>(kNone);
}

/// @brief The module names @p typeConstantPrefix puts a section constant in reach of.
///
/// A module name is reachable only when the type's constant prefix is the whole of its first
/// component: for `DSDL.1.0` the prefix is `DSDL`, so `DSDL_FULL_NAME` is `FULL_NAME` behind that
/// prefix and a DSDL constant of that name reaches it. For every other type nothing here is
/// reachable and nothing is reserved, which keeps this from renaming constants on types that were
/// never at risk.
///
/// These are reserved rather than declared: the scope treats a second `declare` of one source name
/// as the same entry, which is right for a caller that walks a section twice and wrong here, where
/// the module owns the name and the DSDL constant is the one that has to move.
std::vector<std::string> reachableModuleMetadata(const CodegenNamingLanguage language,
                                                 const llvm::StringRef       typeConstantPrefix)
{
    std::vector<std::string> out;
    if (typeConstantPrefix.empty())
    {
        return out;
    }
    for (const llvm::StringRef name : moduleMetadataNames(language))
    {
        if (name.starts_with(typeConstantPrefix) && (name.size() > typeConstantPrefix.size()) &&
            (name[typeConstantPrefix.size()] == '_'))
        {
            out.push_back(name.substr(typeConstantPrefix.size() + 1).str());
        }
    }
    return out;
}

/// @brief Declares everything @p language puts in one region with @p section's constants.
///
/// The order is what decides which name moves when two collide, and it runs from least to most
/// willing to move. Fields are first because a field's identifier is the ABI a caller writes against
/// and has to be predictable from the DSDL alone; the generated array metadata and union option tags
/// are next; DSDL constants are last; of the four, only they can be renamed without changing the
/// wire format or breaking a field access. The module's own names sit outside this order: they are
/// reserved before the scope is opened, so nothing declared here can take one.
void declareConstantRegion(NamingScope& scope, const SemanticSection& section, const CodegenNamingLanguage language)
{
    if (emitsArrayMetadata(language))
    {
        declareArrayMetadata(scope, section, language);
    }
    declareUnionOptionTags(scope, section, language);
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

std::string unionOptionTagName(const CodegenNamingLanguage language, const llvm::StringRef fieldName)
{
    return fieldName.str() + "_OPTION_TAG" + ((language == CodegenNamingLanguage::C) ? "_" : "");
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

NamingScope makeSectionConstantScope(const CodegenNamingLanguage language,
                                     const SemanticSection&      section,
                                     const llvm::StringRef       typeConstantPrefix)
{
    if (constantsShareTheFieldScope(language))
    {
        return makeSectionFieldScope(language, section);
    }
    // The module's own names are reserved before anything is declared, so a DSDL constant that
    // reaches one is escaped past it rather than redefining it.
    const std::vector<std::string>     reserved = reachableModuleMetadata(language, typeConstantPrefix);
    const std::vector<llvm::StringRef> reservedRefs(reserved.begin(), reserved.end());
    NamingScope                        scope(language, reservedRefs);
    declareConstantRegion(scope, section, language);
    return scope;
}

}  // namespace llvmdsdl
