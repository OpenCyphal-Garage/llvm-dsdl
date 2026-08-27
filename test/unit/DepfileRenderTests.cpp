//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <iterator>
#include <llvm/Support/Error.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "llvmdsdl/CodeGen/EmitCommon.h"

#include "UnitTests.h"

namespace
{

std::filesystem::path makeUniqueTempDir()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("llvmdsdl-depfile-tests-" + std::to_string(now));
}

std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return {};
    }
    return {(std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()};
}

bool endsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

std::size_t countOccurrences(std::string_view text, std::string_view needle)
{
    if (needle.empty())
    {
        return 0;
    }
    std::size_t count = 0;
    std::size_t pos   = 0;
    while ((pos = text.find(needle, pos)) != std::string_view::npos)
    {
        ++count;
        pos += needle.size();
    }
    return count;
}

}  // namespace

bool runDepfileRenderTests()
{
    {
        const std::string              target        = "/tmp/out file:$.c";
        const std::vector<std::string> deps          = {"z path\\tab\t#.dsdl", "a:dep.dsdl", "z path\\tab\t#.dsdl"};
        const std::string              rendered      = llvmdsdl::renderMakeDepfile(target, deps);
        const std::string              escapedTarget = "/tmp/out\\ file\\:$$.c: ";
        const std::string              escapedDepA   = "a\\:dep.dsdl";
        const std::string              escapedDepZ   = "z\\ path\\\\tab\\\t\\#.dsdl";
        if (!rendered.starts_with(escapedTarget) || !rendered.ends_with("\n") ||
            countOccurrences(rendered, escapedDepA) != 1 || countOccurrences(rendered, escapedDepZ) != 1 ||
            rendered.find(escapedDepA) > rendered.find(escapedDepZ))
        {
            std::cerr << "renderMakeDepfile escaping/dedup output mismatch\n";
            return false;
        }
    }

    {
        const std::string rendered = llvmdsdl::renderMakeDepfile("/tmp/target", {});
        if (rendered != "/tmp/target:\n")
        {
            std::cerr << "renderMakeDepfile empty dependency formatting mismatch\n";
            return false;
        }
    }

    const std::filesystem::path tmpRoot = makeUniqueTempDir();
    std::error_code             ec;
    std::filesystem::create_directories(tmpRoot, ec);
    if (ec)
    {
        std::cerr << "failed to create temp depfile test dir: " << ec.message() << "\n";
        return false;
    }

    auto fail = [&](std::string_view message) {
        std::cerr << message << "\n";
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    };

    const std::filesystem::path depA = tmpRoot / "deps" / "a.dsdl";
    const std::filesystem::path depB = tmpRoot / "deps" / "b.dsdl";
    std::filesystem::create_directories(depA.parent_path(), ec);
    if (ec)
    {
        return fail("failed to create dependency fixture directory");
    }
    {
        std::ofstream const outA(depA);
        std::ofstream const outB(depB);
        if (!outA || !outB)
        {
            return fail("failed to create dependency fixture files");
        }
    }

    const std::filesystem::path outputPath = tmpRoot / "out dir" / "generated.c";
    std::vector<std::string>    recorded;

    llvmdsdl::EmitWritePolicy policy;
    policy.fileMode        = 0640U;
    policy.recordedOutputs = &recorded;

    const std::vector<std::string> deps = {depB.string(), depA.string(), depB.string()};
    if (auto err = llvmdsdl::writeDepfileForGeneratedOutput(outputPath, deps, policy))
    {
        std::cerr << "writeDepfileForGeneratedOutput failed unexpectedly: " << llvm::toString(std::move(err)) << "\n";
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }

    std::filesystem::path const depfilePath = outputPath.string() + ".d";
    if (!std::filesystem::exists(depfilePath, ec) || ec)
    {
        return fail("expected depfile output was not created");
    }

    const std::string              depfileContent = readTextFile(depfilePath);
    std::vector<std::string> const normalizedDeps = {
        std::filesystem::absolute(depA, ec).lexically_normal().string(),
        std::filesystem::absolute(depB, ec).lexically_normal().string(),
    };
    const std::string expectedDepfile =
        llvmdsdl::renderMakeDepfile(std::filesystem::absolute(outputPath, ec).lexically_normal().string(),
                                    normalizedDeps);
    if (depfileContent != expectedDepfile)
    {
        return fail("depfile content did not match expected normalized+escaped rule");
    }

    if (recorded.size() != 1 || !endsWith(recorded.front(), ".d"))
    {
        return fail("depfile write did not record one .d output path");
    }

    const std::filesystem::path     preparedOutputPath = tmpRoot / "prepared" / "generated_prepared.c";
    std::vector<std::string> const& preparedDeps       = normalizedDeps;
    if (auto err = llvmdsdl::writeDepfileForGeneratedOutputPrepared(preparedOutputPath, preparedDeps, policy))
    {
        std::cerr << "writeDepfileForGeneratedOutputPrepared failed unexpectedly: " << llvm::toString(std::move(err))
                  << "\n";
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }
    const std::filesystem::path preparedDepfilePath = preparedOutputPath.string() + ".d";
    const std::string           preparedDepfileText = readTextFile(preparedDepfilePath);
    const std::string           expectedPreparedDepfile =
        llvmdsdl::renderMakeDepfile(std::filesystem::absolute(preparedOutputPath, ec).lexically_normal().string(),
                                    normalizedDeps);
    if (preparedDepfileText != expectedPreparedDepfile)
    {
        return fail("prepared depfile writer content mismatch");
    }

    llvmdsdl::EmitWritePolicy noOverwritePolicy = policy;
    noOverwritePolicy.noOverwrite               = true;
    noOverwritePolicy.recordedOutputs           = nullptr;
    if (auto err = llvmdsdl::writeDepfileForGeneratedOutput(outputPath, deps, noOverwritePolicy))
    {
        llvm::consumeError(std::move(err));
    }
    else
    {
        return fail("expected no-overwrite policy to reject existing depfile");
    }

    std::vector<std::string>  dryRunRecorded;
    llvmdsdl::EmitWritePolicy dryRunPolicy;
    dryRunPolicy.dryRun                       = true;
    dryRunPolicy.recordedOutputs              = &dryRunRecorded;
    const std::filesystem::path dryOutputPath = tmpRoot / "dry" / "dry_generated.c";
    if (auto err = llvmdsdl::writeDepfileForGeneratedOutput(dryOutputPath, deps, dryRunPolicy))
    {
        std::cerr << "dry-run depfile write failed unexpectedly: " << llvm::toString(std::move(err)) << "\n";
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }

    std::filesystem::path const dryDepfilePath = dryOutputPath.string() + ".d";
    if (std::filesystem::exists(dryDepfilePath, ec))
    {
        return fail("dry-run depfile write should not create files");
    }
    if (dryRunRecorded.size() != 1 || !endsWith(dryRunRecorded.front(), ".d"))
    {
        return fail("dry-run depfile write should record one .d output path");
    }

    std::filesystem::remove_all(tmpRoot, ec);
    return true;
}
