//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <iostream>
#include <llvm/Support/Error.h>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Frontend/Lexer.h"
#include "llvmdsdl/Frontend/Parser.h"
#include "llvmdsdl/LSP/Lint.h"
#include "llvmdsdl/Semantics/Analyzer.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/Diagnostics.h"

#include "UnitTests.h"

namespace
{

llvm::Expected<llvmdsdl::DefinitionAST> parseDefinition(const std::string&          path,
                                                        const std::string&          text,
                                                        llvmdsdl::DiagnosticEngine& diagnostics)
{
    llvmdsdl::Lexer              lexer(path, text);
    std::vector<llvmdsdl::Token> tokens = lexer.lex();
    llvmdsdl::Parser             parser(path, std::move(tokens), diagnostics);
    return parser.parseDefinition();
}

std::optional<llvmdsdl::lsp::LintDocument> makeDocument(const std::string&              path,
                                                        const std::string&              uri,
                                                        const std::string&              shortName,
                                                        const std::vector<std::string>& ns,
                                                        const std::string&              text)
{
    llvmdsdl::DiagnosticEngine              diagnostics;
    llvm::Expected<llvmdsdl::DefinitionAST> parsed = parseDefinition(path, text, diagnostics);
    if (!parsed)
    {
        llvm::consumeError(parsed.takeError());
        return std::nullopt;
    }

    llvmdsdl::DiscoveredDefinition info;
    info.filePath            = path;
    info.rootNamespacePath   = std::filesystem::path(path).parent_path().string();
    info.shortName           = shortName;
    info.majorVersion        = 1;
    info.minorVersion        = 0;
    info.namespaceComponents = ns;
    info.fullName            = (ns.empty() ? shortName : (ns.front() + "." + shortName));
    info.text                = text;

    return llvmdsdl::lsp::LintDocument{path, uri, info, *parsed, text};
}

std::vector<std::string> readLines(const std::filesystem::path& path)
{
    std::ifstream            in(path, std::ios::binary);
    std::vector<std::string> out;
    std::string              line;
    while (std::getline(in, line))
    {
        if (!line.empty())
        {
            out.push_back(line);
        }
    }
    return out;
}

std::set<std::string> findingIds(const std::vector<llvmdsdl::lsp::LintFinding>& findings)
{
    std::set<std::string> ids;
    for (const auto& finding : findings)
    {
        ids.insert(finding.ruleId);
    }
    return ids;
}

}  // namespace

namespace
{

/// @brief Analyses one definition so a rule that asks about layout has a resolved model to read.
std::optional<llvmdsdl::SemanticModule> analyseOne(const llvmdsdl::lsp::LintDocument& document)
{
    llvmdsdl::ASTModule module;
    module.definitions.push_back(llvmdsdl::ParsedDefinition{document.info, document.ast});
    llvmdsdl::DiagnosticEngine diagnostics;
    auto                       analysed = llvmdsdl::analyze(module, diagnostics);
    if (!analysed)
    {
        llvm::consumeError(analysed.takeError());
        return std::nullopt;
    }
    return std::move(*analysed);
}

/// @brief The candidate rule's findings for one definition, or nullopt when analysis failed.
std::optional<std::vector<std::string>> aliasableCandidateMessages(const std::string& shortName,
                                                                   const std::string& text)
{
    auto document = makeDocument("/tmp/candidate.dsdl", "file:///tmp/candidate.dsdl", shortName, {"vendor"}, text);
    if (!document.has_value())
    {
        return std::nullopt;
    }
    const auto analysed = analyseOne(*document);
    if (!analysed.has_value())
    {
        return std::nullopt;
    }
    document->semantic = &analysed->definitions.front();

    llvmdsdl::lsp::LintExecutionConfig config;
    config.enabled = true;
    const llvmdsdl::lsp::LintEngine engine(llvmdsdl::lsp::LintRegistry{}, config);
    std::vector<std::string>        messages;
    for (const auto& finding : engine.run({*document}).findings)
    {
        if (finding.ruleId == "layout.aliasable_candidate")
        {
            messages.push_back(finding.message);
        }
    }
    return messages;
}

/// @brief Checks what the candidate rule says about one definition.
bool expectCandidate(const std::string& shortName, const std::string& text, const std::string& expectedSubstring)
{
    const auto messages = aliasableCandidateMessages(shortName, text);
    if (!messages.has_value())
    {
        std::cerr << "aliasable candidate: could not analyse " << shortName << "\n";
        return false;
    }
    if (expectedSubstring.empty())
    {
        if (!messages->empty())
        {
            std::cerr << "aliasable candidate: " << shortName << " should say nothing, said '" << messages->front()
                      << "'\n";
            return false;
        }
        return true;
    }
    if (messages->size() != 1U)
    {
        std::cerr << "aliasable candidate: " << shortName << " produced " << messages->size() << " findings\n";
        return false;
    }
    if (!messages->front().contains(expectedSubstring))
    {
        std::cerr << "aliasable candidate: " << shortName << " said '" << messages->front()
                  << "', expected to mention '" << expectedSubstring << "'\n";
        return false;
    }
    return true;
}

}  // namespace

