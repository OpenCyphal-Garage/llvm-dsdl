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

namespace
{

/// @brief Prints the `dsdld` usage summary.
/// @details Laid out in the same section shape as `dsdlc --help` (NAME / SYNOPSIS / ...) because the
///   man pages are generated from this text -- see tools/man/generate_manpage.py.
void printUsage()
{
    llvm::outs() << R"(NAME
  dsdld - Language Server Protocol server for DSDL

SYNOPSIS
  dsdld
  dsdld --help
  dsdld --version

DESCRIPTION
  Runs a Language Server Protocol server over stdio, reusing the same frontend,
  AST, and semantic pipeline as dsdlc. It is normally launched by an editor
  rather than invoked directly; with no arguments it reads JSON-RPC messages
  from standard input and writes responses to standard output.

  Standard output carries only JSON-RPC frames. Structured log records are
  written to standard error, honouring the trace level negotiated by the
  client.

OPTIONS
  --help, -h
      Print this message and exit.
  --version, -V
      Print the version and exit.
)";
}

}  // namespace

int main(int argc, char** argv)
{
    llvm::InitLLVM const y(argc, argv);
    for (int i = 1; i < argc; ++i)
    {
        const llvm::StringRef arg(argv[i]);
        if (arg == "--version" || arg == "-V")
        {
            llvm::outs() << "dsdld " << llvmdsdl::kVersionString << "\n";
            return 0;
        }
        // Without this, --help falls through to the JSON-RPC loop and blocks on stdin, so asking a
        // language server for help hangs the terminal.
        if (arg == "--help" || arg == "-h")
        {
            printUsage();
            return 0;
        }
    }

    llvmdsdl::lsp::JsonRpcStdioTransport transport(std::cin, std::cout);
    llvmdsdl::lsp::Server                server(
        [&transport](const llvm::json::Value& message) {
            if (!transport.writeMessage(message))
            {
                llvm::errs() << "[dsdld] failed to write JSON-RPC message\n";
            }
        },
        // No external telemetry sink: the structured log below carries the same per-request facts
        // (method / latency_us / outcome) as parseable JSON, and unlike the previous ad-hoc line it
        // honours the negotiated trace level instead of printing on every request unconditionally.
        /*metricSink=*/{},
        // Structured records go to stderr: stdout carries the JSON-RPC frames and must never be
        // interleaved with log text. The logger serialises writes, so lines stay intact across the
        // main thread and the request-scheduler worker.
        [](const std::string& line) { llvm::errs() << line << '\n'; });

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
