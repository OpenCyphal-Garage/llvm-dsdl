//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Stdio JSON-RPC framing utilities for Language Server Protocol transport.
///
/// Messages are encoded with `Content-Length` framing over stdio and decoded
/// into LLVM JSON values.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_LSP_JSON_RPC_IO_H
#define LLVMDSDL_LSP_JSON_RPC_IO_H

#include "llvm/Support/JSON.h"

#include <cstddef>
#include <iosfwd>
#include <mutex>
#include <string>

namespace llvmdsdl::lsp
{

/// @brief JSON-RPC stdio transport with `Content-Length` framing.
class JsonRpcStdioTransport final
{
public:
    /// @brief Default upper bound on an accepted `Content-Length` (64 MiB).
    ///
    /// The payload buffer is allocated up front from the client-supplied length,
    /// so an unbounded value would let a malformed or hostile header trigger an
    /// out-of-memory abort. This cap keeps a single message allocation bounded;
    /// legitimate LSP traffic is far below it.
    static constexpr std::size_t kDefaultMaxContentLength = static_cast<std::size_t>(64) * 1024 * 1024;

    /// @brief Creates a transport over input and output streams.
    /// @param[in] in Input stream.
    /// @param[in] out Output stream.
    /// @param[in] maxContentLength Largest `Content-Length` accepted before the
    /// frame is rejected without allocating the payload buffer.
    JsonRpcStdioTransport(std::istream& in,
                          std::ostream& out,
                          std::size_t   maxContentLength = kDefaultMaxContentLength);

    /// @brief Reads one framed JSON-RPC message.
    /// @param[out] message Parsed JSON payload.
    /// @param[out] error Parsing/framing error text when read fails.
    /// @return `true` when a full message is read and parsed.
    [[nodiscard]] bool readMessage(llvm::json::Value& message, std::string& error);

    /// @brief Writes one framed JSON-RPC message.
    /// @param[in] message JSON payload to write.
    /// @return `true` when write succeeds.
    [[nodiscard]] bool writeMessage(const llvm::json::Value& message);

private:
    std::istream& input_;
    std::ostream& output_;
    std::size_t   maxContentLength_;
    std::mutex    writeMutex_;
};

}  // namespace llvmdsdl::lsp

#endif  // LLVMDSDL_LSP_JSON_RPC_IO_H
