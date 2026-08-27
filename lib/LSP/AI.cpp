//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements policy-gated AI assist helpers for `dsdld`.
///
/// The built-in provider is deterministic and offline, enabling AI workflow
/// plumbing and safety tests without requiring network model integration.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/LSP/AI.h"
#include "llvmdsdl/LSP/Analysis.h"
#include "llvmdsdl/LSP/DocumentStore.h"
#include "llvmdsdl/LSP/Index.h"
#include "llvmdsdl/LSP/ServerConfig.h"
#include "llvmdsdl/Support/Diagnostics.h"

#include "llvm/ADT/StringRef.h"

#include <algorithm>
#include <cctype>  // IWYU pragma: keep -- libstdc++ reaches this transitively; libc++ needs it named.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <llvm/Support/JSON.h>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace llvmdsdl::lsp
{
namespace
{

constexpr std::size_t MaxSnippetBytes       = 640;
constexpr std::size_t MaxDiagnosticMessages = 8;
constexpr std::size_t MaxSymbolHints        = 24;

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::size_t              lineStart = 0;
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        if (text[index] != '\n')
        {
            continue;
        }
        lines.push_back(text.substr(lineStart, index - lineStart));
        lineStart = index + 1U;
    }
    lines.push_back(text.substr(lineStart));
    return lines;
}

std::string extractSnippet(const std::string& text, const std::uint32_t startLine, const std::uint32_t endLine)
{
    const std::vector<std::string> lines = splitLines(text);
    if (lines.empty())
    {
        return {};
    }

    const std::uint32_t boundedStartLine =
        std::min<std::uint32_t>(startLine, static_cast<std::uint32_t>(lines.size() - 1U));
    const std::uint32_t boundedEndLine =
        std::min<std::uint32_t>(endLine, static_cast<std::uint32_t>(lines.size() - 1U));
    const std::uint32_t contextStart = boundedStartLine > 2U ? boundedStartLine - 2U : 0U;
    const std::uint32_t contextEnd =
        std::min<std::uint32_t>(static_cast<std::uint32_t>(lines.size() - 1U), boundedEndLine + 2U);

    std::ostringstream stream;
    for (std::uint32_t line = contextStart; line <= contextEnd; ++line)
    {
        stream << (line + 1U) << ": " << lines[line] << '\n';
        if (stream.tellp() >= static_cast<std::streampos>(MaxSnippetBytes))
        {
            break;
        }
    }
    std::string snippet = stream.str();
    if (snippet.size() > MaxSnippetBytes)
    {
        snippet.resize(MaxSnippetBytes);
    }
    return snippet;
}

AiDocumentFacts collectDocumentFacts(const std::string& text)
{
    AiDocumentFacts facts;
    facts.endsWithNewline    = !text.empty() && text.back() == '\n';
    facts.hasSealedDirective = text.contains("@sealed");

    std::uint32_t lineIndex     = 0;
    std::uint32_t lineLength    = 0;
    std::uint32_t currentLength = 0;
    for (const char value : text)
    {
        if (value == '\n')
        {
            lineLength    = currentLength;
            currentLength = 0;
            ++lineIndex;
            continue;
        }
        ++currentLength;
    }
    if (!text.empty() && text.back() != '\n')
    {
        lineLength = currentLength;
    }

    facts.lastLine       = lineIndex;
    facts.lastLineLength = lineLength;
    return facts;
}

std::string makeSuggestionId(const AiCodeActionContext& context, llvm::StringRef stem)
{
    const std::string input = context.uri + "|" + stem.str() + "|" + context.selectionSnippet;
    const std::size_t hash  = std::hash<std::string>{}(input);
    return "ai-" + stem.str() + "-" + std::to_string(static_cast<std::uint64_t>(hash));
}

std::vector<std::string> boundedUniqueStrings(const std::vector<std::string>& input, const std::size_t limit)
{
    std::vector<std::string>        out;
    std::unordered_set<std::string> seen;
    out.reserve(std::min<std::size_t>(input.size(), limit));
    for (const std::string& value : input)
    {
        if (value.empty())
        {
            continue;
        }
        if (!seen.insert(value).second)
        {
            continue;
        }
        out.push_back(value);
        if (out.size() >= limit)
        {
            break;
        }
    }
    return out;
}

std::string formatDiagnosticSummary(const std::vector<std::string>& diagnostics)
{
    if (diagnostics.empty())
    {
        return "No diagnostics were attached to this request.";
    }

    std::ostringstream stream;
    stream << "Diagnostics considered:";
    const std::size_t count = std::min<std::size_t>(diagnostics.size(), MaxDiagnosticMessages);
    for (std::size_t index = 0; index < count; ++index)
    {
        stream << "\n- " << diagnostics[index];
    }
    if (diagnostics.size() > count)
    {
        stream << "\n- ... (" << (diagnostics.size() - count) << " more)";
    }
    return stream.str();
}

}  // namespace

