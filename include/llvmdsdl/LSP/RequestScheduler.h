//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Cooperative request scheduler with cancellation support.
///
/// The scheduler runs long-lived requests on a worker thread and exposes
/// per-request cancellation tokens for responsive abort handling.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_LSP_REQUEST_SCHEDULER_H
#define LLVMDSDL_LSP_REQUEST_SCHEDULER_H

#include "llvm/Support/JSON.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace llvmdsdl::lsp
{

/// @brief Cooperative cancellation token for a scheduled request.
class CancellationToken final
{
public:
    /// @brief Returns whether cancellation has been requested.
    /// @return `true` when cancellation is requested.
    [[nodiscard]] bool isCancellationRequested() const;

private:
    friend class RequestScheduler;
    explicit CancellationToken(std::shared_ptr<std::atomic_bool> state);

    std::shared_ptr<std::atomic_bool> state_;
};

/// @brief Status outcome of a scheduled request task.
enum class RequestTaskStatus
{
    /// @brief Task completed successfully.
    Completed,

    /// @brief Task was cancelled cooperatively.
    Cancelled,

    /// @brief Task failed with an internal error.
    Failed,
};

/// @brief Result envelope for scheduled request work.
struct RequestTaskResult final
{
    /// @brief Task outcome status.
    RequestTaskStatus status{RequestTaskStatus::Failed};

    /// @brief Optional JSON result payload when completed.
    llvm::json::Value value{llvm::json::Object{}};

    /// @brief Optional error message when failed.
    std::string errorMessage;
};

/// @brief Cooperative unit of scheduled request work.
using RequestTask = std::function<RequestTaskResult(CancellationToken token)>;

/// @brief Completion callback invoked on task finish.
using RequestCompletion = std::function<void(RequestTaskResult result, std::uint64_t latencyMicros)>;

/// @brief Single-threaded worker scheduler for cancellable LSP requests.
class RequestScheduler final
{
public:
    /// @brief Default upper bound on queued + in-flight requests (memory-DoS guard).
    static constexpr std::size_t kDefaultMaxPendingRequests = 4096;

    /// @brief Creates a scheduler.
    /// @param[in] maxPendingRequests Upper bound on queued + in-flight requests before
    ///            enqueue() rejects (returns false). Overridable mainly for testing.
    explicit RequestScheduler(std::size_t maxPendingRequests = kDefaultMaxPendingRequests);
    ~RequestScheduler();

    RequestScheduler(const RequestScheduler&)            = delete;
    RequestScheduler& operator=(const RequestScheduler&) = delete;
    RequestScheduler(RequestScheduler&&)                 = delete;
    RequestScheduler& operator=(RequestScheduler&&)      = delete;

    /// @brief Enqueues request work for execution.
    /// @param[in] requestKey Stable request key (typically serialized LSP id).
    /// @param[in] method Request method name for tracing.
    /// @param[in] task Request task body.
    /// @param[in] completion Completion callback invoked once.
    /// @return `true` when queued successfully.
    [[nodiscard]] bool enqueue(std::string       requestKey,
                               std::string       method,
                               RequestTask       task,
                               RequestCompletion completion);

    /// @brief Requests cancellation for the given request key.
    /// @param[in] requestKey Stable request key.
    /// @return `true` when a matching in-flight or queued request exists.
    [[nodiscard]] bool cancel(const std::string& requestKey);

    /// @brief Stops the scheduler worker and cancels outstanding requests.
    void shutdown();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace llvmdsdl::lsp

#endif  // LLVMDSDL_LSP_REQUEST_SCHEDULER_H
