//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Covers the distinction between a target the user named and one a folder sweep brought in.
///
/// Everything that decides what to *emit* reads `explicitTargetFiles`, where the two are
/// deliberately the same thing. A rule about what may be *dropped* by default cannot use that set,
/// because pointing at a folder would then look identical to naming a file inside it. These tests
/// pin the narrower set that tells them apart.
///
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <llvm/Support/Error.h>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "llvmdsdl/Frontend/TargetResolution.h"
#include "llvmdsdl/Support/Diagnostics.h"

#include "UnitTests.h"

namespace
{

std::filesystem::path makeUniqueTempDir()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("llvmdsdl-target-resolution-tests-" + std::to_string(now));
}

void writeDefinition(const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << "uint8 value\n@sealed\n";
}

/// @brief True when @p files contains an entry whose filename is @p name.
bool containsFileNamed(const std::vector<std::string>& files, const std::string& name)
{
    return std::ranges::any_of(files, [&name](const std::string& file) {
        return std::filesystem::path(file).filename().string() == name;
    });
}

}  // namespace

bool runTargetResolutionTests()
{
    const auto root = makeUniqueTempDir();
    struct Cleanup final
    {
        explicit Cleanup(std::filesystem::path removeAtScopeExit)
            : path(std::move(removeAtScopeExit))
        {
        }

        std::filesystem::path path;
        ~Cleanup()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }

        Cleanup(const Cleanup&)            = delete;
        Cleanup& operator=(const Cleanup&) = delete;
        Cleanup(Cleanup&&)                 = delete;
        Cleanup& operator=(Cleanup&&)      = delete;
    } const cleanup{root};

    const auto nsDir = root / "ns";
    writeDefinition(nsDir / "Alpha.1.0.dsdl");
    writeDefinition(nsDir / "Alpha.2.0.dsdl");
    writeDefinition(nsDir / "Beta.1.0.dsdl");

    llvmdsdl::DiagnosticEngine           diagnostics;
    llvmdsdl::TargetResolveOptions const options;

    // A folder target sweeps. Everything under it is targeted, and nothing under it was named.
    {
        auto resolved = llvmdsdl::resolveTargets({nsDir.string()}, options, diagnostics);
        if (!resolved)
        {
            llvm::consumeError(resolved.takeError());
            std::cerr << "folder target should resolve\n";
            return false;
        }
        if (resolved->explicitTargetFiles.size() != 3U)
        {
            std::cerr << "folder target should expand to all three definitions, got "
                      << resolved->explicitTargetFiles.size() << "\n";
            return false;
        }
        if (!resolved->namedTargetFiles.empty())
        {
            std::cerr << "a folder target names nothing individually, got " << resolved->namedTargetFiles.size()
                      << " named file(s)\n";
            return false;
        }
    }

    // A file target names exactly itself, and is targeted like any other. A bare file path cannot
    // infer its own root namespace, so it comes with a lookup root the way a user would pass one.
    {
        llvmdsdl::TargetResolveOptions withLookup = options;
        withLookup.lookupDirs.push_back(root.string());

        const auto alpha1   = (nsDir / "Alpha.1.0.dsdl").string();
        auto       resolved = llvmdsdl::resolveTargets({alpha1}, withLookup, diagnostics);
        if (!resolved)
        {
            llvm::consumeError(resolved.takeError());
            std::cerr << "file target should resolve\n";
            return false;
        }
        if (resolved->namedTargetFiles.size() != 1U || !containsFileNamed(resolved->namedTargetFiles, "Alpha.1.0.dsdl"))
        {
            std::cerr << "a file target should name exactly itself\n";
            return false;
        }
        if (!containsFileNamed(resolved->explicitTargetFiles, "Alpha.1.0.dsdl"))
        {
            std::cerr << "a named file is still an explicit target\n";
            return false;
        }
    }

    // Colon syntax names one file under a root, and the root does not sweep with it.
    {
        const auto token    = root.string() + ":ns/Alpha.1.0.dsdl";
        auto       resolved = llvmdsdl::resolveTargets({token}, options, diagnostics);
        if (!resolved)
        {
            llvm::consumeError(resolved.takeError());
            std::cerr << "colon-syntax target should resolve\n";
            return false;
        }
        if (resolved->namedTargetFiles.size() != 1U || !containsFileNamed(resolved->namedTargetFiles, "Alpha.1.0.dsdl"))
        {
            std::cerr << "colon syntax should name exactly the file after the colon\n";
            return false;
        }
    }

    // The case the whole distinction exists for: a folder plus one named version inside it. Both
    // versions are targeted; only the named one is named.
    {
        const auto alpha1   = (nsDir / "Alpha.1.0.dsdl").string();
        auto       resolved = llvmdsdl::resolveTargets({nsDir.string(), alpha1}, options, diagnostics);
        if (!resolved)
        {
            llvm::consumeError(resolved.takeError());
            std::cerr << "folder plus named file should resolve\n";
            return false;
        }
        if (resolved->explicitTargetFiles.size() != 3U)
        {
            std::cerr << "naming a file already swept in should not duplicate it, got "
                      << resolved->explicitTargetFiles.size() << "\n";
            return false;
        }
        if (resolved->namedTargetFiles.size() != 1U || !containsFileNamed(resolved->namedTargetFiles, "Alpha.1.0.dsdl"))
        {
            std::cerr << "only the file named alongside the folder should be named\n";
            return false;
        }
        if (containsFileNamed(resolved->namedTargetFiles, "Alpha.2.0.dsdl") ||
            containsFileNamed(resolved->namedTargetFiles, "Beta.1.0.dsdl"))
        {
            std::cerr << "the folder's other definitions were swept, not named\n";
            return false;
        }
    }

    // Named targets are a subset of explicit ones, always. Nothing downstream should have to
    // consider a named file that is not also targeted.
    {
        const auto alpha1   = (nsDir / "Alpha.1.0.dsdl").string();
        auto       resolved = llvmdsdl::resolveTargets({nsDir.string(), alpha1}, options, diagnostics);
        if (!resolved)
        {
            llvm::consumeError(resolved.takeError());
            return false;
        }
        for (const auto& named : resolved->namedTargetFiles)
        {
            if (std::find(resolved->explicitTargetFiles.begin(), resolved->explicitTargetFiles.end(), named) ==
                resolved->explicitTargetFiles.end())
            {
                std::cerr << "named target is not an explicit target: " << named << "\n";
                return false;
            }
        }
    }

    return true;
}
