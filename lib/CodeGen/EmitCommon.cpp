//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared emission helpers for file-write policy and type-key selection.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/EmitCommon.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <system_error>

namespace llvmdsdl
{

namespace
{

std::filesystem::perms permsFromMode(const std::uint32_t mode)
{
    using Perm = std::filesystem::perms;
    Perm out   = Perm::none;

    if ((mode & 0400U) != 0U)
    {
        out |= Perm::owner_read;
    }
    if ((mode & 0200U) != 0U)
    {
        out |= Perm::owner_write;
    }
    if ((mode & 0100U) != 0U)
    {
        out |= Perm::owner_exec;
    }
    if ((mode & 0040U) != 0U)
    {
        out |= Perm::group_read;
    }
    if ((mode & 0020U) != 0U)
    {
        out |= Perm::group_write;
    }
    if ((mode & 0010U) != 0U)
    {
        out |= Perm::group_exec;
    }
    if ((mode & 0004U) != 0U)
    {
        out |= Perm::others_read;
    }
    if ((mode & 0002U) != 0U)
    {
        out |= Perm::others_write;
    }
    if ((mode & 0001U) != 0U)
    {
        out |= Perm::others_exec;
    }

    return out;
}

std::string absoluteNormalizedPath(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto      absolute = std::filesystem::absolute(path, ec);
    if (ec)
    {
        return path.lexically_normal().string();
    }
    return absolute.lexically_normal().string();
}

void recordOutputPath(const std::filesystem::path&    path,
                      const EmitWritePolicy&          policy,
                      const std::vector<std::string>& requiredTypeKeys)
{
    const std::string normalizedPath = absoluteNormalizedPath(path);

    if (policy.recordedOutputs == nullptr)
    {
        if (policy.recordedOutputRequiredTypeKeys != nullptr && !requiredTypeKeys.empty())
        {
            policy.recordedOutputRequiredTypeKeys->insert_or_assign(normalizedPath, requiredTypeKeys);
        }
    }
    else
    {
        policy.recordedOutputs->push_back(normalizedPath);
        if (policy.recordedOutputRequiredTypeKeys != nullptr && !requiredTypeKeys.empty())
        {
            policy.recordedOutputRequiredTypeKeys->insert_or_assign(normalizedPath, requiredTypeKeys);
        }
    }
}

std::string escapeMakeToken(llvm::StringRef text)
{
    std::string out;
    out.reserve(text.size());

    for (const char c : text)
    {
        switch (c)
        {
        case '\\':
            out.append("\\\\");
            break;
        case ' ':
            out.append("\\ ");
            break;
        case '\t':
            out.push_back('\\');
            out.push_back('\t');
            break;
        case '#':
            out.append("\\#");
            break;
        case '$':
            out.append("$$");
            break;
        case ':':
            out.append("\\:");
            break;
        default:
            out.push_back(c);
            break;
        }
    }

    return out;
}

std::string renderMakeRuleFromPreparedDeps(llvm::StringRef target, const std::vector<std::string>& preparedDeps)
{
    std::string out;
    out += escapeMakeToken(target);
    out += ':';

    for (const auto& dep : preparedDeps)
    {
        out.push_back(' ');
        out += escapeMakeToken(dep);
    }
    out.push_back('\n');
    return out;
}

}  // namespace

std::string definitionTypeKey(const DiscoveredDefinition& info)
{
    return info.fullName + ":" + std::to_string(info.majorVersion) + ":" + std::to_string(info.minorVersion);
}

std::string deprecationNotice(const std::string&  fullName,
                              const std::uint32_t majorVersion,
                              const std::uint32_t minorVersion)
{
    return "Deprecated: " + fullName + "." + std::to_string(majorVersion) + "." + std::to_string(minorVersion) +
           " is marked @deprecated in its DSDL definition and is scheduled for removal; "
           "migrate to a newer version of this type.";
}

std::vector<std::string> wrapCommentText(const std::string& text, const std::size_t columns)
{
    std::vector<std::string> lines;
    std::string              current;

    std::size_t pos = 0;
    while (pos < text.size())
    {
        const std::size_t wordStart = text.find_first_not_of(" \t", pos);
        if (wordStart == std::string::npos)
        {
            break;
        }
        std::size_t wordEnd = text.find_first_of(" \t", wordStart);
        if (wordEnd == std::string::npos)
        {
            wordEnd = text.size();
        }
        const std::string word = text.substr(wordStart, wordEnd - wordStart);
        pos                    = wordEnd;

        if (current.empty())
        {
            current = word;
        }
        else if (current.size() + 1U + word.size() <= columns)
        {
            current += " " + word;
        }
        else
        {
            lines.push_back(current);
            current = word;
        }
    }

    if (!current.empty())
    {
        lines.push_back(current);
    }
    if (lines.empty())
    {
        lines.push_back(std::string{});
    }
    return lines;
}

AttachedDoc docWithDeprecationNotice(const AttachedDoc&  doc,
                                     const bool          deprecated,
                                     const std::string&  fullName,
                                     const std::uint32_t majorVersion,
                                     const std::uint32_t minorVersion)
{
    if (!deprecated)
    {
        return doc;
    }

    // The location is synthetic: doc emission renders only the text of each line, and this line has no
    // source token of its own.
    const SourceLocation location{"<deprecated>", 1, 1};

    AttachedDoc out = doc;
    if (!out.lines.empty())
    {
        out.lines.push_back(AttachedDocLine{location, ""});
    }
    // Wrapped, not pushed as one line: the notice is a full sentence and would otherwise be several
    // times wider than the surrounding documentation, which is wrapped by whoever authored the DSDL.
    for (const auto& line : wrapCommentText(deprecationNotice(fullName, majorVersion, minorVersion)))
    {
        out.lines.push_back(AttachedDocLine{location, line});
    }
    return out;
}

std::unordered_set<std::string> makeTypeKeySet(const std::vector<std::string>& typeKeys)
{
    return std::unordered_set<std::string>(typeKeys.begin(), typeKeys.end());
}

bool shouldEmitDefinition(const DiscoveredDefinition&            info,
                          const std::unordered_set<std::string>& selectedTypeKeys,
                          const SupportGeneration                mode)
{
    if (!shouldEmitTypes(mode))
    {
        return false;
    }
    if (selectedTypeKeys.empty())
    {
        return true;
    }
    return selectedTypeKeys.contains(definitionTypeKey(info));
}

bool shouldEmitSupport(const SupportGeneration mode, const bool anyTypeEmitted)
{
    switch (mode)
    {
    case SupportGeneration::Never:
        return false;
    case SupportGeneration::Always:
    case SupportGeneration::Only:
        return true;
    case SupportGeneration::AsNeeded:
        break;
    }
    return anyTypeEmitted;
}

bool shouldEmitTypes(const SupportGeneration mode)
{
    return mode != SupportGeneration::Only;
}

llvm::Error writeGeneratedFile(const std::filesystem::path&    path,
                               llvm::StringRef                 content,
                               const EmitWritePolicy&          policy,
                               const std::vector<std::string>& requiredTypeKeys)
{
    recordOutputPath(path, policy, requiredTypeKeys);

    if (policy.dryRun)
    {
        return llvm::Error::success();
    }

    std::error_code ec;

    const auto parent = path.parent_path();
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, ec);
        if (ec)
        {
            return llvm::createStringError(ec, "failed to create output directory %s", parent.string().c_str());
        }
    }

    const bool exists = std::filesystem::exists(path, ec);
    if (ec)
    {
        return llvm::createStringError(ec, "failed to stat output path %s", path.string().c_str());
    }
    if (exists)
    {
        if (policy.noOverwrite)
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "refusing to overwrite existing output file: %s",
                                           path.string().c_str());
        }
        const bool removed = std::filesystem::remove(path, ec);
        if (ec || !removed)
        {
            return llvm::createStringError(ec ? ec : llvm::inconvertibleErrorCode(),
                                           "failed to remove existing output file %s",
                                           path.string().c_str());
        }
    }

    llvm::raw_fd_ostream os(path.string(), ec, llvm::sys::fs::OF_Text);
    if (ec)
    {
        return llvm::createStringError(ec, "failed to open %s", path.string().c_str());
    }
    os << content;
    os.close();

    std::filesystem::permissions(path, permsFromMode(policy.fileMode), std::filesystem::perm_options::replace, ec);
    if (ec)
    {
        return llvm::createStringError(ec, "failed to set mode on %s", path.string().c_str());
    }

    return llvm::Error::success();
}

