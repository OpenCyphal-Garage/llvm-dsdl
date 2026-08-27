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
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "UnitTests.h"

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
            std::scoped_lock const lock(mutex);
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
        std::scoped_lock const lock(mutex);
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

// Structured logging is the post-mortem trail: every request must produce one parseable record, the
// negotiated trace level must actually gate output (the `trace` knob was previously parsed but unused),
// and records must never reach the JSON-RPC channel.
bool runLspStructuredLoggingTests()
{
    const auto collect = [](const llvm::json::Value&              initializeParams,
                            const std::vector<llvm::json::Value>& extra) -> std::vector<std::string> {
        std::mutex                     mutex;
        std::vector<std::string>       logLines;
        std::vector<llvm::json::Value> outgoing;
        llvmdsdl::lsp::Server          server(
            [&mutex, &outgoing](llvm::json::Value message) {
                std::scoped_lock const lock(mutex);
                outgoing.push_back(std::move(message));
            },
            {},
            [&mutex, &logLines](const std::string& line) {
                std::scoped_lock const lock(mutex);
                logLines.push_back(line);
            });
        server.handleMessage(
            llvm::json::Object{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}, {"params", initializeParams}});
        for (const auto& message : extra)
        {
            server.handleMessage(message);
        }
        std::scoped_lock const lock(mutex);
        return logLines;
    };

    // Built fresh per use: llvm::json::Value is constructed from an Object rvalue.
    const auto symbolRequest = []() {
        return llvm::json::Value(
            llvm::json::Object{{"jsonrpc", "2.0"},
                               {"id", 2},
                               {"method", "textDocument/documentSymbol"},
                               {"params",
                                llvm::json::Object{{"textDocument", llvm::json::Object{{"uri", "file:///x.dsdl"}}}}}});
    };

    // trace=off from initialize: the handshake and everything after it stay silent.
    const auto silent = collect(llvm::json::Object{{"trace", "off"}}, {symbolRequest()});
    if (!silent.empty())
    {
        std::cerr << "expected no log records at trace=off, got " << silent.size() << "\n";
        return false;
    }

    // trace=messages: one structured record per request, carrying the post-mortem fields.
    const auto basic = collect(llvm::json::Object{{"trace", "messages"}}, {symbolRequest()});
    if (basic.size() < 2)
    {
        std::cerr << "expected request records at trace=messages, got " << basic.size() << "\n";
        return false;
    }
    bool sawSymbolRequest = false;
    for (const std::string& line : basic)
    {
        llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(line);
        if (!parsed)
        {
            std::cerr << "log record is not valid JSON: " << line << "\n";
            return false;
        }
        const auto* object = parsed->getAsObject();
        if (!object || !object->getString("event") || !object->getString("level"))
        {
            std::cerr << "log record missing event/level: " << line << "\n";
            return false;
        }
        const auto method = object->getString("method");
        if (method && *method == "textDocument/documentSymbol")
        {
            if (object->getString("outcome") != "ok" || !object->getInteger("latency_us"))
            {
                std::cerr << "request record missing outcome/latency: " << line << "\n";
                return false;
            }
            sawSymbolRequest = true;
        }
    }
    if (!sawSymbolRequest)
    {
        std::cerr << "no structured record for the documentSymbol request\n";
        return false;
    }

    // $/setTrace must take effect at runtime: silence after switching off.
    const auto afterOff =
        collect(llvm::json::Object{{"trace", "messages"}},
                {llvm::json::Value(llvm::json::Object{{"jsonrpc", "2.0"},
                                                      {"method", "$/setTrace"},
                                                      {"params", llvm::json::Object{{"value", "off"}}}}),
                 symbolRequest()});
    for (const std::string& line : afterOff)
    {
        if (line.contains("documentSymbol"))
        {
            std::cerr << "request logged after $/setTrace off: " << line << "\n";
            return false;
        }
    }

    // A failed request is recorded as a warning so a post-mortem can find it.
    const auto errors   = collect(llvm::json::Object{{"trace", "messages"}},
                                  {llvm::json::Value(llvm::json::Object{{"jsonrpc", "2.0"},
                                                                        {"id", 3},
                                                                        {"method", "nope/bogus"},
                                                                        {"params", llvm::json::Object{}}})});
    bool       sawError = false;
    for (const std::string& line : errors)
    {
        if (line.contains(R"("event":"error_response")") && line.contains(R"("level":"warn")"))
        {
            sawError = true;
        }
    }
    if (!sawError)
    {
        std::cerr << "expected an error_response warning record for an unknown method\n";
        return false;
    }

    return true;
}

