//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements filesystem discovery for DSDL namespaces and types.
///
/// Discovery routines scan namespace roots, classify type files, and construct normalised lookup structures.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Frontend/Discovery.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/Support/ReservedIdentifiers.h"
#include "llvmdsdl/Support/SectionScopes.h"
#include "llvmdsdl/Support/SurfacePlan.h"

#include <algorithm>
#include <array>
#include <ios>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/STLExtras.h>
#include <map>
#include <set>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace llvmdsdl
{
namespace
{

std::string toLower(std::string s)
{
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool isValidNameComponent(const std::string& s)
{
    static const std::regex re("^[A-Za-z_][A-Za-z0-9_]*$");
    return std::regex_match(s, re);
}

/// @brief Reads one of the decimal fields of a DSDL file name.
///
/// The name is matched by a regex that accepts any run of digits, so the run can be longer than
/// anything it will be read into. `std::stoul` answers such a run by throwing, and the value it does
/// return is a `long`, which narrowing to the field's own width would wrap: `4294967296` becomes
/// zero, and a port-ID nobody wrote is what the generated code then carries.
///
/// @param[in] text The matched digits.
/// @param[out] out The value, when it fits.
/// @return True when @p text names a value the field can hold.
bool parseFileNameNumber(const std::string& text, std::uint32_t& out)
{
    std::uint64_t value = 0;
    for (const char c : text)
    {
        if ((c < '0') || (c > '9'))
        {
            return false;
        }
        value = (value * 10U) + static_cast<std::uint64_t>(c - '0');
        if (value > std::numeric_limits<std::uint32_t>::max())
        {
            return false;
        }
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

bool readTextFile(const std::filesystem::path& path, std::string& out)
{
    std::ifstream const in(path, std::ios::binary);
    if (!in.good())
    {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

std::vector<std::string> splitPathComponents(const std::filesystem::path& p)
{
    std::vector<std::string> out;
    for (const auto& part : p)
    {
        const auto s = part.string();
        if (s.empty() || s == ".")
        {
            continue;
        }
        out.push_back(s);
    }
    return out;
}

void discoverInRoot(const std::filesystem::path&       root,
                    bool                               isPrimaryRoot,
                    std::vector<DiscoveredDefinition>& out,
                    DiagnosticEngine&                  diagnostics)
{
    const auto canonicalRoot = std::filesystem::weakly_canonical(root);
    if (!std::filesystem::exists(canonicalRoot))
    {
        diagnostics.error({root.string(), 1, 1}, "namespace root does not exist: " + root.string());
        return;
    }

    static const std::regex fileRe(R"(^((\d+)\.)?([A-Za-z_][A-Za-z0-9_]*)\.(\d+)\.(\d+)\.dsdl$)");
    const std::string       rootNamespace = canonicalRoot.filename().string();
    if (!isValidNameComponent(rootNamespace))
    {
        diagnostics.error({root.string(), 1, 1}, "invalid root namespace directory name: " + rootNamespace);
        return;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(canonicalRoot))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        const auto& path = entry.path();
        if (path.extension() != ".dsdl")
        {
            continue;
        }

        std::smatch       m;
        const std::string fileName = path.filename().string();
        if (!std::regex_match(fileName, m, fileRe))
        {
            diagnostics.error({path.string(), 1, 1}, "invalid DSDL filename format: " + fileName);
            continue;
        }

        DiscoveredDefinition def;
        def.filePath          = path.string();
        def.rootNamespacePath = canonicalRoot.string();
        def.shortName         = m[3].str();

        const auto readNumber = [&](const std::string& text, const char* const what, std::uint32_t& out) {
            if (parseFileNameNumber(text, out))
            {
                return true;
            }
            std::string message(what);
            message.append(" ").append(text).append(" in ").append(fileName).append(" is too large");
            diagnostics.error({path.string(), 1, 1}, message);
            return false;
        };

        bool numbersRead = readNumber(m[4].str(), "major version", def.majorVersion);
        numbersRead      = readNumber(m[5].str(), "minor version", def.minorVersion) && numbersRead;
        if (m[2].matched)
        {
            std::uint32_t portId = 0;
            if (readNumber(m[2].str(), "fixed port-ID", portId))
            {
                def.fixedPortId = portId;
            }
            else
            {
                numbersRead = false;
            }
        }
        if (!numbersRead)
        {
            continue;
        }

        if (def.majorVersion == 0 && def.minorVersion == 0)
        {
            diagnostics.error({path.string(), 1, 1}, "version 0.0 is not allowed in DSDL definitions");
            continue;
        }

        const auto relativeParent = std::filesystem::relative(path.parent_path(), canonicalRoot);
        auto       ns             = splitPathComponents(relativeParent);
        ns.insert(ns.begin(), rootNamespace);

        bool validNamespace = true;
        for (const std::string& comp : ns)
        {
            if (!isValidNameComponent(comp))
            {
                diagnostics.error({path.string(), 1, 1}, "invalid namespace component: " + comp);
                validNamespace = false;
            }
            else if (isReservedIdentifier(comp))
            {
                // DSDL spec v1.0 section 3.2.5 / table 3.5: a name component may
                // not match a reserved identifier pattern.
                diagnostics.error({path.string(), 1, 1}, "namespace component is a reserved identifier: " + comp);
                validNamespace = false;
            }
        }
        if (isReservedIdentifier(def.shortName))
        {
            diagnostics.error({path.string(), 1, 1}, "type name is a reserved identifier: " + def.shortName);
            validNamespace = false;
        }
        if (!validNamespace)
        {
            continue;
        }

        def.namespaceComponents = ns;
        std::ostringstream fullName;
        for (std::size_t i = 0; i < ns.size(); ++i)
        {
            if (i > 0)
            {
                fullName << '.';
            }
            fullName << ns[i];
        }
        fullName << '.' << def.shortName;
        def.fullName = fullName.str();

        if (!readTextFile(path, def.text))
        {
            diagnostics.error({path.string(), 1, 1}, "failed to read DSDL source file");
            continue;
        }

        if (isPrimaryRoot || !def.text.empty())
        {
            out.push_back(std::move(def));
        }
    }
}

}  // namespace

namespace
{

/// @brief What produced a generated type name, for the collision diagnostic.
struct TypeNameOrigin final
{
    /// @brief Full DSDL name of the definition that produced it, or of the namespace.
    std::string fullName;

    /// @brief Section that produced it: `request`, `response`, or empty for the definition itself.
    std::string section;

    /// @brief Source file, so the diagnostic points at something the user can open.
    std::string filePath;

    /// @brief For a namespace, the full name of a definition it holds; empty for a type.
    std::string heldBy;

    /// @brief The name it takes under @ref TypeNameVersioning::Versioned.
    std::string versionedName;
};

/// @brief Renders an origin as a diagnostic phrase.
std::string describeOrigin(const TypeNameOrigin& origin)
{
    if (!origin.heldBy.empty())
    {
        return "namespace '" + origin.fullName + "', which holds '" + origin.heldBy + "'";
    }
    if (origin.section.empty())
    {
        return "'" + origin.fullName + "'";
    }
    return "the " + origin.section + " section of '" + origin.fullName + "'";
}

/// @brief Renders target language names as a diagnostic phrase, as in `target languages 'rust', 'go'`.
std::string describeLanguages(const std::vector<std::string>& languages)
{
    std::string out = (languages.size() > 1U) ? "target languages " : "target language ";
    for (std::size_t i = 0; i < languages.size(); ++i)
    {
        out += (i > 0 ? ", " : "") + ("'" + languages[i] + "'");
    }
    return out;
}

/// @brief Rejects a definition whose output file is the module a namespace's directory also is.
///
/// Where a file and a directory of one name are one module, `ns/File.1.0` beside a namespace
/// `ns.file_1_0` makes `file_1_0.rs` and `file_1_0/` one Rust `mod`, which rustc refuses, and
/// Python's package `file_1_0/` hides its module `file_1_0.py`.
///
/// @param[in] definitions Every definition discovered, sorted.
/// @param[in] outputLanguages Languages whose output names are checked.
/// @param[in,out] diagnostics Diagnostic sink.
void checkFileDirectoryCollisions(const std::vector<DiscoveredDefinition>& definitions,
                                  const llvm::ArrayRef<LanguageTraits>     outputLanguages,
                                  const std::vector<SurfacePlan>&          plans,
                                  DiagnosticEngine&                        diagnostics)
{
    // What takes one module name: the first definition whose file does, and the first namespace
    // whose directory does, with the first definition that namespace holds.
    struct ModuleOwners final
    {
        const DiscoveredDefinition* file{};
        std::string                 namespaceName;
        const DiscoveredDefinition* heldBy{};
    };
    struct Collision final
    {
        ModuleOwners             owners;
        std::vector<std::string> languages;
    };
    // Keyed on the file and the namespace rather than on the module, so a pair that collides in
    // several languages is one diagnostic naming all of them.
    std::map<std::pair<std::string, std::string>, Collision> collisions;

    for (std::size_t language = 0; language < outputLanguages.size(); ++language)
    {
        const LanguageTraits& row = outputLanguages[language];
        if (!row.composition.fileAndDirectoryAreOneModule)
        {
            continue;
        }
        std::map<std::string, ModuleOwners> modules;
        for (std::size_t index = 0; index < definitions.size(); ++index)
        {
            const DiscoveredDefinition& def   = definitions[index];
            const DefinitionNames&      names = plans[language].definitions[index];
            std::string                 path;
            std::string                 namespaceName;
            for (std::size_t depth = 0; depth < def.namespaceComponents.size(); ++depth)
            {
                path += names.namespaceNames[depth];
                namespaceName += def.namespaceComponents[depth];
                ModuleOwners& owners = modules[path];
                if (owners.heldBy == nullptr)
                {
                    owners.namespaceName = namespaceName;
                    owners.heldBy        = &def;
                }
                path.push_back('/');
                namespaceName.push_back('.');
            }
            path += names.fileStem;
            ModuleOwners& owners = modules[path];
            if (owners.file == nullptr)
            {
                owners.file = &def;
            }
        }
        for (const auto& [path, owners] : modules)
        {
            if ((owners.file == nullptr) || (owners.heldBy == nullptr))
            {
                continue;
            }
            Collision& collision = collisions[{owners.file->filePath, owners.namespaceName}];
            collision.owners     = owners;
            collision.languages.push_back(row.name.str());
        }
    }

    for (const auto& [key, collision] : collisions)
    {
        const DiscoveredDefinition& file = *collision.owners.file;
        std::string                 message;
        message.append("type name collision in generated output: ")
            .append(file.fullName)
            .append(".")
            .append(std::to_string(file.majorVersion))
            .append(".")
            .append(std::to_string(file.minorVersion))
            .append(" and namespace ")
            .append(collision.owners.namespaceName)
            .append(", which holds ")
            .append(collision.owners.heldBy->fullName)
            .append(", map to the same module name for ")
            .append(describeLanguages(collision.languages));
        diagnostics.error({file.filePath, 1, 1}, message);
    }
}

}  // namespace

void checkScopedTypeNameCollisions(const llvm::ArrayRef<ParsedDefinition> definitions,
                                   const llvm::ArrayRef<LanguageTraits>   outputLanguages,
                                   const TypeNameVersioning               versioning,
                                   DiagnosticEngine&                      diagnostics)
{
    if (outputLanguages.empty())
    {
        return;
    }

    // Keyed on the identifier as emitted, not on the DSDL name plus a version: under the unversioned
    // scheme the version is not in the identifier, so `Foo.1.0`'s request section and a sibling
    // `Foo_Request.2.0` do meet, and a key carrying the version would miss it.
    std::map<std::string, TypeNameOrigin> emitted;
    // A namespace is claimed once per language, however many definitions it holds.
    std::set<std::string> claimedNamespaces;

    const auto record = [&](const LanguageTraits& language,
                            const std::string&    scope,
                            const std::string&    name,
                            const TypeNameOrigin& origin) {
        const std::string key     = std::string(language.name) + ":" + scope + ":" + name;
        const auto [it, inserted] = emitted.emplace(key, origin);
        if (inserted)
        {
            return;
        }
        // Two namespaces of one name are one namespace. Two versions of one definition are the same
        // DSDL type and are D20's business, not this check's.
        const bool earlierIsNamespace = !it->second.heldBy.empty();
        const bool laterIsNamespace   = !origin.heldBy.empty();
        if ((earlierIsNamespace == laterIsNamespace) && (laterIsNamespace || (it->second.fullName == origin.fullName)))
        {
            return;
        }
        // A namespace is reported against the type it meets, at the type's file.
        const TypeNameOrigin& first  = laterIsNamespace ? it->second : origin;
        const TypeNameOrigin& second = laterIsNamespace ? origin : it->second;
        // The versioned scheme is suggested where it would part the two: `Foo`'s request section
        // and `Foo_Request`, but not two definitions of one version whose names meet whole.
        const bool versioningParts =
            (versioning == TypeNameVersioning::Unversioned) && (first.versionedName != second.versionedName);
        const char* const remedy =
            versioningParts ? "pass --versioned-type-names, or rename one of them" : "rename one of them";
        diagnostics.error({first.filePath, 1, 1},
                          "type name collision in generated output: " + describeOrigin(first) + " and " +
                              describeOrigin(second) + (second.heldBy.empty() ? "" : ",") + " both emit '" + name +
                              "' for target language '" + std::string(language.name) + "'; " + remedy);
    };

    // Each language's names, from the allocation the emitters' names come from, under the run's
    // versioning and under the versioned scheme the diagnostic suggests.
    std::vector<DefinitionParts> parts;
    parts.reserve(definitions.size());
    for (const auto& parsed : definitions)
    {
        parts.push_back(discoveredParts(parsed.info, parsed.ast.isService(), parsed.ast.isDeprecated()));
    }
    std::vector<std::pair<SurfacePlan, SurfacePlan>> plans;
    plans.reserve(outputLanguages.size());
    for (const auto& language : outputLanguages)
    {
        plans.emplace_back(allocateSurface(language,
                                           parts,
                                           SurfaceOptions{.packageName   = {},
                                                          .versioning    = versioning,
                                                          .accessorsOnly = false,
                                                          .profile       = {}}),
                           allocateSurface(language,
                                           parts,
                                           SurfaceOptions{.packageName   = {},
                                                          .versioning    = TypeNameVersioning::Versioned,
                                                          .accessorsOnly = false,
                                                          .profile       = {}}));
    }

    for (std::size_t index = 0; index < definitions.size(); ++index)
    {
        const auto& info = definitions[index].info;
        for (std::size_t row = 0; row < outputLanguages.size(); ++row)
        {
            const LanguageTraits& language = outputLanguages[row];
            for (const ScopedTypeName& type :
                 scopedTypeNames(language, plans[row].first, plans[row].second, index, info.namespaceComponents))
            {
                if (type.namespaceName.empty())
                {
                    record(language,
                           type.scope,
                           type.name,
                           TypeNameOrigin{info.fullName, type.section, info.filePath, "", type.versionedName});
                }
                else if (claimedNamespaces.insert(std::string(language.name) + ":" + type.namespaceName).second)
                {
                    record(language,
                           type.scope,
                           type.name,
                           TypeNameOrigin{type.namespaceName, "", info.filePath, info.fullName, type.versionedName});
                }
            }
        }
    }
}

DefinitionParts discoveredParts(const DiscoveredDefinition& info, const bool isService, const bool isDeprecated)
{
    return DefinitionParts{.ref         = DefinitionRef{.namespaceComponents = info.namespaceComponents,
                                                        .shortName           = info.shortName,
                                                        .majorVersion        = info.majorVersion,
                                                        .minorVersion        = info.minorVersion},
                           .fixedPortId = info.fixedPortId,
                           .service     = isService,
                           .deprecated  = isDeprecated,
                           .request     = SectionParts{},
                           .response    = isService ? std::optional<SectionParts>(SectionParts{}) : std::nullopt,
                           .bodies      = {}};
}

std::string sharedScopeOf(const SurfacePlan& plan, const std::size_t index)
{
    // A language that joins the namespace into the identifier opens no scope for it, and declares
    // every definition's names in one global scope. C joins with `__`, which a DSDL name may hold as
    // well, so `ns.A__B` and `ns.A.B` are both `ns__A__B`.
    std::vector<std::string> path;
    for (std::optional<std::size_t> scope = plan.scopes[plan.definitions[index].fileScope].parent;
         scope && (plan.scopes[*scope].kind != SurfaceScopeKind::Root);
         scope = plan.scopes[*scope].parent)
    {
        path.push_back(plan.scopes[*scope].name);
    }
    std::string out;
    for (const std::string& component : llvm::reverse(path))
    {
        out += component + ".";
    }
    return out;
}

std::vector<ScopedTypeName> scopedTypeNames(const LanguageTraits&             language,
                                            const SurfacePlan&                plan,
                                            const SurfacePlan&                versioned,
                                            const std::size_t                 index,
                                            const llvm::ArrayRef<std::string> namespaceComponents)
{
    std::vector<ScopedTypeName> out;
    if (!language.composition.definitionsShareNamespaceScope)
    {
        return out;
    }
    const DefinitionNames& names          = plan.definitions[index];
    const DefinitionNames& versionedNames = versioned.definitions[index];
    if (language.composition.namespaceAndTypeShareScope)
    {
        std::vector<std::size_t> spaces;
        for (std::optional<std::size_t> scope = plan.scopes[names.fileScope].parent;
             scope && (plan.scopes[*scope].kind != SurfaceScopeKind::Root);
             scope = plan.scopes[*scope].parent)
        {
            spaces.insert(spaces.begin(), *scope);
        }
        std::string parent;
        std::string namespaceName;
        for (std::size_t depth = 0; depth < spaces.size(); ++depth)
        {
            const std::string& name = plan.scopes[spaces[depth]].name;
            namespaceName += namespaceComponents[depth];
            out.push_back(ScopedTypeName{parent, name, name, "", namespaceName});
            parent += name + ".";
            namespaceName.push_back('.');
        }
    }

    const std::string scope = sharedScopeOf(plan, index);
    if (names.sections.front().section == "request")
    {
        out.push_back(ScopedTypeName{scope, names.typeName, versionedNames.typeName, "", ""});
    }
    for (std::size_t section = 0; section < names.sections.size(); ++section)
    {
        const SectionNames& held          = names.sections[section];
        const SectionNames& versionedHeld = versionedNames.sections[section];
        out.push_back(ScopedTypeName{scope, held.typeName, versionedHeld.typeName, held.section, ""});
        // A deprecated definition's struct is declared under a name of its own, which a sibling may
        // be called; that name is claimed beside the public one.
        const std::string& declared = plan.scopes[held.typeScope].name;
        if (declared != held.typeName)
        {
            out.push_back(
                ScopedTypeName{scope, declared, versioned.scopes[versionedHeld.typeScope].name, held.section, ""});
        }
    }
    return out;
}

std::vector<DiscoveredDefinition> discoverDefinitions(const std::vector<std::string>&      rootNamespaceDirs,
                                                      const std::vector<std::string>&      lookupDirs,
                                                      DiagnosticEngine&                    diagnostics,
                                                      const llvm::ArrayRef<LanguageTraits> outputLanguages)
{
    std::vector<DiscoveredDefinition> definitions;

    for (const std::string& root : rootNamespaceDirs)
    {
        discoverInRoot(root, true, definitions, diagnostics);
    }
    for (const std::string& lookup : lookupDirs)
    {
        discoverInRoot(lookup, false, definitions, diagnostics);
    }

    std::ranges::sort(definitions, [](const DiscoveredDefinition& a, const DiscoveredDefinition& b) {
        if (a.fullName != b.fullName)
        {
            return a.fullName < b.fullName;
        }
        if (a.majorVersion != b.majorVersion)
        {
            return a.majorVersion > b.majorVersion;
        }
        if (a.minorVersion != b.minorVersion)
        {
            return a.minorVersion > b.minorVersion;
        }
        return a.filePath < b.filePath;
    });

    std::unordered_map<std::string, std::string> caseInsensitiveNames;
    std::unordered_map<std::string, std::string> versionUnique;
    std::unordered_map<std::string, std::string> generatedOutputNames;

    // Each language's names, from the allocation the emitters' names come from. Nothing is parsed
    // yet, so each definition is taken as a message; the names compared here do not depend on it.
    std::vector<DefinitionParts> parts;
    parts.reserve(definitions.size());
    for (const auto& def : definitions)
    {
        parts.push_back(discoveredParts(def, false, false));
    }
    std::vector<SurfacePlan> plans;
    plans.reserve(outputLanguages.size());
    for (const LanguageTraits& row : outputLanguages)
    {
        plans.push_back(allocateSurface(row,
                                        parts,
                                        SurfaceOptions{.packageName   = {},
                                                       .versioning    = {},
                                                       .accessorsOnly = false,
                                                       .profile       = {}}));
    }

    for (std::size_t index = 0; index < definitions.size(); ++index)
    {
        const DiscoveredDefinition& def       = definitions[index];
        const std::string           lowerName = toLower(def.fullName);
        const auto [itName, insertedName]     = caseInsensitiveNames.emplace(lowerName, def.fullName);
        if (!insertedName && itName->second != def.fullName)
        {
            diagnostics.error({def.filePath, 1, 1},
                              "name collision on case-insensitive filesystem: " + def.fullName + " conflicts with " +
                                  itName->second);
        }

        // Two distinct DSDL types can land on one generated name, because both the file-stem and the
        // type-name projections are many-to-one and they fold differently: the stem keeps
        // underscores and the type name drops them. `FooBar`/`Foo_bar` collide as file names,
        // `Break`/`Break_` collide once the keyword escape fires, and `_foo`/`foo_` take two files
        // but one type name. Whichever half collides, one type is lost or the output does not
        // compile, so the pair is rejected here.
        //
        // The keys come from the same engine the emitters name with, so the check cannot drift from
        // what is written. Only the languages selected for this invocation are checked; see the
        // decisions section of docs/development/identifier-stropping.md.
        const std::string versionSuffix =
            ":" + std::to_string(def.majorVersion) + ":" + std::to_string(def.minorVersion);
        const std::array<std::pair<IdentifierRole, const char*>, 2> kOutputNames = {
            {{IdentifierRole::FileStem, "output file name"}, {IdentifierRole::TypeName, "generated type name"}}};

        // One diagnostic per colliding pair, naming every language it affects: renaming one of the
        // two types fixes all of them at once, so a line per language would be a line per reader
        // eye-roll. The role loop is outermost for the same reason -- a pair whose file names
        // already collide does not also need to be told its type names do.
        std::set<std::string> reportedAgainst;
        // Path-level renames are announced without asking (see the decisions section of
        // docs/development/identifier-stropping.md): a renamed output file or package directory
        // changes what a build has to reference, and nothing else tells the user it happened. The
        // set keeps one note per (language, name) even though the name is projected several times.
        std::set<std::string> renameNotes;
        for (const auto& [role, what] : kOutputNames)
        {
            std::map<std::string, std::vector<std::string>> collidedWith;
            for (std::size_t language = 0; language < outputLanguages.size(); ++language)
            {
                const LanguageTraits&  row          = outputLanguages[language];
                const llvm::StringRef  languageName = row.name;
                const DefinitionNames& names        = plans[language].definitions[index];
                std::string            namespacePath;
                for (std::size_t depth = 0; depth < def.namespaceComponents.size(); ++depth)
                {
                    const std::string& component = def.namespaceComponents[depth];
                    // Reported once per language, under the file-stem pass, so a namespace does not
                    // announce itself again for the type-name pass.
                    if ((role == IdentifierRole::FileStem) &&
                        codegenProjectIdentifierDetailed(row.language, IdentifierRole::NamespaceName, component)
                            .escaped)
                    {
                        renameNotes.emplace(std::string(languageName) + ":" + component + ":" +
                                            names.namespaceNames[depth]);
                    }
                    namespacePath += names.namespaceNames[depth];
                    namespacePath.push_back('/');
                }
                std::string outputName = names.typeName;
                if (role == IdentifierRole::FileStem)
                {
                    outputName = names.fileStem;
                    if (renderDefinitionFileStemDetailed(row.language,
                                                         def.shortName,
                                                         def.majorVersion,
                                                         def.minorVersion)
                            .escaped)
                    {
                        renameNotes.emplace(
                            std::string(languageName) + ":" +
                            renderDefinitionFileStemSource(def.shortName, def.majorVersion, def.minorVersion) + ":" +
                            outputName);
                    }
                }
                std::string key;
                key.append(languageName)
                    .append(":")
                    .append(std::to_string(static_cast<int>(role)))
                    .append(":")
                    .append(namespacePath)
                    .append(outputName)
                    .append(versionSuffix);
                const auto [it, inserted] = generatedOutputNames.emplace(key, def.fullName);
                if (!inserted && it->second != def.fullName)
                {
                    collidedWith[it->second].push_back(languageName.str());
                }
            }
            for (const auto& [other, languages] : collidedWith)
            {
                if (!reportedAgainst.insert(other).second)
                {
                    continue;
                }
                std::string collision;
                collision.append("type name collision in generated output: ")
                    .append(def.fullName)
                    .append(" and ")
                    .append(other)
                    .append(" map to the same ")
                    .append(what)
                    .append(" for ")
                    .append(describeLanguages(languages));
                diagnostics.error({def.filePath, 1, 1}, collision);
            }
        }

        for (const auto& note : renameNotes)
        {
            const auto firstColon  = note.find(':');
            const auto secondColon = note.find(':', firstColon + 1);
            diagnostics.note({def.filePath, 1, 1},
                             "'" + note.substr(firstColon + 1, secondColon - firstColon - 1) + "' is emitted as '" +
                                 note.substr(secondColon + 1) + "' for target language '" + note.substr(0, firstColon) +
                                 "'");
        }

        const std::string versionKey =
            lowerName + ":" + std::to_string(def.majorVersion) + ":" + std::to_string(def.minorVersion);
        const auto [itV, insertedV] = versionUnique.emplace(versionKey, def.filePath);
        if (!insertedV)
        {
            diagnostics.error({def.filePath, 1, 1},
                              "duplicate definition version: " + def.fullName + "." + std::to_string(def.majorVersion) +
                                  "." + std::to_string(def.minorVersion));
        }
    }

    checkFileDirectoryCollisions(definitions, outputLanguages, plans, diagnostics);

    return definitions;
}

}  // namespace llvmdsdl