std::string renderMakeDepfile(const std::string& target, const std::vector<std::string>& deps)
{
    std::vector<std::string> normalizedDeps = deps;
    std::sort(normalizedDeps.begin(), normalizedDeps.end());
    normalizedDeps.erase(std::unique(normalizedDeps.begin(), normalizedDeps.end()), normalizedDeps.end());
    return renderMakeRuleFromPreparedDeps(target, normalizedDeps);
}

llvm::Error writeDepfileForGeneratedOutput(const std::filesystem::path&    outputPath,
                                           const std::vector<std::string>& deps,
                                           const EmitWritePolicy&          policy)
{
    const std::filesystem::path depfilePath = outputPath.string() + ".d";

    std::vector<std::string> normalizedDeps;
    normalizedDeps.reserve(deps.size());
    for (const auto& dep : deps)
    {
        normalizedDeps.push_back(absoluteNormalizedPath(dep));
    }

    const std::string depfileContent = renderMakeDepfile(absoluteNormalizedPath(outputPath), normalizedDeps);
    return writeGeneratedFile(depfilePath, depfileContent, policy);
}

namespace
{

/// @brief First line of a prune manifest. Version it so a format change is a clean refusal.
constexpr llvm::StringLiteral kPruneManifestHeader = "# llvm-dsdl prune manifest v1";

/// @brief True when @p candidate is lexically inside @p root.
///
/// Lexical rather than filesystem-resolved on purpose: this runs against paths read from a file
/// that may name things which no longer exist, and `weakly_canonical` on a dangling path is not
/// obliged to be useful. Both sides are normalised first so `a/../b` cannot walk out.
bool isInside(const std::filesystem::path& root, const std::filesystem::path& candidate)
{
    const auto normalizedRoot      = root.lexically_normal();
    const auto normalizedCandidate = candidate.lexically_normal();
    const auto relative            = normalizedCandidate.lexically_relative(normalizedRoot);
    if (relative.empty() || relative.native() == ".")
    {
        return false;
    }
    return *relative.begin() != "..";
}

std::vector<std::string> readPruneManifest(const std::filesystem::path& manifestPath)
{
    std::ifstream stream(manifestPath);
    if (!stream)
    {
        return {};
    }
    std::string line;
    if (!std::getline(stream, line) || llvm::StringRef(line).trim() != kPruneManifestHeader)
    {
        // An unrecognised manifest is treated as absent rather than as an error. The cost of being
        // wrong is one stale file surviving a format change, which the next run cleans up; the cost
        // of failing the build is a compiler upgrade breaking every configured tree.
        return {};
    }
    std::vector<std::string> entries;
    while (std::getline(stream, line))
    {
        const auto trimmed = llvm::StringRef(line).trim();
        if (!trimmed.empty())
        {
            entries.emplace_back(trimmed.str());
        }
    }
    return entries;
}

}  // namespace