// The server represents positions as byte (UTF-8) offsets, so it must negotiate the LSP position
// encoding: advertise utf-8 when the client supports it (byte offsets are then correct by declaration)
// and fall back to the protocol's utf-16 default otherwise.
bool runLspPositionEncodingTests()
{
    const auto negotiatedEncoding = [](const llvm::json::Value& initializeParams) -> std::optional<std::string> {
        std::mutex                     mutex;
        std::vector<llvm::json::Value> outgoing;
        llvmdsdl::lsp::Server          server([&mutex, &outgoing](llvm::json::Value message) {
            std::scoped_lock const lock(mutex);
            outgoing.push_back(std::move(message));
        });
        server.handleMessage(
            llvm::json::Object{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}, {"params", initializeParams}});
        std::scoped_lock const lock(mutex);
        const auto*            response = findResponseByIntegerId(outgoing, 1);
        if (response == nullptr)
        {
            return std::nullopt;
        }
        const auto* capabilities = response->getObject("result");
        if (capabilities == nullptr)
        {
            return std::nullopt;
        }
        const auto* caps = capabilities->getObject("capabilities");
        if (caps == nullptr)
        {
            return std::nullopt;
        }
        const auto encoding = caps->getString("positionEncoding");
        if (!encoding.has_value())
        {
            return std::nullopt;
        }
        return encoding->str();
    };

    // Client advertises utf-8 support -> server negotiates utf-8.
    const auto utf8 = negotiatedEncoding(llvm::json::Object{
        {"capabilities",
         llvm::json::Object{
             {"general", llvm::json::Object{{"positionEncodings", llvm::json::Array{"utf-16", "utf-8"}}}}}}});
    if (utf8 != "utf-8")
    {
        std::cerr << "expected utf-8 negotiation, got " << utf8.value_or("<none>") << "\n";
        return false;
    }

    // Client offers only utf-16 -> server falls back to utf-16.
    const auto utf16Only = negotiatedEncoding(llvm::json::Object{
        {"capabilities",
         llvm::json::Object{{"general", llvm::json::Object{{"positionEncodings", llvm::json::Array{"utf-16"}}}}}}});
    if (utf16Only != "utf-16")
    {
        std::cerr << "expected utf-16 fallback, got " << utf16Only.value_or("<none>") << "\n";
        return false;
    }

    // Client (older) advertises no encodings -> server falls back to utf-16.
    const auto legacy = negotiatedEncoding(llvm::json::Object{{"capabilities", llvm::json::Object{}}});
    if (legacy != "utf-16")
    {
        std::cerr << "expected utf-16 for a client without positionEncodings, got " << legacy.value_or("<none>")
                  << "\n";
        return false;
    }

    return true;
}