bool runLspLintTests()
{
    const std::string path = "/tmp/lint_fixture.dsdl";
    const std::string uri  = "file:///tmp/lint_fixture.dsdl";
    const std::string text = "# dsdld-lint-disable: arrays.large_variable_bound\n"
                             "uint8 BadField\t \n"
                             "uint8 badConst = 1\n"
                             "uint8[5001] hugeArray\n"
                             "uint8[<=2001] hugeVar\n"
                             "@union\n"
                             "@sealed\n"
                             "@deprecated\n"
                             "@assert true\n"
                             "@print 1";

    const auto document = makeDocument(path, uri, "bad_type", {"DemoNs"}, text);
    if (!document.has_value())
    {
        std::cerr << "failed to construct lint fixture document\n";
        return false;
    }

    {
        llvmdsdl::lsp::LintExecutionConfig config;
        config.enabled = true;
        llvmdsdl::lsp::LintEngine const engine(llvmdsdl::lsp::LintRegistry{}, config);

        const llvmdsdl::lsp::LintRunResult first  = engine.run({*document});
        const llvmdsdl::lsp::LintRunResult second = engine.run({*document});

        std::vector<std::string> canonical;
        canonical.reserve(first.findings.size());
        for (const auto& finding : first.findings)
        {
            canonical.push_back(finding.ruleId + "|" + std::to_string(finding.location.line));
        }

        std::vector<std::string> canonicalSecond;
        canonicalSecond.reserve(second.findings.size());
        for (const auto& finding : second.findings)
        {
            canonicalSecond.push_back(finding.ruleId + "|" + std::to_string(finding.location.line));
        }

        if (canonical != canonicalSecond)
        {
            std::cerr << "lint engine output is not deterministic\n";
            return false;
        }

        const std::filesystem::path sourcePath = std::filesystem::path(__FILE__).lexically_normal();
        const std::filesystem::path goldenPath =
            sourcePath.parent_path().parent_path() / "lint" / "golden" / "lint_fixture_diagnostics.golden";
        const std::vector<std::string> expected = readLines(goldenPath);
        if (canonical != expected)
        {
            std::cerr << "lint golden diagnostics mismatch\n";
            std::cerr << "expected:\n";
            for (const std::string& line : expected)
            {
                std::cerr << "  " << line << "\n";
            }
            std::cerr << "actual:\n";
            for (const std::string& line : canonical)
            {
                std::cerr << "  " << line << "\n";
            }
            return false;
        }

        bool sawFieldFix = false;
        for (const auto& finding : first.findings)
        {
            if (finding.ruleId == "naming.field_snake_case" && finding.hasFix && !finding.fixes.empty())
            {
                sawFieldFix = true;
                break;
            }
        }
        if (!sawFieldFix)
        {
            std::cerr << "expected autofix for naming.field_snake_case\n";
            return false;
        }

        const std::vector<std::string> baseline = llvmdsdl::lsp::LintEngine::baselineRuleIds();
        if (baseline.size() < 10)
        {
            std::cerr << "expected at least ten baseline lint rules\n";
            return false;
        }
    }

    {
        llvmdsdl::lsp::LintExecutionConfig suppressed;
        suppressed.disabledRules.insert("naming.type_pascal_case");
        suppressed.fileDisabledRules[uri].insert("style.no_tabs");

        llvmdsdl::lsp::LintEngine const    engine(llvmdsdl::lsp::LintRegistry{}, suppressed);
        const llvmdsdl::lsp::LintRunResult result = engine.run({*document});
        const auto                         ids    = findingIds(result.findings);

        if (ids.contains("naming.type_pascal_case") || ids.contains("style.no_tabs") ||
            ids.contains("arrays.large_variable_bound"))
        {
            std::cerr << "lint suppression model did not suppress expected rules\n";
            return false;
        }
    }

    {
        std::string textComplex = "uint8 field_0\n";
        for (int i = 1; i < 66; ++i)
        {
            textComplex += "uint8 field_" + std::to_string(i) + "\n";
        }
        for (int i = 0; i < 33; ++i)
        {
            textComplex += "uint8 CONST_" + std::to_string(i) + " = 1\n";
        }
        textComplex += "@sealed\n";

        const auto complexDoc =
            makeDocument("/tmp/complex.dsdl", "file:///tmp/complex.dsdl", "Complex", {"demo"}, textComplex);
        if (!complexDoc.has_value())
        {
            std::cerr << "failed to parse complexity fixture\n";
            return false;
        }

        llvmdsdl::lsp::LintEngine const    engine(llvmdsdl::lsp::LintRegistry{}, llvmdsdl::lsp::LintExecutionConfig{});
        const llvmdsdl::lsp::LintRunResult result = engine.run({*complexDoc});
        const auto                         ids    = findingIds(result.findings);
        if (!ids.contains("complexity.max_fields_per_type") || !ids.contains("complexity.max_constants_per_type"))
        {
            std::cerr << "complexity lint rules did not trigger as expected\n";
            return false;
        }
    }

    {
        class PluginRule final : public llvmdsdl::lsp::LintRule
        {
        public:
            [[nodiscard]] std::string id() const override
            {
                return "plugin.example_rule";
            }

            [[nodiscard]] std::string title() const override
            {
                return "Plugin Example Rule";
            }

            void run(const llvmdsdl::lsp::LintDocument&       document,
                     std::vector<llvmdsdl::lsp::LintFinding>& findings) const override
            {
                findings.push_back(llvmdsdl::lsp::LintFinding{
                    id(),
                    document.uri,
                    document.ast.location,
                    "plugin rule fired",
                    llvmdsdl::lsp::LintSeverity::Info,
                    false,
                    false,
                    {},
                });
            }
        };

        llvmdsdl::lsp::LintRegistry registry;
        registry.registerRuleFactory([]() { return std::make_unique<PluginRule>(); });

        llvmdsdl::lsp::LintEngine const    engine(std::move(registry), llvmdsdl::lsp::LintExecutionConfig{});
        const llvmdsdl::lsp::LintRunResult result = engine.run({*document});
        const auto                         ids    = findingIds(result.findings);
        if (!ids.contains("plugin.example_rule"))
        {
            std::cerr << "custom lint rule factory was not executed\n";
            return false;
        }

        llvmdsdl::lsp::LintRegistry dynamicRegistry;
        std::string                 error;
        if (dynamicRegistry.loadPluginLibrary("/definitely/not/a/real/plugin.so", &error))
        {
            std::cerr << "expected missing plugin load to fail\n";
            return false;
        }
    }

    // A field that blocks the fast path is chosen early and would otherwise surface at sealing,
    // which is when it is most expensive to change. These say so while it is still cheap.
    {
        bool ok = true;
        // Byte-clean already: sealing is all that stands in the way.
        ok = expectCandidate("Evolving", "uint32 a\nuint16 b\n@extent 128\n", "would be @aliasable once sealed") && ok;
        // One narrow field stands in the way, and the rule names it.
        ok = expectCandidate("NearMiss", "uint2 health\nuint8 code\n@extent 128\n", "field 'health'") && ok;
        // A variable-length array is a design decision rather than an oversight.
        ok = expectCandidate("Deliberate", "uint8[<=64] payload\n@extent 1024\n", "") && ok;
        // A sealed type has already made its choice, and @aliasable is how it states it.
        ok = expectCandidate("Locked", "uint32 a\n@sealed\n", "") && ok;
        if (!ok)
        {
            return false;
        }
    }

    return true;
}