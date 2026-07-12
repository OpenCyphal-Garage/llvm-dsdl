//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements LSP message dispatch, state updates, and response handling.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/LSP/Server.h"
#include "llvmdsdl/Version.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace llvmdsdl::lsp
{
namespace
{

constexpr int JsonRpcErrorMethodNotFound   = -32601;
constexpr int JsonRpcErrorInternal         = -32603;
constexpr int JsonRpcErrorRequestCancelled = -32800;

std::optional<std::string> parseTextDocumentUri(const llvm::json::Value* params)
{
    if (!params)
    {
        return std::nullopt;
    }
    const auto* paramsObject = params->getAsObject();
    if (!paramsObject)
    {
        return std::nullopt;
    }
    const auto* textDocumentValue = paramsObject->get("textDocument");
    if (!textDocumentValue)
    {
        return std::nullopt;
    }
    const auto* textDocument = textDocumentValue->getAsObject();
    if (!textDocument)
    {
        return std::nullopt;
    }
    const auto uri = textDocument->getString("uri");
    if (!uri.has_value())
    {
        return std::nullopt;
    }
    return uri->str();
}

std::optional<std::int64_t> parseTextDocumentVersion(const llvm::json::Value* params)
{
    if (!params)
    {
        return std::nullopt;
    }
    const auto* paramsObject = params->getAsObject();
    if (!paramsObject)
    {
        return std::nullopt;
    }
    const auto* textDocumentValue = paramsObject->get("textDocument");
    if (!textDocumentValue)
    {
        return std::nullopt;
    }
    const auto* textDocument = textDocumentValue->getAsObject();
    if (!textDocument)
    {
        return std::nullopt;
    }
    return textDocument->getInteger("version");
}

std::optional<std::string> parseDidOpenText(const llvm::json::Value* params)
{
    if (!params)
    {
        return std::nullopt;
    }
    const auto* paramsObject = params->getAsObject();
    if (!paramsObject)
    {
        return std::nullopt;
    }
    const auto* textDocumentValue = paramsObject->get("textDocument");
    if (!textDocumentValue)
    {
        return std::nullopt;
    }
    const auto* textDocument = textDocumentValue->getAsObject();
    if (!textDocument)
    {
        return std::nullopt;
    }
    const auto text = textDocument->getString("text");
    if (!text.has_value())
    {
        return std::nullopt;
    }
    return text->str();
}

std::optional<std::string> parseDidChangeText(const llvm::json::Value* params)
{
    if (!params)
    {
        return std::nullopt;
    }
    const auto* paramsObject = params->getAsObject();
    if (!paramsObject)
    {
        return std::nullopt;
    }
    const auto* changesValue = paramsObject->get("contentChanges");
    if (!changesValue)
    {
        return std::nullopt;
    }
    const auto* changes = changesValue->getAsArray();
    if (!changes || changes->empty())
    {
        return std::nullopt;
    }
    const auto* firstChange = (*changes)[0].getAsObject();
    if (!firstChange)
    {
        return std::nullopt;
    }
    const auto text = firstChange->getString("text");
    if (!text.has_value())
    {
        return std::nullopt;
    }
    return text->str();
}

struct DocumentPosition final
{
    std::string   uri;
    std::uint32_t line{0};
    std::uint32_t character{0};
};

struct DocumentRange final
{
    std::string   uri;
    std::uint32_t startLine{0};
    std::uint32_t startCharacter{0};
    std::uint32_t endLine{0};
    std::uint32_t endCharacter{0};
};

std::optional<DocumentPosition> parseDocumentPosition(const llvm::json::Value* params)
{
    const auto uri = parseTextDocumentUri(params);
    if (!uri.has_value())
    {
        return std::nullopt;
    }
    if (!params)
    {
        return std::nullopt;
    }
    const auto* paramsObject = params->getAsObject();
    if (!paramsObject)
    {
        return std::nullopt;
    }
    const auto* positionValue = paramsObject->get("position");
    if (!positionValue)
    {
        return std::nullopt;
    }
    const auto* position = positionValue->getAsObject();
    if (!position)
    {
        return std::nullopt;
    }

    const std::optional<std::int64_t> line      = position->getInteger("line");
    const std::optional<std::int64_t> character = position->getInteger("character");
    if (!line.has_value() || !character.has_value() || *line < 0 || *character < 0)
    {
        return std::nullopt;
    }

    return DocumentPosition{
        *uri,
        static_cast<std::uint32_t>(*line),
        static_cast<std::uint32_t>(*character),
    };
}

std::optional<DocumentRange> parseDocumentRange(const llvm::json::Value* params)
{
    const auto uri = parseTextDocumentUri(params);
    if (!uri.has_value() || !params)
    {
        return std::nullopt;
    }
    const auto* paramsObject = params->getAsObject();
    if (!paramsObject)
    {
        return std::nullopt;
    }
    const auto* rangeValue = paramsObject->get("range");
    if (!rangeValue)
    {
        return std::nullopt;
    }
    const auto* rangeObject = rangeValue->getAsObject();
    if (!rangeObject)
    {
        return std::nullopt;
    }
    const auto* startObject = rangeObject->getObject("start");
    const auto* endObject   = rangeObject->getObject("end");
    if (!startObject || !endObject)
    {
        return std::nullopt;
    }

    const auto startLine      = startObject->getInteger("line");
    const auto startCharacter = startObject->getInteger("character");
    const auto endLine        = endObject->getInteger("line");
    const auto endCharacter   = endObject->getInteger("character");
    if (!startLine.has_value() || !startCharacter.has_value() || !endLine.has_value() || !endCharacter.has_value() ||
        *startLine < 0 || *startCharacter < 0 || *endLine < 0 || *endCharacter < 0)
    {
        return std::nullopt;
    }

    return DocumentRange{
        *uri,
        static_cast<std::uint32_t>(*startLine),
        static_cast<std::uint32_t>(*startCharacter),
        static_cast<std::uint32_t>(*endLine),
        static_cast<std::uint32_t>(*endCharacter),
    };
}

std::optional<std::string> parseRenameNewName(const llvm::json::Value* params)
{
    if (!params)
    {
        return std::nullopt;
    }
    const auto* paramsObject = params->getAsObject();
    if (!paramsObject)
    {
        return std::nullopt;
    }
    const auto value = paramsObject->getString("newName");
    if (!value.has_value())
    {
        return std::nullopt;
    }
    return value->str();
}

bool parseIncludeDeclaration(const llvm::json::Value* params)
{
    if (!params)
    {
        return false;
    }
    const auto* paramsObject = params->getAsObject();
    if (!paramsObject)
    {
        return false;
    }
    const auto* contextValue = paramsObject->get("context");
    if (!contextValue)
    {
        return false;
    }
    const auto* context = contextValue->getAsObject();
    if (!context)
    {
        return false;
    }
    return context->getBoolean("includeDeclaration").value_or(false);
}

std::string parseWorkspaceSymbolQuery(const llvm::json::Value* params)
{
    if (!params)
    {
        return {};
    }
    const auto* paramsObject = params->getAsObject();
    if (!paramsObject)
    {
        return {};
    }
    const auto query = paramsObject->getString("query");
    if (!query.has_value())
    {
        return {};
    }
    return query->str();
}

std::int64_t parseWorkspaceSymbolLimit(const llvm::json::Value* params, const std::int64_t fallback)
{
    if (!params)
    {
        return fallback;
    }
    const auto* paramsObject = params->getAsObject();
    if (!paramsObject)
    {
        return fallback;
    }
    const auto value = paramsObject->getInteger("limit");
    if (!value.has_value() || *value <= 0)
    {
        return fallback;
    }
    return *value;
}

std::string makeSortText(const std::size_t index)
{
    std::ostringstream stream;
    stream << std::setw(6) << std::setfill('0') << index;
    return stream.str();
}

std::vector<std::string> parseCodeActionDiagnosticMessages(const llvm::json::Value* params)
{
    std::vector<std::string> messages;
    if (!params)
    {
        return messages;
    }
    const auto* paramsObject = params->getAsObject();
    if (!paramsObject)
    {
        return messages;
    }
    const auto* contextValue = paramsObject->get("context");
    if (!contextValue)
    {
        return messages;
    }
    const auto* contextObject = contextValue->getAsObject();
    if (!contextObject)
    {
        return messages;
    }
    const auto* diagnosticsArray = contextObject->getArray("diagnostics");
    if (!diagnosticsArray)
    {
        return messages;
    }
    messages.reserve(diagnosticsArray->size());
    for (const llvm::json::Value& diagnosticValue : *diagnosticsArray)
    {
        const auto* diagnosticObject = diagnosticValue.getAsObject();
        if (!diagnosticObject)
        {
            continue;
        }
        if (const auto message = diagnosticObject->getString("message"))
        {
            messages.push_back(message->str());
        }
    }
    return messages;
}

struct AiResolveRequest final
{
    std::string suggestionId;
    bool        confirmed{false};
};

std::optional<AiResolveRequest> parseAiResolveRequestFromData(const llvm::json::Object& data)
{
    const auto* dsdldValue = data.get("dsdld");
    if (!dsdldValue)
    {
        return std::nullopt;
    }
    const auto* dsdld = dsdldValue->getAsObject();
    if (!dsdld)
    {
        return std::nullopt;
    }
    const auto suggestionId = dsdld->getString("aiSuggestionId");
    if (!suggestionId.has_value())
    {
        return std::nullopt;
    }
    return AiResolveRequest{
        suggestionId->str(),
        dsdld->getBoolean("confirmed").value_or(false),
    };
}

std::optional<AiResolveRequest> parseAiResolveRequest(const llvm::json::Value* params)
{
    if (!params)
    {
        return std::nullopt;
    }
    const auto* paramsObject = params->getAsObject();
    if (!paramsObject)
    {
        return std::nullopt;
    }

    if (const auto suggestionId = paramsObject->getString("id"))
    {
        return AiResolveRequest{
            suggestionId->str(),
            paramsObject->getBoolean("confirmed").value_or(false),
        };
    }

    if (const auto* data = paramsObject->getObject("data"))
    {
        return parseAiResolveRequestFromData(*data);
    }

    return std::nullopt;
}

std::vector<std::string> extractSymbolHints(const std::vector<DocumentSymbolData>& symbols)
{
    std::vector<std::string> hints;
    hints.reserve(symbols.size());
    for (const DocumentSymbolData& symbol : symbols)
    {
        if (!symbol.name.empty())
        {
            hints.push_back(symbol.name);
        }
    }
    return hints;
}

llvm::json::Value analysisLocationToLsp(const AnalysisLocation& location)
{
    return llvm::json::Object{
        {"uri", location.uri},
        {"range",
         llvm::json::Object{
             {"start",
              llvm::json::Object{
                  {"line", static_cast<std::int64_t>(location.line)},
                  {"character", static_cast<std::int64_t>(location.character)},
              }},
             {"end",
              llvm::json::Object{
                  {"line", static_cast<std::int64_t>(location.line)},
                  {"character", static_cast<std::int64_t>(location.character + location.length)},
              }},
         }},
    };
}

llvm::json::Value cloneJsonId(const llvm::json::Value& id)
{
    if (const auto text = id.getAsString())
    {
        return llvm::json::Value(text->str());
    }
    if (const auto integer = id.getAsInteger())
    {
        return llvm::json::Value(*integer);
    }
    if (const auto number = id.getAsNumber())
    {
        return llvm::json::Value(*number);
    }
    if (const auto boolean = id.getAsBoolean())
    {
        return llvm::json::Value(*boolean);
    }
    return llvm::json::Value(nullptr);
}

int diagnosticSeverityToLsp(const DiagnosticLevel level)
{
    switch (level)
    {
    case DiagnosticLevel::Error:
        return 1;
    case DiagnosticLevel::Warning:
        return 2;
    case DiagnosticLevel::Note:
        return 3;
    }
    return 3;
}

llvm::json::Value toLspDiagnostic(const Diagnostic& diagnostic)
{
    const std::int64_t line =
        diagnostic.location.line == 0 ? 0 : static_cast<std::int64_t>(diagnostic.location.line - 1U);
    const std::int64_t character =
        diagnostic.location.column == 0 ? 0 : static_cast<std::int64_t>(diagnostic.location.column - 1U);
    const std::int64_t length = std::max<std::int64_t>(1, diagnostic.length);
    return llvm::json::Object{
        {"range",
         llvm::json::Object{
             {"start", llvm::json::Object{{"line", line}, {"character", character}}},
             {"end", llvm::json::Object{{"line", line}, {"character", character + length}}},
         }},
        {"severity", diagnosticSeverityToLsp(diagnostic.level)},
        {"source", "dsdld"},
        {"message", diagnostic.message},
    };
}

llvm::json::Object textEditToLsp(const TextEditData& edit)
{
    return llvm::json::Object{
        {"range",
         llvm::json::Object{
             {"start",
              llvm::json::Object{
                  {"line", static_cast<std::int64_t>(edit.line)},
                  {"character", static_cast<std::int64_t>(edit.character)},
              }},
             {"end",
              llvm::json::Object{
                  {"line", static_cast<std::int64_t>(edit.line)},
                  {"character", static_cast<std::int64_t>(edit.character + edit.length)},
              }},
         }},
        {"newText", edit.newText},
    };
}

llvm::json::Value workspaceEditToLsp(const WorkspaceEditData& edit)
{
    std::unordered_map<std::string, llvm::json::Array> byUri;
    for (const TextEditData& textEdit : edit.textEdits)
    {
        byUri[textEdit.uri].push_back(textEditToLsp(textEdit));
    }

    if (!edit.fileRename.has_value())
    {
        llvm::json::Object changes;
        for (auto& [uri, edits] : byUri)
        {
            changes[uri] = std::move(edits);
        }
        return llvm::json::Object{{"changes", std::move(changes)}};
    }

    llvm::json::Array documentChanges;
    for (auto& [uri, edits] : byUri)
    {
        documentChanges.push_back(llvm::json::Object{
            {"textDocument",
             llvm::json::Object{
                 {"uri", uri},
                 {"version", llvm::json::Value(nullptr)},
             }},
            {"edits", std::move(edits)},
        });
    }
    documentChanges.push_back(llvm::json::Object{
        {"kind", "rename"},
        {"oldUri", edit.fileRename->oldUri},
        {"newUri", edit.fileRename->newUri},
    });
    return llvm::json::Object{{"documentChanges", std::move(documentChanges)}};
}

struct RankedCompletion final
{
    CompletionData   item;
    RankingBreakdown breakdown;
};

struct RankedSymbol final
{
    WorkspaceSymbolResult item;
    RankingBreakdown      breakdown;
};

std::vector<RankedCompletion> rerankCompletions(const std::vector<CompletionData>& completions,
                                                const std::string&                 queryPrefix,
                                                AdaptiveSignalStore*               signalStore)
{
    std::vector<RankedCompletion> ranked;
    ranked.reserve(completions.size());

    const std::uint64_t nowTick = signalStore ? signalStore->currentTick() : 0U;
    for (const CompletionData& completion : completions)
    {
        const std::optional<RankingSignal> signal =
            signalStore ? signalStore->signalFor(completion.rankingKey) : std::nullopt;
        const RankingBreakdown breakdown = RankingModel::scoreCompletion(queryPrefix,
                                                                         CompletionRankingInput{
                                                                             completion.rankingKey,
                                                                             completion.label,
                                                                             completion.detail,
                                                                             completion.kind,
                                                                             completion.baseScore,
                                                                         },
                                                                         signal,
                                                                         nowTick);
        ranked.push_back(RankedCompletion{completion, breakdown});
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedCompletion& lhs, const RankedCompletion& rhs) {
        if (lhs.breakdown.totalScore != rhs.breakdown.totalScore)
        {
            return lhs.breakdown.totalScore > rhs.breakdown.totalScore;
        }
        return std::tie(lhs.item.label, lhs.item.kind) < std::tie(rhs.item.label, rhs.item.kind);
    });
    return ranked;
}

std::vector<RankedSymbol> rerankSymbols(const std::vector<WorkspaceSymbolResult>& symbols,
                                        const std::string&                        query,
                                        AdaptiveSignalStore*                      signalStore)
{
    std::vector<RankedSymbol> ranked;
    ranked.reserve(symbols.size());

    const std::uint64_t nowTick = signalStore ? signalStore->currentTick() : 0U;
    for (const WorkspaceSymbolResult& symbol : symbols)
    {
        const std::optional<RankingSignal> signal =
            signalStore ? signalStore->signalFor("symbol:" + symbol.usr) : std::nullopt;
        const RankingBreakdown breakdown = RankingModel::scoreSymbol(query,
                                                                     SymbolRankingInput{
                                                                         "symbol:" + symbol.usr,
                                                                         symbol.name,
                                                                         symbol.qualifiedName,
                                                                         symbol.containerName,
                                                                         symbol.detail,
                                                                         symbol.kind,
                                                                         symbol.score,
                                                                     },
                                                                     signal,
                                                                     nowTick);
        ranked.push_back(RankedSymbol{symbol, breakdown});
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedSymbol& lhs, const RankedSymbol& rhs) {
        if (lhs.breakdown.totalScore != rhs.breakdown.totalScore)
        {
            return lhs.breakdown.totalScore > rhs.breakdown.totalScore;
        }
        return std::tie(lhs.item.qualifiedName, lhs.item.uri, lhs.item.line, lhs.item.character) <
               std::tie(rhs.item.qualifiedName, rhs.item.uri, rhs.item.line, rhs.item.character);
    });
    return ranked;
}

