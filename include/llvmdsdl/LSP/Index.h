//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Workspace indexing model and persistence helpers for the DSDL LSP server.
///
/// The index stores per-file symbol/reference shards, supports deterministic
/// serialization, and provides background maintenance/query surfaces for
/// `workspace/symbol`.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_LSP_INDEX_H
#define LLVMDSDL_LSP_INDEX_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace llvmdsdl::lsp
{

/// @brief On-disk schema version for persisted index shards.
///
/// Version 2 encodes `text_hash` as a decimal string so a full 64-bit
/// `std::size_t` hash round-trips without overflowing JSON's signed integer.
inline constexpr std::uint32_t LspIndexSchemaVersion = 2;

/// @brief Source location payload used by indexed symbols/references.
struct IndexLocation final
{
    /// @brief Source document URI.
    std::string uri;

    /// @brief Zero-based line.
    std::uint32_t line{0};

    /// @brief Zero-based start column.
    std::uint32_t character{0};

    /// @brief Token length in bytes/UTF-8 code units.
    std::uint32_t length{0};
};

/// @brief Indexed symbol record persisted inside one file shard.
struct IndexSymbolRecord final
{
    /// @brief Stable symbol identifier used for references.
    std::string usr;

    /// @brief Display symbol name.
    std::string name;

    /// @brief Fully qualified symbol name.
    std::string qualifiedName;

    /// @brief Optional container (namespace/type) display name.
    std::string containerName;

    /// @brief Additional detail text.
    std::string detail;

    /// @brief LSP SymbolKind numeric value.
    std::int64_t kind{13};

    /// @brief Source-file normalized path.
    std::string filePath;

    /// @brief Source location.
    IndexLocation location;
};

/// @brief Indexed reference record persisted inside one file shard.
struct IndexReferenceRecord final
{
    /// @brief Target symbol USR.
    std::string targetUsr;

    /// @brief Source-file normalized path.
    std::string filePath;

    /// @brief Source location.
    IndexLocation location;

    /// @brief Indicates declaration site when true.
    bool isDeclaration{false};
};

/// @brief Metadata block for one persisted file shard.
struct IndexShardMetadata final
{
    /// @brief Schema version of this shard.
    std::uint32_t schemaVersion{LspIndexSchemaVersion};

    /// @brief Normalized source path this shard represents.
    std::string filePath;

    /// @brief Source URI used by editor-facing payloads.
    std::string sourceUri;

    /// @brief Hash of the source text used to build this shard.
    std::size_t textHash{0};

    /// @brief Analysis snapshot version associated with this shard.
    std::uint64_t snapshotVersion{0};
};

/// @brief Complete per-file index shard persisted to disk.
struct IndexFileShard final
{
    /// @brief Shard metadata.
    IndexShardMetadata metadata;

    /// @brief Symbol rows for this file.
    std::vector<IndexSymbolRecord> symbols;

    /// @brief Reference rows for this file.
    std::vector<IndexReferenceRecord> references;
};

/// @brief `workspace/symbol` query result payload.
struct WorkspaceSymbolResult final
{
    /// @brief Symbol USR.
    std::string usr;

    /// @brief Symbol display name.
    std::string name;

    /// @brief Fully qualified symbol name.
    std::string qualifiedName;

    /// @brief Optional container display name.
    std::string containerName;

    /// @brief Detail text.
    std::string detail;

    /// @brief LSP SymbolKind numeric value.
    std::int64_t kind{13};

    /// @brief Source URI.
    std::string uri;

    /// @brief Zero-based line.
    std::uint32_t line{0};

    /// @brief Zero-based column.
    std::uint32_t character{0};

    /// @brief Symbol token length.
    std::uint32_t length{0};

    /// @brief Internal ranking score used for sorting.
    double score{0.0};
};

/// @brief Result from index consistency verification/repair.
struct IndexRepairReport final
{
    /// @brief Number of shard files inspected.
    std::size_t inspectedFiles{0};

    /// @brief Number of invalid shard files found.
    std::size_t invalidFiles{0};

    /// @brief Number of duplicate logical shards removed.
    std::size_t duplicateFiles{0};

    /// @brief Number of files removed during repair.
    std::size_t removedFiles{0};

    /// @brief Absolute shard file paths removed by repair.
    std::vector<std::string> removedShardPaths;
};

/// @brief Persistent shard storage helper.
class IndexStorage final
{
public:
    /// @brief Constructs storage bound to a cache directory.
    /// @param[in] cacheDirectory Absolute or relative cache directory.
    explicit IndexStorage(const std::string& cacheDirectory = {});

    /// @brief Returns configured cache directory.
    /// @return Cache directory path string.
    [[nodiscard]] const std::string& cacheDirectory() const
    {
        return cacheDirectory_;
    }

    /// @brief Writes one shard atomically to disk.
    /// @param[in] shard Shard payload to persist.
    /// @param[out] errorMessage Optional failure message.
    /// @return `true` when write succeeds.
    [[nodiscard]] bool writeShard(const IndexFileShard& shard, std::string* errorMessage = nullptr) const;

    /// @brief Loads shard for a normalized file path.
    /// @param[in] normalizedPath Source path key.
    /// @param[out] errorMessage Optional failure message.
    /// @return Shard when present and parseable.
    [[nodiscard]] std::optional<IndexFileShard> readShardForPath(const std::string& normalizedPath,
                                                                 std::string*       errorMessage = nullptr) const;

    /// @brief Loads and parses all shards from cache directory.
    /// @param[out] invalidShardPaths Optional invalid shard paths encountered.
    /// @return Parsed shards.
    [[nodiscard]] std::vector<IndexFileShard> loadAllShards(
        std::vector<std::string>* invalidShardPaths = nullptr) const;

    /// @brief Removes shard file associated with a normalized source path.
    /// @param[in] normalizedPath Source path key.
    /// @return `true` when a file was removed.
    [[nodiscard]] bool removeShardForPath(const std::string& normalizedPath) const;

    /// @brief Validates shard set and optionally removes invalid entries.
    /// @param[in] removeInvalidShards Remove invalid/duplicate shard files when true.
    /// @return Verification/repair report.
    [[nodiscard]] IndexRepairReport verifyAndRepair(bool removeInvalidShards) const;

private:
    std::string cacheDirectory_;
};

/// @brief In-memory merged workspace symbol index.
class WorkspaceIndex final
{
public:
    /// @brief Replaces the complete in-memory index with given shards.
    /// @param[in] shards Workspace shard set.
    void replaceAll(std::vector<IndexFileShard> shards);

    /// @brief Queries ranked workspace symbols.
    /// @param[in] query Free-text query.
    /// @param[in] limit Maximum result rows.
    /// @return Sorted symbol rows.
    [[nodiscard]] std::vector<WorkspaceSymbolResult> querySymbols(const std::string& query, std::size_t limit) const;

    /// @brief Returns indexed file count.
    /// @return Number of indexed files.
    [[nodiscard]] std::size_t fileCount() const
    {
        return shardsByPath_.size();
    }

    /// @brief Returns indexed symbol count.
    /// @return Number of indexed symbols.
    [[nodiscard]] std::size_t symbolCount() const
    {
        return flattenedSymbols_.size();
    }

    /// @brief Returns currently indexed source paths.
    /// @return Source-path list.
    [[nodiscard]] std::vector<std::string> indexedPaths() const;

private:
    /// @brief Flattened symbol row paired with searchable lowercase strings.
    struct SearchableSymbol final
    {
        IndexSymbolRecord record;
        std::string       nameLower;
        std::string       qualifiedLower;
        std::string       containerLower;
        std::string       detailLower;
    };

    void rebuildFlattenedSymbols();

    std::vector<IndexFileShard>                  shards_;
    std::vector<SearchableSymbol>                flattenedSymbols_;
    std::unordered_map<std::string, std::size_t> shardsByPath_;
};

/// @brief Runtime counters for background index maintenance.
struct IndexManagerStats final
{
    /// @brief Number of rebuild jobs scheduled.
    std::uint64_t scheduledJobs{0};

    /// @brief Number of rebuild jobs completed.
    std::uint64_t completedJobs{0};

    /// @brief Number of rebuild jobs cancelled/replaced.
    std::uint64_t cancelledJobs{0};

    /// @brief Last completed snapshot version.
    std::uint64_t lastCompletedSnapshotVersion{0};

    /// @brief Number of indexed files in the latest completed snapshot.
    std::size_t indexedFileCount{0};

    /// @brief Number of indexed symbols in the latest completed snapshot.
    std::size_t indexedSymbolCount{0};

    /// @brief Indicates at least one warm-start shard was loaded on startup.
    bool warmStartLoaded{false};
};

/// @brief Background shard writer and workspace index maintainer.
class IndexManager final
{
public:
    /// @brief Constructs manager bound to a cache directory.
    /// @param[in] cacheDirectory Cache directory path.
    explicit IndexManager(const std::string& cacheDirectory);
    ~IndexManager();

    IndexManager(const IndexManager&)            = delete;
    IndexManager& operator=(const IndexManager&) = delete;

    /// @brief Returns cache directory in use.
    /// @return Cache directory path string.
    [[nodiscard]] const std::string& cacheDirectory() const;

    /// @brief Schedules a background rebuild for one analysis snapshot.
    /// @param[in] snapshotVersion Source analysis snapshot version.
    /// @param[in] shards Full workspace shard set for that snapshot.
    void scheduleRebuild(std::uint64_t snapshotVersion, std::vector<IndexFileShard> shards);

    /// @brief Waits until at least `snapshotVersion` is indexed or timeout elapses.
    /// @param[in] snapshotVersion Minimum completed snapshot.
    /// @param[in] timeout Maximum wait duration.
    /// @return `true` when requested snapshot threshold is met.
    [[nodiscard]] bool waitForSnapshot(std::uint64_t snapshotVersion, std::chrono::milliseconds timeout);

    /// @brief Queries ranked workspace symbols from latest committed index.
    /// @param[in] query Free-text query.
    /// @param[in] limit Maximum number of results.
    /// @return Sorted symbol rows.
    [[nodiscard]] std::vector<WorkspaceSymbolResult> workspaceSymbols(const std::string& query,
                                                                      std::size_t        limit) const;

    /// @brief Runs index verification/repair and reloads in-memory index.
    /// @param[in] removeInvalidShards Remove invalid shard files when true.
    /// @return Verification/repair report.
    [[nodiscard]] IndexRepairReport verifyAndRepair(bool removeInvalidShards);

    /// @brief Returns current manager counters.
    /// @return Stats snapshot.
    [[nodiscard]] IndexManagerStats stats() const;

    /// @brief Stops worker thread and cancels queued/in-flight jobs.
    void shutdown();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace llvmdsdl::lsp

#endif  // LLVMDSDL_LSP_INDEX_H
