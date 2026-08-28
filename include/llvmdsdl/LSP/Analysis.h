//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Overlay-aware analysis pipeline for LSP diagnostics and semantic snapshots.
///
/// This module wraps discovery, parsing, semantic analysis, and optional MLIR
/// lowering with caching and dependency-driven invalidation.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_LSP_ANALYSIS_H
#define LLVMDSDL_LSP_ANALYSIS_H

#include "llvmdsdl/LSP/DocumentStore.h"
#include "llvmdsdl/LSP/Index.h"
#include "llvmdsdl/LSP/Lint.h"
#include "llvmdsdl/LSP/ServerConfig.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Frontend/Lexer.h"
#include "llvmdsdl/Support/Diagnostics.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace llvmdsdl::lsp
{

/// @brief Aggregated counters from analysis execution.
struct AnalysisStats final
{
    /// @brief Number of full workspace rebuilds performed.
    std::uint64_t fullRebuildCount{0};

    /// @brief Number of incremental workspace updates performed.
    std::uint64_t incrementalRebuildCount{0};

    /// @brief Dirty definition count from the most recent run.
    std::size_t lastDirtyDefinitionCount{0};

    /// @brief Impacted definition count from the most recent run.
    std::size_t lastImpactedDefinitionCount{0};

    /// @brief Snapshot version produced by the most recent run.
    std::uint64_t lastSnapshotVersion{0};
};

/// @brief Result payload from one analysis run.
struct AnalysisResult final
{
    /// @brief Snapshot version generated for this run.
    std::uint64_t snapshotVersion{0};

    /// @brief Whether this run performed a full rebuild.
    bool fullRebuild{false};

    /// @brief Dirty definition count observed in this run.
    std::size_t dirtyDefinitionCount{0};

    /// @brief Impacted definition count observed in this run.
    std::size_t impactedDefinitionCount{0};

    /// @brief True when at least one error-level diagnostic was emitted.
    bool hasErrors{false};

    /// @brief Diagnostics grouped by file URI.
    std::unordered_map<std::string, std::vector<Diagnostic>> diagnosticsByUri;

    /// @brief Optional MLIR module snapshot when enabled and available.
    std::optional<std::string> mlirSnapshot;
};

/// @brief LSP location-like record.
struct AnalysisLocation final
{
    /// @brief URI of the source document.
    std::string uri;

    /// @brief Zero-based line number.
    std::uint32_t line{0};

    /// @brief Zero-based start column.
    std::uint32_t character{0};

    /// @brief Token length in characters.
    std::uint32_t length{0};
};

/// @brief Hover payload computed from analysis snapshot.
struct HoverData final
{
    /// @brief Markdown/plain text content.
    std::string contents;
};

/// @brief Flat document symbol payload.
struct DocumentSymbolData final
{
    /// @brief Symbol name.
    std::string name;

    /// @brief Symbol detail text.
    std::string detail;

    /// @brief LSP SymbolKind numeric value.
    std::int64_t kind{13};

    /// @brief Symbol range.
    AnalysisLocation location;
};

/// @brief Completion candidate from analysis snapshot.
struct CompletionData final
{
    /// @brief Display label.
    std::string label;

    /// @brief LSP CompletionItemKind numeric value.
    std::int64_t kind{1};

    /// @brief Additional detail string.
    std::string detail;

    /// @brief Stable ranking key used by adaptive signal store.
    std::string rankingKey;

    /// @brief Base lexical score before adaptive reranking.
    double baseScore{0.0};
};

/// @brief Single text edit payload for workspace edits.
struct TextEditData final
{
    /// @brief Target URI.
    std::string uri;

    /// @brief Zero-based start line.
    std::uint32_t line{0};

    /// @brief Zero-based start character.
    std::uint32_t character{0};

    /// @brief Replaced span length in characters.
    std::uint32_t length{0};

    /// @brief Replacement text.
    std::string newText;
};

/// @brief Optional file-rename operation attached to a workspace edit plan.
struct FileRenameData final
{
    /// @brief Old file URI.
    std::string oldUri;

    /// @brief New file URI.
    std::string newUri;
};

/// @brief Workspace edit payload returned by rename/fix-it planners.
struct WorkspaceEditData final
{
    /// @brief Text replacement edits.
    std::vector<TextEditData> textEdits;

    /// @brief Optional file rename.
    std::optional<FileRenameData> fileRename;
};

/// @brief Prepare-rename response payload.
struct PrepareRenameData final
{
    /// @brief Replaceable range for rename.
    AnalysisLocation range;

    /// @brief Placeholder/default symbol name.
    std::string placeholder;
};

/// @brief Rename planning result payload.
struct RenamePlanData final
{
    /// @brief True when rename planning succeeded.
    bool ok{false};

    /// @brief Failure message when planning fails.
    std::string errorMessage;

    /// @brief Conflict messages detected by planner.
    std::vector<std::string> conflicts;

    /// @brief Workspace edit plan.
    WorkspaceEditData edit;
};

/// @brief Code action payload derived from diagnostics and semantic context.
struct CodeActionData final
{
    /// @brief LSP code action title.
    std::string title;

    /// @brief LSP code action kind string.
    std::string kind;

    /// @brief Indicates preferred action among siblings.
    bool isPreferred{false};

    /// @brief Optional associated diagnostic message.
    std::string diagnosticMessage;

    /// @brief Optional edit payload.
    WorkspaceEditData edit;

    /// @brief Indicates whether @ref edit is populated.
    bool hasEdit{false};
};

/// @brief Converts a `file://` URI to a normalized filesystem path.
/// @param[in] uri File URI.
/// @return Normalized absolute path when possible.
[[nodiscard]] std::string uriToNormalizedPath(const std::string& uri);

/// @brief Converts a filesystem path to a `file://` URI.
/// @param[in] filePath Filesystem path.
/// @return `file://` URI string.
[[nodiscard]] std::string normalizedPathToFileUri(const std::string& filePath);

/// @brief Workspace analysis engine with incremental invalidation and snapshot caching.
class AnalysisPipeline final
{
public:
    /// @brief Executes one analysis pass over workspace roots and open overlays.
    /// @param[in] config Current runtime configuration.
    /// @param[in] documents Open document overlay store.
    /// @return Analysis result payload for this run.
    [[nodiscard]] AnalysisResult run(const ServerConfig& config, const DocumentStore& documents);

    /// @brief Returns latest pipeline statistics.
    /// @return Stats snapshot.
    [[nodiscard]] const AnalysisStats& stats() const
    {
        return stats_;
    }

    /// @brief Returns current semantic snapshot version.
    /// @return Latest snapshot version.
    [[nodiscard]] std::uint64_t currentSnapshotVersion() const
    {
        return snapshotVersion_;
    }

    /// @brief Indicates whether `version` is the current snapshot.
    /// @param[in] version Snapshot version to validate.
    /// @return `true` when `version` matches the latest.
    [[nodiscard]] bool isCurrentSnapshot(const std::uint64_t version) const
    {
        return version == snapshotVersion_;
    }

    /// @brief Returns latest optional MLIR snapshot.
    /// @return Optional MLIR module text.
    [[nodiscard]] const std::optional<std::string>& latestMlirSnapshot() const
    {
        return latestMlirSnapshot_;
    }

    /// @brief Reports whether the current snapshot text for `uri` matches `text`.
    /// @param[in] uri Document URI.
    /// @param[in] text Candidate document text.
    /// @return `true` when the URI exists in snapshot and cached source text matches `text`.
    [[nodiscard]] bool documentTextMatches(const std::string& uri, const std::string& text) const;

    /// @brief Resolves hover text at a document position.
    /// @param[in] uri Document URI.
    /// @param[in] line Zero-based line.
    /// @param[in] character Zero-based character.
    /// @return Hover payload when resolvable.
    [[nodiscard]] std::optional<HoverData> hover(const std::string& uri,
                                                 std::uint32_t      line,
                                                 std::uint32_t      character) const;

    /// @brief Resolves definition target at a document position.
    /// @param[in] uri Document URI.
    /// @param[in] line Zero-based line.
    /// @param[in] character Zero-based character.
    /// @return Definition location when resolvable.
    [[nodiscard]] std::optional<AnalysisLocation> definition(const std::string& uri,
                                                             std::uint32_t      line,
                                                             std::uint32_t      character) const;

    /// @brief Resolves references for symbol under cursor.
    /// @param[in] uri Document URI.
    /// @param[in] line Zero-based line.
    /// @param[in] character Zero-based character.
    /// @param[in] includeDeclaration Include declaration location when true.
    /// @return Reference locations.
    [[nodiscard]] std::vector<AnalysisLocation> references(const std::string& uri,
                                                           std::uint32_t      line,
                                                           std::uint32_t      character,
                                                           bool               includeDeclaration) const;

    /// @brief Produces flat document symbols for a given URI.
    /// @param[in] uri Document URI.
    /// @return Document symbol records.
    [[nodiscard]] std::vector<DocumentSymbolData> documentSymbols(const std::string& uri) const;

    /// @brief Produces baseline completions at a given URI/position.
    /// @param[in] uri Document URI.
    /// @param[in] line Zero-based line.
    /// @param[in] character Zero-based character.
    /// @param[out] queryPrefix Optional completion prefix used by completion model.
    /// @return Completion candidates.
    [[nodiscard]] std::vector<CompletionData> completions(const std::string& uri,
                                                          std::uint32_t      line,
                                                          std::uint32_t      character,
                                                          std::string*       queryPrefix = nullptr) const;

    /// @brief Produces semantic token tuples at a given URI.
    /// @param[in] uri Document URI.
    /// @return Encoded semantic token rows (line, column, length, type, modifiers).
    [[nodiscard]] std::vector<std::array<std::uint32_t, 5>> semanticTokens(const std::string& uri) const;

    /// @brief Exports per-file index shards for the current snapshot.
    /// @return Sorted shard set for the active workspace snapshot.
    [[nodiscard]] std::vector<IndexFileShard> buildIndexShards() const;

    /// @brief Resolves a rename target at URI/position.
    /// @param[in] uri Document URI.
    /// @param[in] line Zero-based line.
    /// @param[in] character Zero-based character.
    /// @return Prepare-rename payload when a symbol is renameable.
    [[nodiscard]] std::optional<PrepareRenameData> prepareRename(const std::string& uri,
                                                                 std::uint32_t      line,
                                                                 std::uint32_t      character) const;

    /// @brief Plans a workspace rename edit.
    /// @param[in] uri Document URI.
    /// @param[in] line Zero-based line.
    /// @param[in] character Zero-based character.
    /// @param[in] newName New identifier.
    /// @param[in] includeFileRename Includes declaration file rename when true.
    /// @return Rename planning result.
    [[nodiscard]] RenamePlanData planRename(const std::string& uri,
                                            std::uint32_t      line,
                                            std::uint32_t      character,
                                            const std::string& newName,
                                            bool               includeFileRename) const;

    /// @brief Builds code actions for a document range and diagnostics.
    /// @param[in] uri Document URI.
    /// @param[in] startLine Zero-based range start line.
    /// @param[in] startCharacter Zero-based range start character.
    /// @param[in] endLine Zero-based range end line.
    /// @param[in] endCharacter Zero-based range end character.
    /// @param[in] diagnosticMessages Diagnostic messages from request context.
    /// @return Code actions.
    [[nodiscard]] std::vector<CodeActionData> codeActions(const std::string&              uri,
                                                          std::uint32_t                   startLine,
                                                          std::uint32_t                   startCharacter,
                                                          std::uint32_t                   endLine,
                                                          std::uint32_t                   endCharacter,
                                                          const std::vector<std::string>& diagnosticMessages) const;

private:
    void rebuildDependencyGraph();

    struct CachedDefinition final
    {
        /// @brief Type-reference occurrence for navigation queries.
        struct TypeReference final
        {
            /// @brief Referenced type key (`namespace.Type.M.m`).
            std::string typeKey;

            /// @brief Display text for this type reference.
            std::string display;

            /// @brief Zero-based line.
            std::uint32_t line{0};

            /// @brief Zero-based character.
            std::uint32_t character{0};

            /// @brief Token length.
            std::uint32_t length{0};
        };

        /// @brief Field symbol occurrence for hover and document symbols.
        struct FieldSymbol final
        {
            /// @brief Field name.
            std::string name;

            /// @brief Field type display string.
            std::string typeDisplay;

            /// @brief Zero-based line.
            std::uint32_t line{0};

            /// @brief Zero-based character.
            std::uint32_t character{0};

            /// @brief Token length.
            std::uint32_t length{0};
        };

        /// @brief Constant symbol occurrence for document symbols.
        struct ConstantSymbol final
        {
            /// @brief Constant name.
            std::string name;

            /// @brief Constant type display string.
            std::string typeDisplay;

            /// @brief Zero-based line.
            std::uint32_t line{0};

            /// @brief Zero-based character.
            std::uint32_t character{0};

            /// @brief Token length.
            std::uint32_t length{0};
        };

        /// @brief Directive token occurrence.
        struct DirectiveToken final
        {
            /// @brief Directive spelling including `@`.
            std::string text;

            /// @brief Zero-based line.
            std::uint32_t line{0};

            /// @brief Zero-based character.
            std::uint32_t character{0};

            /// @brief Token length.
            std::uint32_t length{0};
        };

        /// @brief Discovery metadata for the parsed definition.
        DiscoveredDefinition info;

        /// @brief Cached parsed AST.
        DefinitionAST ast;

        /// @brief Cached source text.
        std::string sourceText;

        /// @brief Cached lexer token stream.
        std::vector<Token> lexTokens;

        /// @brief Original source URI for LSP location payloads.
        std::string sourceUri;

        /// @brief Hash of current definition text.
        std::size_t textHash{0};

        /// @brief Referenced versioned-type dependency keys.
        std::vector<std::string> dependencyTypeKeys;

        /// @brief Parse diagnostics associated with this definition.
        std::vector<Diagnostic> parseDiagnostics;

        /// @brief Versioned type-reference occurrences.
        std::vector<TypeReference> typeReferences;

        /// @brief Field symbol occurrences.
        std::vector<FieldSymbol> fieldSymbols;

        /// @brief Constant symbol occurrences.
        std::vector<ConstantSymbol> constantSymbols;

        /// @brief Directive token occurrences.
        std::vector<DirectiveToken> directiveTokens;
    };

    struct SectionExtentInfo final
    {
        /// @brief Minimal valid extent in bits for the section payload.
        std::int64_t requiredBits{0};

        /// @brief Declared extent value (when present in source).
        std::optional<std::int64_t> declaredBits;
    };

    struct DefinitionExtentInfo final
    {
        /// @brief Request/message section extent constraints.
        SectionExtentInfo request;

        /// @brief Optional service response section extent constraints.
        std::optional<SectionExtentInfo> response;
    };

    static void populateCachedDefinitionMetadata(CachedDefinition& cached);

    std::unordered_map<std::string, CachedDefinition>         cachedDefinitionsByPath_;
    std::unordered_map<std::string, std::vector<Diagnostic>>  parseDiagnosticsByPath_;
    std::unordered_map<std::string, std::vector<Diagnostic>>  latestDiagnosticsByUri_;
    std::unordered_map<std::string, std::vector<LintFinding>> latestLintFindingsByUri_;
    std::unordered_map<std::string, DefinitionExtentInfo>     latestExtentInfoByPath_;
    std::unordered_map<std::string, std::string>              typeKeyToPath_;
    std::unordered_map<std::string, std::vector<std::string>> reverseDependenciesByTypeKey_;
    std::vector<std::string>                                  cachedRootNamespaceDirs_;
    std::vector<std::string>                                  cachedLookupDirs_;
    AnalysisStats                                             stats_;
    std::uint64_t                                             snapshotVersion_{0};
    std::optional<std::string>                                latestMlirSnapshot_;
};

}  // namespace llvmdsdl::lsp

#endif  // LLVMDSDL_LSP_ANALYSIS_H
