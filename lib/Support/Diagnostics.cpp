//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements diagnostic collection and formatting helpers.
///
/// The diagnostic engine records source-aware notes, warnings, and errors consumed throughout the frontend pipeline.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Support/Diagnostics.h"

#include <algorithm>
#include <utility>

namespace llvmdsdl
{

void DiagnosticEngine::report(DiagnosticLevel       level,
                              const SourceLocation& location,
                              std::string           message,
                              const std::uint32_t   length)
{
    report(level, location, std::move(message), length, std::nullopt);
}

void DiagnosticEngine::report(DiagnosticLevel                   level,
                              const SourceLocation&             location,
                              std::string                       message,
                              const std::uint32_t               length,
                              const std::optional<std::int64_t> suggestedInteger)
{
    diagnostics_.push_back(
        Diagnostic{level, location, std::move(message), std::max<std::uint32_t>(1, length), suggestedInteger});
}

void DiagnosticEngine::note(const SourceLocation& location, std::string message, const std::uint32_t length)
{
    note(location, std::move(message), length, std::nullopt);
}

void DiagnosticEngine::note(const SourceLocation&             location,
                            std::string                       message,
                            const std::uint32_t               length,
                            const std::optional<std::int64_t> suggestedInteger)
{
    report(DiagnosticLevel::Note, location, std::move(message), length, suggestedInteger);
}

void DiagnosticEngine::warning(const SourceLocation& location, std::string message, const std::uint32_t length)
{
    warning(location, std::move(message), length, std::nullopt);
}

void DiagnosticEngine::warning(const SourceLocation&             location,
                               std::string                       message,
                               const std::uint32_t               length,
                               const std::optional<std::int64_t> suggestedInteger)
{
    report(DiagnosticLevel::Warning, location, std::move(message), length, suggestedInteger);
}

void DiagnosticEngine::error(const SourceLocation& location, std::string message, const std::uint32_t length)
{
    error(location, std::move(message), length, std::nullopt);
}

void DiagnosticEngine::error(const SourceLocation&             location,
                             std::string                       message,
                             const std::uint32_t               length,
                             const std::optional<std::int64_t> suggestedInteger)
{
    report(DiagnosticLevel::Error, location, std::move(message), length, suggestedInteger);
}

bool DiagnosticEngine::hasErrors() const
{
    return std::ranges::any_of(diagnostics_, [](const Diagnostic& d) { return d.level == DiagnosticLevel::Error; });
}

}  // namespace llvmdsdl
