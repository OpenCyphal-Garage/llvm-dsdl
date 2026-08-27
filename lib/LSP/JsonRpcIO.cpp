//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements `Content-Length` framed JSON-RPC stdio transport.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/LSP/JsonRpcIO.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

#include <cstddef>
#include <istream>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>

namespace llvmdsdl::lsp
{
namespace
{

bool parseContentLengthHeader(const std::string& line, std::size_t& contentLength)
{
    static constexpr llvm::StringRef Prefix = "Content-Length:";
    llvm::StringRef                  header = line;
    if (!header.consume_front(Prefix))
    {
        return false;
    }
    header = header.trim();
    if (header.empty())
    {
        return false;
    }
    std::size_t value    = 0;
    bool        overflow = false;
    for (const char ch : header)
    {
        if (ch < '0' || ch > '9')
        {
            return false;
        }
        const auto digit = static_cast<std::size_t>(ch - '0');
        // Saturate instead of wrapping so an absurdly long digit string is
        // rejected by the size cap in readMessage rather than silently
        // overflowing size_t into a small, valid-looking length.
        if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10U)
        {
            overflow = true;
            break;
        }
        value = (value * 10U) + digit;
    }
    contentLength = overflow ? std::numeric_limits<std::size_t>::max() : value;
    return true;
}

// Bounds on the header section, enforced BEFORE the Content-Length cap can apply
// (the payload cap does nothing for a hostile header that never terminates a line
// or streams unlimited header lines — both would balloon memory in the read loop).
// Real LSP headers are a few dozen bytes; these limits are generous.
constexpr std::size_t kMaxHeaderLineBytes = std::size_t{8} * 1024;
constexpr std::size_t kMaxHeaderBytes     = std::size_t{64} * 1024;

// Reads one CRLF/LF-terminated header line into `line`, bounded to kMaxHeaderLineBytes
// so a line without a terminator cannot grow without limit. Returns false on stream end
// or when the per-line cap is exceeded (caller treats the latter as a framing error).
bool readBoundedHeaderLine(std::istream& input, std::string& line, bool& overflowed)
{
    line.clear();
    overflowed = false;
    int ch     = 0;
    while ((ch = input.get()) != std::char_traits<char>::eof())
    {
        if (ch == '\n')
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            return true;
        }
        if (line.size() >= kMaxHeaderLineBytes)
        {
            overflowed = true;
            return true;  // a full (over-long) line is available; caller rejects it
        }
        line.push_back(static_cast<char>(ch));
    }
    return false;  // stream ended before a newline
}

}  // namespace

JsonRpcStdioTransport::JsonRpcStdioTransport(std::istream& in, std::ostream& out, std::size_t maxContentLength)
    : input_(in)
    , output_(out)
    , maxContentLength_(maxContentLength)
{
}

bool JsonRpcStdioTransport::readMessage(llvm::json::Value& message, std::string& error, bool* recoverable)
{
    if (recoverable != nullptr)
    {
        *recoverable = false;
    }
    std::size_t contentLength    = 0U;
    bool        hasHeaders       = false;
    std::size_t headerBytesTotal = 0U;
    std::string line;
    bool        lineOverflowed = false;
    while (readBoundedHeaderLine(input_, line, lineOverflowed))
    {
        if (lineOverflowed)
        {
            error = "JSON-RPC header line exceeds maximum";
            return false;
        }
        headerBytesTotal += line.size() + 2U;  // account for the stripped CRLF
        if (headerBytesTotal > kMaxHeaderBytes)
        {
            error = "JSON-RPC header section exceeds maximum";
            return false;
        }
        if (line.empty())
        {
            break;
        }

        hasHeaders = true;
        if (std::size_t parsedLength = 0; parseContentLengthHeader(line, parsedLength))
        {
            contentLength = parsedLength;
        }
    }

    if (!hasHeaders)
    {
        return false;
    }

    if (contentLength == 0U)
    {
        error = "missing Content-Length header";
        return false;
    }

    if (contentLength > maxContentLength_)
    {
        error = "Content-Length exceeds maximum";
        return false;
    }

    std::string payload(contentLength, '\0');
    input_.read(payload.data(), static_cast<std::streamsize>(contentLength));
    if (input_.gcount() != static_cast<std::streamsize>(contentLength))
    {
        error = "truncated JSON-RPC payload";
        return false;
    }

    llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(payload);
    if (!parsed)
    {
        std::string              parseMessage;
        llvm::raw_string_ostream parseStream(parseMessage);
        parseStream << parsed.takeError();
        parseStream.flush();
        error = "invalid JSON payload: " + parseMessage;
        // The frame was well-formed and its payload fully consumed, so the stream is still
        // synchronized at the next frame — the caller can reply with a parse error and
        // keep serving rather than terminate on a single bad message.
        if (recoverable != nullptr)
        {
            *recoverable = true;
        }
        return false;
    }

    message = std::move(*parsed);
    return true;
}

bool JsonRpcStdioTransport::writeMessage(const llvm::json::Value& message)
{
    std::string              payload;
    llvm::raw_string_ostream payloadStream(payload);
    payloadStream << message;
    payloadStream.flush();

    std::scoped_lock const lock(writeMutex_);
    output_ << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
    output_.flush();
    return static_cast<bool>(output_);
}

}  // namespace llvmdsdl::lsp
