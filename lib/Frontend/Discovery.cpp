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
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/Support/ReservedIdentifiers.h"

#include <algorithm>
#include <array>
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
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool isValidNameComponent(const std::string& s)
{
    static const std::regex re("^[A-Za-z_][A-Za-z0-9_]*$");
    return std::regex_match(s, re);
}

bool readTextFile(const std::filesystem::path& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
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
        const auto path = entry.path();
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

    std::sort(definitions.begin(), definitions.end(), [](const DiscoveredDefinition& a, const DiscoveredDefinition& b) {
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
        // The keys come from the same engine the emitters name with, so this check cannot drift from
        // what is actually written -- which is how the earlier hand-rolled fold came to miss the
        // keyword escape. Only the languages selected for this invocation are checked; see the
        // decisions section of docs/development/identifier-stropping.md.
        for (const auto& [language, languageName] : outputLanguages)
        {
            std::string namespacePath;
            for (const auto& component : def.namespaceComponents)
            {
                namespacePath += codegenProjectIdentifier(language, IdentifierRole::NamespaceName, component);
                namespacePath.push_back('/');
            }
            const std::string versionSuffix =
                ":" + std::to_string(def.majorVersion) + ":" + std::to_string(def.minorVersion);

            const std::array<std::pair<IdentifierRole, const char*>, 2> kOutputNames = {
                {{IdentifierRole::FileStem, "output file name"}, {IdentifierRole::TypeName, "generated type name"}}};

            // A pair that collides on both halves is still one problem to the reader, so the first
            // one reported wins and the second is suppressed.
            bool reported = false;
            for (const auto& [role, what] : kOutputNames)
            {
                const std::string key = std::string(languageName) + ":" + std::to_string(static_cast<int>(role)) + ":" +
                                        namespacePath + codegenProjectIdentifier(language, role, def.shortName) +
                                        versionSuffix;
                const auto [it, inserted] = generatedOutputNames.emplace(key, def.fullName);
                if (!inserted && it->second != def.fullName && !reported)
                {
                    diagnostics.error({def.filePath, 1, 1},
                                      "type name collision in generated output: " + def.fullName + " and " +
                                          it->second + " map to the same " + what + " for target language '" +
                                          languageName.str() + "'");
                    reported = true;
                }
            }
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
