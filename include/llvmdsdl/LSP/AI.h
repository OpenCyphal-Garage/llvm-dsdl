//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// AI-assist primitives for optional `dsdld` agentic workflows.
///
/// This module defines policy-gated abstractions for AI suggestions, bounded
/// context packing, audit logging with redaction, and safe local tool-use.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_LSP_AI_H
#define LLVMDSDL_LSP_AI_H

#include "llvmdsdl/LSP/Analysis.h"
#include "llvmdsdl/LSP/DocumentStore.h"
#include "llvmdsdl/LSP/Index.h"
#include "llvmdsdl/LSP/ServerConfig.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace llvmdsdl::lsp
{

/// @brief Document-derived features used to shape AI edit suggestions.
struct AiDocumentFacts final
{
    /// @brief Last line index in the document.
    std::uint32_t lastLine{0};

    /// @brief Last line character count.
    std::uint32_t lastLineLength{0};

    /// @brief True when the document ends with a newline.
    bool endsWithNewline{false};

    /// @brief True when `@sealed` already appears in the document.
    bool hasSealedDirective{false};
};

/// @brief Bounded prompt context for AI code-action suggestion generation.
struct AiCodeActionContext final
{
    /// @brief Target document URI.
    std::string uri;

    /// @brief Selection start line (zero-based).
    std::uint32_t startLine{0};

    /// @brief Selection start character (zero-based).
    std::uint32_t startCharacter{0};

    /// @brief Selection end line (zero-based).
    std::uint32_t endLine{0};

    /// @brief Selection end character (zero-based).
    std::uint32_t endCharacter{0};

    /// @brief Bounded text snippet around the selection.
    std::string selectionSnippet;

    /// @brief Diagnostic messages associated with this action request.
    std::vector<std::string> diagnostics;

    /// @brief Nearby symbol names from the semantic snapshot.
    std::vector<std::string> symbolHints;

    /// @brief Derived facts about the full document text.
    AiDocumentFacts documentFacts;
};

/// @brief AI-suggested code action prior to server policy resolution.
struct AiCodeActionSuggestion final
{
    /// @brief Stable suggestion identifier.
    std::string id;

    /// @brief User-visible action title.
    std::string title;

    /// @brief LSP code-action kind.
    std::string kind{"quickfix"};

    /// @brief Human-readable explanation attached to metadata.
    std::string explanation;

    /// @brief Optional diagnostic message this suggestion targets.
    std::string diagnosticMessage;

    /// @brief Candidate workspace edit payload.
    WorkspaceEditData edit;

    /// @brief True when @ref edit is populated.
    bool hasEdit{false};

    /// @brief True when explicit confirmation is required before materializing edits.
    bool requiresConfirmation{true};
};

/// @brief Result of trying to materialize a pending AI edit.
struct AiResolveEditResult final
{
    /// @brief True when the request can be fulfilled.
    bool ok{false};

    /// @brief True when an edit payload is available.
    bool hasEdit{false};

    /// @brief Error or guidance message for rejected resolutions.
    std::string message;

    /// @brief Resolved workspace edit payload.
    WorkspaceEditData edit;
};

/// @brief Result payload from executing a safe AI tool-use request.
struct AiToolResult final
{
    /// @brief True when tool execution succeeded.
    bool ok{false};

    /// @brief Tool result payload.
    llvm::json::Value value{llvm::json::Object{}};

    /// @brief Error message when execution fails.
    std::string errorMessage;
};

/// @brief Audit record for AI policy and tool-use operations.
struct AiAuditRecord final
{
    /// @brief Event category.
    std::string category;

    /// @brief Redacted event detail.
    std::string detail;
};

/// @brief Policy helpers for AI mode and tool-use safety checks.
class AiPolicyGate final
{
public:
    /// @brief Returns whether AI features are active for this mode.
    /// @param[in] mode Current AI mode.
    /// @return `true` when mode is not `Off`.
    [[nodiscard]] static bool isEnabled(AiMode mode);

    /// @brief Returns whether suggestion-only AI actions are allowed.
    /// @param[in] mode Current AI mode.
    /// @return `true` when suggestions may be emitted.
    [[nodiscard]] static bool canSuggest(AiMode mode);

    /// @brief Returns whether explicit confirmation can materialize edits.
    /// @param[in] mode Current AI mode.
    /// @return `true` only for `ApplyWithConfirmation`.
    [[nodiscard]] static bool canApplyConfirmedEdits(AiMode mode);

    /// @brief Returns whether `tool` is in the safe allow-list.
    /// @param[in] tool Requested tool name.
    /// @return `true` when the tool may execute.
    [[nodiscard]] static bool isAllowedTool(llvm::StringRef tool);
};

/// @brief Bounded context packer for AI prompt/action requests.
class AiContextPacker final
{
public:
    /// @brief Builds a context payload for a code-action request.
    /// @param[in] uri Target document URI.
    /// @param[in] sourceText Full source text for the document.
    /// @param[in] startLine Selection start line.
    /// @param[in] startCharacter Selection start character.
    /// @param[in] endLine Selection end line.
    /// @param[in] endCharacter Selection end character.
    /// @param[in] diagnostics Associated diagnostic messages.
    /// @param[in] symbolHints Nearby symbol names.
    /// @return Bounded AI context payload.
    [[nodiscard]] AiCodeActionContext buildCodeActionContext(const std::string&              uri,
                                                             const std::string&              sourceText,
                                                             std::uint32_t                   startLine,
                                                             std::uint32_t                   startCharacter,
                                                             std::uint32_t                   endLine,
                                                             std::uint32_t                   endCharacter,
                                                             const std::vector<std::string>& diagnostics,
                                                             const std::vector<std::string>& symbolHints) const;
};

/// @brief Abstract AI provider interface for code-action assistance.
class AiProvider
{
public:
    virtual ~AiProvider() = default;

    /// @brief Produces AI-assisted code-action suggestions.
    /// @param[in] mode Active AI mode.
    /// @param[in] context Bounded request context.
    /// @return Candidate suggestions.
    [[nodiscard]] virtual std::vector<AiCodeActionSuggestion> suggestCodeActions(
        AiMode                     mode,
        const AiCodeActionContext& context) = 0;
};

/// @brief Deterministic built-in provider used when no external model is configured.
class OfflineAiProvider final : public AiProvider
{
public:
    [[nodiscard]] std::vector<AiCodeActionSuggestion> suggestCodeActions(AiMode                     mode,
                                                                         const AiCodeActionContext& context) override;
};

/// @brief Bounded in-memory audit logger with deterministic redaction.
class AiAuditLogger final
{
public:
    /// @brief Appends a redacted audit event.
    /// @param[in] category Event category.
    /// @param[in] detail Event detail text before redaction.
    void record(std::string category, std::string detail);

    /// @brief Returns a snapshot of currently retained records.
    /// @return Copy of audit records.
    [[nodiscard]] std::vector<AiAuditRecord> snapshot() const;

    /// @brief Redacts common secret-like tokens from arbitrary text.
    /// @param[in] text Raw text to sanitize.
    /// @return Redacted text.
    [[nodiscard]] static std::string redactSensitive(std::string text);

private:
    static constexpr std::size_t MaxRecords = 256;
    // Per-record detail cap: bounds total audit memory to ~MaxRecords * MaxDetailBytes
    // regardless of request size, since `detail` carries attacker-controlled request text.
    static constexpr std::size_t MaxDetailBytes = 4096;

    mutable std::mutex         mutex_;
    std::vector<AiAuditRecord> records_;
};

/// @brief Executes a safe allow-listed AI tool request.
/// @param[in] tool Requested tool name.
/// @param[in] arguments JSON object containing tool arguments.
/// @param[in,out] analysis Analysis pipeline used by tool implementations.
/// @param[in] config Current server configuration.
/// @param[in] documents Open-document overlay store.
/// @param[in] indexManager Optional workspace index manager.
/// @return Tool execution result.
[[nodiscard]] AiToolResult runAiTool(llvm::StringRef           tool,
                                     const llvm::json::Object& arguments,
                                     AnalysisPipeline&         analysis,
                                     const ServerConfig&       config,
                                     const DocumentStore&      documents,
                                     const IndexManager*       indexManager);

}  // namespace llvmdsdl::lsp

#endif  // LLVMDSDL_LSP_AI_H
