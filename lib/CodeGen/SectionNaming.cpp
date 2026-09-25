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
#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include <string>

namespace llvmdsdl
{

namespace
{

/// @brief True when @p language declares a section's fields and constants into one region.
///
/// That is where the output declares a type's constants in the type's own scope and the language
/// makes those constants and the fields one namespace. C++ does both. Rust declares its constants
/// in the type's scope as associated items, which are apart from the fields; the others declare
/// them outside the type, and C emits them as macros carrying the type name as a prefix.
bool constantsShareTheFieldScope(const Language language)
{
    const LanguageTraits& traits = languageTraits(language);
    return (traits.composition.constants == ConstantsScope::Type) && traits.classification.constantsShareFieldNamespace;
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
/// A language that exposes an array's capacity through the container it is declared as has no such
/// constant and nothing to allocate a name for.
bool emitsArrayMetadata(const Language language)
{
    return languageTraits(language).composition.arrayMetadataConstants;
}

/// @brief Declares @p section's array-metadata constants into @p scope, in DSDL field order.
void declareArrayMetadata(NamingScope& scope, const SemanticSection& section, const Language language)
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
void declareUnionOptionTags(NamingScope& scope, const SemanticSection& section, const Language language)
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

/// @brief The names a module declares for the definition itself, where a section's constants are
///        the module's too.
///
/// They sit in the module's scope, which is also where a section's constants are declared, so a
/// type whose constant prefix is `DSDL` or `LLVMDSDL` can reach them.
llvm::ArrayRef<llvm::StringRef> moduleMetadataNames(const Language language)
{
    static constexpr std::array<llvm::StringRef, 13> kNames = {"LLVMDSDL_GENERATOR_VERSION",
                                                               "DSDL_FULL_NAME",
                                                               "DSDL_IS_DEPRECATED",
                                                               "DSDL_VERSION_MAJOR",
                                                               "DSDL_VERSION_MINOR",
                                                               "DSDL_HAS_FIXED_PORT_ID",
                                                               "DSDL_FIXED_PORT_ID",
                                                               "DSDL_WIRE_FLAT",
                                                               "DSDL_WIRE_FLAT_REASON",
                                                               "DSDL_REQUEST_WIRE_FLAT",
                                                               "DSDL_REQUEST_WIRE_FLAT_REASON",
                                                               "DSDL_RESPONSE_WIRE_FLAT",
                                                               "DSDL_RESPONSE_WIRE_FLAT_REASON"};
    static constexpr std::array<llvm::StringRef, 0>  kNone  = {};
    const bool moduleScoped = languageTraits(language).composition.constants == ConstantsScope::Module;
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
std::vector<std::string> reachableModuleMetadata(const Language language, const llvm::StringRef typeConstantPrefix)
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
            const llvm::StringRef remainder = name.substr(typeConstantPrefix.size() + 1);
            // A name the module writes for one payload of a service is reached through that
            // payload's own prefix, which carries the qualifier: `DSDL_REQUEST` reaches
            // `DSDL_REQUEST_WIRE_FLAT` as `WIRE_FLAT`. Reaching the same name from the bare type
            // name would reserve `REQUEST_WIRE_FLAT` on a message, whose module writes no such
            // name, and rename a constant that collides with nothing.
            if (remainder.starts_with("REQUEST_") || remainder.starts_with("RESPONSE_"))
            {
                continue;
            }
            out.push_back(remainder.str());
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
void declareConstantRegion(NamingScope& scope, const SemanticSection& section, const Language language)
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
NamingScope makeGoConstantScope(const SemanticSection& section, const llvm::StringRef typeName)
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
        if (!field.isPadding)
        {
            claim({typeName, field.name, "OPTION_TAG"});
        }
    }
    for (const auto& c : section.constants)
    {
        claim({typeName, c.name});
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

NamingScope makeSectionFieldScope(const Language language, const SemanticSection& section)
{
    NamingScope scope(language);
    declareFields(scope, section);
    if (constantsShareTheFieldScope(language))
    {
        declareConstantRegion(scope, section, language);
    }
    return scope;
}

NamingScope makeSectionConstantScope(const Language         language,
                                     const SemanticSection& section,
                                     const llvm::StringRef  typeConstantPrefix)
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