llvm::json::Object rankingBreakdownToJson(const RankingBreakdown& breakdown)
{
    return llvm::json::Object{
        {"lexical_base", breakdown.lexicalBase},
        {"match_quality", breakdown.matchQuality},
        {"fuzzy_boost", breakdown.fuzzyBoost},
        {"frequency_boost", breakdown.frequencyBoost},
        {"recency_boost", breakdown.recencyBoost},
        {"kind_boost", breakdown.kindBoost},
        {"length_penalty", breakdown.lengthPenalty},
        {"total_score", breakdown.totalScore},
    };
}

}  // namespace

Server::Server(SendMessageFn sendMessage, RequestMetricSink metricSink)
    : sendMessage_(std::move(sendMessage))
{
    aiProvider_ = std::make_unique<OfflineAiProvider>();
    telemetry_.setSink(std::move(metricSink));
}

Server::~Server()
{
    shutdown();
}

void Server::handleMessage(const llvm::json::Value& message)
{
    const auto* object = message.getAsObject();
    if (!object)
    {
        return;
    }

    const auto method = object->getString("method");
    if (!method.has_value())
    {
        return;
    }

    if (const auto* id = object->get("id"))
    {
        const auto start        = std::chrono::steady_clock::now();
        const bool asynchronous = handleRequest(*object, *method, *id);
        if (!asynchronous)
        {
            const auto end = std::chrono::steady_clock::now();
            recordRequestTelemetry(*method,
                                   static_cast<std::uint64_t>(
                                       std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()),
                                   false);
        }
        return;
    }

    handleNotification(*object, *method);
}

