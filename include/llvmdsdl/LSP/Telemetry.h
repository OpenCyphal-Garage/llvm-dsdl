//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Request telemetry aggregation and sink integration.
///
/// This module records lightweight request latency metrics and forwards them to
/// optional sinks for tracing, tests, and diagnostics.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_LSP_TELEMETRY_H
#define LLVMDSDL_LSP_TELEMETRY_H

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace llvmdsdl::lsp
{

/// @brief Immutable telemetry sample for a completed request.
struct RequestMetric final
{
    /// @brief LSP method name.
    std::string method;

    /// @brief Request latency in microseconds.
    std::uint64_t latencyMicros{0};

    /// @brief Whether the request completed via cancellation.
    bool cancelled{false};
};

/// @brief Sink callback invoked for each telemetry sample.
using RequestMetricSink = std::function<void(const RequestMetric&)>;

/// @brief Thread-safe request telemetry recorder.
class Telemetry final
{
public:
    /// @brief Sets the sink callback for newly recorded metrics.
    /// @param[in] sink Sink callback. Empty sink disables forwarding.
    void setSink(RequestMetricSink sink);

    /// @brief Records one request metric sample.
    /// @param[in] method LSP method name.
    /// @param[in] latencyMicros Elapsed time in microseconds.
    /// @param[in] cancelled Whether request finished by cancellation.
    void record(std::string method, std::uint64_t latencyMicros, bool cancelled);

    /// @brief Returns total recorded request count for the method.
    /// @param[in] method LSP method name.
    /// @return Number of samples recorded for `method`.
    [[nodiscard]] std::uint64_t requestCount(std::string_view method) const;

private:
    // Upper bound on distinct method keys retained, so an attacker streaming distinct
    // (unknown) method names cannot grow requestCounts_ without limit. The real server
    // handles well under 100 methods; overflow aggregates under kOverflowMethodKey.
    static constexpr std::size_t      kMaxDistinctMethods = 512;
    static constexpr std::string_view kOverflowMethodKey  = "<other>";

    mutable std::mutex                             mutex_;
    RequestMetricSink                              sink_;
    std::unordered_map<std::string, std::uint64_t> requestCounts_;
};

}  // namespace llvmdsdl::lsp

#endif  // LLVMDSDL_LSP_TELEMETRY_H
