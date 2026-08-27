//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements structured, level-gated logging for `dsdld`.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/LSP/Logging.h"

#include <utility>

#include "llvm/Support/raw_ostream.h"

namespace llvmdsdl::lsp
{
namespace
{

llvm::StringRef levelName(const LogLevel level)
{
    switch (level)
    {
    case LogLevel::Error:
        return "error";
    case LogLevel::Warn:
        return "warn";
    case LogLevel::Info:
        return "info";
    case LogLevel::Debug:
        return "debug";
    }
    return "info";
}

/// @brief True when @p level passes the verbosity gate @p trace.
bool passes(const TraceLevel trace, const LogLevel level)
{
    switch (trace)
    {
    case TraceLevel::Off:
        return false;
    case TraceLevel::Basic:
        return level != LogLevel::Debug;
    case TraceLevel::Verbose:
        return true;
    }
    return false;
}

}  // namespace

StructuredLogger::StructuredLogger(LogSink sink)
    : sink_(std::move(sink))
{
}

void StructuredLogger::setTraceLevel(const TraceLevel level)
{
    const std::scoped_lock lock(mutex_);
    level_ = level;
}

TraceLevel StructuredLogger::traceLevel() const
{
    const std::scoped_lock lock(mutex_);
    return level_;
}

bool StructuredLogger::enabled(const LogLevel level) const
{
    if (!sink_)
    {
        return false;
    }
    const std::scoped_lock lock(mutex_);
    return passes(level_, level);
}

void StructuredLogger::log(const LogLevel level, const llvm::StringRef event, llvm::json::Object fields)
{
    if (!sink_)
    {
        return;
    }
    // Serialise formatting and the sink write: records are emitted from both the main thread and the
    // request-scheduler worker, and an interleaved line would defeat the point of a post-mortem log.
    const std::scoped_lock lock(mutex_);
    if (!passes(level_, level))
    {
        return;
    }
    fields["level"] = levelName(level);
    fields["event"] = event;

    std::string              line;
    llvm::raw_string_ostream stream(line);
    stream << llvm::json::Value(std::move(fields));
    stream.flush();
    sink_(line);
}

}  // namespace llvmdsdl::lsp