void Server::invalidateAnalysisSnapshot()
{
    analysisDirty_ = true;
}

const AnalysisResult& Server::ensureAnalysisSnapshot(const bool scheduleIndex, const bool waitForSnapshot)
{
    if (analysisDirty_ || !latestAnalysisResult_.has_value())
    {
        latestAnalysisResult_ = analysis_.run(config_, documents_);
        analysisDirty_        = false;
    }

    if (scheduleIndex && latestAnalysisResult_.has_value())
    {
        scheduleWorkspaceIndex(*latestAnalysisResult_, waitForSnapshot);
    }
    return *latestAnalysisResult_;
}

bool Server::canReuseDidOpenAnalysis(const std::string& uri, const std::string& text) const
{
    if (analysisDirty_ || !latestAnalysisResult_.has_value())
    {
        return false;
    }
    return analysis_.documentTextMatches(uri, text);
}

bool Server::handleRequest(const llvm::json::Object& message, const llvm::StringRef method, const llvm::json::Value& id)
{
    if (method == "initialize")
    {
        llvm::json::Object result;
        result["capabilities"] = llvm::json::Object{
            {"textDocumentSync", llvm::json::Object{{"openClose", true}, {"change", 1}}},
            {"completionProvider", llvm::json::Object{{"resolveProvider", true}}},
            {"renameProvider", llvm::json::Object{{"prepareProvider", true}}},
            {"codeActionProvider",
             llvm::json::Object{
                 {"resolveProvider", true},
                 {"codeActionKinds",
                  llvm::json::Array{
                      "quickfix",
                      "refactor.extract",
                      "refactor.rewrite",
                  }},
             }},
            {"semanticTokensProvider",
             llvm::json::Object{
                 {"legend",
                  llvm::json::Object{
                      {"tokenTypes", llvm::json::Array{"keyword", "type", "property", "comment", "operator"}},
                      {"tokenModifiers", llvm::json::Array{}},
                  }},
                 {"full", true},
                 {"range", false},
             }},
            {"workspaceSymbolProvider", true},
            {"workspace",
             llvm::json::Object{
                 {"workspaceFolders", llvm::json::Object{{"supported", true}, {"changeNotifications", false}}}}},
        };
        result["serverInfo"] = llvm::json::Object{{"name", "dsdld"}, {"version", llvmdsdl::kVersionString}};
        sendResult(id, std::move(result));
        return false;
    }

    if (method == "shutdown")
    {
        shutdownRequested_ = true;
        sendResult(id, llvm::json::Value(nullptr));
        return false;
    }

    if (method == "textDocument/semanticTokens/full")
    {
        std::string uri;
        if (const auto* params = message.get("params"))
        {
            if (const auto parsedUri = parseTextDocumentUri(params))
            {
                uri = *parsedUri;
            }
        }
        (void) ensureAnalysisSnapshot(false, false);
        sendResult(id, buildSemanticTokens(uri));
        return false;
    }

    if (method == "textDocument/hover")
    {
        const auto position = parseDocumentPosition(message.get("params"));
        if (!position)
        {
            sendResult(id, llvm::json::Value(nullptr));
            return false;
        }
        (void) ensureAnalysisSnapshot(false, false);
        const auto hover = analysis_.hover(position->uri, position->line, position->character);
        if (!hover)
        {
            sendResult(id, llvm::json::Value(nullptr));
            return false;
        }
        sendResult(id,
                   llvm::json::Object{
                       {"contents", llvm::json::Object{{"kind", "markdown"}, {"value", hover->contents}}},
                   });
        return false;
    }

    if (method == "textDocument/definition")
    {
        const auto position = parseDocumentPosition(message.get("params"));
        if (!position)
        {
            sendResult(id, llvm::json::Value(nullptr));
            return false;
        }
        (void) ensureAnalysisSnapshot(false, false);
        const auto definitionLocation = analysis_.definition(position->uri, position->line, position->character);
        if (!definitionLocation)
        {
            sendResult(id, llvm::json::Value(nullptr));
            return false;
        }
        sendResult(id, analysisLocationToLsp(*definitionLocation));
        return false;
    }

    if (method == "textDocument/references")
    {
        const auto position = parseDocumentPosition(message.get("params"));
        if (!position)
        {
            sendResult(id, llvm::json::Array{});
            return false;
        }
        const bool includeDeclaration = parseIncludeDeclaration(message.get("params"));
        (void) ensureAnalysisSnapshot(false, false);
        const std::vector<AnalysisLocation> references =
            analysis_.references(position->uri, position->line, position->character, includeDeclaration);
        llvm::json::Array payload;
        for (const AnalysisLocation& reference : references)
        {
            payload.push_back(analysisLocationToLsp(reference));
        }
        sendResult(id, std::move(payload));
        return false;
    }

    if (method == "textDocument/documentSymbol")
    {
        std::string uri;
        if (const auto* params = message.get("params"))
        {
            if (const auto parsedUri = parseTextDocumentUri(params))
            {
                uri = *parsedUri;
            }
        }
        (void) ensureAnalysisSnapshot(false, false);
        const std::vector<DocumentSymbolData> symbols = analysis_.documentSymbols(uri);
        llvm::json::Array                     payload;
        for (const DocumentSymbolData& symbol : symbols)
        {
            payload.push_back(llvm::json::Object{
                {"name", symbol.name},
                {"detail", symbol.detail},
                {"kind", symbol.kind},
                {"range",
                 llvm::json::Object{
                     {"start",
                      llvm::json::Object{
                          {"line", static_cast<std::int64_t>(symbol.location.line)},
                          {"character", static_cast<std::int64_t>(symbol.location.character)},
                      }},
                     {"end",
                      llvm::json::Object{
                          {"line", static_cast<std::int64_t>(symbol.location.line)},
                          {"character", static_cast<std::int64_t>(symbol.location.character + symbol.location.length)},
                      }},
                 }},
                {"selectionRange",
                 llvm::json::Object{
                     {"start",
                      llvm::json::Object{
                          {"line", static_cast<std::int64_t>(symbol.location.line)},
                          {"character", static_cast<std::int64_t>(symbol.location.character)},
                      }},
                     {"end",
                      llvm::json::Object{
                          {"line", static_cast<std::int64_t>(symbol.location.line)},
                          {"character", static_cast<std::int64_t>(symbol.location.character + symbol.location.length)},
                      }},
                 }},
            });
        }
        sendResult(id, std::move(payload));
        return false;
    }

    if (method == "textDocument/completion")
    {
        const auto position = parseDocumentPosition(message.get("params"));
        if (!position)
        {
            sendResult(id, llvm::json::Object{{"isIncomplete", false}, {"items", llvm::json::Array{}}});
            return false;
        }
        (void) ensureAnalysisSnapshot(false, false);
        ensureSignalStore();
        std::string                       queryPrefix;
        const std::vector<CompletionData> completions =
            analysis_.completions(position->uri, position->line, position->character, &queryPrefix);
        const std::vector<RankedCompletion> ranked = rerankCompletions(completions, queryPrefix, signalStore_.get());

        std::vector<std::string> topKeys;
        topKeys.reserve(std::min<std::size_t>(ranked.size(), 8));
        llvm::json::Array items;
        std::size_t       rankIndex = 0;
        for (const RankedCompletion& completion : ranked)
        {
            topKeys.push_back(completion.item.rankingKey);
            items.push_back(llvm::json::Object{
                {"label", completion.item.label},
                {"kind", completion.item.kind},
                {"detail", completion.item.detail},
                {"sortText", makeSortText(rankIndex++)},
                {"data",
                 llvm::json::Object{
                     {"rankingKey", completion.item.rankingKey},
                     {"query", queryPrefix},
                 }},
            });
        }
        if (signalStore_)
        {
            signalStore_->noteTopExposures(topKeys, 8);
            (void) signalStore_->flush();
        }
        sendResult(id, llvm::json::Object{{"isIncomplete", false}, {"items", std::move(items)}});
        return false;
    }

    if (method == "textDocument/prepareRename")
    {
        const auto position = parseDocumentPosition(message.get("params"));
        if (!position)
        {
            sendResult(id, llvm::json::Value(nullptr));
            return false;
        }
        (void) ensureAnalysisSnapshot(false, false);
        const auto prepare = analysis_.prepareRename(position->uri, position->line, position->character);
        if (!prepare.has_value())
        {
            sendResult(id, llvm::json::Value(nullptr));
            return false;
        }
        sendResult(id,
                   llvm::json::Object{
                       {"range",
                        llvm::json::Object{
                            {"start",
                             llvm::json::Object{
                                 {"line", static_cast<std::int64_t>(prepare->range.line)},
                                 {"character", static_cast<std::int64_t>(prepare->range.character)},
                             }},
                            {"end",
                             llvm::json::Object{
                                 {"line", static_cast<std::int64_t>(prepare->range.line)},
                                 {"character",
                                  static_cast<std::int64_t>(prepare->range.character + prepare->range.length)},
                             }},
                        }},
                       {"placeholder", prepare->placeholder},
                   });
        return false;
    }

    if (method == "textDocument/rename")
    {
        const auto position = parseDocumentPosition(message.get("params"));
        const auto newName  = parseRenameNewName(message.get("params"));
        if (!position || !newName.has_value())
        {
            sendError(id, JsonRpcErrorInternal, "invalid rename request parameters");
            return false;
        }

        (void) ensureAnalysisSnapshot(false, false);
        const RenamePlanData plan =
            analysis_.planRename(position->uri, position->line, position->character, *newName, true);
        if (!plan.ok)
        {
            const std::string messageText = plan.errorMessage.empty() ? "rename failed" : plan.errorMessage;
            sendError(id, JsonRpcErrorInternal, messageText);
            return false;
        }
        sendResult(id, workspaceEditToLsp(plan.edit));
        return false;
    }

    if (method == "textDocument/codeAction")
    {
        const auto range = parseDocumentRange(message.get("params"));
        if (!range.has_value())
        {
            sendResult(id, llvm::json::Array{});
            return false;
        }

        (void) ensureAnalysisSnapshot(false, false);
        const std::vector<std::string>    diagnosticMessages = parseCodeActionDiagnosticMessages(message.get("params"));
        const std::vector<CodeActionData> actions            = analysis_.codeActions(range->uri,
                                                                          range->startLine,
                                                                          range->startCharacter,
                                                                          range->endLine,
                                                                          range->endCharacter,
                                                                          diagnosticMessages);
        llvm::json::Array                 payload;
        for (const CodeActionData& action : actions)
        {
            llvm::json::Object item{
                {"title", action.title},
                {"kind", action.kind},
                {"isPreferred", action.isPreferred},
            };
            if (action.hasEdit)
            {
                item["edit"] = workspaceEditToLsp(action.edit);
            }
            payload.push_back(std::move(item));
        }

        appendAiCodeActions(range->uri,
                            range->startLine,
                            range->startCharacter,
                            range->endLine,
                            range->endCharacter,
                            diagnosticMessages,
                            payload);
        sendResult(id, std::move(payload));
        return false;
    }

    if (method == "codeAction/resolve")
    {
        if (const auto* paramsObject = message.getObject("params"))
        {
            llvm::json::Object resolvedAction = *paramsObject;
            if (const auto* data = resolvedAction.getObject("data"))
            {
                if (const auto request = parseAiResolveRequestFromData(*data))
                {
                    const AiResolveEditResult resolved =
                        resolveAiCodeActionEdit(request->suggestionId, request->confirmed);
                    llvm::json::Object updatedData = *data;
                    if (const auto* existingDsdld = updatedData.getObject("dsdld"))
                    {
                        llvm::json::Object dsdldData     = *existingDsdld;
                        dsdldData["confirmed"]           = request->confirmed;
                        dsdldData["confirmationMessage"] = resolved.message;
                        updatedData["dsdld"]             = std::move(dsdldData);
                    }
                    resolvedAction["data"] = std::move(updatedData);
                    if (resolved.ok && resolved.hasEdit)
                    {
                        resolvedAction["edit"] = workspaceEditToLsp(resolved.edit);
                    }
                }
            }
            sendResult(id, std::move(resolvedAction));
        }
        else
        {
            sendResult(id, llvm::json::Object{});
        }
        return false;
    }

    if (method == "workspace/symbol")
    {
        const std::string query = parseWorkspaceSymbolQuery(message.get("params"));
        const std::size_t limit = static_cast<std::size_t>(
            std::max<std::int64_t>(1,
                                   std::min<std::int64_t>(1000,
                                                          parseWorkspaceSymbolLimit(message.get("params"), 200))));
        (void) ensureAnalysisSnapshot(true, true);
        ensureSignalStore();

        llvm::json::Array payload;
        if (indexManager_)
        {
            const std::vector<WorkspaceSymbolResult> symbols =
                indexManager_->workspaceSymbols(query, std::max<std::size_t>(limit, 400));
            const std::vector<RankedSymbol> ranked = rerankSymbols(symbols, query, signalStore_.get());

            std::vector<std::string> topKeys;
            topKeys.reserve(std::min<std::size_t>(ranked.size(), 8));
            std::size_t emitted = 0;
            for (const RankedSymbol& symbol : ranked)
            {
                if (emitted >= limit)
                {
                    break;
                }
                topKeys.push_back("symbol:" + symbol.item.usr);
                payload.push_back(llvm::json::Object{
                    {"name", symbol.item.name},
                    {"kind", symbol.item.kind},
                    {"location",
                     llvm::json::Object{
                         {"uri", symbol.item.uri},
                         {"range",
                          llvm::json::Object{
                              {"start",
                               llvm::json::Object{
                                   {"line", static_cast<std::int64_t>(symbol.item.line)},
                                   {"character", static_cast<std::int64_t>(symbol.item.character)},
                               }},
                              {"end",
                               llvm::json::Object{
                                   {"line", static_cast<std::int64_t>(symbol.item.line)},
                                   {"character", static_cast<std::int64_t>(symbol.item.character + symbol.item.length)},
                               }},
                          }},
                     }},
                    {"containerName", symbol.item.containerName},
                });
                ++emitted;
            }
            if (signalStore_)
            {
                signalStore_->noteTopExposures(topKeys, 8);
                (void) signalStore_->flush();
            }
        }
        sendResult(id, std::move(payload));
        return false;
    }

    if (method == "completionItem/resolve")
    {
        if (const auto* params = message.get("params"))
        {
            if (const auto* item = params->getAsObject())
            {
                if (const auto* dataValue = item->get("data"))
                {
                    if (const auto* data = dataValue->getAsObject())
                    {
                        if (const auto rankingKey = data->getString("rankingKey"))
                        {
                            ensureSignalStore();
                            if (signalStore_)
                            {
                                signalStore_->noteSelection(rankingKey->str());
                                (void) signalStore_->flush();
                            }
                        }
                    }
                }
            }
        }
        if (const auto* paramsObject = message.getObject("params"))
        {
            llvm::json::Object responseItem = *paramsObject;
            sendResult(id, std::move(responseItem));
        }
        else
        {
            sendResult(id, llvm::json::Object{});
        }
        return false;
    }

    if (method == "dsdld/ai/resolveEdit")
    {
        const auto request = parseAiResolveRequest(message.get("params"));
        if (!request.has_value())
        {
            sendError(id, JsonRpcErrorInternal, "invalid AI resolve parameters");
            return false;
        }

        const AiResolveEditResult resolved = resolveAiCodeActionEdit(request->suggestionId, request->confirmed);
        if (!resolved.ok)
        {
            sendError(id, JsonRpcErrorInternal, resolved.message.empty() ? "AI resolve failed" : resolved.message);
            return false;
        }

        llvm::json::Object payload{
            {"id", request->suggestionId},
            {"confirmed", request->confirmed},
            {"message", resolved.message},
        };
        if (resolved.hasEdit)
        {
            payload["edit"] = workspaceEditToLsp(resolved.edit);
        }
        sendResult(id, std::move(payload));
        return false;
    }

    if (method == "dsdld/ai/toolUse")
    {
        if (!AiPolicyGate::isEnabled(config_.aiMode))
        {
            sendError(id, JsonRpcErrorInternal, "AI mode is disabled");
            return false;
        }

        const auto* params = message.get("params");
        if (!params)
        {
            sendError(id, JsonRpcErrorInternal, "missing AI tool parameters");
            return false;
        }
        const auto* paramsObject = params->getAsObject();
        if (!paramsObject)
        {
            sendError(id, JsonRpcErrorInternal, "invalid AI tool parameters");
            return false;
        }

        const auto tool = paramsObject->getString("tool");
        if (!tool.has_value())
        {
            sendError(id, JsonRpcErrorInternal, "missing AI tool name");
            return false;
        }

        const llvm::json::Object* arguments = paramsObject->getObject("arguments");
        const llvm::json::Object  emptyArguments;
        const AiToolResult        result = runAiTool(*tool,
                                              arguments ? *arguments : emptyArguments,
                                              analysis_,
                                              config_,
                                              documents_,
                                              indexManager_.get());
        std::string               argumentsText;
        llvm::raw_string_ostream  argumentsStream(argumentsText);
        llvm::json::Object        argumentsObject = arguments ? *arguments : emptyArguments;
        argumentsStream << llvm::json::Value(std::move(argumentsObject));
        argumentsStream.flush();
        aiAuditLogger_.record("tool_use", "tool=" + tool->str() + " args=" + argumentsText);
        if (!result.ok)
        {
            sendError(id, JsonRpcErrorInternal, result.errorMessage.empty() ? "AI tool failed" : result.errorMessage);
            return false;
        }
        sendResult(id, result.value);
        return false;
    }

    if (method == "dsdld/debug/renamePreview")
    {
        const auto position = parseDocumentPosition(message.get("params"));
        const auto newName  = parseRenameNewName(message.get("params"));
        if (!position || !newName.has_value())
        {
            sendResult(id, llvm::json::Object{{"ok", false}, {"error", "invalid rename preview parameters"}});
            return false;
        }

        (void) ensureAnalysisSnapshot(false, false);
        const RenamePlanData plan =
            analysis_.planRename(position->uri, position->line, position->character, *newName, true);

        llvm::json::Array conflicts;
        for (const std::string& conflict : plan.conflicts)
        {
            conflicts.push_back(conflict);
        }
        sendResult(id,
                   llvm::json::Object{
                       {"ok", plan.ok},
                       {"error", plan.errorMessage},
                       {"conflicts", std::move(conflicts)},
                       {"edit", workspaceEditToLsp(plan.edit)},
                   });
        return false;
    }

    if (method == "dsdld/debug/analysisStats")
    {
        const AnalysisStats& stats = analysis_.stats();
        sendResult(id,
                   llvm::json::Object{
                       {"snapshot_version", static_cast<std::int64_t>(analysis_.currentSnapshotVersion())},
                       {"full_rebuilds", static_cast<std::int64_t>(stats.fullRebuildCount)},
                       {"incremental_rebuilds", static_cast<std::int64_t>(stats.incrementalRebuildCount)},
                       {"last_dirty_definitions", static_cast<std::int64_t>(stats.lastDirtyDefinitionCount)},
                       {"last_impacted_definitions", static_cast<std::int64_t>(stats.lastImpactedDefinitionCount)},
                       {"mlir_snapshot_available", analysis_.latestMlirSnapshot().has_value()},
                   });
        return false;
    }

    if (method == "dsdld/debug/indexStats")
    {
        ensureIndexManager();
        ensureSignalStore();
        const IndexManagerStats stats = indexManager_ ? indexManager_->stats() : IndexManagerStats{};
        sendResult(id,
                   llvm::json::Object{
                       {"cache_directory", indexCacheDirectory_},
                       {"scheduled_jobs", static_cast<std::int64_t>(stats.scheduledJobs)},
                       {"completed_jobs", static_cast<std::int64_t>(stats.completedJobs)},
                       {"cancelled_jobs", static_cast<std::int64_t>(stats.cancelledJobs)},
                       {"last_snapshot_version", static_cast<std::int64_t>(stats.lastCompletedSnapshotVersion)},
                       {"indexed_files", static_cast<std::int64_t>(stats.indexedFileCount)},
                       {"indexed_symbols", static_cast<std::int64_t>(stats.indexedSymbolCount)},
                       {"warm_start_loaded", stats.warmStartLoaded},
                       {"signal_store_path", signalStorePath_},
                       {"signal_entries", static_cast<std::int64_t>(signalStore_ ? signalStore_->size() : 0)},
                   });
        return false;
    }

    if (method == "dsdld/debug/scoreExplain")
    {
        const auto* params = message.get("params");
        if (!params)
        {
            sendResult(id, llvm::json::Array{});
            return false;
        }
        const auto* paramsObject = params->getAsObject();
        if (!paramsObject)
        {
            sendResult(id, llvm::json::Array{});
            return false;
        }

        const auto kind = paramsObject->getString("kind");
        if (!kind.has_value())
        {
            sendResult(id, llvm::json::Array{});
            return false;
        }

        ensureSignalStore();
        const std::string query = paramsObject->getString("query").value_or("").str();
        const std::size_t limit = static_cast<std::size_t>(
            std::max<std::int64_t>(1, std::min<std::int64_t>(1000, paramsObject->getInteger("limit").value_or(50))));

        llvm::json::Array payload;
        if (*kind == "completion")
        {
            const auto uri       = paramsObject->getString("uri");
            const auto line      = paramsObject->getInteger("line");
            const auto character = paramsObject->getInteger("character");
            if (!uri.has_value() || !line.has_value() || !character.has_value() || *line < 0 || *character < 0)
            {
                sendResult(id, llvm::json::Array{});
                return false;
            }

            (void) ensureAnalysisSnapshot(false, false);
            std::string                       completionPrefix;
            const std::vector<CompletionData> completions =
                analysis_.completions(uri->str(),
                                      static_cast<std::uint32_t>(*line),
                                      static_cast<std::uint32_t>(*character),
                                      &completionPrefix);
            const std::vector<RankedCompletion> ranked =
                rerankCompletions(completions, query.empty() ? completionPrefix : query, signalStore_.get());
            std::size_t emitted = 0;
            for (const RankedCompletion& candidate : ranked)
            {
                if (emitted++ >= limit)
                {
                    break;
                }
                payload.push_back(llvm::json::Object{
                    {"key", candidate.item.rankingKey},
                    {"label", candidate.item.label},
                    {"detail", candidate.item.detail},
                    {"kind", candidate.item.kind},
                    {"score", candidate.breakdown.totalScore},
                    {"breakdown", rankingBreakdownToJson(candidate.breakdown)},
                });
            }
            sendResult(id, std::move(payload));
            return false;
        }

        if (*kind == "workspaceSymbol")
        {
            (void) ensureAnalysisSnapshot(true, true);
            if (indexManager_)
            {
                const std::vector<WorkspaceSymbolResult> symbols =
                    indexManager_->workspaceSymbols(query, std::max<std::size_t>(limit, 400));
                const std::vector<RankedSymbol> ranked  = rerankSymbols(symbols, query, signalStore_.get());
                std::size_t                     emitted = 0;
                for (const RankedSymbol& candidate : ranked)
                {
                    if (emitted++ >= limit)
                    {
                        break;
                    }
                    payload.push_back(llvm::json::Object{
                        {"key", "symbol:" + candidate.item.usr},
                        {"name", candidate.item.name},
                        {"qualified_name", candidate.item.qualifiedName},
                        {"container_name", candidate.item.containerName},
                        {"kind", candidate.item.kind},
                        {"score", candidate.breakdown.totalScore},
                        {"breakdown", rankingBreakdownToJson(candidate.breakdown)},
                    });
                }
            }
            sendResult(id, std::move(payload));
            return false;
        }

        sendResult(id, llvm::json::Array{});
        return false;
    }

    if (method == "dsdld/debug/aiAuditLog")
    {
        llvm::json::Array payload;
        for (const AiAuditRecord& record : aiAuditLogger_.snapshot())
        {
            payload.push_back(llvm::json::Object{
                {"category", record.category},
                {"detail", record.detail},
            });
        }
        sendResult(id, std::move(payload));
        return false;
    }

    if (method == "dsdld/debug/indexVerifyRepair")
    {
        ensureIndexManager();
        bool removeInvalid = true;
        if (const auto* params = message.get("params"))
        {
            if (const auto* paramsObject = params->getAsObject())
            {
                removeInvalid = paramsObject->getBoolean("removeInvalid").value_or(true);
            }
        }

        const IndexRepairReport report =
            indexManager_ ? indexManager_->verifyAndRepair(removeInvalid) : IndexRepairReport{};
        llvm::json::Array removed;
        for (const std::string& path : report.removedShardPaths)
        {
            removed.push_back(path);
        }
        sendResult(id,
                   llvm::json::Object{
                       {"inspected_files", static_cast<std::int64_t>(report.inspectedFiles)},
                       {"invalid_files", static_cast<std::int64_t>(report.invalidFiles)},
                       {"duplicate_files", static_cast<std::int64_t>(report.duplicateFiles)},
                       {"removed_files", static_cast<std::int64_t>(report.removedFiles)},
                       {"removed_shards", std::move(removed)},
                   });
        return false;
    }

    if (method == "dsdld/debug/sleep")
    {
        std::int64_t sleepMilliseconds = 200;
        if (const auto* params = message.get("params"))
        {
            if (const auto* paramsObject = params->getAsObject())
            {
                if (const auto parsedDuration = paramsObject->getInteger("duration_ms"))
                {
                    sleepMilliseconds = std::max<std::int64_t>(0, *parsedDuration);
                }
            }
        }

        const std::string requestKey = requestKeyFromId(id);
        const bool        queued     = scheduler_.enqueue(
            requestKey,
            method.str(),
            [sleepMilliseconds](CancellationToken token) {
                const auto start = std::chrono::steady_clock::now();
                while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(sleepMilliseconds))
                {
                    if (token.isCancellationRequested())
                    {
                        return RequestTaskResult{RequestTaskStatus::Cancelled, llvm::json::Value(nullptr), {}};
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                return RequestTaskResult{RequestTaskStatus::Completed,
                                         llvm::json::Object{{"slept_ms", sleepMilliseconds}},
                                                    {}};
            },
            [this, requestId = cloneJsonId(id), requestMethod = method.str()](RequestTaskResult   result,
                                                                              const std::uint64_t latencyMicros) {
                if (result.status == RequestTaskStatus::Completed)
                {
                    sendResult(requestId, std::move(result.value));
                    recordRequestTelemetry(requestMethod, latencyMicros, false);
                    return;
                }
                if (result.status == RequestTaskStatus::Cancelled)
                {
                    sendError(requestId, JsonRpcErrorRequestCancelled, "request cancelled");
                    recordRequestTelemetry(requestMethod, latencyMicros, true);
                    return;
                }
                sendError(requestId,
                          JsonRpcErrorInternal,
                          result.errorMessage.empty() ? "request failed" : std::move(result.errorMessage));
                recordRequestTelemetry(requestMethod, latencyMicros, false);
            });

        if (!queued)
        {
            sendError(id, JsonRpcErrorInternal, "failed to queue request");
            return false;
        }
        return true;
    }

    sendError(id, JsonRpcErrorMethodNotFound, "method not found: " + method.str());
    return false;
}

void Server::handleNotification(const llvm::json::Object& message, const llvm::StringRef method)
{
    if (method == "textDocument/didOpen")
    {
        const auto* params  = message.get("params");
        const auto  uri     = parseTextDocumentUri(params);
        const auto  text    = parseDidOpenText(params);
        const auto  version = parseTextDocumentVersion(params).value_or(0);
        if (uri && text)
        {
            const bool reuseCachedAnalysis = canReuseDidOpenAnalysis(*uri, *text);
            documents_.open(*uri, *text, version);
            if (reuseCachedAnalysis)
            {
                publishDiagnosticsForUriFromCachedAnalysis(*uri);
            }
            else
            {
                invalidateAnalysisSnapshot();
                publishDiagnosticsFromAnalysis();
            }
        }
        return;
    }

    if (method == "textDocument/didChange")
    {
        const auto* params  = message.get("params");
        const auto  uri     = parseTextDocumentUri(params);
        const auto  text    = parseDidChangeText(params);
        const auto  version = parseTextDocumentVersion(params).value_or(0);
        if (uri && text)
        {
            const bool updated = documents_.applyFullTextChange(*uri, *text, version);
            (void) updated;
            invalidateAnalysisSnapshot();
            publishDiagnosticsFromAnalysis();
        }
        return;
    }

    if (method == "textDocument/didClose")
    {
        if (const auto uri = parseTextDocumentUri(message.get("params")))
        {
            const bool closed = documents_.close(*uri);
            (void) closed;
            invalidateAnalysisSnapshot();
            publishDiagnosticsFromAnalysis();
        }
        return;
    }

    if (method == "workspace/didChangeConfiguration")
    {
        if (const auto* params = message.get("params"))
        {
            const bool applied = applyDidChangeConfiguration(*params, config_);
            (void) applied;
            invalidateAnalysisSnapshot();
            publishDiagnosticsFromAnalysis();
        }
        return;
    }

    if (method == "$/cancelRequest")
    {
        const auto* params = message.get("params");
        if (!params)
        {
            return;
        }
        const auto* paramsObject = params->getAsObject();
        if (!paramsObject)
        {
            return;
        }
        if (const auto* id = paramsObject->get("id"))
        {
            const bool cancelled = scheduler_.cancel(requestKeyFromId(*id));
            (void) cancelled;
        }
        return;
    }

    if (method == "exit")
    {
        shouldExit_ = true;
        if (!shutdownRequested_)
        {
            exitCode_ = 1;
        }
        return;
    }
}

void Server::sendResult(const llvm::json::Value& id, llvm::json::Value result)
{
    sendMessage_(llvm::json::Object{
        {"jsonrpc", "2.0"},
        {"id", cloneJsonId(id)},
        {"result", std::move(result)},
    });
}

void Server::sendError(const llvm::json::Value& id, const int code, std::string message)
{
    sendMessage_(llvm::json::Object{
        {"jsonrpc", "2.0"},
        {"id", cloneJsonId(id)},
        {"error", llvm::json::Object{{"code", code}, {"message", std::move(message)}}},
    });
}

void Server::sendNotification(std::string method, llvm::json::Value params)
{
    sendMessage_(llvm::json::Object{
        {"jsonrpc", "2.0"},
        {"method", std::move(method)},
        {"params", std::move(params)},
    });
}

void Server::publishEmptyDiagnostics(const std::string& uri)
{
    sendNotification("textDocument/publishDiagnostics",
                     llvm::json::Object{
                         {"uri", uri},
                         {"diagnostics", llvm::json::Array{}},
                     });
}

void Server::publishDiagnosticsForUriFromCachedAnalysis(const std::string& uri)
{
    if (!latestAnalysisResult_.has_value())
    {
        publishDiagnosticsFromAnalysis();
        return;
    }

    llvm::json::Array payload;
    if (const auto it = latestAnalysisResult_->diagnosticsByUri.find(uri);
        it != latestAnalysisResult_->diagnosticsByUri.end())
    {
        for (const Diagnostic& diagnostic : it->second)
        {
            payload.push_back(toLspDiagnostic(diagnostic));
        }
    }

    sendNotification("textDocument/publishDiagnostics",
                     llvm::json::Object{
                         {"uri", uri},
                         {"diagnostics", std::move(payload)},
                     });
    publishedDiagnosticUris_.insert(uri);
}

void Server::publishDiagnosticsFromAnalysis()
{
    const AnalysisResult& analysisResult = ensureAnalysisSnapshot(true, false);

    std::unordered_map<std::string, llvm::json::Array> diagnosticsByUri;
    for (const auto& [uri, diagnostics] : analysisResult.diagnosticsByUri)
    {
        llvm::json::Array payload;
        for (const Diagnostic& diagnostic : diagnostics)
        {
            payload.push_back(toLspDiagnostic(diagnostic));
        }
        diagnosticsByUri.emplace(uri, std::move(payload));
    }

    for (const DocumentSnapshot& snapshot : documents_.snapshots())
    {
        diagnosticsByUri.try_emplace(snapshot.uri, llvm::json::Array{});
    }

    std::unordered_set<std::string> currentUris;
    currentUris.reserve(diagnosticsByUri.size());
    for (auto& [uri, diagnostics] : diagnosticsByUri)
    {
        sendNotification("textDocument/publishDiagnostics",
                         llvm::json::Object{
                             {"uri", uri},
                             {"diagnostics", std::move(diagnostics)},
                         });
        currentUris.insert(uri);
    }

    for (const std::string& previousUri : publishedDiagnosticUris_)
    {
        if (!currentUris.contains(previousUri))
        {
            publishEmptyDiagnostics(previousUri);
        }
    }
    publishedDiagnosticUris_ = std::move(currentUris);
}

llvm::json::Value Server::buildSemanticTokens(const std::string& uri) const
{
    llvm::json::Array                               encoded;
    const std::vector<std::array<std::uint32_t, 5>> rows = analysis_.semanticTokens(uri);
    for (const std::array<std::uint32_t, 5>& row : rows)
    {
        encoded.push_back(static_cast<std::int64_t>(row[0]));
        encoded.push_back(static_cast<std::int64_t>(row[1]));
        encoded.push_back(static_cast<std::int64_t>(row[2]));
        encoded.push_back(static_cast<std::int64_t>(row[3]));
        encoded.push_back(static_cast<std::int64_t>(row[4]));
    }
    return llvm::json::Object{{"data", std::move(encoded)}};
}

void Server::ensureIndexManager()
{
    const std::string cacheDirectory = resolveIndexCacheDirectory();
    if (cacheDirectory.empty())
    {
        return;
    }
    if (indexManager_ && cacheDirectory == indexCacheDirectory_)
    {
        return;
    }

    if (indexManager_)
    {
        indexManager_->shutdown();
    }
    indexManager_        = std::make_unique<IndexManager>(cacheDirectory);
    indexCacheDirectory_ = cacheDirectory;
    lastScheduledIndexSnapshotVersion_.reset();
    lastTimedOutIndexWaitSnapshotVersion_.reset();
}

void Server::ensureSignalStore()
{
    const std::string storePath = resolveSignalStorePath();
    if (storePath.empty())
    {
        return;
    }
    if (signalStore_ && storePath == signalStorePath_)
    {
        return;
    }

    if (signalStore_)
    {
        (void) signalStore_->flush();
    }
    signalStore_     = std::make_unique<AdaptiveSignalStore>(storePath);
    signalStorePath_ = storePath;
}

void Server::scheduleWorkspaceIndex(const AnalysisResult& analysisResult, const bool waitForSnapshot)
{
    ensureIndexManager();
    if (!indexManager_)
    {
        return;
    }

    const bool snapshotAlreadyScheduled = lastScheduledIndexSnapshotVersion_.has_value() &&
                                          *lastScheduledIndexSnapshotVersion_ == analysisResult.snapshotVersion;
    if (!snapshotAlreadyScheduled)
    {
        indexManager_->scheduleRebuild(analysisResult.snapshotVersion, analysis_.buildIndexShards());
        lastScheduledIndexSnapshotVersion_ = analysisResult.snapshotVersion;
    }

    if (waitForSnapshot)
    {
        const IndexManagerStats stats = indexManager_->stats();
        if (stats.lastCompletedSnapshotVersion >= analysisResult.snapshotVersion)
        {
            if (lastTimedOutIndexWaitSnapshotVersion_.has_value() &&
                *lastTimedOutIndexWaitSnapshotVersion_ == analysisResult.snapshotVersion)
            {
                lastTimedOutIndexWaitSnapshotVersion_.reset();
            }
            return;
        }

        const bool previouslyTimedOut = lastTimedOutIndexWaitSnapshotVersion_.has_value() &&
                                        *lastTimedOutIndexWaitSnapshotVersion_ == analysisResult.snapshotVersion;
        if (previouslyTimedOut)
        {
            return;
        }

        const bool hasUsableIndex = stats.indexedSymbolCount > 0U;
        const auto waitBudget     = hasUsableIndex ? std::chrono::milliseconds(25) : std::chrono::milliseconds(250);
        const bool ready          = indexManager_->waitForSnapshot(analysisResult.snapshotVersion, waitBudget);
        if (!ready)
        {
            lastTimedOutIndexWaitSnapshotVersion_ = analysisResult.snapshotVersion;
        }
        else if (lastTimedOutIndexWaitSnapshotVersion_.has_value() &&
                 *lastTimedOutIndexWaitSnapshotVersion_ == analysisResult.snapshotVersion)
        {
            lastTimedOutIndexWaitSnapshotVersion_.reset();
        }
    }
}

std::string Server::resolveIndexCacheDirectory() const
{
    if (!config_.indexCacheDir.empty())
    {
        return config_.indexCacheDir;
    }

    std::error_code ec;
    if (!config_.rootNamespaceDirs.empty())
    {
        const std::filesystem::path root(config_.rootNamespaceDirs.front());
        return (std::filesystem::absolute(root, ec) / ".dsdld-index").lexically_normal().string();
    }

    return (std::filesystem::temp_directory_path(ec) / "llvmdsdl-dsdld-index").lexically_normal().string();
}

std::string Server::resolveSignalStorePath() const
{
    const std::string cacheDirectory = resolveIndexCacheDirectory();
    if (cacheDirectory.empty())
    {
        return {};
    }
    return (std::filesystem::path(cacheDirectory) / "ranking-signals.json").lexically_normal().string();
}

void Server::appendAiCodeActions(const std::string&              uri,
                                 const std::uint32_t             startLine,
                                 const std::uint32_t             startCharacter,
                                 const std::uint32_t             endLine,
                                 const std::uint32_t             endCharacter,
                                 const std::vector<std::string>& diagnosticMessages,
                                 llvm::json::Array&              payload)
{
    if (!aiProvider_ || !AiPolicyGate::canSuggest(config_.aiMode))
    {
        return;
    }

    const std::optional<DocumentSnapshot> snapshot   = documents_.lookup(uri);
    const std::string                     sourceText = snapshot ? snapshot->text : std::string{};
    const std::vector<std::string> symbolHints = extractSymbolHints(analysis_.documentSymbols(uri));

    const AiCodeActionContext                 context     = aiContextPacker_.buildCodeActionContext(uri,
                                                                                sourceText,
                                                                                startLine,
                                                                                startCharacter,
                                                                                endLine,
                                                                                endCharacter,
                                                                                diagnosticMessages,
                                                                                symbolHints);
    const std::vector<AiCodeActionSuggestion> suggestions = aiProvider_->suggestCodeActions(config_.aiMode, context);

    aiAuditLogger_.record("code_action",
                          "uri=" + uri + " diagnostics=" + std::to_string(diagnosticMessages.size()) +
                              " snippet=" + context.selectionSnippet);

    for (const AiCodeActionSuggestion& suggestion : suggestions)
    {
        aiSuggestionsById_.insert_or_assign(suggestion.id, suggestion);
        if (aiSuggestionsById_.size() > 1024U)
        {
            aiSuggestionsById_.erase(aiSuggestionsById_.begin());
        }

        llvm::json::Object dsdldData{
            {"aiSuggestionId", suggestion.id},
            {"confirmed", false},
            {"requiresConfirmation", suggestion.requiresConfirmation},
            {"explanation", suggestion.explanation},
        };
        if (!suggestion.diagnosticMessage.empty())
        {
            dsdldData["diagnosticMessage"] = suggestion.diagnosticMessage;
        }

        payload.push_back(llvm::json::Object{
            {"title", suggestion.title},
            {"kind", suggestion.kind},
            {"isPreferred", false},
            {"data", llvm::json::Object{{"dsdld", std::move(dsdldData)}}},
        });
    }
}

AiResolveEditResult Server::resolveAiCodeActionEdit(const llvm::StringRef suggestionId, const bool confirmed) const
{
    const auto suggestionIt = aiSuggestionsById_.find(suggestionId.str());
    if (suggestionIt == aiSuggestionsById_.end())
    {
        return AiResolveEditResult{
            false,
            false,
            "unknown AI suggestion id",
            WorkspaceEditData{},
        };
    }
    const AiCodeActionSuggestion& suggestion = suggestionIt->second;

    aiAuditLogger_.record("resolve_edit",
                          "id=" + suggestion.id + " confirmed=" + (confirmed ? std::string("true") : "false"));

    if (!suggestion.hasEdit)
    {
        return AiResolveEditResult{
            true,
            false,
            suggestion.explanation,
            WorkspaceEditData{},
        };
    }
    if (suggestion.requiresConfirmation && !confirmed)
    {
        return AiResolveEditResult{
            true,
            false,
            "AI edit requires explicit confirmation.",
            WorkspaceEditData{},
        };
    }
    if (!AiPolicyGate::canApplyConfirmedEdits(config_.aiMode))
    {
        return AiResolveEditResult{
            false,
            false,
            "AI mode does not permit applying edits. Use `apply_with_confirmation`.",
            WorkspaceEditData{},
        };
    }
    return AiResolveEditResult{
        true,
        true,
        "AI edit resolved.",
        suggestion.edit,
    };
}

void Server::recordRequestTelemetry(const llvm::StringRef method,
                                    const std::uint64_t   latencyMicros,
                                    const bool            cancelled)
{
    telemetry_.record(method.str(), latencyMicros, cancelled);
}

std::string Server::requestKeyFromId(const llvm::json::Value& id)
{
    if (const auto text = id.getAsString())
    {
        return ("s:" + text->str());
    }
    if (const auto integer = id.getAsInteger())
    {
        return ("i:" + std::to_string(*integer));
    }

    std::string              serialized;
    llvm::raw_string_ostream stream(serialized);
    stream << id;
    stream.flush();
    return ("j:" + serialized);
}

void Server::shutdown()
{
    scheduler_.shutdown();
    if (signalStore_)
    {
        (void) signalStore_->flush();
    }
    if (indexManager_)
    {
        indexManager_->shutdown();
    }
}

}  // namespace llvmdsdl::lsp
