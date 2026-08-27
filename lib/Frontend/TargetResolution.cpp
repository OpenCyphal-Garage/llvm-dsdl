//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Target path resolution for nnvg-style CLI inputs.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Frontend/TargetResolution.h"

#include "llvmdsdl/Frontend/SourceLocation.h"
#include "llvmdsdl/Support/Diagnostics.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace llvmdsdl
{
namespace
{

struct SplitToken final
{
    std::string lhs;
    std::string rhs;
    bool        hasColon{false};
};

llvm::Expected<SplitToken> splitUnescapedColon(llvm::StringRef token)
{
    SplitToken out;

    bool sawColon = false;
    for (std::size_t i = 0; i < token.size(); ++i)
    {
        const char c = token[i];
        if (c == '\\' && (i + 1U < token.size()) && token[i + 1U] == ':')
        {
            (sawColon ? out.rhs : out.lhs).push_back(':');
            ++i;
            continue;
        }
        if (c == ':')
        {
            if (sawColon)
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "invalid target path (too many unescaped colons): %s",
                                               token.str().c_str());
            }
            sawColon     = true;
            out.hasColon = true;
            continue;
        }
        (sawColon ? out.rhs : out.lhs).push_back(c);
    }
    return out;
}

std::filesystem::path normalizePath(const std::filesystem::path& path)
{
    std::error_code ec;
    auto            normalized = std::filesystem::weakly_canonical(path, ec);
    if (ec)
    {
        ec.clear();
        normalized = std::filesystem::absolute(path, ec);
        if (ec)
        {
            return path.lexically_normal();
        }
    }
    return normalized.lexically_normal();
}

bool isUnderRoot(const std::filesystem::path& file, const std::filesystem::path& root)
{
    std::error_code ec;
    auto            rel = std::filesystem::relative(file, root, ec);
    if (ec)
    {
        return false;
    }
    if (rel.empty())
    {
        return false;
    }
    if (rel.is_absolute())
    {
        return false;
    }
    auto text = rel.generic_string();
    return !(llvm::StringRef(text).starts_with(".."));
}

char envPathSeparator()
{
#ifdef _WIN32
    return ';';
#else
    return ':';
#endif
}

std::vector<std::filesystem::path> splitEnvPaths(const char* value)
{
    std::vector<std::filesystem::path> out;
    if (value == nullptr || *value == '\0')
    {
        return out;
    }

    const char  separator = envPathSeparator();
    std::string current;
    for (const char* it = value; *it != '\0'; ++it)
    {
        if (*it == separator)
        {
            if (!current.empty())
            {
                out.emplace_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(*it);
    }
    if (!current.empty())
    {
        out.emplace_back(current);
    }
    return out;
}

std::set<std::filesystem::path> lookupDirsFromEnvironment()
{
    std::set<std::filesystem::path> out;

    for (const auto& includePath : splitEnvPaths(std::getenv("DSDL_INCLUDE_PATH")))
    {
        if (includePath.empty())
        {
            continue;
        }
        out.insert(normalizePath(includePath));
    }

    for (const auto& cyphalRoot : splitEnvPaths(std::getenv("CYPHAL_PATH")))
    {
        if (cyphalRoot.empty())
        {
            continue;
        }
        const auto      base = normalizePath(cyphalRoot);
        std::error_code ec;
        if (!std::filesystem::exists(base, ec) || ec || !std::filesystem::is_directory(base, ec))
        {
            continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(base, ec))
        {
            if (ec)
            {
                ec.clear();
                continue;
            }
            if (entry.is_directory(ec) && !ec)
            {
                out.insert(normalizePath(entry.path()));
            }
            ec.clear();
        }
    }

    return out;
}

llvm::Error expandRootTargets(const std::filesystem::path& root, std::set<std::filesystem::path>& explicitTargets)
{
    std::error_code                                     ec;
    std::filesystem::recursive_directory_iterator       it(root,
                                                           std::filesystem::directory_options::skip_permission_denied,
                                                           ec);
    std::filesystem::recursive_directory_iterator const end;
    if (ec)
    {
        return llvm::createStringError(ec, "failed to enumerate root namespace directory %s", root.string().c_str());
    }
    for (; it != end; it.increment(ec))
    {
        if (ec)
        {
            ec.clear();
            continue;
        }
        if (!it->is_regular_file(ec) || ec)
        {
            ec.clear();
            continue;
        }
        if (it->path().extension() == ".dsdl")
        {
            explicitTargets.insert(normalizePath(it->path()));
        }
    }
    return llvm::Error::success();
}

llvm::Expected<std::filesystem::path> resolveExistingDirectory(const std::string& rawPath)
{
    std::error_code             ec;
    const std::filesystem::path p(rawPath);
    if (!std::filesystem::exists(p, ec) || ec || !std::filesystem::is_directory(p, ec))
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "directory does not exist: %s", rawPath.c_str());
    }
    return normalizePath(p);
}

