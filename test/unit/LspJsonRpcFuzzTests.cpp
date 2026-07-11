//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Validates malformed JSON-RPC framing/payload handling for LSP components.
///
/// The test suite intentionally feeds malformed and fuzz-like inputs through
/// transport and server entrypoints to ensure failures are reported without
/// crashes.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/LSP/JsonRpcIO.h"
#include "llvmdsdl/LSP/Server.h"
#include "llvm/Support/JSON.h"

#include <cstdint>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
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

std::string encodeLspFrame(const std::string& payload)
{
    std::ostringstream out;
    out << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
    return out.str();
}

}  // namespace

bool runLspJsonRpcFuzzTests()
{
    {
        std::istringstream                   in("Header: value\r\n\r\n{}");
        std::ostringstream                   out;
        llvmdsdl::lsp::JsonRpcStdioTransport transport(in, out);
        llvm::json::Value                    message(llvm::json::Object{});
        std::string                          error;
        if (transport.readMessage(message, error) || error != "missing Content-Length header")
        {
            std::cerr << "expected missing Content-Length framing error\n";
            return false;
        }
    }

    {
        std::istringstream                   in("Content-Length: abc\r\n\r\n{}");
        std::ostringstream                   out;
        llvmdsdl::lsp::JsonRpcStdioTransport transport(in, out);
        llvm::json::Value                    message(llvm::json::Object{});
        std::string                          error;
        if (transport.readMessage(message, error) || error != "missing Content-Length header")
        {
            std::cerr << "expected invalid Content-Length to fail as missing header\n";
            return false;
        }
    }

    {
        std::istringstream                   in("Content-Length: 10\r\n\r\n{}");
        std::ostringstream                   out;
        llvmdsdl::lsp::JsonRpcStdioTransport transport(in, out);
        llvm::json::Value                    message(llvm::json::Object{});
        std::string                          error;
        if (transport.readMessage(message, error) || error != "truncated JSON-RPC payload")
        {
            std::cerr << "expected truncated payload error\n";
            return false;
        }
    }

    {
        const std::string                    payload = R"({"jsonrpc":2.0)";
        std::istringstream                   in(encodeLspFrame(payload));
        std::ostringstream                   out;
        llvmdsdl::lsp::JsonRpcStdioTransport transport(in, out);
        llvm::json::Value                    message(llvm::json::Object{});
        std::string                          error;
        if (transport.readMessage(message, error) || error.find("invalid JSON payload: ") != 0U)
        {
            std::cerr << "expected invalid JSON payload error\n";
            return false;
        }
    }

    {
        // A Content-Length above the configured cap must be rejected before the
        // payload buffer is allocated (unbounded-allocation / OOM DoS guard).
        std::istringstream                   in("Content-Length: 100\r\n\r\n");
        std::ostringstream                   out;
        llvmdsdl::lsp::JsonRpcStdioTransport transport(in, out, /*maxContentLength=*/16U);
        llvm::json::Value                    message(llvm::json::Object{});
        std::string                          error;
        if (transport.readMessage(message, error) || error != "Content-Length exceeds maximum")
        {
            std::cerr << "expected oversized Content-Length to be rejected\n";
            return false;
        }
    }

    {
        // A Content-Length long enough to overflow size_t must saturate and be
        // rejected by the cap, never wrap to a small valid-looking length.
        std::istringstream                   in("Content-Length: 999999999999999999999999999999\r\n\r\n");
        std::ostringstream                   out;
        llvmdsdl::lsp::JsonRpcStdioTransport transport(in, out);
        llvm::json::Value                    message(llvm::json::Object{});
        std::string                          error;
        if (transport.readMessage(message, error) || error != "Content-Length exceeds maximum")
        {
            std::cerr << "expected overflowing Content-Length to be rejected\n";
            return false;
        }
    }

    {
        // A legitimately framed message at exactly the cap size still parses, so
        // the bound rejects only oversized frames.
        const std::string                    payload = "{}";
        std::istringstream                   in(encodeLspFrame(payload));
        std::ostringstream                   out;
        llvmdsdl::lsp::JsonRpcStdioTransport transport(in, out, /*maxContentLength=*/payload.size());
        llvm::json::Value                    message(llvm::json::Object{});
        std::string                          error;
        if (!transport.readMessage(message, error) || !error.empty())
        {
            std::cerr << "expected in-bound message to parse cleanly, got error: " << error << "\n";
            return false;
        }
    }

    {
        std::istringstream                   in;
        std::ostringstream                   out;
        llvmdsdl::lsp::JsonRpcStdioTransport transport(in, out);
        const llvm::json::Value              message = llvm::json::Object{
                         {"jsonrpc", "2.0"},
                         {"id", 1},
                         {"method", "initialize"},
        };
        if (!transport.writeMessage(message))
        {
            std::cerr << "writeMessage unexpectedly failed\n";
            return false;
        }
        const std::string framed = out.str();
        if (framed.find("Content-Length: ") != 0U || framed.find("\"initialize\"") == std::string::npos)
        {
            std::cerr << "writeMessage emitted unexpected frame\n";
            return false;
        }
    }

    std::vector<llvm::json::Value> outgoing;
    llvmdsdl::lsp::Server server([&outgoing](llvm::json::Value message) { outgoing.push_back(std::move(message)); });

    server.handleMessage(parseJson(R"({"jsonrpc":"2.0","id":1,"method":"no/such/method","params":{}})"));
    {
        const auto* response = findResponseByIntegerId(outgoing, 1);
        const auto* error    = response ? response->getObject("error") : nullptr;
        if (!error || error->getInteger("code").value_or(0) != -32601)
        {
            std::cerr << "expected method-not-found response for unknown request\n";
            return false;
        }
    }

    // Fuzz-like malformed values at the server entrypoint.
    server.handleMessage(llvm::json::Value(nullptr));
    server.handleMessage(llvm::json::Array{});
    server.handleMessage(llvm::json::Object{{"jsonrpc", "2.0"}});
    server.handleMessage(llvm::json::Object{{"jsonrpc", "2.0"}, {"method", 99}});
    server.handleMessage(llvm::json::Object{{"jsonrpc", "2.0"}, {"id", 2}, {"method", ""}});

    std::mt19937                       rng(0xD5D1u);
    std::uniform_int_distribution<int> methodLength(0, 18);
    std::uniform_int_distribution<int> charDist(0, 25);
    std::uniform_int_distribution<int> idDist(3, 200);
    for (int iteration = 0; iteration < 256; ++iteration)
    {
        std::string method;
        const int   length = methodLength(rng);
        method.reserve(static_cast<std::size_t>(length));
        for (int idx = 0; idx < length; ++idx)
        {
            method.push_back(static_cast<char>('a' + charDist(rng)));
        }

        llvm::json::Object message{
            {"jsonrpc", "2.0"},
            {"method", method},
        };
        if (iteration % 2 == 0)
        {
            message["id"] = idDist(rng);
        }
        if (iteration % 3 == 0)
        {
            message["params"] = llvm::json::Array{llvm::json::Value(nullptr)};
        }
        server.handleMessage(llvm::json::Value(std::move(message)));
    }

    // Server should remain healthy after malformed traffic.
    server.handleMessage(parseJson(R"({"jsonrpc":"2.0","id":9000,"method":"initialize","params":{}})"));
    const auto* initializeResponse = findResponseByIntegerId(outgoing, 9000);
    if (!initializeResponse || !initializeResponse->getObject("result"))
    {
        std::cerr << "server did not recover after malformed input fuzzing\n";
        return false;
    }

    server.handleMessage(parseJson(R"({"jsonrpc":"2.0","id":9001,"method":"shutdown","params":null})"));
    server.handleMessage(parseJson(R"({"jsonrpc":"2.0","method":"exit"})"));
    if (!server.shutdownRequested() || !server.shouldExit())
    {
        std::cerr << "expected orderly shutdown after malformed input fuzzing\n";
        return false;
    }

    return true;
}
