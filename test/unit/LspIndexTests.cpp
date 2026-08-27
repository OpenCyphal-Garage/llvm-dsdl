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
#include <thread>

#include "llvmdsdl/LSP/Index.h"

#include "UnitTests.h"

namespace
{

std::filesystem::path makeUniqueTempDir()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("llvmdsdl-lsp-index-" + std::to_string(now));
}

bool writeTextFile(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.good())
    {
        return false;
    }
    out << text;
    return out.good();
}

llvmdsdl::lsp::IndexFileShard makeShard(const std::string&  path,
                                        const std::string&  uri,
                                        const std::string&  typeKey,
                                        const std::string&  shortName,
                                        const std::uint64_t snapshotVersion)
{
    llvmdsdl::lsp::IndexFileShard shard;
    shard.metadata.filePath        = path;
    shard.metadata.sourceUri       = uri;
    shard.metadata.textHash        = std::hash<std::string>{}(typeKey);
    shard.metadata.snapshotVersion = snapshotVersion;

    shard.symbols.push_back(llvmdsdl::lsp::IndexSymbolRecord{
        "type:" + typeKey,
        shortName,
        typeKey,
        "demo",
        "message",
        23,
        path,
        llvmdsdl::lsp::IndexLocation{uri, 0, 0, static_cast<std::uint32_t>(shortName.size())},
    });

    shard.references.push_back(llvmdsdl::lsp::IndexReferenceRecord{
        "type:" + typeKey,
        path,
        llvmdsdl::lsp::IndexLocation{uri, 0, 0, static_cast<std::uint32_t>(shortName.size())},
        true,
    });

    return shard;
}

}  // namespace

bool runLspIndexTests()
{
    const std::filesystem::path tmpRoot  = makeUniqueTempDir();
    const std::filesystem::path cacheDir = tmpRoot / "cache";

    std::error_code ec;
    std::filesystem::create_directories(cacheDir, ec);
    if (ec)
    {
        std::cerr << "failed to create temporary index cache dir: " << ec.message() << "\n";
        return false;
    }

    llvmdsdl::lsp::IndexStorage const storage(cacheDir.string());

    const std::string filePathA = (tmpRoot / "demo" / "TypeA.1.0.dsdl").lexically_normal().string();
    const std::string fileUriA  = std::string("file://") + filePathA;
    const auto        shardA    = makeShard(filePathA, fileUriA, "demo.TypeA.1.0", "TypeA", 1);

    std::string writeError;
    if (!storage.writeShard(shardA, &writeError))
    {
        std::cerr << "failed to write shard A: " << writeError << "\n";
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }

    std::string readError;
    const auto  loadedA = storage.readShardForPath(filePathA, &readError);
    if (!loadedA.has_value() || loadedA->symbols.empty() || loadedA->metadata.filePath != filePathA)
    {
        std::cerr << "failed to read back shard A: " << readError << "\n";
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }

    const std::vector<llvmdsdl::lsp::IndexFileShard> initialShards = storage.loadAllShards();
    if (initialShards.size() != 1)
    {
        std::cerr << "expected one initial shard\n";
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }

    llvmdsdl::lsp::WorkspaceIndex workspaceIndex;
    workspaceIndex.replaceAll(initialShards);
    const auto symbolResults = workspaceIndex.querySymbols("TypeA", 10);
    if (symbolResults.empty() || symbolResults.front().qualifiedName != "demo.TypeA.1.0")
    {
        std::cerr << "workspace index query failed to return expected symbol\n";
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }

    llvmdsdl::lsp::IndexManager manager(cacheDir.string());
    manager.scheduleRebuild(1, {shardA});
    if (!manager.waitForSnapshot(1, std::chrono::seconds(2)))
    {
        std::cerr << "timed out waiting for snapshot 1 indexing\n";
        manager.shutdown();
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }

    const std::string filePathB = (tmpRoot / "demo" / "TypeB.1.0.dsdl").lexically_normal().string();
    const std::string fileUriB  = std::string("file://") + filePathB;
    const auto        shardB    = makeShard(filePathB, fileUriB, "demo.TypeB.1.0", "TypeB", 2);

    manager.scheduleRebuild(2, {shardA, shardB});
    manager.scheduleRebuild(3, {shardB});
    if (!manager.waitForSnapshot(3, std::chrono::seconds(3)))
    {
        std::cerr << "timed out waiting for snapshot 3 indexing\n";
        manager.shutdown();
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }

    const auto managerResults = manager.workspaceSymbols("TypeB", 10);
    if (managerResults.empty() || managerResults.front().qualifiedName != "demo.TypeB.1.0")
    {
        std::cerr << "index manager query did not return expected TypeB symbol\n";
        manager.shutdown();
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }

    const llvmdsdl::lsp::IndexManagerStats stats = manager.stats();
    if (stats.completedJobs == 0 || stats.lastCompletedSnapshotVersion < 3)
    {
        std::cerr << "index manager stats did not advance as expected\n";
        manager.shutdown();
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }

    const std::filesystem::path invalidShard = cacheDir / "invalid.index.json";
    if (!writeTextFile(invalidShard, "{not-json"))
    {
        std::cerr << "failed to write invalid shard fixture\n";
        manager.shutdown();
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }

    const llvmdsdl::lsp::IndexRepairReport repair = manager.verifyAndRepair(true);
    if (repair.invalidFiles == 0 || repair.removedFiles == 0)
    {
        std::cerr << "expected verify/repair to remove invalid shard fixture\n";
        manager.shutdown();
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    }

    manager.shutdown();
    std::filesystem::remove_all(tmpRoot, ec);
    return true;
}