bool AiPolicyGate::isEnabled(const AiMode mode)
{
    return mode != AiMode::Off;
}

bool AiPolicyGate::canSuggest(const AiMode mode)
{
    return mode == AiMode::Suggest || mode == AiMode::Assist || mode == AiMode::ApplyWithConfirmation;
}

bool AiPolicyGate::canApplyConfirmedEdits(const AiMode mode)
{
    return mode == AiMode::ApplyWithConfirmation;
}

bool AiPolicyGate::isAllowedTool(const llvm::StringRef tool)
{
    return tool == "analysis.stats" || tool == "workspace.symbols" || tool == "document.symbols" ||
           tool == "document.diagnostics";
}

AiCodeActionContext AiContextPacker::buildCodeActionContext(const std::string&              uri,
                                                            const std::string&              sourceText,
                                                            const std::uint32_t             startLine,
                                                            const std::uint32_t             startCharacter,
                                                            const std::uint32_t             endLine,
                                                            const std::uint32_t             endCharacter,
                                                            const std::vector<std::string>& diagnostics,
                                                            const std::vector<std::string>& symbolHints)
{
    AiCodeActionContext context;
    context.uri              = uri;
    context.startLine        = startLine;
    context.startCharacter   = startCharacter;
    context.endLine          = endLine;
    context.endCharacter     = endCharacter;
    context.selectionSnippet = extractSnippet(sourceText, startLine, endLine);
    context.diagnostics      = boundedUniqueStrings(diagnostics, MaxDiagnosticMessages);
    context.symbolHints      = boundedUniqueStrings(symbolHints, MaxSymbolHints);
    context.documentFacts    = collectDocumentFacts(sourceText);
    return context;
}

std::vector<AiCodeActionSuggestion> OfflineAiProvider::suggestCodeActions(const AiMode               mode,
                                                                          const AiCodeActionContext& context)
{
    if (!AiPolicyGate::canSuggest(mode))
    {
        return {};
    }

    std::vector<AiCodeActionSuggestion> actions;

    AiCodeActionSuggestion explain;
    explain.id                   = makeSuggestionId(context, "explain");
    explain.title                = "AI: Explain diagnostics";
    explain.kind                 = "quickfix";
    explain.explanation          = formatDiagnosticSummary(context.diagnostics);
    explain.requiresConfirmation = false;
    explain.hasEdit              = false;
    if (!context.diagnostics.empty())
    {
        explain.diagnosticMessage = context.diagnostics.front();
    }
    actions.push_back(std::move(explain));

    AiCodeActionSuggestion suggest;
    suggest.id                   = makeSuggestionId(context, "suggest");
    suggest.title                = "AI: Suggest next refactor";
    suggest.kind                 = "refactor.rewrite";
    suggest.requiresConfirmation = false;
    suggest.hasEdit              = false;
    if (!context.symbolHints.empty())
    {
        suggest.explanation = "Candidate symbols near cursor: ";
        for (std::size_t index = 0; index < std::min<std::size_t>(context.symbolHints.size(), 5U); ++index)
        {
            if (index != 0)
            {
                suggest.explanation += ", ";
            }
            suggest.explanation += context.symbolHints[index];
        }
    }
    else
    {
        suggest.explanation = "No nearby symbols detected; consider narrowing selection for stronger suggestions.";
    }
    actions.push_back(std::move(suggest));

    if (!context.documentFacts.hasSealedDirective && (mode == AiMode::Assist || mode == AiMode::ApplyWithConfirmation))
    {
        AiCodeActionSuggestion fix;
        fix.id                   = makeSuggestionId(context, "add-sealed");
        fix.title                = "AI: Add @sealed directive";
        fix.kind                 = "quickfix";
        fix.explanation          = "Adds @sealed at the end of the definition when missing.";
        fix.requiresConfirmation = true;
        fix.hasEdit              = true;
        fix.edit.textEdits.push_back(TextEditData{
            context.uri,
            context.documentFacts.lastLine,
            context.documentFacts.lastLineLength,
            0,
            context.documentFacts.endsWithNewline ? "@sealed\n" : "\n@sealed\n",
        });
        actions.push_back(std::move(fix));
    }

    return actions;
}

void AiAuditLogger::record(std::string category, std::string detail)
{
    // `detail` embeds attacker-controlled request text (e.g. serialized tool arguments,
    // bounded only by the JSON-RPC frame cap). Redact secrets, then cap the retained size
    // so MaxRecords records cannot amplify a large request into unbounded audit memory.
    std::string redacted = redactSensitive(std::move(detail));
    if (redacted.size() > MaxDetailBytes)
    {
        redacted.resize(MaxDetailBytes);
        redacted += "...[truncated]";
    }

    std::scoped_lock const lock(mutex_);
    records_.push_back(AiAuditRecord{
        std::move(category),
        std::move(redacted),
    });
    if (records_.size() > MaxRecords)
    {
        records_.erase(records_.begin(), records_.begin() + static_cast<std::ptrdiff_t>(records_.size() - MaxRecords));
    }
}

