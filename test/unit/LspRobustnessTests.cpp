//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Stress tests cancellation storms and rapid document churn for `dsdld`.
///
/// These checks validate that the LSP scheduler remains responsive under heavy
/// cancellation pressure while diagnostics are being published from frequent
/// in-memory file edits.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/LSP/Server.h"
#include "llvm/Support/JSON.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{

llvm::json::Value parseJson(const std::string& text)
{
    llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(text);
    if (!parsed)
    {
        std::cerr << "invalid JSON test fixture\n";
        std::abort();
    }
    return std::move(*parsed);
}

const llvm::json::Object* findResponseByStringId(const std::vector<llvm::json::Value>& outgoing, llvm::StringRef id)
{
    for (const llvm::json::Value& message : outgoing)
    {
        const auto* object = message.getAsObject();
        if (!object)
        {
            continue;
        }
        const auto responseId = object->getString("id");
        if (responseId && *responseId == id)
        {
            return object;
        }
    }
    return nullptr;
}

const llvm::json::Object* findResponseByIntegerId(const std::vector<llvm::json::Value>& outgoing, const std::int64_t id)
{
    for (const llvm::json::Value& message : outgoing)
    {
        const auto* object = message.getAsObject();
        if (!object)
        {
            continue;
        }
        const auto responseId = object->getInteger("id");
        if (responseId && *responseId == id)
        {
            return object;
        }
    }
    return nullptr;
}

}  // namespace

bool runLspRobustnessTests()
{
    std::mutex                     mutex;
    std::condition_variable        cv;
    std::vector<llvm::json::Value> outgoing;

    llvmdsdl::lsp::Server server([&mutex, &cv, &outgoing](llvm::json::Value message) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            outgoing.push_back(std::move(message));
        }
        cv.notify_all();
    });

    server.handleMessage(parseJson(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})"));
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!cv.wait_for(lock, std::chrono::seconds(2), [&outgoing]() {
                return findResponseByIntegerId(outgoing, 1) != nullptr;
            }))
        {
            std::cerr << "timeout waiting for initialize response in robustness test\n";
            return false;
        }
    }

    const std::string uri = "file:///tmp/robust.dsdl";
    server.handleMessage(llvm::json::Object{
        {"jsonrpc", "2.0"},
        {"method", "textDocument/didOpen"},
        {"params",
         llvm::json::Object{
             {"textDocument",
              llvm::json::Object{
                  {"uri", uri},
                  {"version", 1},
                  {"text", "uint8 value\n@sealed\n"},
              }},
         }},
    });

    constexpr int kChurnIterations = 120;
    std::thread   churnThread([&server, &uri]() {
        for (int index = 0; index < kChurnIterations; ++index)
        {
            const std::string text =
                (index % 2 == 0) ? "uint8 value\n@sealed\n" : "demo.DoesNotExist.1.0 value\n@sealed\n";
            server.handleMessage(llvm::json::Object{
                  {"jsonrpc", "2.0"},
                  {"method", "textDocument/didChange"},
                  {"params",
                 llvm::json::Object{
                       {"textDocument",
                      llvm::json::Object{
                            {"uri", uri},
                            {"version", index + 2},
                      }},
                       {"contentChanges", llvm::json::Array{llvm::json::Object{{"text", text}}}},
                 }},
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    constexpr int kStormRequests = 64;
    for (int index = 0; index < kStormRequests; ++index)
    {
        const std::string id = "storm-" + std::to_string(index);
        server.handleMessage(llvm::json::Object{
            {"jsonrpc", "2.0"},
            {"id", id},
            {"method", "dsdld/debug/sleep"},
            {"params", llvm::json::Object{{"duration_ms", 350}}},
        });
        server.handleMessage(llvm::json::Object{
            {"jsonrpc", "2.0"},
            {"method", "$/cancelRequest"},
            {"params", llvm::json::Object{{"id", id}}},
        });
    }

    churnThread.join();

    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!cv.wait_for(lock, std::chrono::seconds(10), [&outgoing]() {
                for (int index = 0; index < kStormRequests; ++index)
                {
                    if (!findResponseByStringId(outgoing, "storm-" + std::to_string(index)))
                    {
                        return false;
                    }
                }
                return true;
            }))
        {
            std::cerr << "timeout waiting for cancellation storm responses\n";
            return false;
        }
    }

    int cancelledCount = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (int index = 0; index < kStormRequests; ++index)
        {
            const auto* response = findResponseByStringId(outgoing, "storm-" + std::to_string(index));
            if (!response)
            {
                continue;
            }
            const auto* error = response->getObject("error");
            if (error && error->getInteger("code").value_or(0) == -32800)
            {
                ++cancelledCount;
            }
        }
    }
    if (cancelledCount < kStormRequests / 2)
    {
        std::cerr << "expected at least half of storm requests to observe cancellation, got " << cancelledCount << "\n";
        return false;
    }

    server.handleMessage(llvm::json::Object{
        {"jsonrpc", "2.0"},
        {"id", 2000},
        {"method", "textDocument/documentSymbol"},
        {"params", llvm::json::Object{{"textDocument", llvm::json::Object{{"uri", uri}}}}},
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!cv.wait_for(lock, std::chrono::seconds(3), [&outgoing]() {
                return findResponseByIntegerId(outgoing, 2000) != nullptr;
            }))
        {
            std::cerr << "server became unresponsive after churn + cancellation storm\n";
            return false;
        }
    }

    const auto snapshot = server.documentStore().lookup(uri);
    if (!snapshot || snapshot->version != kChurnIterations + 1)
    {
        std::cerr << "document overlay version did not track churned updates\n";
        return false;
    }

    server.handleMessage(parseJson(R"({"jsonrpc":"2.0","id":2,"method":"shutdown","params":null})"));
    server.handleMessage(parseJson(R"({"jsonrpc":"2.0","method":"exit"})"));
    if (!server.shutdownRequested() || !server.shouldExit())
    {
        std::cerr << "robustness test expected orderly shutdown state\n";
        return false;
    }

    return true;
}
