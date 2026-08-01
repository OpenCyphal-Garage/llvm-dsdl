//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Runtime configuration model for the DSDL language server.
///
/// Configuration values are updated from `workspace/didChangeConfiguration`
/// notifications and consumed by server subsystems like linting and tracing.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_LSP_SERVER_CONFIG_H
#define LLVMDSDL_LSP_SERVER_CONFIG_H

#include "llvm/Support/JSON.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace llvmdsdl::lsp
{

/// @brief Operating mode for optional AI-assisted LSP features.
enum class AiMode
{
    /// @brief Disable all AI-assisted behaviour.
    Off,

    /// @brief Offer read-only suggestions and explanations.
    Suggest,

    /// @brief Offer deeper suggestions while still requiring explicit edit confirmation.
    Assist,

    /// @brief Allow edit materialization only after explicit confirmation.
    ApplyWithConfirmation,
};

/// @brief Trace verbosity level for server logs and telemetry.
enum class TraceLevel
{
    /// @brief Disable trace output.
    Off,

    /// @brief Emit concise request-level traces.
    Basic,

    /// @brief Emit verbose traces for debugging.
    Verbose,
};

/// @brief Mutable runtime configuration for `dsdld`.
struct ServerConfig final
{
    /// @brief Root namespace directories used for discovery.
    std::vector<std::string> rootNamespaceDirs;

    /// @brief Additional lookup directories for dependencies.
    std::vector<std::string> lookupDirs;

    /// @brief Directory for persisted workspace index shards.
    std::string indexCacheDir;

    /// @brief Enables lint diagnostics when true.
    bool lintEnabled{true};

    /// @brief Workspace-level disabled lint rule IDs.
    std::unordered_set<std::string> lintDisabledRules;

    /// @brief Per-file disabled lint rule IDs keyed by URI or normalized path.
    std::unordered_map<std::string, std::unordered_set<std::string>> lintFileDisabledRules;

    /// @brief Dynamic lint rule-pack library paths.
    std::vector<std::string> lintPluginLibraries;

    /// @brief AI feature mode for suggestions/tooling and confirmation-gated edits.
    AiMode aiMode{AiMode::Off};

    /// @brief Enables optional semantic-to-MLIR snapshot generation.
    bool enableMlirSnapshot{false};

    /// @brief Configured trace verbosity.
    TraceLevel traceLevel{TraceLevel::Basic};
};

/// @brief Applies settings from `workspace/didChangeConfiguration` params.
/// @param[in] params Notification params object.
/// @param[in,out] config Configuration instance to update.
/// @return `true` when params had a parseable `settings` object.
[[nodiscard]] bool applyDidChangeConfiguration(const llvm::json::Value& params, ServerConfig& config);

}  // namespace llvmdsdl::lsp

#endif  // LLVMDSDL_LSP_SERVER_CONFIG_H
