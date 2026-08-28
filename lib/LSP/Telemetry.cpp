//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements request telemetry recording and sink forwarding.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/LSP/Telemetry.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace llvmdsdl::lsp
{

void Telemetry::setSink(RequestMetricSink sink)
{
    std::scoped_lock<std::mutex> const lock(mutex_);
    sink_ = std::move(sink);
}

void Telemetry::record(const std::string& method, const std::uint64_t latencyMicros, const bool cancelled)
{
    RequestMetricSink   sink;
    RequestMetric const metric{method, latencyMicros, cancelled};
    {
        std::scoped_lock<std::mutex> const lock(mutex_);
        // The `method` string is attacker-controlled (any JSON-RPC request, including
        // unknown/garbage methods, reaches here). Bound the number of distinct keys so a
        // client streaming distinct method names cannot grow this map without limit
        // (memory-exhaustion DoS). Overflow methods bucket into a single sentinel key,
        // preserving useful aggregate telemetry while capping memory. `kMaxDistinctMethods`
        // is far above the fixed set of real LSP methods this server handles.
        if (!requestCounts_.contains(method) && requestCounts_.size() >= kMaxDistinctMethods)
        {
            ++requestCounts_[std::string(kOverflowMethodKey)];
        }
        else
        {
            ++requestCounts_[method];
        }
        sink = sink_;
    }
    if (sink)
    {
        sink(metric);
    }
}

std::uint64_t Telemetry::requestCount(const std::string_view method) const
{
    std::scoped_lock<std::mutex> const lock(mutex_);
    const auto                         it = requestCounts_.find(std::string(method));
    return it == requestCounts_.end() ? 0U : it->second;
}

}  // namespace llvmdsdl::lsp