llvm::Expected<std::filesystem::path> resolveExistingFile(const std::filesystem::path& path)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec || !std::filesystem::is_regular_file(path, ec))
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "file does not exist: %s",
                                       path.string().c_str());
    }
    return normalizePath(path);
}

}  // namespace

llvm::Expected<ResolvedTargets> resolveTargets(const std::vector<std::string>& targetFilesOrRootNamespace,
                                               const TargetResolveOptions&     options,
                                               DiagnosticEngine&               diagnostics)
{
    std::set<std::filesystem::path> roots;
    std::set<std::filesystem::path> lookupRoots;
    std::set<std::filesystem::path> explicitTargets;
    // The subset of explicitTargets the user named one at a time, rather than swept in by pointing
    // at a folder. Everything that generates cares only about the first set; a rule about what may
    // be dropped by default has to be able to see the difference.
    std::set<std::filesystem::path>    namedTargets;
    std::vector<std::filesystem::path> unresolvedFileTargets;

    for (const auto& envLookup : lookupDirsFromEnvironment())
    {
        lookupRoots.insert(envLookup);
    }

    for (const auto& lookupRaw : options.lookupDirs)
    {
        auto split = splitUnescapedColon(lookupRaw);
        if (!split)
        {
            diagnostics.error({lookupRaw, 1, 1}, llvm::toString(split.takeError()));
            return llvm::createStringError(llvm::inconvertibleErrorCode(), "lookup resolution failed");
        }

        if (split->hasColon)
        {
            auto resolvedRoot = resolveExistingDirectory(split->lhs);
            if (!resolvedRoot)
            {
                diagnostics.error({lookupRaw, 1, 1}, llvm::toString(resolvedRoot.takeError()));
                return llvm::createStringError(llvm::inconvertibleErrorCode(), "lookup resolution failed");
            }
            if (split->rhs.empty())
            {
                diagnostics.error({lookupRaw, 1, 1}, "lookup target after colon is empty");
                return llvm::createStringError(llvm::inconvertibleErrorCode(), "lookup resolution failed");
            }
            const std::filesystem::path rel(split->rhs);
            if (rel.is_absolute())
            {
                diagnostics.error({lookupRaw, 1, 1}, "lookup target path cannot be absolute when using colon syntax");
                return llvm::createStringError(llvm::inconvertibleErrorCode(), "lookup resolution failed");
            }
            auto resolvedFile = resolveExistingFile((*resolvedRoot) / rel);
            if (!resolvedFile)
            {
                diagnostics.error({lookupRaw, 1, 1}, llvm::toString(resolvedFile.takeError()));
                return llvm::createStringError(llvm::inconvertibleErrorCode(), "lookup resolution failed");
            }
            lookupRoots.insert(*resolvedRoot);
            explicitTargets.insert(*resolvedFile);
            namedTargets.insert(*resolvedFile);
            continue;
        }

        std::error_code             ec;
        const std::filesystem::path path(split->lhs);
        if (std::filesystem::exists(path, ec) && !ec && std::filesystem::is_directory(path, ec))
        {
            lookupRoots.insert(normalizePath(path));
            continue;
        }
        if (std::filesystem::exists(path, ec) && !ec && std::filesystem::is_regular_file(path, ec))
        {
            unresolvedFileTargets.push_back(normalizePath(path));
            continue;
        }
        diagnostics.error({lookupRaw, 1, 1}, "lookup path does not exist: " + lookupRaw);
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "lookup resolution failed");
    }

    for (const auto& token : targetFilesOrRootNamespace)
    {
        auto split = splitUnescapedColon(token);
        if (!split)
        {
            diagnostics.error({token, 1, 1}, llvm::toString(split.takeError()));
            return llvm::createStringError(llvm::inconvertibleErrorCode(), "target resolution failed");
        }

        if (split->hasColon)
        {
            auto resolvedRoot = resolveExistingDirectory(split->lhs);
            if (!resolvedRoot)
            {
                diagnostics.error({token, 1, 1}, llvm::toString(resolvedRoot.takeError()));
                return llvm::createStringError(llvm::inconvertibleErrorCode(), "target resolution failed");
            }
            if (split->rhs.empty())
            {
                diagnostics.error({token, 1, 1}, "target path after colon is empty");
                return llvm::createStringError(llvm::inconvertibleErrorCode(), "target resolution failed");
            }
            const std::filesystem::path rel(split->rhs);
            if (rel.is_absolute())
            {
                diagnostics.error({token, 1, 1}, "target file path cannot be absolute when using colon syntax");
                return llvm::createStringError(llvm::inconvertibleErrorCode(), "target resolution failed");
            }
            auto resolvedFile = resolveExistingFile((*resolvedRoot) / rel);
            if (!resolvedFile)
            {
                diagnostics.error({token, 1, 1}, llvm::toString(resolvedFile.takeError()));
                return llvm::createStringError(llvm::inconvertibleErrorCode(), "target resolution failed");
            }
            roots.insert(*resolvedRoot);
            explicitTargets.insert(*resolvedFile);
            namedTargets.insert(*resolvedFile);
            continue;
        }

        const std::filesystem::path rawPath(split->lhs);
        std::error_code             ec;
        if (std::filesystem::exists(rawPath, ec) && !ec && std::filesystem::is_directory(rawPath, ec))
        {
            if (options.noTargetNamespaces)
            {
                diagnostics.error({token, 1, 1},
                                  "root directory cannot be a folder (--no-target-namespaces): " +
                                      normalizePath(rawPath).string());
                return llvm::createStringError(llvm::inconvertibleErrorCode(), "target resolution failed");
            }
            const auto root = normalizePath(rawPath);
            roots.insert(root);
            if (auto err = expandRootTargets(root, explicitTargets))
            {
                diagnostics.error({token, 1, 1}, llvm::toString(std::move(err)));
                return llvm::createStringError(llvm::inconvertibleErrorCode(), "target resolution failed");
            }
            continue;
        }

        if (std::filesystem::exists(rawPath, ec) && !ec && std::filesystem::is_regular_file(rawPath, ec))
        {
            unresolvedFileTargets.push_back(normalizePath(rawPath));
            continue;
        }

        diagnostics.error({token, 1, 1}, "target path does not exist: " + token);
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "target resolution failed");
    }

    std::set<std::filesystem::path> candidateRoots = lookupRoots;
    candidateRoots.insert(roots.begin(), roots.end());

    for (const auto& file : unresolvedFileTargets)
    {
        std::vector<std::filesystem::path> matches;
        for (const auto& candidate : candidateRoots)
        {
            if (isUnderRoot(file, candidate))
            {
                matches.push_back(candidate);
            }
        }

        if (matches.empty())
        {
            diagnostics.error({file.string(), 1, 1},
                              "cannot infer root namespace for target file; specify --lookup-dir or use colon syntax");
            return llvm::createStringError(llvm::inconvertibleErrorCode(), "target resolution failed");
        }
        if (matches.size() > 1U)
        {
            std::string message = "ambiguous root namespace for target file " + file.string() + ": ";
            for (std::size_t i = 0; i < matches.size(); ++i)
            {
                if (i > 0)
                {
                    message += ", ";
                }
                message += matches[i].string();
            }
            diagnostics.error({file.string(), 1, 1}, message);
            return llvm::createStringError(llvm::inconvertibleErrorCode(), "target resolution failed");
        }

        roots.insert(matches.front());
        candidateRoots.insert(matches.front());
        explicitTargets.insert(file);
        namedTargets.insert(file);
    }

    ResolvedTargets out;
    for (const auto& root : roots)
    {
        out.rootNamespaceDirs.push_back(root.string());
    }
    for (const auto& lookup : lookupRoots)
    {
        if (roots.contains(lookup))
        {
            continue;
        }
        out.lookupDirs.push_back(lookup.string());
    }
    for (const auto& target : explicitTargets)
    {
        out.explicitTargetFiles.push_back(target.string());
    }
    for (const auto& target : namedTargets)
    {
        out.namedTargetFiles.push_back(target.string());
    }

    std::ranges::sort(out.rootNamespaceDirs);
    std::ranges::sort(out.lookupDirs);
    std::ranges::sort(out.explicitTargetFiles);
    std::ranges::sort(out.namedTargetFiles);
    out.rootNamespaceDirs.erase(std::ranges::unique(out.rootNamespaceDirs).begin(), out.rootNamespaceDirs.end());
    out.lookupDirs.erase(std::ranges::unique(out.lookupDirs).begin(), out.lookupDirs.end());
    out.explicitTargetFiles.erase(std::ranges::unique(out.explicitTargetFiles).begin(), out.explicitTargetFiles.end());
    out.namedTargetFiles.erase(std::ranges::unique(out.namedTargetFiles).begin(), out.namedTargetFiles.end());
    return out;
}

}  // namespace llvmdsdl
