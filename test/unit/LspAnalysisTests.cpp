//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "llvmdsdl/LSP/Analysis.h"
#include "llvmdsdl/LSP/DocumentStore.h"
#include "llvmdsdl/LSP/ServerConfig.h"

#include "UnitTests.h"

namespace
{

std::filesystem::path makeUniqueTempDir()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("llvmdsdl-lsp-analysis-" + std::to_string(now));
}

bool writeTextFile(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary);
    if (!out.good())
    {
        return false;
    }
    out << text;
    return out.good();
}

bool hasErrorDiagnostics(const llvmdsdl::lsp::AnalysisResult& result)
{
    for (const auto& [_, diagnostics] : result.diagnosticsByUri)
    {
        for (const auto& diagnostic : diagnostics)
        {
            if (diagnostic.level == llvmdsdl::DiagnosticLevel::Error)
            {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

bool runLspAnalysisTests()
{
    const std::filesystem::path tmpRoot = makeUniqueTempDir();
    const std::filesystem::path rootDir = tmpRoot / "demo";

    std::error_code ec;
    std::filesystem::create_directories(rootDir, ec);
    if (ec)
    {
        std::cerr << "failed to create temporary root dir: " << ec.message() << "\n";
        return false;
    }

    const std::filesystem::path typeAPath = rootDir / "TypeA.1.0.dsdl";
    const std::filesystem::path typeBPath = rootDir / "TypeB.1.0.dsdl";
    if (!writeTextFile(typeAPath, "uint8 value\n@sealed\n") ||
        !writeTextFile(typeBPath, "demo.TypeA.1.0 member\n@sealed\n"))
    {
        std::cerr << "failed to write temporary test definitions\n";
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }

    llvmdsdl::lsp::AnalysisPipeline pipeline;
    llvmdsdl::lsp::ServerConfig     config;
    llvmdsdl::lsp::DocumentStore    documents;
    config.rootNamespaceDirs.push_back(rootDir.string());

    const llvmdsdl::lsp::AnalysisResult first = pipeline.run(config, documents);
    if (first.snapshotVersion == 0 || hasErrorDiagnostics(first))
    {
        std::cerr << "expected first analysis run to produce a clean snapshot\n";
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }
    if (pipeline.stats().fullRebuildCount == 0 || pipeline.stats().lastDirtyDefinitionCount < 2 ||
        pipeline.stats().lastImpactedDefinitionCount < 2)
    {
        std::cerr << "unexpected full rebuild stats from first run\n";
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }

    const std::string typeAUri = llvmdsdl::lsp::normalizedPathToFileUri(typeAPath.string());
    documents.open(typeAUri, "demo.DoesNotExist.1.0 broken\n@sealed\n", 2);

    const llvmdsdl::lsp::AnalysisResult second = pipeline.run(config, documents);
    if (!hasErrorDiagnostics(second))
    {
        std::cerr << "expected diagnostics after invalid overlay change\n";
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }
    if (!second.diagnosticsByUri.contains(typeAUri))
    {
        std::cerr << "expected changed document diagnostics to be mapped by URI\n";
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }
    if (pipeline.stats().incrementalRebuildCount == 0 || pipeline.stats().lastDirtyDefinitionCount != 1 ||
        pipeline.stats().lastImpactedDefinitionCount < 2)
    {
        std::cerr << "expected incremental invalidation to rebuild changed file plus dependents\n";
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }
    if (!pipeline.isCurrentSnapshot(second.snapshotVersion) || pipeline.isCurrentSnapshot(second.snapshotVersion - 1))
    {
        std::cerr << "snapshot current-version tracking is inconsistent\n";
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }

    if (!documents.applyFullTextChange(typeAUri, "uint8 value\n@sealed\n", 3))
    {
        std::cerr << "failed to apply follow-up overlay change\n";
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }
    const llvmdsdl::lsp::AnalysisResult third = pipeline.run(config, documents);
    if (hasErrorDiagnostics(third))
    {
        std::cerr << "expected diagnostics to clear after overlay repair\n";
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }

    config.enableMlirSnapshot                  = true;
    const llvmdsdl::lsp::AnalysisResult fourth = pipeline.run(config, documents);
    if (!fourth.mlirSnapshot.has_value() || !fourth.mlirSnapshot->contains("module"))
    {
        std::cerr << "expected optional MLIR snapshot when enabled\n";
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }
    if (!pipeline.latestMlirSnapshot().has_value() || pipeline.isCurrentSnapshot(third.snapshotVersion))
    {
        std::cerr << "expected latest snapshot to supersede previous versions\n";
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }

    std::filesystem::remove_all(tmpRoot, ec);
    return true;
}
