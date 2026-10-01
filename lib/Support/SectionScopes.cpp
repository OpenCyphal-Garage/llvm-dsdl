//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements the naming scopes of one section's attributes.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Support/SectionScopes.h"

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringRef.h>

#include "llvmdsdl/Support/GeneratedFact.h"
#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/NamingPolicy.h"

namespace llvmdsdl
{

namespace
{

/// @brief True when @p language declares a section's fields and constants into one region.
///
/// That is where the output declares a type's constants in the type's own scope and the language
/// keeps no class of names for the fields. C++ does both. Rust declares its constants in the
/// type's scope as associated items, which are apart from the fields; the others declare them
/// outside the type, and C emits them as macros carrying the type name as a prefix.
bool constantsShareTheFieldScope(const Language language)
{
    const LanguageTraits& traits = languageTraits(language);
    return (traits.classification.typeConstants == ConstantsScope::Type) &&
           !traits.classification.nameClasses.fieldsApart;
}

/// @brief The names a scope of a type's members claims in @p language: the type's own, where the
///        language puts it among its members' names.
std::vector<llvm::StringRef> typeMemberClaims(const Language language, const llvm::StringRef declaredTypeName)
{
    if (languageTraits(language).classification.nameClasses.typeNameAmongMembers)
    {
        return {declaredTypeName};
    }
    return {};
}

/// @brief Declares @p section's non-padding fields into @p scope, in DSDL order.
void declareFields(NamingScope& scope, const SectionParts& section)
{
    for (const auto& field : section.fields)
    {
        if (!field.padding)
        {
            (void) scope.declare(IdentifierRole::FieldName, field.name);
        }
    }
}

/// @brief Declares @p section's constants into @p scope, in DSDL order.
void declareConstants(NamingScope& scope, const SectionParts& section)
{
    for (const auto& constant : section.constants)
    {
        (void) scope.declare(IdentifierRole::ConstantName, constant);
    }
}

/// @brief True when @p language emits the per-array-field metadata constants.
///
/// A language that exposes an array's capacity through the container it is declared as has no such
/// constant and nothing to allocate a name for.
bool emitsArrayMetadata(const Language language)
{
    return languageTraits(language).composition.arrayMetadataConstants;
}

/// @brief Declares @p section's array-metadata constants into @p scope, in DSDL field order.
void declareArrayMetadata(NamingScope& scope, const SectionParts& section, const Language language)
{
    for (const auto& field : section.fields)
    {
        if (field.padding || !field.array)
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
void declareUnionOptionTags(NamingScope& scope, const SectionParts& section, const Language language)
{
    if (!section.isUnion)
    {
        return;
    }
    for (const auto& field : section.fields)
    {
        if (!field.padding)
        {
            (void) scope.declare(IdentifierRole::MacroName, unionOptionTagName(language, field.name));
        }
    }
}

/// @brief Declares everything @p language puts in one region with @p section's constants.
///
/// The order is what decides which name moves when two collide, and it runs from least to most
/// willing to move. Fields are first because a field's identifier is the ABI a caller writes against
/// and has to be predictable from the DSDL alone; the generated array metadata and union option tags
/// are next; DSDL constants are last; of the four, only they can be renamed without changing the
/// wire format or breaking a field access.
void declareConstantRegion(NamingScope& scope, const SectionParts& section, const Language language)
{
    if (emitsArrayMetadata(language))
    {
        declareArrayMetadata(scope, section, language);
    }
    declareUnionOptionTags(scope, section, language);
    declareConstants(scope, section);
}

}  // namespace

/// @brief Names one of a definition's package-level constants from @p parts.
///
/// A Go constant is exported and CamelCase, and the package holds a whole DSDL namespace, so the
/// name carries the type it belongs to: `RecordFullName`, `RecordFooBarOptionTag`.
///
/// Each part is projected on its own and the results are joined, because the case of a part is
/// what says how to read it. `FULL_NAME` has no lower case, so it is a screaming-snake token of
/// two words and means `FullName`; `VSLAMPoseUpdate` has its own capitals and they are the
/// author's. Joining first would put the type name's lower case into the token's and leave
/// `FULL_NAME` standing as it was written.
/// @param[in] parts The name's parts, outermost first.
/// @return The constant's name.
std::string goConstantName(const std::vector<llvm::StringRef>& parts)
{
    std::string out;
    for (const llvm::StringRef part : parts)
    {
        // The first part is the type's own name, which the caller has already projected. Taking it
        // as it stands is what keeps a constant spelled like the type it belongs to: under
        // versioned names that is `SystemHealth_1_0`, and folding its separators away would leave
        // `SystemHealth10`, where 1.23 and 12.3 of one type reach one name.
        if (out.empty())
        {
            out = part.str();
            continue;
        }
        std::string projected = codegenProjectIdentifier(Language::Go, IdentifierRole::ConstantName, part);
        // A part that begins with a digit is escaped with a leading underscore, which is for the
        // start of an identifier. Anywhere else the digit already follows a letter.
        if (!projected.empty() && (projected.front() == '_'))
        {
            projected.erase(projected.begin());
        }
        out += projected;
    }
    return out;
}

/// @brief What distinguishes one of a section's constants from its siblings.
///
/// The parts as DSDL wrote them, which is what a scope keys on: `barBaz` and `bar_baz` are two
/// constants and `CBarBaz` is one name, so keying on the name would lose the collision the scope
/// exists to repair.
/// @param[in] parts The name's parts, outermost first.
/// @return The key.
std::string goConstantKey(const std::vector<llvm::StringRef>& parts)
{
    std::string out;
    for (const llvm::StringRef part : parts)
    {
        out += part.str() + "\x1f";
    }
    return out;
}

/// @brief What distinguishes one of the generated constants from a DSDL one of the same name.
///
/// A definition may declare a constant named `FULL_NAME`, which is the case the claim exists for,
/// and the two are not one name the scope should answer twice.
/// @param[in] typeName The section's Go type name.
/// @param[in] token What this constant says about the type.
/// @return The key.
std::string goGeneratedConstantKey(const llvm::StringRef typeName, const llvm::StringRef token)
{
    return "\x1egenerated\x1e" + goConstantKey({typeName, token});
}

/// @brief The scope a section's package-level constants are declared into.
///
/// The generated names are declared before any DSDL one, so a DSDL constant that folds onto one of
/// them is the side that moves.
/// @param[in] section The section whose constants these are.
/// @param[in] typeName The section's Go type name.
/// @return The scope.
NamingScope makeGoConstantScope(const SectionParts& section, const llvm::StringRef typeName)
{
    NamingScope scope(Language::Go);
    const auto  claim = [&scope](const std::vector<llvm::StringRef>& parts) {
        (void) scope.declare(IdentifierRole::ConstantName, goConstantKey(parts), goConstantName(parts));
    };
    for (const llvm::StringRef token : codegenGeneratedConstantTokens())
    {
        (void) scope.declare(IdentifierRole::ConstantName,
                             goGeneratedConstantKey(typeName, token),
                             goConstantName({typeName, token}));
    }
    for (const auto& field : section.fields)
    {
        if (!field.padding)
        {
            claim({typeName, field.name, "OPTION_TAG"});
        }
    }
    for (const auto& c : section.constants)
    {
        claim({typeName, c});
    }
    return scope;
}

std::string arrayMetadataName(const Language language, const llvm::StringRef fieldName, const ArrayMetadataKind kind)
{
    return fieldName.str() + ((kind == ArrayMetadataKind::Capacity) ? "_ARRAY_CAPACITY" : "_ARRAY_IS_VARIABLE_LENGTH") +
           languageTraits(language).composition.generatedConstantSuffix.str();
}

std::string unionOptionTagName(const Language language, const llvm::StringRef fieldName)
{
    return fieldName.str() + "_OPTION_TAG" + languageTraits(language).composition.generatedConstantSuffix.str();
}

NamingScope makeSectionFieldScope(const Language        language,
                                  const SectionParts&   section,
                                  const llvm::StringRef declaredTypeName)
{
    NamingScope scope(language, {}, typeMemberClaims(language, declaredTypeName));
    declareFields(scope, section);
    if (constantsShareTheFieldScope(language))
    {
        declareConstantRegion(scope, section, language);
    }
    return scope;
}

std::vector<std::pair<std::string, std::string>> poolClassConstantNames(const Language      language,
                                                                        const SectionParts& section)
{
    std::vector<std::pair<std::string, std::string>> out;
    const auto* const pool = llvm::find_if(generatedTypeMembers(language), [](const GeneratedName& name) {
        return name.fact == GeneratedFact::PoolClass;
    });
    if (pool == generatedTypeMembers(language).end())
    {
        return out;
    }
    std::set<std::string> used;
    for (const FieldParts& field : section.fields)
    {
        if (field.padding || !field.variableLength)
        {
            continue;
        }
        const std::string base =
            pool->name.str() + codegenProjectIdentifier(language, IdentifierRole::ConstantName, field.name);
        std::string name = base;
        for (std::uint32_t suffix = 1U; !used.insert(name).second; ++suffix)
        {
            name = base + "_" + std::to_string(suffix);
        }
        out.emplace_back(field.name, name);
    }
    return out;
}

NamingScope makeSectionConstantScope(const Language        language,
                                     const SectionParts&   section,
                                     const llvm::StringRef declaredTypeName)
{
    if (constantsShareTheFieldScope(language))
    {
        return makeSectionFieldScope(language, section, declaredTypeName);
    }
    const bool  inType = languageTraits(language).classification.typeConstants == ConstantsScope::Type;
    NamingScope scope(language,
                      {},
                      inType ? typeMemberClaims(language, declaredTypeName) : std::vector<llvm::StringRef>{});
    declareConstantRegion(scope, section, language);
    return scope;
}

}  // namespace llvmdsdl
