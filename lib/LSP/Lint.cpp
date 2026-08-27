//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements rule registration, suppression handling, and lint execution.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/LSP/Lint.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <dlfcn.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace llvmdsdl::lsp
{
namespace
{

std::string toLower(std::string text)
{
    std::ranges::transform(text, text.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::string trim(const std::string& text)
{
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])))
    {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1U])))
    {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool isSnakeCase(const std::string& text)
{
    if (text.empty())
    {
        return false;
    }
    if (std::isdigit(static_cast<unsigned char>(text.front())))
    {
        return false;
    }
    for (const unsigned char c : text)
    {
        if (!std::islower(c) && !std::isdigit(c) && c != '_')
        {
            return false;
        }
    }
    return true;
}

bool isUpperSnakeCase(const std::string& text)
{
    if (text.empty())
    {
        return false;
    }
    if (std::isdigit(static_cast<unsigned char>(text.front())))
    {
        return false;
    }
    for (const unsigned char c : text)
    {
        if (!std::isupper(c) && !std::isdigit(c) && c != '_')
        {
            return false;
        }
    }
    return true;
}

bool isPascalCase(const std::string& text)
{
    if (text.empty())
    {
        return false;
    }
    if (!std::isupper(static_cast<unsigned char>(text.front())))
    {
        return false;
    }
    for (const unsigned char c : text)
    {
        if (!std::isalnum(c) && c != '_')
        {
            return false;
        }
    }
    return true;
}

std::string toSnakeCase(const std::string& text)
{
    if (text.empty())
    {
        return text;
    }
    std::string out;
    out.reserve(text.size() * 2U);
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const auto c = static_cast<unsigned char>(text[i]);
        if (std::isupper(c) && i > 0 && out.back() != '_')
        {
            out.push_back('_');
        }
        if (c == '-' || c == ' ')
        {
            if (out.empty() || out.back() != '_')
            {
                out.push_back('_');
            }
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

std::string toUpperSnakeCase(const std::string& text)
{
    std::string out = toSnakeCase(text);
    std::ranges::transform(out, out.begin(), [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

std::optional<std::int64_t> evaluateCapacity(const std::shared_ptr<ExprAST>& expr)
{
    if (!expr)
    {
        return std::nullopt;
    }
    const auto* rational = std::get_if<Rational>(&expr->value);
    if (!rational)
    {
        return std::nullopt;
    }
    // Yields the value only when it is an integer that fits `std::int64_t`; a capacity outside that
    // range is not a meaningful array length.
    return rational->asInteger();
}

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::size_t              start = 0;
    while (start <= text.size())
    {
        const std::size_t newline = text.find('\n', start);
        if (newline == std::string::npos)
        {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, newline - start));
        start = newline + 1U;
    }
    return lines;
}

std::uint32_t zeroLine(const SourceLocation& location)
{
    return location.line == 0 ? 0U : location.line - 1U;
}

std::uint32_t zeroColumn(const SourceLocation& location)
{
    return location.column == 0 ? 0U : location.column - 1U;
}

LintFinding makeFinding(const std::string&    ruleId,
                        const LintDocument&   document,
                        const SourceLocation& location,
                        std::string           message,
                        const LintSeverity    severity)
{
    LintFinding finding;
    finding.ruleId   = ruleId;
    finding.uri      = document.uri;
    finding.location = location;
    finding.message  = std::move(message);
    finding.severity = severity;
    return finding;
}

class TypePascalCaseRule final : public LintRule
{
public:
    [[nodiscard]] std::string id() const override
    {
        return "naming.type_pascal_case";
    }

    [[nodiscard]] std::string title() const override
    {
        return "Type names should use PascalCase";
    }

    void run(const LintDocument& document, std::vector<LintFinding>& findings) const override
    {
        if (isPascalCase(document.info.shortName))
        {
            return;
        }
        findings.push_back(makeFinding(id(),
                                       document,
                                       document.ast.location,
                                       "type name should use PascalCase: " + document.info.shortName,
                                       LintSeverity::Warning));
    }
};

class FieldSnakeCaseRule final : public LintRule
{
public:
    [[nodiscard]] std::string id() const override
    {
        return "naming.field_snake_case";
    }

    [[nodiscard]] std::string title() const override
    {
        return "Field names should use snake_case";
    }

    void run(const LintDocument& document, std::vector<LintFinding>& findings) const override
    {
        for (const StatementAST& statement : document.ast.statements)
        {
            const auto* field = std::get_if<FieldDeclAST>(&statement);
            if (!field || isSnakeCase(field->name) || field->type.isVoid())
            {
                continue;
            }
            LintFinding finding  = makeFinding(id(),
                                               document,
                                               field->nameLocation,
                                               "field should use snake_case: " + field->name,
                                               LintSeverity::Warning);
            finding.hasFix       = true;
            finding.preferredFix = true;
            finding.fixes.push_back(LintFixEdit{
                zeroLine(field->nameLocation),
                zeroColumn(field->nameLocation),
                static_cast<std::uint32_t>(field->name.size()),
                toSnakeCase(field->name),
            });
            findings.push_back(std::move(finding));
        }
    }
};

class ConstantUpperSnakeCaseRule final : public LintRule
{
public:
    [[nodiscard]] std::string id() const override
    {
        return "naming.constant_upper_snake_case";
    }

    [[nodiscard]] std::string title() const override
    {
        return "Constant names should use UPPER_SNAKE_CASE";
    }

    void run(const LintDocument& document, std::vector<LintFinding>& findings) const override
    {
        for (const StatementAST& statement : document.ast.statements)
        {
            const auto* constant = std::get_if<ConstantDeclAST>(&statement);
            if (!constant || isUpperSnakeCase(constant->name))
            {
                continue;
            }
            LintFinding finding  = makeFinding(id(),
                                               document,
                                               constant->nameLocation,
                                               "constant should use UPPER_SNAKE_CASE: " + constant->name,
                                               LintSeverity::Warning);
            finding.hasFix       = true;
            finding.preferredFix = true;
            finding.fixes.push_back(LintFixEdit{
                zeroLine(constant->nameLocation),
                zeroColumn(constant->nameLocation),
                static_cast<std::uint32_t>(constant->name.size()),
                toUpperSnakeCase(constant->name),
            });
            findings.push_back(std::move(finding));
        }
    }
};

class NamespaceLowerCaseRule final : public LintRule
{
public:
    [[nodiscard]] std::string id() const override
    {
        return "naming.namespace_lowercase";
    }

    [[nodiscard]] std::string title() const override
    {
        return "Namespace components should be lower-case";
    }

    void run(const LintDocument& document, std::vector<LintFinding>& findings) const override
    {
        for (const std::string& component : document.info.namespaceComponents)
        {
            const std::string lowered = toLower(component);
            if (component == lowered)
            {
                continue;
            }
            findings.push_back(makeFinding(id(),
                                           document,
                                           document.ast.location,
                                           "namespace component should be lower-case: " + component,
                                           LintSeverity::Warning));
            break;
        }
    }
};

class NoTabsRule final : public LintRule
{
public:
    [[nodiscard]] std::string id() const override
    {
        return "style.no_tabs";
    }

    [[nodiscard]] std::string title() const override
    {
        return "Source should not contain tab characters";
    }

    void run(const LintDocument& document, std::vector<LintFinding>& findings) const override
    {
        const std::vector<std::string> lines = splitLines(document.sourceText);
        for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
        {
            const std::string& line   = lines[lineIndex];
            const std::size_t  tabPos = line.find('\t');
            if (tabPos == std::string::npos)
            {
                continue;
            }
            SourceLocation const location{document.info.filePath,
                                          static_cast<std::uint32_t>(lineIndex + 1U),
                                          static_cast<std::uint32_t>(tabPos + 1U)};
            LintFinding          finding =
                makeFinding(id(), document, location, "replace tab characters with spaces", LintSeverity::Warning);
            finding.hasFix       = true;
            finding.preferredFix = false;
            finding.fixes.push_back(LintFixEdit{
                static_cast<std::uint32_t>(lineIndex),
                static_cast<std::uint32_t>(tabPos),
                1,
                "  ",
            });
            findings.push_back(std::move(finding));
        }
    }
};

class TrailingWhitespaceRule final : public LintRule
{
public:
    [[nodiscard]] std::string id() const override
    {
        return "style.trailing_whitespace";
    }

    [[nodiscard]] std::string title() const override
    {
        return "Source lines should not end with trailing whitespace";
    }

    void run(const LintDocument& document, std::vector<LintFinding>& findings) const override
    {
        const std::vector<std::string> lines = splitLines(document.sourceText);
        for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
        {
            const std::string& line = lines[lineIndex];
            std::size_t        end  = line.size();
            while (end > 0 && std::isspace(static_cast<unsigned char>(line[end - 1U])) && line[end - 1U] != '\t')
            {
                --end;
            }
            if (end == line.size())
            {
                continue;
            }
            SourceLocation const location{document.info.filePath,
                                          static_cast<std::uint32_t>(lineIndex + 1U),
                                          static_cast<std::uint32_t>(end + 1U)};
            LintFinding          finding =
                makeFinding(id(), document, location, "remove trailing whitespace", LintSeverity::Info);
            finding.hasFix       = true;
            finding.preferredFix = true;
            finding.fixes.push_back(LintFixEdit{
                static_cast<std::uint32_t>(lineIndex),
                static_cast<std::uint32_t>(end),
                static_cast<std::uint32_t>(line.size() - end),
                {},
            });
            findings.push_back(std::move(finding));
        }
    }
};

class SingleTrailingNewlineRule final : public LintRule
{
public:
    [[nodiscard]] std::string id() const override
    {
        return "style.single_trailing_newline";
    }

    [[nodiscard]] std::string title() const override
    {
        return "Source should end with exactly one trailing newline";
    }

    void run(const LintDocument& document, std::vector<LintFinding>& findings) const override
    {
        const std::string& text = document.sourceText;
        if (text.empty())
        {
            return;
        }

        std::size_t trailingNewlines = 0;
        for (std::size_t i = text.size(); i > 0 && text[i - 1U] == '\n'; --i)
        {
            ++trailingNewlines;
        }
        if (trailingNewlines == 1)
        {
            return;
        }

        SourceLocation const location{document.info.filePath, 1, 1};
        LintFinding          finding =
            makeFinding(id(), document, location, "file should end with exactly one newline", LintSeverity::Info);
        finding.hasFix       = true;
        finding.preferredFix = true;

        const std::vector<std::string> lines  = splitLines(text);
        const std::uint32_t            line   = lines.empty() ? 0U : static_cast<std::uint32_t>(lines.size() - 1U);
        const std::uint32_t            column = lines.empty() ? 0U : static_cast<std::uint32_t>(lines.back().size());

        if (trailingNewlines == 0)
        {
            finding.fixes.push_back(LintFixEdit{line, column, 0, "\n"});
        }
        else
        {
            finding.fixes.push_back(LintFixEdit{
                static_cast<std::uint32_t>(std::max<std::size_t>(1, lines.size()) - 1U),
                0,
                static_cast<std::uint32_t>(trailingNewlines - 1U),
                {},
            });
        }
        findings.push_back(std::move(finding));
    }
};

class MaxFieldsRule final : public LintRule
{
public:
    [[nodiscard]] std::string id() const override
    {
        return "complexity.max_fields_per_type";
    }

    [[nodiscard]] std::string title() const override
    {
        return "Type should avoid excessive field counts";
    }

    void run(const LintDocument& document, std::vector<LintFinding>& findings) const override
    {
        std::size_t fieldCount = 0;
        for (const StatementAST& statement : document.ast.statements)
        {
            fieldCount += std::holds_alternative<FieldDeclAST>(statement) ? 1U : 0U;
        }
        if (fieldCount <= 64)
        {
            return;
        }
        findings.push_back(makeFinding(id(),
                                       document,
                                       document.ast.location,
                                       "type declares " + std::to_string(fieldCount) + " fields (recommended <= 64)",
                                       LintSeverity::Warning));
    }
};

class MaxConstantsRule final : public LintRule
{
public:
    [[nodiscard]] std::string id() const override
    {
        return "complexity.max_constants_per_type";
    }

    [[nodiscard]] std::string title() const override
    {
        return "Type should avoid excessive constant counts";
    }

    void run(const LintDocument& document, std::vector<LintFinding>& findings) const override
    {
        std::size_t constantCount = 0;
        for (const StatementAST& statement : document.ast.statements)
        {
            constantCount += std::holds_alternative<ConstantDeclAST>(statement) ? 1U : 0U;
        }
        if (constantCount <= 32)
        {
            return;
        }
        findings.push_back(
            makeFinding(id(),
                        document,
                        document.ast.location,
                        "type declares " + std::to_string(constantCount) + " constants (recommended <= 32)",
                        LintSeverity::Warning));
    }
};

class MaxDirectiveCountRule final : public LintRule
{
public:
    [[nodiscard]] std::string id() const override
    {
        return "complexity.max_directives_per_type";
    }

    [[nodiscard]] std::string title() const override
    {
        return "Type should avoid excessive directive usage";
    }

    void run(const LintDocument& document, std::vector<LintFinding>& findings) const override
    {
        std::vector<const DirectiveAST*> directives;
        for (const StatementAST& statement : document.ast.statements)
        {
            if (const auto* directive = std::get_if<DirectiveAST>(&statement))
            {
                directives.push_back(directive);
            }
        }
        const std::size_t directiveCount = directives.size();
        if (directiveCount <= 4)
        {
            return;
        }

        SourceLocation anchor = document.ast.location;
        if (!directives.empty())
        {
            // Prefer anchoring on @extent when present because it is commonly
            // interpreted as the directive that pushed policy complexity.
            const auto extentDirective = std::ranges::find_if(directives, [](const DirectiveAST* directive) {
                return directive->kind == DirectiveKind::Extent;
            });
            if (extentDirective != directives.end())
            {
                anchor = (*extentDirective)->location;
            }
            else
            {
                anchor = directives.back()->location;
            }
        }

        findings.push_back(
            makeFinding(id(),
                        document,
                        anchor,
                        "type declares " + std::to_string(directiveCount) + " directives (recommended <= 4)",
                        LintSeverity::Info));
    }
};

class LargeFixedArrayBoundRule final : public LintRule
{
public:
    [[nodiscard]] std::string id() const override
    {
        return "arrays.large_fixed_bound";
    }

    [[nodiscard]] std::string title() const override
    {
        return "Fixed arrays should avoid very large bounds";
    }

    void run(const LintDocument& document, std::vector<LintFinding>& findings) const override
    {
        for (const StatementAST& statement : document.ast.statements)
        {
            const auto* field = std::get_if<FieldDeclAST>(&statement);
            if (!field || field->type.arrayKind != ArrayKind::Fixed)
            {
                continue;
            }
            const auto capacity = evaluateCapacity(field->type.arrayCapacity);
            if (!capacity.has_value() || *capacity <= 4096)
            {
                continue;
            }
            findings.push_back(makeFinding(id(),
                                           document,
                                           field->location,
                                           "fixed array bound is very large (" + std::to_string(*capacity) + ")",
                                           LintSeverity::Warning));
        }
    }
};

class LargeVariableArrayBoundRule final : public LintRule
{
public:
    [[nodiscard]] std::string id() const override
    {
        return "arrays.large_variable_bound";
    }

    [[nodiscard]] std::string title() const override
    {
        return "Variable arrays should avoid very large bounds";
    }

    void run(const LintDocument& document, std::vector<LintFinding>& findings) const override
    {
        for (const StatementAST& statement : document.ast.statements)
        {
            const auto* field = std::get_if<FieldDeclAST>(&statement);
            if (!field || (field->type.arrayKind != ArrayKind::VariableExclusive &&
                           field->type.arrayKind != ArrayKind::VariableInclusive))
            {
                continue;
            }
            const auto capacity = evaluateCapacity(field->type.arrayCapacity);
            if (!capacity.has_value() || *capacity <= 1024)
            {
                continue;
            }
            findings.push_back(makeFinding(id(),
                                           document,
                                           field->location,
                                           "variable array bound is very large (" + std::to_string(*capacity) + ")",
                                           LintSeverity::Warning));
        }
    }
};

void registerBuiltinRules(LintRegistry& registry)
{
    registry.registerRuleFactory([]() { return std::make_unique<TypePascalCaseRule>(); });
    registry.registerRuleFactory([]() { return std::make_unique<FieldSnakeCaseRule>(); });
    registry.registerRuleFactory([]() { return std::make_unique<ConstantUpperSnakeCaseRule>(); });
    registry.registerRuleFactory([]() { return std::make_unique<NamespaceLowerCaseRule>(); });
    registry.registerRuleFactory([]() { return std::make_unique<NoTabsRule>(); });
    registry.registerRuleFactory([]() { return std::make_unique<TrailingWhitespaceRule>(); });
    registry.registerRuleFactory([]() { return std::make_unique<SingleTrailingNewlineRule>(); });
    registry.registerRuleFactory([]() { return std::make_unique<MaxFieldsRule>(); });
    registry.registerRuleFactory([]() { return std::make_unique<MaxConstantsRule>(); });
    registry.registerRuleFactory([]() { return std::make_unique<MaxDirectiveCountRule>(); });
    registry.registerRuleFactory([]() { return std::make_unique<LargeFixedArrayBoundRule>(); });
    registry.registerRuleFactory([]() { return std::make_unique<LargeVariableArrayBoundRule>(); });
}

}  // namespace

struct LintRegistry::PluginHandle final
{
    explicit PluginHandle(void* inHandle)
        : handle(inHandle)
    {
    }

    ~PluginHandle()
    {
        if (handle)
        {
            dlclose(handle);
        }
    }

    void* handle{nullptr};
};

LintRegistry::LintRegistry()
{
    registerBuiltinRules(*this);
}

void LintRegistry::registerRuleFactory(LintRuleFactory factory)
{
    if (!factory)
    {
        return;
    }
    factories_.push_back(std::move(factory));
}

bool LintRegistry::loadPluginLibrary(const std::string& libraryPath, std::string* errorMessage)
{
    void* handle = dlopen(libraryPath.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!handle)
    {
        if (errorMessage)
        {
            *errorMessage = dlerror();
        }
        return false;
    }

    using RegisterFn = void (*)(LintRegistry&);
    void* symbol     = dlsym(handle, "llvmdsdlRegisterLintRules");
    if (!symbol)
    {
        if (errorMessage)
        {
            *errorMessage = "missing symbol llvmdsdlRegisterLintRules";
        }
        dlclose(handle);
        return false;
    }

    auto registerFn = reinterpret_cast<RegisterFn>(symbol);
    registerFn(*this);
    pluginHandles_.push_back(std::make_shared<PluginHandle>(handle));
    return true;
}

std::vector<std::unique_ptr<LintRule>> LintRegistry::createRules() const
{
    std::vector<std::unique_ptr<LintRule>> rules;
    rules.reserve(factories_.size());
    for (const LintRuleFactory& factory : factories_)
    {
        std::unique_ptr<LintRule> rule = factory();
        if (!rule)
        {
            continue;
        }
        rules.push_back(std::move(rule));
    }
    std::ranges::sort(rules,

                      [](const std::unique_ptr<LintRule>& lhs, const std::unique_ptr<LintRule>& rhs) {
                          if (!lhs || !rhs)
                          {
                              return static_cast<bool>(rhs);
                          }
                          return lhs->id() < rhs->id();
                      });
    return rules;
}

LintEngine::LintEngine(LintRegistry registry, LintExecutionConfig config)
    : registry_(std::move(registry))
    , config_(std::move(config))
{
    for (const std::string& library : config_.pluginLibraries)
    {
        std::string error;
        const bool  loaded = registry_.loadPluginLibrary(library, &error);
        (void) loaded;
        (void) error;
    }
}

LintRunResult LintEngine::run(const std::vector<LintDocument>& documents) const
{
    LintRunResult result;
    if (!config_.enabled)
    {
        return result;
    }

    std::vector<std::unique_ptr<LintRule>> rules = registry_.createRules();
    std::ranges::sort(rules,

                      [](const std::unique_ptr<LintRule>& lhs, const std::unique_ptr<LintRule>& rhs) {
                          if (!lhs || !rhs)
                          {
                              return static_cast<bool>(rhs);
                          }
                          return lhs->id() < rhs->id();
                      });

    std::vector<const LintDocument*> orderedDocuments;
    orderedDocuments.reserve(documents.size());
    for (const LintDocument& document : documents)
    {
        orderedDocuments.push_back(&document);
    }
    std::ranges::sort(orderedDocuments,
                      [](const LintDocument* lhs, const LintDocument* rhs) { return lhs->path < rhs->path; });

    for (const LintDocument* document : orderedDocuments)
    {
        const std::unordered_set<std::string> sourceSuppressions = parseSourceSuppressions(document->sourceText);

        for (const std::unique_ptr<LintRule>& rule : rules)
        {
            if (isSuppressed(*document, sourceSuppressions, rule->id()))
            {
                continue;
            }

            std::vector<LintFinding> emitted;
            rule->run(*document, emitted);

            std::ranges::sort(emitted, [](const LintFinding& lhs, const LintFinding& rhs) {
                return std::tie(lhs.ruleId, lhs.location.file, lhs.location.line, lhs.location.column, lhs.message) <
                       std::tie(rhs.ruleId, rhs.location.file, rhs.location.line, rhs.location.column, rhs.message);
            });

            for (LintFinding finding : emitted)
            {
                finding.uri = document->uri;
                result.findingsByUri[finding.uri].push_back(finding);
                result.findings.push_back(std::move(finding));
            }
        }
    }

    for (auto& [_, findings] : result.findingsByUri)
    {
        std::ranges::sort(findings, [](const LintFinding& lhs, const LintFinding& rhs) {
            return std::tie(lhs.ruleId, lhs.location.file, lhs.location.line, lhs.location.column, lhs.message) <
                   std::tie(rhs.ruleId, rhs.location.file, rhs.location.line, rhs.location.column, rhs.message);
        });
    }

    std::ranges::sort(result.findings, [](const LintFinding& lhs, const LintFinding& rhs) {
        return std::tie(lhs.uri, lhs.ruleId, lhs.location.file, lhs.location.line, lhs.location.column, lhs.message) <
               std::tie(rhs.uri, rhs.ruleId, rhs.location.file, rhs.location.line, rhs.location.column, rhs.message);
    });

    return result;
}

std::vector<std::string> LintEngine::baselineRuleIds()
{
    LintRegistry const       registry;
    std::vector<std::string> ids;
    for (const std::unique_ptr<LintRule>& rule : registry.createRules())
    {
        ids.push_back(rule->id());
    }
    std::ranges::sort(ids);
    ids.erase(std::ranges::unique(ids).begin(), ids.end());
    return ids;
}

bool LintEngine::isSuppressed(const LintDocument&                    document,
                              const std::unordered_set<std::string>& sourceSuppressed,
                              const std::string&                     ruleId) const
{
    if (config_.disabledRules.contains("*") || config_.disabledRules.contains(ruleId))
    {
        return true;
    }

    const auto uriIt = config_.fileDisabledRules.find(document.uri);
    if (uriIt != config_.fileDisabledRules.end() && (uriIt->second.contains("*") || uriIt->second.contains(ruleId)))
    {
        return true;
    }

    const auto pathIt = config_.fileDisabledRules.find(document.path);
    if (pathIt != config_.fileDisabledRules.end() && (pathIt->second.contains("*") || pathIt->second.contains(ruleId)))
    {
        return true;
    }

    if (sourceSuppressed.contains("*") || sourceSuppressed.contains(ruleId))
    {
        return true;
    }

    return false;
}

std::unordered_set<std::string> LintEngine::parseSourceSuppressions(const std::string& sourceText)
{
    std::unordered_set<std::string> rules;
    const std::vector<std::string>  lines = splitLines(sourceText);
    for (const std::string& line : lines)
    {
        const std::string text = trim(line);
        if (!text.starts_with("#"))
        {
            continue;
        }
        const std::size_t marker = text.find("dsdld-lint-disable:");
        if (marker == std::string::npos)
        {
            continue;
        }

        const std::string list = text.substr(marker + std::string("dsdld-lint-disable:").size());
        std::stringstream parser(list);
        std::string       item;
        while (std::getline(parser, item, ','))
        {
            item = trim(item);
            if (!item.empty())
            {
                rules.insert(item);
            }
        }
    }
    return rules;
}

}  // namespace llvmdsdl::lsp
