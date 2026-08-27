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
/// Discovery routines scan namespace roots, classify type files, and construct normalized lookup structures.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Frontend/Discovery.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/Support/ReservedIdentifiers.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <cctype>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <utility>

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
        def.majorVersion      = static_cast<std::uint32_t>(std::stoul(m[4].str()));
        def.minorVersion      = static_cast<std::uint32_t>(std::stoul(m[5].str()));

        if (m[2].matched)
        {
            def.fixedPortId = static_cast<std::uint32_t>(std::stoul(m[2].str()));
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

llvm::ArrayRef<OutputLanguage> allOutputLanguages()
{
    static const std::array<OutputLanguage, 6> kAll = {{{CodegenNamingLanguage::C, "c"},
                                                        {CodegenNamingLanguage::Cpp, "cpp"},
                                                        {CodegenNamingLanguage::Rust, "rust"},
                                                        {CodegenNamingLanguage::Go, "go"},
                                                        {CodegenNamingLanguage::TypeScript, "ts"},
                                                        {CodegenNamingLanguage::Python, "python"}}};
    return kAll;
}

namespace
{

/// @brief True when @p language puts every definition of a namespace in one scope.
///
/// C has a single global scope and carries the namespace in the identifier; C++ has a namespace per
/// DSDL namespace; Go compiles one per package. Rust, TypeScript and Python give each definition
/// *and version* its own module, so two of them may spell a type the same way without meeting.
bool namespaceIsOneScope(const CodegenNamingLanguage language)
{
    return (language == CodegenNamingLanguage::C) || (language == CodegenNamingLanguage::Cpp) ||
           (language == CodegenNamingLanguage::Go);
}

/// @brief What produced a generated type name, for the collision diagnostic.
struct TypeNameOrigin final
{
    /// @brief Full DSDL name of the definition that produced it.
    std::string fullName;

    /// @brief Section that produced it: `request`, `response`, or empty for the definition itself.
    std::string section;

    /// @brief Source file, so the diagnostic points at something the user can open.
    std::string filePath;
};

/// @brief Renders an origin as a diagnostic phrase.
std::string describeOrigin(const TypeNameOrigin& origin)
{
    if (origin.section.empty())
    {
        return "'" + origin.fullName + "'";
    }
    return "the " + origin.section + " section of '" + origin.fullName + "'";
}

}  // namespace

void checkServiceSectionTypeNameCollisions(const llvm::ArrayRef<ParsedDefinition> definitions,
                                           const llvm::ArrayRef<OutputLanguage>   outputLanguages,
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

    const auto record = [&](const OutputLanguage& language,
                            const std::string&    scope,
                            const std::string&    name,
                            const TypeNameOrigin& origin) {
        const std::string key     = std::string(language.name) + ":" + scope + ":" + name;
        const auto [it, inserted] = emitted.emplace(key, origin);
        if (inserted || (it->second.fullName == origin.fullName))
        {
            return;
        }
        // Two versions of one definition are the same DSDL type and are D20's business, not this
        // check's; the guard above lets them through. This is two *different* types.
        diagnostics.error({origin.filePath, 1, 1},
                          "type name collision in generated output: " + describeOrigin(origin) + " and " +
                              describeOrigin(it->second) + " both emit '" + name + "' for target language '" +
                              std::string(language.name) + "'; pass --versioned-type-names, or rename one of them");
    };

    for (const auto& parsed : definitions)
    {
        const auto& info = parsed.info;
        for (const auto& language : outputLanguages)
        {
            if (!namespaceIsOneScope(language.language))
            {
                continue;
            }
            // The scope a name has to be unique within. C flattens the namespace into the
            // identifier and shares one global scope; C++ and Go put the short name in a scope of
            // their own per namespace, so that namespace is part of the key.
            std::string scope;
            for (const auto& component : info.namespaceComponents)
            {
                scope += codegenProjectIdentifier(language.language, IdentifierRole::NamespaceName, component);
                scope.push_back('.');
            }
            const std::string base = renderDefinitionTypeName(language.language,
                                                              info.namespaceComponents,
                                                              info.shortName,
                                                              info.majorVersion,
                                                              info.minorVersion,
                                                              versioning);
            record(language, scope, base, TypeNameOrigin{info.fullName, "", info.filePath});
            if (!parsed.ast.isService())
            {
                continue;
            }
            for (const llvm::StringRef section : {llvm::StringRef("request"), llvm::StringRef("response")})
            {
                record(language,
                       scope,
                       base + renderSectionTypeSuffix(language.language, section),
                       TypeNameOrigin{info.fullName, section.str(), info.filePath});
            }
        }
    }
}

std::vector<DiscoveredDefinition> discoverDefinitions(const std::vector<std::string>&      rootNamespaceDirs,
                                                      const std::vector<std::string>&      lookupDirs,
                                                      DiagnosticEngine&                    diagnostics,
                                                      const llvm::ArrayRef<OutputLanguage> outputLanguages)
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

    for (const auto& def : definitions)
    {
        const std::string lowerName       = toLower(def.fullName);
        const auto [itName, insertedName] = caseInsensitiveNames.emplace(lowerName, def.fullName);
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
            for (const auto& [language, languageName] : outputLanguages)
            {
                std::string namespacePath;
                for (const auto& component : def.namespaceComponents)
                {
                    const auto projected =
                        codegenProjectIdentifierDetailed(language, IdentifierRole::NamespaceName, component);
                    if (projected.escaped && role == IdentifierRole::FileStem)
                    {
                        // Reported once per language, under the file-stem pass, so a namespace does
                        // not announce itself again for the type-name pass.
                        renameNotes.emplace(std::string(languageName) + ":" + component + ":" + projected.identifier);
                    }
                    namespacePath += projected.identifier;
                    namespacePath.push_back('/');
                }
                const auto projectedName = codegenProjectIdentifierDetailed(language, role, def.shortName);
                if (projectedName.escaped && role == IdentifierRole::FileStem)
                {
                    renameNotes.emplace(std::string(languageName) + ":" + def.shortName + ":" +
                                        projectedName.identifier);
                }
                const std::string key = std::string(languageName) + ":" + std::to_string(static_cast<int>(role)) + ":" +
                                        namespacePath + projectedName.identifier + versionSuffix;
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
                std::string languageList;
                for (std::size_t i = 0; i < languages.size(); ++i)
                {
                    languageList += (i > 0 ? ", " : "") + ("'" + languages[i] + "'");
                }
                diagnostics.error({def.filePath, 1, 1},
                                  "type name collision in generated output: " + def.fullName + " and " + other +
                                      " map to the same " + what + " for target language" +
                                      (languages.size() > 1U ? "s " : " ") + languageList);
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

    return definitions;
}

}  // namespace llvmdsdl
