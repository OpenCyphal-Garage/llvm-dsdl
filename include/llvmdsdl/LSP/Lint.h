//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Extensible linting subsystem for DSDL LSP analysis.
///
/// The lint subsystem provides deterministic rule execution, suppression
/// controls, optional autofix edits, and plugin extension points.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_LSP_LINT_H
#define LLVMDSDL_LSP_LINT_H

#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Frontend/Discovery.h"
#include "llvmdsdl/Frontend/SourceLocation.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace llvmdsdl::lsp
{

/// @brief Lint severity for one finding.
enum class LintSeverity
{
    /// @brief Informational recommendation.
    Info,

    /// @brief Non-fatal issue.
    Warning,

    /// @brief Rule violation requiring attention.
    Error,
};

/// @brief One source edit attached to a lint autofix.
struct LintFixEdit final
{
    /// @brief Zero-based line.
    std::uint32_t line{0};

    /// @brief Zero-based character.
    std::uint32_t character{0};

    /// @brief Replaced span length.
    std::uint32_t length{0};

    /// @brief Replacement text.
    std::string newText;
};

/// @brief One lint finding emitted by a rule.
struct LintFinding final
{
    /// @brief Rule identifier.
    std::string ruleId;

    /// @brief Source URI for the finding.
    std::string uri;

    /// @brief Source location.
    SourceLocation location;

    /// @brief Human-readable message.
    std::string message;

    /// @brief Severity level.
    LintSeverity severity{LintSeverity::Warning};

    /// @brief True when autofix edits are provided.
    bool hasFix{false};

    /// @brief True when autofix is preferred.
    bool preferredFix{false};

    /// @brief Autofix edits.
    std::vector<LintFixEdit> fixes;
};

/// @brief Document snapshot supplied to lint rules.
struct LintDocument final
{
    /// @brief Normalized source path.
    std::string path;

    /// @brief Source URI.
    std::string uri;

    /// @brief Discovery metadata.
    DiscoveredDefinition info;

    /// @brief Parsed AST.
    DefinitionAST ast;

    /// @brief Full source text.
    std::string sourceText;
};

/// @brief Rule execution options and suppression model.
struct LintExecutionConfig final
{
    /// @brief Enables lint execution when true.
    bool enabled{true};

    /// @brief Workspace-level disabled rule IDs.
    std::unordered_set<std::string> disabledRules;

    /// @brief Per-file disabled rule IDs keyed by URI or normalized path.
    std::unordered_map<std::string, std::unordered_set<std::string>> fileDisabledRules;

    /// @brief Dynamic rule-pack library paths.
    std::vector<std::string> pluginLibraries;
};

/// @brief Interface implemented by one lint rule.
class LintRule
{
public:
    virtual ~LintRule() = default;

    /// @brief Returns stable rule identifier.
    /// @return Rule ID.
    [[nodiscard]] virtual std::string id() const = 0;

    /// @brief Returns one-line rule title.
    /// @return Rule title.
    [[nodiscard]] virtual std::string title() const = 0;

    /// @brief Executes the rule for one document.
    /// @param[in] document Document snapshot.
    /// @param[out] findings Findings appended by the rule.
    virtual void run(const LintDocument& document, std::vector<LintFinding>& findings) const = 0;
};

/// @brief Rule factory callback.
using LintRuleFactory = std::function<std::unique_ptr<LintRule>()>;

/// @brief Registry used to install built-in and external lint rules.
class LintRegistry final
{
public:
    /// @brief Constructs registry with built-in rules installed.
    LintRegistry();

    /// @brief Registers one rule factory.
    /// @param[in] factory Rule factory callback.
    void registerRuleFactory(LintRuleFactory factory);

    /// @brief Loads external rules from a shared library.
    /// @param[in] libraryPath Shared library path.
    /// @param[out] errorMessage Optional failure detail.
    /// @return `true` on success.
    [[nodiscard]] bool loadPluginLibrary(const std::string& libraryPath, std::string* errorMessage = nullptr);

    /// @brief Materializes all registered rule instances.
    /// @return Rule instances sorted by rule ID.
    [[nodiscard]] std::vector<std::unique_ptr<LintRule>> createRules() const;

private:
    struct PluginHandle;

    std::vector<LintRuleFactory>               factories_;
    std::vector<std::shared_ptr<PluginHandle>> pluginHandles_;
};

/// @brief Result payload from lint engine execution.
struct LintRunResult final
{
    /// @brief Findings grouped by URI.
    std::unordered_map<std::string, std::vector<LintFinding>> findingsByUri;

    /// @brief Flat finding list sorted deterministically.
    std::vector<LintFinding> findings;
};

/// @brief Deterministic lint execution engine.
class LintEngine final
{
public:
    /// @brief Constructs engine with registry and execution options.
    /// @param[in] registry Rule registry.
    /// @param[in] config Execution options and suppressions.
    LintEngine(LintRegistry registry, LintExecutionConfig config);

    /// @brief Runs lint rules over all documents.
    /// @param[in] documents Document snapshots.
    /// @return Lint run result.
    [[nodiscard]] LintRunResult run(const std::vector<LintDocument>& documents) const;

    /// @brief Returns baseline built-in rule IDs.
    /// @return Sorted rule IDs.
    [[nodiscard]] static std::vector<std::string> baselineRuleIds();

private:
    [[nodiscard]] bool isSuppressed(const LintDocument&                    document,
                                    const std::unordered_set<std::string>& sourceSuppressed,
                                    const std::string&                     ruleId) const;
    [[nodiscard]] static std::unordered_set<std::string> parseSourceSuppressions(const std::string& sourceText);

    LintRegistry        registry_;
    LintExecutionConfig config_;
};

}  // namespace llvmdsdl::lsp

#endif  // LLVMDSDL_LSP_LINT_H