llvm::Error pruneStaleOutputs(const std::filesystem::path&    manifestPath,
                              const std::filesystem::path&    outputRoot,
                              const std::vector<std::string>& currentOutputs,
                              PruneReport*                    report)
{
    const std::vector<std::string> previous = readPruneManifest(manifestPath);

    std::set<std::string> current;
    for (const auto& output : currentOutputs)
    {
        current.insert(std::filesystem::path(output).lexically_normal().string());
    }

    // Deepest first, so a directory is considered only after everything under it has gone.
    std::vector<std::filesystem::path> stale;
    for (const auto& entry : previous)
    {
        const auto path = std::filesystem::path(entry).lexically_normal();
        if (current.count(path.string()) != 0U)
        {
            continue;
        }
        if (!isInside(outputRoot, path))
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "prune manifest %s names %s, which is outside the output "
                                           "directory %s; refusing to remove it",
                                           manifestPath.string().c_str(),
                                           path.string().c_str(),
                                           outputRoot.string().c_str());
        }
        stale.push_back(path);
    }
    std::sort(stale.begin(), stale.end(), [](const auto& a, const auto& b) {
        return a.string().size() > b.string().size();
    });

    std::set<std::filesystem::path> touchedDirectories;
    for (const auto& path : stale)
    {
        std::error_code ec;
        // Generated files are written read-only by default (--file-mode), which does not prevent
        // removal on POSIX -- the directory's permissions govern that -- so no chmod dance here.
        const bool removed = std::filesystem::remove(path, ec);
        if (ec)
        {
            return llvm::createStringError(ec, "failed to remove stale output %s", path.string().c_str());
        }
        if (removed && report != nullptr)
        {
            report->removedFiles.push_back(path.string());
        }
        touchedDirectories.insert(path.parent_path());
    }

    // Bottom-up: removing a leaf may empty its parent, which may empty its parent.
    std::vector<std::filesystem::path> candidates(touchedDirectories.begin(), touchedDirectories.end());
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.string().size() > b.string().size();
    });
    for (std::size_t index = 0; index < candidates.size(); ++index)
    {
        const auto& directory = candidates[index];
        if (!isInside(outputRoot, directory))
        {
            continue;
        }
        std::error_code ec;
        if (!std::filesystem::is_empty(directory, ec) || ec)
        {
            continue;
        }
        if (std::filesystem::remove(directory, ec) && !ec)
        {
            if (report != nullptr)
            {
                report->removedDirectories.push_back(directory.string());
            }
            candidates.push_back(directory.parent_path());
        }
    }

    std::error_code ec;
    const auto      manifestParent = manifestPath.parent_path();
    if (!manifestParent.empty())
    {
        std::filesystem::create_directories(manifestParent, ec);
        if (ec)
        {
            return llvm::createStringError(ec,
                                           "failed to create prune manifest directory %s",
                                           manifestParent.string().c_str());
        }
    }

    std::ofstream out(manifestPath, std::ios::trunc);
    if (!out)
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "failed to write prune manifest %s",
                                       manifestPath.string().c_str());
    }
    out << kPruneManifestHeader.str() << "\n";
    for (const auto& entry : current)
    {
        out << entry << "\n";
    }
    if (!out)
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "failed to write prune manifest %s",
                                       manifestPath.string().c_str());
    }
    return llvm::Error::success();
}

llvm::Error writeDepfileForGeneratedOutputPrepared(const std::filesystem::path&    outputPath,
                                                   const std::vector<std::string>& normalizedSortedDedupDeps,
                                                   const EmitWritePolicy&          policy)
{
    const std::filesystem::path depfilePath = outputPath.string() + ".d";
    const std::string           depfileContent =
        renderMakeRuleFromPreparedDeps(absoluteNormalizedPath(outputPath), normalizedSortedDedupDeps);
    return writeGeneratedFile(depfilePath, depfileContent, policy);
}

}  // namespace llvmdsdl
