//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements cooperative request scheduling and cancellation.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/LSP/RequestScheduler.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

namespace llvmdsdl::lsp
{

CancellationToken::CancellationToken(std::shared_ptr<std::atomic_bool> state)
    : state_(std::move(state))
{
}

bool CancellationToken::isCancellationRequested() const
{
    return state_ && state_->load(std::memory_order_relaxed);
}

class RequestScheduler::Impl final
{
public:
    explicit Impl(const std::size_t maxPendingRequests)
        : maxPendingRequests_(maxPendingRequests)
        , worker_([this]() { run(); })
    {
    }

    ~Impl()
    {
        shutdown();
    }

    bool enqueue(std::string requestKey, std::string method, RequestTask task, RequestCompletion completion)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_)
        {
            return false;
        }
        if (requestStates_.contains(requestKey))
        {
            return false;
        }
        // Bound total tracked (queued + in-flight) requests so a client streaming distinct
        // request keys faster than the worker drains them cannot grow the queue / state map
        // without limit (memory-exhaustion DoS). The caller turns a false return into a
        // "failed to queue request" error; real editors hold far fewer requests in flight.
        if (requestStates_.size() >= maxPendingRequests_)
        {
            return false;
        }

        auto cancellation = std::make_shared<std::atomic_bool>(false);
        requestStates_.emplace(requestKey, cancellation);
        queue_.push_back(WorkItem{std::move(requestKey),
                                  std::move(method),
                                  std::move(task),
                                  std::move(completion),
                                  std::move(cancellation)});
        cv_.notify_one();
        return true;
    }

    bool cancel(const std::string& requestKey)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto                  it = requestStates_.find(requestKey);
        if (it == requestStates_.end())
        {
            return false;
        }
        it->second->store(true, std::memory_order_relaxed);
        cv_.notify_one();
        return true;
    }

    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
            {
                return;
            }
            stopping_ = true;
            for (const auto& [_, state] : requestStates_)
            {
                state->store(true, std::memory_order_relaxed);
            }
        }
        cv_.notify_all();
        if (worker_.joinable())
        {
            worker_.join();
        }
    }

private:
    struct WorkItem final
    {
        std::string                       requestKey;
        std::string                       method;
        RequestTask                       task;
        RequestCompletion                 completion;
        std::shared_ptr<std::atomic_bool> cancellation;
    };

    void run()
    {
        while (true)
        {
            WorkItem item;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() { return stopping_ || !queue_.empty(); });
                if (queue_.empty())
                {
                    if (stopping_)
                    {
                        return;
                    }
                    continue;
                }
                item = std::move(queue_.front());
                queue_.pop_front();
            }

            RequestTaskResult result;
            const auto        start = std::chrono::steady_clock::now();
            if (item.cancellation->load(std::memory_order_relaxed))
            {
                result.status = RequestTaskStatus::Cancelled;
            }
            else
            {
                try
                {
                    result = item.task(CancellationToken(item.cancellation));
                } catch (const std::exception& ex)
                {
                    result.status       = RequestTaskStatus::Failed;
                    result.errorMessage = ex.what();
                } catch (...)
                {
                    result.status       = RequestTaskStatus::Failed;
                    result.errorMessage = "unknown exception";
                }
            }
            const auto finish = std::chrono::steady_clock::now();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                requestStates_.erase(item.requestKey);
            }

            if (item.completion)
            {
                const auto latencyMicros =
                    std::chrono::duration_cast<std::chrono::microseconds>(finish - start).count();
                item.completion(std::move(result), static_cast<std::uint64_t>(latencyMicros));
            }
        }
    }

    const std::size_t                                                  maxPendingRequests_;
    std::mutex                                                         mutex_;
    std::condition_variable                                            cv_;
    std::deque<WorkItem>                                               queue_;
    std::unordered_map<std::string, std::shared_ptr<std::atomic_bool>> requestStates_;
    bool                                                               stopping_{false};
    std::thread                                                        worker_;
};

RequestScheduler::RequestScheduler(const std::size_t maxPendingRequests)
    : impl_(std::make_unique<Impl>(maxPendingRequests))
{
}

RequestScheduler::~RequestScheduler() = default;

bool RequestScheduler::enqueue(std::string       requestKey,
                               std::string       method,
                               RequestTask       task,
                               RequestCompletion completion)
{
    return impl_->enqueue(std::move(requestKey), std::move(method), std::move(task), std::move(completion));
}

bool RequestScheduler::cancel(const std::string& requestKey)
{
    return impl_->cancel(requestKey);
}

void RequestScheduler::shutdown()
{
    impl_->shutdown();
}

}  // namespace llvmdsdl::lsp