std::vector<AiAuditRecord> AiAuditLogger::snapshot() const
{
    std::scoped_lock const lock(mutex_);
    return records_;
}

std::string AiAuditLogger::redactSensitive(std::string text)
{
    static const std::regex jsonKeyValue(R"((\"(?:password|token|secret|api[_-]?key)\"\s*:\s*\")([^\"]+)(\"))",
                                         std::regex_constants::icase);
    static const std::regex keyValue(R"(((?:password|token|secret|api[_-]?key)\s*[:=]\s*)([^\s,;]+))",
                                     std::regex_constants::icase);
    static const std::regex bearer(R"(((?:bearer)\s+)([A-Za-z0-9._-]+))", std::regex_constants::icase);

    text = std::regex_replace(text, jsonKeyValue, "$1[REDACTED]$3");
    text = std::regex_replace(text, keyValue, "$1[REDACTED]");
    text = std::regex_replace(text, bearer, "$1[REDACTED]");
    return text;
}

AiToolResult runAiTool(const llvm::StringRef     tool,
                       const llvm::json::Object& arguments,
                       AnalysisPipeline&         analysis,
                       const ServerConfig&       config,
                       const DocumentStore&      documents,
                       const IndexManager*       indexManager)
{
    AiToolResult result;

    if (!AiPolicyGate::isAllowedTool(tool))
    {
        result.errorMessage = "unsupported tool: " + tool.str();
        return result;
    }

    if (tool == "analysis.stats")
    {
        const AnalysisResult analysisResult = analysis.run(config, documents);
        const AnalysisStats& stats          = analysis.stats();
        result.ok                           = true;
        result.value                        = llvm::json::Object{
            {"snapshot_version", static_cast<std::int64_t>(analysisResult.snapshotVersion)},
            {"has_errors", analysisResult.hasErrors},
            {"full_rebuilds", static_cast<std::int64_t>(stats.fullRebuildCount)},
            {"incremental_rebuilds", static_cast<std::int64_t>(stats.incrementalRebuildCount)},
        };
        return result;
    }

    if (tool == "workspace.symbols")
    {
        if (!indexManager)
        {
            result.errorMessage = "workspace index is not ready";
            return result;
        }

        const std::string query = arguments.getString("query").value_or("").str();
        const std::size_t limit = static_cast<std::size_t>(
            std::max<std::int64_t>(1, std::min<std::int64_t>(200, arguments.getInteger("limit").value_or(20))));
        const std::vector<WorkspaceSymbolResult> symbols = indexManager->workspaceSymbols(query, limit);
        llvm::json::Array                        rows;
        std::size_t                              emitted = 0;
        for (const WorkspaceSymbolResult& symbol : symbols)
        {
            if (emitted++ >= limit)
            {
                break;
            }
            rows.push_back(llvm::json::Object{
                {"name", symbol.name},
                {"qualified_name", symbol.qualifiedName},
                {"uri", symbol.uri},
                {"line", static_cast<std::int64_t>(symbol.line)},
                {"character", static_cast<std::int64_t>(symbol.character)},
            });
        }
        result.ok    = true;
        result.value = std::move(rows);
        return result;
    }

    if (tool == "document.symbols")
    {
        const auto uri = arguments.getString("uri");
        if (!uri.has_value())
        {
            result.errorMessage = "document.symbols requires `uri`";
            return result;
        }
        const AnalysisResult analysisResult = analysis.run(config, documents);
        (void) analysisResult;
        const std::vector<DocumentSymbolData> symbols = analysis.documentSymbols(uri->str());
        llvm::json::Array                     rows;
        for (const DocumentSymbolData& symbol : symbols)
        {
            rows.push_back(llvm::json::Object{
                {"name", symbol.name},
                {"detail", symbol.detail},
                {"line", static_cast<std::int64_t>(symbol.location.line)},
                {"character", static_cast<std::int64_t>(symbol.location.character)},
            });
        }
        result.ok    = true;
        result.value = std::move(rows);
        return result;
    }

    const auto uri = arguments.getString("uri");
    if (!uri.has_value())
    {
        result.errorMessage = "document.diagnostics requires `uri`";
        return result;
    }
    const AnalysisResult analysisResult = analysis.run(config, documents);
    llvm::json::Array    rows;
    if (const auto it = analysisResult.diagnosticsByUri.find(uri->str()); it != analysisResult.diagnosticsByUri.end())
    {
        for (const Diagnostic& diagnostic : it->second)
        {
            rows.push_back(llvm::json::Object{
                {"level", static_cast<std::int64_t>(diagnostic.level)},
                {"line", static_cast<std::int64_t>(diagnostic.location.line)},
                {"column", static_cast<std::int64_t>(diagnostic.location.column)},
                {"message", diagnostic.message},
            });
        }
    }
    result.ok    = true;
    result.value = std::move(rows);
    return result;
}

}  // namespace llvmdsdl::lsp
