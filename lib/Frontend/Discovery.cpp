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
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Support/ReservedIdentifiers.h"

#include <algorithm>
#include <ios>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <filesystem>
#include <fstream>
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

std::vector<DiscoveredDefinition> discoverDefinitions(const std::vector<std::string>& rootNamespaceDirs,
                                                      const std::vector<std::string>& lookupDirs,
                                                      DiagnosticEngine&               diagnostics)
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

    for (const DiscoveredDefinition& def : definitions)
    {
        const std::string lowerName       = toLower(def.fullName);
        const auto [itName, insertedName] = caseInsensitiveNames.emplace(lowerName, def.fullName);
        if (!insertedName && itName->second != def.fullName)
        {
            diagnostics.error({def.filePath, 1, 1},
                              "name collision on case-insensitive filesystem: " + def.fullName + " conflicts with " +
                                  itName->second);
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