// Language-feature requests carry editor-controlled positions and URIs. These must never crash on
// out-of-bounds positions, documents that were never opened or already closed, malformed params, or
// degenerate document contents; the server must answer each gracefully and stay responsive afterwards.
bool runLspAdversarialRequestTests()
{
    std::mutex                     mutex;
    std::condition_variable        cv;
    std::vector<llvm::json::Value> outgoing;

    llvmdsdl::lsp::Server server([&mutex, &cv, &outgoing](llvm::json::Value message) {
        {
            std::scoped_lock const lock(mutex);
            outgoing.push_back(std::move(message));
        }
        cv.notify_all();
    });

    server.handleMessage(parseJson(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})"));

    const std::string uri = "file:///tmp/adversarial.dsdl";
    server.handleMessage(llvm::json::Object{
        {"jsonrpc", "2.0"},
        {"method", "textDocument/didOpen"},
        {"params",
         llvm::json::Object{
             {"textDocument", llvm::json::Object{{"uri", uri}, {"version", 1}, {"text", "uint8 value\n@sealed\n"}}}}},
    });

    const auto positionParams = [](const std::string& targetUri, std::int64_t line, std::int64_t character) {
        return llvm::json::Object{
            {"textDocument", llvm::json::Object{{"uri", targetUri}}},
            {"position", llvm::json::Object{{"line", line}, {"character", character}}},
        };
    };

    // Each entry is a request the server must survive. Feature methods return null/empty on any miss.
    std::vector<llvm::json::Value> hostileRequests;
    const char* const              featureMethods[] = {"textDocument/hover",
                                                       "textDocument/definition",
                                                       "textDocument/references",
                                                       "textDocument/completion"};
    std::int64_t                   nextId           = 100;
    for (const char* method : featureMethods)
    {
        // Position far beyond the document (max uint32) on an open document.
        hostileRequests.emplace_back(llvm::json::Object{{"jsonrpc", "2.0"},
                                                        {"id", nextId++},
                                                        {"method", method},
                                                        {"params", positionParams(uri, 4294967295LL, 4294967295LL)}});
        // A document that was never opened.
        hostileRequests.emplace_back(
            llvm::json::Object{{"jsonrpc", "2.0"},
                               {"id", nextId++},
                               {"method", method},
                               {"params", positionParams("file:///tmp/never-opened.dsdl", 0, 0)}});
        // Missing position object entirely.
        hostileRequests.emplace_back(
            llvm::json::Object{{"jsonrpc", "2.0"},
                               {"id", nextId++},
                               {"method", method},
                               {"params", llvm::json::Object{{"textDocument", llvm::json::Object{{"uri", uri}}}}}});
        // Position fields present but the wrong JSON type.
        hostileRequests.emplace_back(
            llvm::json::Object{{"jsonrpc", "2.0"},
                               {"id", nextId++},
                               {"method", method},
                               {"params",
                                llvm::json::Object{{"textDocument", llvm::json::Object{{"uri", uri}}},
                                                   {"position",
                                                    llvm::json::Object{{"line", "nan"}, {"character", true}}}}}});
        // Params missing altogether.
        hostileRequests.emplace_back(llvm::json::Object{{"jsonrpc", "2.0"}, {"id", nextId++}, {"method", method}});
    }
    // Structural requests on a never-opened document.
    hostileRequests.emplace_back(
        llvm::json::Object{{"jsonrpc", "2.0"},
                           {"id", nextId++},
                           {"method", "textDocument/documentSymbol"},
                           {"params",
                            llvm::json::Object{
                                {"textDocument", llvm::json::Object{{"uri", "file:///tmp/never-opened.dsdl"}}}}}});
    hostileRequests.emplace_back(
        llvm::json::Object{{"jsonrpc", "2.0"},
                           {"id", nextId++},
                           {"method", "textDocument/semanticTokens/full"},
                           {"params",
                            llvm::json::Object{
                                {"textDocument", llvm::json::Object{{"uri", "file:///tmp/never-opened.dsdl"}}}}}});

    for (const auto& request : hostileRequests)
    {
        server.handleMessage(request);
    }

    // Degenerate document contents, then feature requests over them.
    server.handleMessage(
        llvm::json::Object{{"jsonrpc", "2.0"},
                           {"method", "textDocument/didChange"},
                           {"params",
                            llvm::json::Object{{"textDocument", llvm::json::Object{{"uri", uri}, {"version", 2}}},
                                               {"contentChanges",
                                                llvm::json::Array{llvm::json::Object{{"text", ""}}}}}}});
    server.handleMessage(llvm::json::Object{{"jsonrpc", "2.0"},
                                            {"id", nextId++},
                                            {"method", "textDocument/hover"},
                                            {"params", positionParams(uri, 0, 0)}});
    server.handleMessage(llvm::json::Object{{"jsonrpc", "2.0"},
                                            {"id", nextId++},
                                            {"method", "textDocument/completion"},
                                            {"params", positionParams(uri, 100, 100)}});

    // Close the document, then request against it.
    server.handleMessage(
        llvm::json::Object{{"jsonrpc", "2.0"},
                           {"method", "textDocument/didClose"},
                           {"params", llvm::json::Object{{"textDocument", llvm::json::Object{{"uri", uri}}}}}});
    server.handleMessage(llvm::json::Object{{"jsonrpc", "2.0"},
                                            {"id", nextId++},
                                            {"method", "textDocument/definition"},
                                            {"params", positionParams(uri, 0, 0)}});

    // Sentinel: after the barrage the server must still answer a well-formed request.
    server.handleMessage(
        llvm::json::Object{{"jsonrpc", "2.0"},
                           {"id", 999999},
                           {"method", "textDocument/didOpen"},
                           {"params",
                            llvm::json::Object{
                                {"textDocument",
                                 llvm::json::Object{{"uri", uri}, {"version", 3}, {"text", "uint8 ok\n@sealed\n"}}}}}});
    server.handleMessage(
        llvm::json::Object{{"jsonrpc", "2.0"},
                           {"id", 999999},
                           {"method", "textDocument/documentSymbol"},
                           {"params", llvm::json::Object{{"textDocument", llvm::json::Object{{"uri", uri}}}}}});
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!cv.wait_for(lock, std::chrono::seconds(5), [&outgoing]() {
                return findResponseByIntegerId(outgoing, 999999) != nullptr;
            }))
        {
            std::cerr << "server unresponsive after adversarial request barrage\n";
            return false;
        }
    }

    return true;
}
