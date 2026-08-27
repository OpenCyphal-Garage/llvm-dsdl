//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Language server session coordinator for request/notification handling.
///
/// This layer wires JSON-RPC protocol messages to document overlays,
/// configuration state, cancellable request scheduling, and telemetry.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_LSP_SERVER_H
#define LLVMDSDL_LSP_SERVER_H

#include "llvmdsdl/LSP/AI.h"
#include "llvmdsdl/LSP/Analysis.h"
#include "llvmdsdl/LSP/DocumentStore.h"
#include "llvmdsdl/LSP/Index.h"
#include "llvmdsdl/LSP/Logging.h"
#include "llvmdsdl/LSP/Ranking.h"
#include "llvmdsdl/LSP/RequestScheduler.h"
#include "llvmdsdl/LSP/ServerConfig.h"
#include "llvmdsdl/LSP/Telemetry.h"
#include "llvm/Support/JSON.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace llvmdsdl::lsp
{

/// @brief LSP server core for message dispatch and state management.
class Server final
{
public:
    /// @brief Outbound transport callback for JSON-RPC responses/notifications.
    using SendMessageFn = std::function<void(llvm::json::Value message)>;

    /// @brief Constructs the server with send callback and optional telemetry sink.
    /// @param[in] sendMessage Outbound message sink.
    /// @param[in] metricSink Optional telemetry sample sink.
    /// @param[in] sendMessage Outbound JSON-RPC sink.
    /// @param[in] metricSink Optional per-request telemetry sink.
    /// @param[in] logSink Optional structured-log sink. `dsdld` points this at stderr; stdout carries
    ///            the protocol frames and must not be interleaved with log text. Omitting it makes the
    ///            server silent regardless of the negotiated trace level.
    Server(SendMessageFn sendMessage, RequestMetricSink metricSink = {}, LogSink logSink = {});
    ~Server();

    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&)                 = delete;
    Server& operator=(Server&&)      = delete;

    /// @brief Handles one incoming JSON-RPC message.
    /// @param[in] message Parsed message object.
    void handleMessage(const llvm::json::Value& message);

    /// @brief Returns whether an `exit` notification was observed.
    /// @return `true` when process should terminate.
    [[nodiscard]] bool shouldExit() const
    {
        return shouldExit_;
    }

    /// @brief Returns the LSP-conformant process exit code.
    /// @return `0` after orderly `shutdown`+`exit`, otherwise non-zero.
    [[nodiscard]] int exitCode() const
    {
        return exitCode_;
    }

    /// @brief Returns whether `shutdown` has been requested.
    /// @return `true` when shutdown request has been processed.
    [[nodiscard]] bool shutdownRequested() const
    {
        return shutdownRequested_;
    }

    /// @brief Returns read-only access to open-document overlays.
    /// @return Document store reference.
    [[nodiscard]] const DocumentStore& documentStore() const
    {
        return documents_;
    }

    /// @brief Returns read-only access to current server configuration.
    /// @return Configuration reference.
    [[nodiscard]] const ServerConfig& config() const
    {
        return config_;
    }

    /// @brief Returns current analysis stats.
    /// @return Analysis stats snapshot.
    [[nodiscard]] const AnalysisStats& analysisStats() const
    {
        return analysis_.stats();
    }

    /// @brief Returns current analysis snapshot version.
    /// @return Snapshot version.
    [[nodiscard]] std::uint64_t analysisSnapshotVersion() const
    {
        return analysis_.currentSnapshotVersion();
    }

    /// @brief Returns latest optional MLIR snapshot.
    /// @return Optional MLIR module text.
    [[nodiscard]] const std::optional<std::string>& latestMlirSnapshot() const
    {
        return analysis_.latestMlirSnapshot();
    }

    /// @brief Requests server-internal scheduler shutdown.
    void shutdown();

private:
    [[nodiscard]] bool handleRequest(const llvm::json::Object& message,
                                     llvm::StringRef           method,
                                     const llvm::json::Value&  id);
    void               handleNotification(const llvm::json::Object& message, llvm::StringRef method);

    void                                sendResult(const llvm::json::Value& id, llvm::json::Value result);
    void                                sendError(const llvm::json::Value& id, int code, std::string message);
    void                                sendNotification(std::string method, llvm::json::Value params);
    void                                publishEmptyDiagnostics(const std::string& uri);
    void                                publishDiagnosticsFromAnalysis();
    void                                publishDiagnosticsForUriFromCachedAnalysis(const std::string& uri);
    void                                invalidateAnalysisSnapshot();
    [[nodiscard]] const AnalysisResult& ensureAnalysisSnapshot(bool scheduleIndex, bool waitForSnapshot);
    [[nodiscard]] bool                  canReuseDidOpenAnalysis(const std::string& uri, const std::string& text) const;
    llvm::json::Value                   buildSemanticTokens(const std::string& uri) const;
    void                                ensureIndexManager();
    void                                ensureSignalStore();
    void                      scheduleWorkspaceIndex(const AnalysisResult& analysisResult, bool waitForSnapshot);
    [[nodiscard]] std::string resolveIndexCacheDirectory() const;
    [[nodiscard]] std::string resolveSignalStorePath() const;
    void                      appendAiCodeActions(const std::string&              uri,
                                                  std::uint32_t                   startLine,
                                                  std::uint32_t                   startCharacter,
                                                  std::uint32_t                   endLine,
                                                  std::uint32_t                   endCharacter,
                                                  const std::vector<std::string>& diagnosticMessages,
                                                  llvm::json::Array&              payload);
    [[nodiscard]] AiResolveEditResult resolveAiCodeActionEdit(llvm::StringRef suggestionId, bool confirmed) const;

    void recordRequestTelemetry(llvm::StringRef method, std::uint64_t latencyMicros, bool cancelled);

    static std::string requestKeyFromId(const llvm::json::Value& id);

    SendMessageFn                                           sendMessage_;
    DocumentStore                                           documents_;
    ServerConfig                                            config_;
    AnalysisPipeline                                        analysis_;
    RequestScheduler                                        scheduler_;
    Telemetry                                               telemetry_;
    StructuredLogger                                        logger_;
    std::unique_ptr<IndexManager>                           indexManager_;
    std::string                                             indexCacheDirectory_;
    std::unique_ptr<AdaptiveSignalStore>                    signalStore_;
    std::string                                             signalStorePath_;
    std::unique_ptr<AiProvider>                             aiProvider_;
    mutable AiAuditLogger                                   aiAuditLogger_;
    std::unordered_map<std::string, AiCodeActionSuggestion> aiSuggestionsById_;
    std::unordered_set<std::string>                         publishedDiagnosticUris_;
    std::optional<AnalysisResult>                           latestAnalysisResult_;
    std::optional<std::uint64_t>                            lastScheduledIndexSnapshotVersion_;
    std::optional<std::uint64_t>                            lastTimedOutIndexWaitSnapshotVersion_;
    bool                                                    analysisDirty_{true};
    bool                                                    shutdownRequested_{false};
    bool                                                    shouldExit_{false};
    int                                                     exitCode_{0};
};

}  // namespace llvmdsdl::lsp

#endif  // LLVMDSDL_LSP_SERVER_H
