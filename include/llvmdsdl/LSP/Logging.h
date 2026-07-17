//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Structured, level-gated logging for `dsdld` post-mortems.
///
/// Records are emitted as one JSON object per line so an operator can grep or feed them to a log
/// processor after the fact ("which request was in flight when the editor stopped responding?").
/// Verbosity is driven by the negotiated trace level (`$/setTrace` or the `trace` setting), which was
/// previously parsed but unused.
///
/// The sink is injected rather than hard-wired to a stream: `dsdld` points it at **stderr** because
/// stdout carries the JSON-RPC frames and must never be interleaved with log text, while tests capture
/// records in memory.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_LSP_LOGGING_H
#define LLVMDSDL_LSP_LOGGING_H

#include <functional>
#include <mutex>
#include <string>

#include "llvmdsdl/LSP/ServerConfig.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"

namespace llvmdsdl::lsp
{

/// @brief Severity of a structured log record.
enum class LogLevel
{
    /// @brief The server could not honour a request or hit an internal fault.
    Error,

    /// @brief Something was rejected or degraded but the server carried on.
    Warn,

    /// @brief Normal request-level activity (the post-mortem backbone).
    Info,

    /// @brief High-volume detail: analysis rebuilds, document churn, URIs.
    Debug,
};

/// @brief Receives one fully formatted JSON log line (no trailing newline).
using LogSink = std::function<void(const std::string& jsonLine)>;

/// @brief Thread-safe structured logger gated by @ref TraceLevel.
///
/// Cheap when disabled: @ref enabled short-circuits before any JSON is built, so the common
/// `TraceLevel::Off` configuration costs one atomic-free comparison per call site.
class StructuredLogger final
{
public:
    /// @brief Constructs a logger. With no sink the logger is inert regardless of level.
    /// @param[in] sink Destination for formatted records.
    explicit StructuredLogger(LogSink sink = {});

    /// @brief Sets verbosity from the negotiated trace level.
    /// @param[in] level Off emits nothing; Basic emits Error/Warn/Info; Verbose adds Debug.
    void setTraceLevel(TraceLevel level);

    /// @brief Returns the current trace level.
    [[nodiscard]] TraceLevel traceLevel() const;

    /// @brief Returns true when a record at @p level would be emitted.
    /// @param[in] level Severity to test.
    [[nodiscard]] bool enabled(LogLevel level) const;

    /// @brief Emits one record.
    /// @param[in] level Severity; dropped when above the configured verbosity.
    /// @param[in] event Short stable event name (e.g. "request", "error").
    /// @param[in] fields Additional structured fields; must not carry document text or secrets.
    void log(LogLevel level, llvm::StringRef event, llvm::json::Object fields = {});

private:
    /// @brief Formatted-record destination.
    LogSink sink_;

    /// @brief Guards `level_` and serialises sink writes from the main and worker threads.
    mutable std::mutex mutex_;

    /// @brief Current verbosity.
    TraceLevel level_{TraceLevel::Off};
};

}  // namespace llvmdsdl::lsp

#endif  // LLVMDSDL_LSP_LOGGING_H
