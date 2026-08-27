//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>

#include "llvmdsdl/LSP/RequestScheduler.h"

#include "UnitTests.h"

bool runLspRequestSchedulerTests()
{
    llvmdsdl::lsp::RequestScheduler scheduler;

    std::mutex                       mutex;
    std::condition_variable          cv;
    bool                             sawCallback = false;
    llvmdsdl::lsp::RequestTaskResult completionResult;
    std::uint64_t                    completionLatency = 0;

    const bool queued = scheduler.enqueue(
        "i:7",
        "test/sleep",
        [](const llvmdsdl::lsp::CancellationToken& token) -> llvmdsdl::lsp::RequestTaskResult {
            const auto start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - start < std::chrono::seconds(1))
            {
                if (token.isCancellationRequested())
                {
                    return {llvmdsdl::lsp::RequestTaskStatus::Cancelled, llvm::json::Value(nullptr), {}};
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return {llvmdsdl::lsp::RequestTaskStatus::Completed, llvm::json::Object{{"done", true}}, {}};
        },
        [&mutex, &cv, &sawCallback, &completionResult, &completionLatency](llvmdsdl::lsp::RequestTaskResult result,
                                                                           const std::uint64_t latencyMicros) {
            {
                std::scoped_lock const lock(mutex);
                sawCallback       = true;
                completionResult  = std::move(result);
                completionLatency = latencyMicros;
            }
            cv.notify_all();
        });

    if (!queued)
    {
        std::cerr << "expected enqueue to succeed\n";
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    if (!scheduler.cancel("i:7"))
    {
        std::cerr << "expected cancel to find enqueued request\n";
        return false;
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!cv.wait_for(lock, std::chrono::seconds(3), [&sawCallback]() { return sawCallback; }))
        {
            std::cerr << "timeout waiting for completion callback\n";
            return false;
        }
    }

    if (completionResult.status != llvmdsdl::lsp::RequestTaskStatus::Cancelled)
    {
        std::cerr << "expected cancelled task status\n";
        return false;
    }

    if (completionLatency == 0)
    {
        std::cerr << "expected non-zero latency metric\n";
        return false;
    }

    scheduler.shutdown();

    // Pending-request cap: a client streaming distinct request keys faster than the
    // worker drains them must be bounded. With blocking tasks that never finish until
    // released, the queue + in-flight set fills to the cap and further enqueues are
    // rejected (return false), rather than growing without limit.
    {
        constexpr std::size_t           cap = 4;
        llvmdsdl::lsp::RequestScheduler bounded(cap);
        std::mutex                      gateMutex;
        std::condition_variable         gateCv;
        bool                            release = false;

        auto blockingTask = [&](const llvmdsdl::lsp::CancellationToken&) {
            std::unique_lock<std::mutex> lock(gateMutex);
            gateCv.wait(lock, [&release]() { return release; });
            return llvmdsdl::lsp::RequestTaskResult{llvmdsdl::lsp::RequestTaskStatus::Completed,
                                                    llvm::json::Value(nullptr),
                                                    {}};
        };

        std::size_t accepted = 0;
        for (std::size_t i = 0; i < cap + 8; ++i)
        {
            if (bounded.enqueue("k:" + std::to_string(i), "blocking", blockingTask, {}))
            {
                ++accepted;
            }
        }
        if (accepted > cap)
        {
            std::cerr << "scheduler accepted more than the pending-request cap (" << accepted << " > " << cap << ")\n";
            {
                std::scoped_lock const lock(gateMutex);
                release = true;
            }
            gateCv.notify_all();
            bounded.shutdown();
            return false;
        }

        {
            std::scoped_lock const lock(gateMutex);
            release = true;
        }
        gateCv.notify_all();
        bounded.shutdown();
    }

    return true;
}
