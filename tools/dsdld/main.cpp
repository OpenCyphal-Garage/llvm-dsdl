//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Entry point for the `dsdld` Language Server Protocol executable.
///
/// The process runs a stdio JSON-RPC loop and dispatches protocol messages to
/// the LSP server core.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/LSP/JsonRpcIO.h"
#include "llvmdsdl/LSP/Server.h"
#include "llvmdsdl/Version.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdint>
#include <iostream>

int main(int argc, char** argv)
{
    llvm::InitLLVM y(argc, argv);
    for (int i = 1; i < argc; ++i)
    {
        const llvm::StringRef arg(argv[i]);
        if (arg == "--version" || arg == "-V")
        {
            llvm::outs() << "dsdld " << llvmdsdl::kVersionString << "\n";
            return 0;
        }
    }

    llvmdsdl::lsp::JsonRpcStdioTransport transport(std::cin, std::cout);
    llvmdsdl::lsp::Server                server(
        [&transport](llvm::json::Value message) {
            if (!transport.writeMessage(message))
            {
                llvm::errs() << "[dsdld] failed to write JSON-RPC message\n";
            }
        },
        [](const llvmdsdl::lsp::RequestMetric& metric) {
            llvm::errs() << "[dsdld][telemetry] method=" << metric.method
                         << " latency_us=" << static_cast<std::uint64_t>(metric.latencyMicros)
                         << " cancelled=" << (metric.cancelled ? "true" : "false") << "\n";
        });

    while (!server.shouldExit())
    {
        llvm::json::Value message(llvm::json::Object{});
        std::string       error;
        bool              recoverable = false;
        if (!transport.readMessage(message, error, &recoverable))
        {
            if (!error.empty())
            {
                llvm::errs() << "[dsdld] " << error << "\n";
            }
            if (recoverable)
            {
                // A single malformed-JSON message must not kill the server: the stream is
                // still framed, so reply with a JSON-RPC parse error (id null) and continue.
                (void) transport.writeMessage(llvm::json::Object{
                    {"jsonrpc", "2.0"},
                    {"id", nullptr},
                    {"error", llvm::json::Object{{"code", -32700}, {"message", "Parse error"}}},
                });
                continue;
            }
            break;
        }
        server.handleMessage(message);
    }

    server.shutdown();
    return server.exitCode();
}
