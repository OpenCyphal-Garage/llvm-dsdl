//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "llvmdsdl/Frontend/Lexer.h"
#include "llvmdsdl/Frontend/Parser.h"
#include "llvmdsdl/Semantics/Analyzer.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Support/ReservedIdentifiers.h"
#include "llvm/Support/Error.h"
#include "llvmdsdl/Frontend/AST.h"

bool runAnalyzerTests()
{
    const std::string text = "@union\n"
                             "uint8 a\n"
                             "uint16 b\n"
                             "@sealed\n"
                             "@assert _offset_.min == 16\n"
                             "@assert _offset_.max == 24\n"
                             "@assert _offset_ % 8 == {0}\n";

    llvmdsdl::DiagnosticEngine parseDiag;
    llvmdsdl::Lexer            lexer("uavcan.test.UnionOffset.1.0.dsdl", text);
    auto                       tokens = lexer.lex();
    llvmdsdl::Parser           parser("uavcan.test.UnionOffset.1.0.dsdl", std::move(tokens), parseDiag);
    auto                       def = parser.parseDefinition();
    if (!def)
    {
        llvm::consumeError(def.takeError());
        std::cerr << "analyzer fixture parse failed unexpectedly\n";
        return false;
    }
    if (parseDiag.hasErrors())
    {
        std::cerr << "analyzer fixture parse diagnostics contained errors\n";
        return false;
    }

    llvmdsdl::DiscoveredDefinition discovered;
    discovered.filePath            = "uavcan/test/UnionOffset.1.0.dsdl";
    discovered.rootNamespacePath   = "uavcan";
    discovered.fullName            = "uavcan.test.UnionOffset";
    discovered.shortName           = "UnionOffset";
    discovered.namespaceComponents = {"uavcan", "test"};
    discovered.majorVersion        = 1;
    discovered.minorVersion        = 0;
    discovered.text                = text;

    llvmdsdl::ASTModule module;
    module.definitions.push_back(llvmdsdl::ParsedDefinition{discovered, *def});

    llvmdsdl::DiagnosticEngine semDiag;
    auto                       semantic = llvmdsdl::analyze(module, semDiag);
    if (!semantic)
    {
        llvm::consumeError(semantic.takeError());
        std::cerr << "analyzer unexpectedly failed on union offset fixture\n";
        return false;
    }
    if (semDiag.hasErrors())
    {
        std::cerr << "analyzer produced unexpected errors on union offset fixture\n";
        return false;
    }

    if (semantic->definitions.size() != 1)
    {
        std::cerr << "unexpected semantic definition count\n";
        return false;
    }
    const auto& section = semantic->definitions.front().request;
    if (!section.isUnion)
    {
        std::cerr << "expected analyzed section to be a union\n";
        return false;
    }
    if (section.fields.size() != 2)
    {
        std::cerr << "unexpected union field count in semantic section\n";
        return false;
    }
    if (section.fields[0].unionOptionIndex != 0 || section.fields[1].unionOptionIndex != 1 ||
        section.fields[0].unionTagBits != 8 || section.fields[1].unionTagBits != 8)
    {
        std::cerr << "unexpected union tag metadata\n";
        return false;
    }
    if (section.minBitLength != 16 || section.maxBitLength != 24 || section.serializationBufferSizeBits != 24 ||
        section.fixedSize)
    {
        std::cerr << "unexpected union section size metadata\n";
        return false;
    }

    const std::string extentText = "uint16 sample\n"
                                   "@extent 8\n";

    llvmdsdl::DiagnosticEngine extentParseDiag;
    llvmdsdl::Lexer            extentLexer("uavcan.test.BadExtent.1.0.dsdl", extentText);
    auto                       extentTokens = extentLexer.lex();
    llvmdsdl::Parser           extentParser("uavcan.test.BadExtent.1.0.dsdl", std::move(extentTokens), extentParseDiag);
    auto                       extentDef = extentParser.parseDefinition();
    if (!extentDef)
    {
        llvm::consumeError(extentDef.takeError());
        std::cerr << "bad-extent fixture parse failed unexpectedly\n";
        return false;
    }
    if (extentParseDiag.hasErrors())
    {
        std::cerr << "bad-extent fixture parse diagnostics contained errors\n";
        return false;
    }

    llvmdsdl::DiscoveredDefinition extentDiscovered;
    extentDiscovered.filePath            = "uavcan/test/BadExtent.1.0.dsdl";
    extentDiscovered.rootNamespacePath   = "uavcan";
    extentDiscovered.fullName            = "uavcan.test.BadExtent";
    extentDiscovered.shortName           = "BadExtent";
    extentDiscovered.namespaceComponents = {"uavcan", "test"};
    extentDiscovered.majorVersion        = 1;
    extentDiscovered.minorVersion        = 0;
    extentDiscovered.text                = extentText;

    llvmdsdl::ASTModule extentModule;
    extentModule.definitions.push_back(llvmdsdl::ParsedDefinition{extentDiscovered, *extentDef});

    llvmdsdl::DiagnosticEngine extentSemDiag;
    auto                       extentSemantic = llvmdsdl::analyze(extentModule, extentSemDiag);
    if (extentSemantic)
    {
        std::cerr << "analyzer unexpectedly succeeded for invalid extent fixture\n";
        return false;
    }
    llvm::consumeError(extentSemantic.takeError());

    bool sawExtentDiagnosticAtExtentValue = false;
    for (const llvmdsdl::Diagnostic& diagnostic : extentSemDiag.diagnostics())
    {
        if (diagnostic.message == "extent smaller than maximal serialized length" && diagnostic.location.line == 2 &&
            diagnostic.location.column == 9 && diagnostic.length == 1)
        {
            sawExtentDiagnosticAtExtentValue = true;
            break;
        }
    }
    if (!sawExtentDiagnosticAtExtentValue)
    {
        std::cerr << "expected extent diagnostic to point at the @extent value span\n";
        return false;
    }

    const std::string extentMultipleDigitsText = "uint16 sample\n"
                                                 "@extent 13\n";

    llvmdsdl::DiagnosticEngine extentDigitsParseDiag;
    llvmdsdl::Lexer            extentDigitsLexer("uavcan.test.BadExtentDigits.1.0.dsdl", extentMultipleDigitsText);
    auto                       extentDigitsTokens = extentDigitsLexer.lex();
    llvmdsdl::Parser           extentDigitsParser("uavcan.test.BadExtentDigits.1.0.dsdl",
                                                  std::move(extentDigitsTokens),
                                                  extentDigitsParseDiag);
    auto                       extentDigitsDef = extentDigitsParser.parseDefinition();
    if (!extentDigitsDef)
    {
        llvm::consumeError(extentDigitsDef.takeError());
        std::cerr << "bad-extent-digits fixture parse failed unexpectedly\n";
        return false;
    }
    if (extentDigitsParseDiag.hasErrors())
    {
        std::cerr << "bad-extent-digits fixture parse diagnostics contained errors\n";
        return false;
    }

    llvmdsdl::DiscoveredDefinition extentDigitsDiscovered;
    extentDigitsDiscovered.filePath            = "uavcan/test/BadExtentDigits.1.0.dsdl";
    extentDigitsDiscovered.rootNamespacePath   = "uavcan";
    extentDigitsDiscovered.fullName            = "uavcan.test.BadExtentDigits";
    extentDigitsDiscovered.shortName           = "BadExtentDigits";
    extentDigitsDiscovered.namespaceComponents = {"uavcan", "test"};
    extentDigitsDiscovered.majorVersion        = 1;
    extentDigitsDiscovered.minorVersion        = 0;
    extentDigitsDiscovered.text                = extentMultipleDigitsText;

    llvmdsdl::ASTModule extentDigitsModule;
    extentDigitsModule.definitions.push_back(llvmdsdl::ParsedDefinition{extentDigitsDiscovered, *extentDigitsDef});

    llvmdsdl::DiagnosticEngine extentDigitsSemDiag;
    auto                       extentDigitsSemantic = llvmdsdl::analyze(extentDigitsModule, extentDigitsSemDiag);
    if (extentDigitsSemantic)
    {
        std::cerr << "analyzer unexpectedly succeeded for invalid multi-digit extent fixture\n";
        return false;
    }
    llvm::consumeError(extentDigitsSemantic.takeError());

    bool sawMultiDigitExtentSpan = false;
    for (const llvmdsdl::Diagnostic& diagnostic : extentDigitsSemDiag.diagnostics())
    {
        if (diagnostic.message == "extent must be a multiple of 8 bits" && diagnostic.location.line == 2 &&
            diagnostic.location.column == 9 && diagnostic.length == 2)
        {
            sawMultiDigitExtentSpan = true;
            break;
        }
    }
    if (!sawMultiDigitExtentSpan)
    {
        std::cerr << "expected multi-digit extent diagnostic span to match expression width\n";
        return false;
    }

    const std::string          docText = "# type docs\n"
                                         "uint8 field # field docs\n"
                                         "uint8 LIMIT = 1 # const docs\n"
                                         "@sealed\n";
    llvmdsdl::DiagnosticEngine docParseDiag;
    llvmdsdl::Lexer            docLexer("uavcan.test.Docs.1.0.dsdl", docText);
    auto                       docTokens = docLexer.lex();
    llvmdsdl::Parser           docParser("uavcan.test.Docs.1.0.dsdl", std::move(docTokens), docParseDiag);
    auto                       docDef = docParser.parseDefinition();
    if (!docDef)
    {
        llvm::consumeError(docDef.takeError());
        std::cerr << "doc fixture parse failed unexpectedly\n";
        return false;
    }
    if (docParseDiag.hasErrors())
    {
        std::cerr << "doc fixture parse diagnostics contained errors\n";
        return false;
    }

    llvmdsdl::DiscoveredDefinition docDiscovered;
    docDiscovered.filePath            = "uavcan/test/Docs.1.0.dsdl";
    docDiscovered.rootNamespacePath   = "uavcan";
    docDiscovered.fullName            = "uavcan.test.Docs";
    docDiscovered.shortName           = "Docs";
    docDiscovered.namespaceComponents = {"uavcan", "test"};
    docDiscovered.majorVersion        = 1;
    docDiscovered.minorVersion        = 0;
    docDiscovered.text                = docText;

    llvmdsdl::ASTModule docModule;
    docModule.definitions.push_back(llvmdsdl::ParsedDefinition{docDiscovered, *docDef});

    llvmdsdl::DiagnosticEngine docSemDiag;
    auto                       docSemantic = llvmdsdl::analyze(docModule, docSemDiag);
    if (!docSemantic)
    {
        llvm::consumeError(docSemantic.takeError());
        std::cerr << "analyzer unexpectedly failed on docs fixture\n";
        return false;
    }
    if (docSemDiag.hasErrors())
    {
        std::cerr << "analyzer produced diagnostics for docs fixture\n";
        return false;
    }
    if (docSemantic->definitions.empty())
    {
        std::cerr << "docs fixture produced no semantic definitions\n";
        return false;
    }
    const auto& docDefinition = docSemantic->definitions.front();
    if (docDefinition.doc.lines.size() != 1 || docDefinition.doc.lines[0].text != "type docs")
    {
        std::cerr << "definition docs did not propagate into semantic model\n";
        return false;
    }
    if (docDefinition.request.fields.empty() || docDefinition.request.fields.front().doc.lines.size() != 1 ||
        docDefinition.request.fields.front().doc.lines[0].text != "field docs")
    {
        std::cerr << "field docs did not propagate into semantic model\n";
        return false;
    }
    if (docDefinition.request.constants.empty() || docDefinition.request.constants.front().doc.lines.size() != 1 ||
        docDefinition.request.constants.front().doc.lines[0].text != "const docs")
    {
        std::cerr << "constant docs did not propagate into semantic model\n";
        return false;
    }

    // Reserved identifier predicate (DSDL spec v1.0 section 3.2.5 / table 3.5).
    {
        const char* const reservedNames[] = {
            "truncated", "saturated", "true",    "false", "bool",  "byte",  "utf8",     "int",  "uint",  "int8",
            "uint64",    "float",     "float32", "void",  "void8", "q16_8", "const",    "type", "self",  "and",
            "or",        "not",       "enum",    "com1",  "lpt9",  "nul",   "_offset_", "TRUE", "UInt8", "VOID3",
        };
        for (const char* name : reservedNames)
        {
            if (!llvmdsdl::isReservedIdentifier(name))
            {
                std::cerr << "isReservedIdentifier missed reserved name: " << name << "\n";
                return false;
            }
        }
        const char* const allowedNames[] = {
            "value",
            "flag",
            "Heartbeat",
            "x",
            "_",
            "uint8x",
            "intensity",
            "format",
            "node_id",
            "myType",
            "_x",
        };
        for (const char* name : allowedNames)
        {
            if (llvmdsdl::isReservedIdentifier(name))
            {
                std::cerr << "isReservedIdentifier wrongly flagged allowed name: " << name << "\n";
                return false;
            }
        }
    }

    // The analyzer rejects reserved attribute names (section 3.5.1) but accepts
    // ordinary ones, and a padding field (no name) is exempt.
    {
        const auto analyzerRejects = [](const std::string& body) -> bool {
            llvmdsdl::DiagnosticEngine parse;
            llvmdsdl::Lexer            lex("uavcan.test.Reserved.1.0.dsdl", body);
            auto                       toks = lex.lex();
            llvmdsdl::Parser           parse2("uavcan.test.Reserved.1.0.dsdl", std::move(toks), parse);
            auto                       ast = parse2.parseDefinition();
            if (!ast)
            {
                llvm::consumeError(ast.takeError());
                return true;
            }
            if (parse.hasErrors())
            {
                return true;
            }
            llvmdsdl::DiscoveredDefinition disc;
            disc.filePath            = "uavcan/test/Reserved.1.0.dsdl";
            disc.rootNamespacePath   = "uavcan";
            disc.fullName            = "uavcan.test.Reserved";
            disc.shortName           = "Reserved";
            disc.namespaceComponents = {"uavcan", "test"};
            disc.majorVersion        = 1;
            disc.minorVersion        = 0;
            disc.text                = body;
            llvmdsdl::ASTModule mod;
            mod.definitions.push_back(llvmdsdl::ParsedDefinition{disc, *ast});
            llvmdsdl::DiagnosticEngine sem;
            auto                       model = llvmdsdl::analyze(mod, sem);
            if (!model)
            {
                llvm::consumeError(model.takeError());
                return true;
            }
            return sem.hasErrors();
        };

        const std::pair<std::string, bool> attrCases[] = {
            {"uint8 value\n@sealed\n", false},     // ordinary field name
            {"uint8 X = 1\n@sealed\n", false},     // ordinary constant name
            {"uint8 type\n@sealed\n", true},       // reserved field name
            {"uint8 uint8 = 1\n@sealed\n", true},  // reserved constant name
            {"bool true\n@sealed\n", true},        // reserved (parser also rejects)
            {"uint8 _offset_\n@sealed\n", true},   // matches the _.*_ pattern
            {"void3\n@sealed\n", false},           // padding field has no name -> exempt
        };
        for (const auto& attrCase : attrCases)
        {
            if (analyzerRejects(attrCase.first) != attrCase.second)
            {
                std::cerr << "reserved-attribute analysis mismatch for: " << attrCase.first << "\n";
                return false;
            }
        }
    }

    // Exactness contract for `_offset_` (supersedes the BLS-D2 truncation warning): the analyzer
    // binds `_offset_` symbolically, `.min`/`.max`/`% k` and singleton comparisons are exact at
    // any cardinality, and an expression that cannot be evaluated exactly is a hard error — an
    // assertion is never evaluated against a truncated offset set, so no `_offset_` warning may
    // exist at all anymore.
    {
        struct OffsetOutcome final
        {
            bool        succeeded{false};
            bool        anyOffsetWarning{false};
            std::string errors;
        };
        const auto analyzeOffsets = [](const std::string& source) -> OffsetOutcome {
            OffsetOutcome              outcome;
            llvmdsdl::DiagnosticEngine parse;
            llvmdsdl::Lexer            lexer("uavcan.test.OffsetLimit.1.0.dsdl", source);
            auto                       tokens = lexer.lex();
            llvmdsdl::Parser           parser("uavcan.test.OffsetLimit.1.0.dsdl", std::move(tokens), parse);
            auto                       parsed = parser.parseDefinition();
            if (!parsed)
            {
                llvm::consumeError(parsed.takeError());
                return outcome;
            }
            llvmdsdl::DiscoveredDefinition discovered;
            discovered.filePath            = "uavcan/test/OffsetLimit.1.0.dsdl";
            discovered.rootNamespacePath   = "uavcan";
            discovered.fullName            = "uavcan.test.OffsetLimit";
            discovered.shortName           = "OffsetLimit";
            discovered.namespaceComponents = {"uavcan", "test"};
            discovered.majorVersion        = 1;
            discovered.minorVersion        = 0;
            discovered.text                = source;
            llvmdsdl::ASTModule module;
            module.definitions.push_back(llvmdsdl::ParsedDefinition{discovered, *parsed});
            llvmdsdl::DiagnosticEngine sem;
            auto                       semantic = llvmdsdl::analyze(module, sem);
            if (!semantic)
            {
                llvm::consumeError(semantic.takeError());
            }
            outcome.succeeded = true;
            for (const llvmdsdl::Diagnostic& d : sem.diagnostics())
            {
                if (d.level == llvmdsdl::DiagnosticLevel::Warning && d.message.find("_offset_") != std::string::npos)
                {
                    outcome.anyOffsetWarning = true;
                }
                if (d.level == llvmdsdl::DiagnosticLevel::Error)
                {
                    outcome.succeeded = false;
                    outcome.errors += d.message + "\n";
                }
            }
            return outcome;
        };

        // A wide offset set (5001 candidates — beyond the old 4096 truncation ceiling) must
        // evaluate exactly: length prefix is 16 bits, so max/min/count/residues are all known.
        const std::string wide = "uint8[<=5000] payload\n"
                                 "@assert _offset_.min == 16\n"
                                 "@assert _offset_.max == 16 + 5000 * 8\n"
                                 "@assert _offset_.count == 5001\n"
                                 "@assert _offset_ % 8 == {0}\n"
                                 "@sealed\n";
        {
            const auto outcome = analyzeOffsets(wide);
            if (!outcome.succeeded || outcome.anyOffsetWarning)
            {
                std::cerr << "wide offset set must evaluate exactly with no warning; errors: " << outcome.errors
                          << "\n";
                return false;
            }
        }

        // The answer the old truncated expansion used to report for `.max` (the 4096th smallest
        // value) must now fail the assertion — pinning the truncated-set wrong-answer bug shut.
        const std::string stale = "uint8[<=5000] payload\n@assert _offset_.max == 16 + 4095 * 8\n@sealed\n";
        {
            const auto outcome = analyzeOffsets(stale);
            if (outcome.succeeded || outcome.errors.find("assertion failed") == std::string::npos)
            {
                std::cerr << "the formerly-truncated .max answer must now fail its assertion\n";
                return false;
            }
        }

        // Beyond the exact-materialization capacity (16384): symbolic queries stay exact —
        // including .count and set comparisons, which the run representation answers in closed
        // form at any cardinality (20001 possible offsets here)...
        const std::string huge = "uint8[<=20000] payload\n"
                                 "@assert _offset_.min == 16\n"
                                 "@assert _offset_.max == 16 + 20000 * 8\n"
                                 "@assert _offset_ % 8 == {0}\n"
                                 "@assert _offset_.count == 20001\n"
                                 "@assert _offset_ >= {16, 24, 160016}\n"
                                 "@assert !(_offset_ >= {17})\n"
                                 "@assert (_offset_ % 100000).count == 12500\n"
                                 "@assert (_offset_ % 100000).min == 0\n"
                                 "@assert (_offset_ % 100000).max == 99992\n"
                                 "@sealed\n";
        {
            const auto outcome = analyzeOffsets(huge);
            if (!outcome.succeeded || outcome.anyOffsetWarning)
            {
                std::cerr << "symbolic offset queries must stay exact beyond the materialization "
                             "capacity; errors: "
                          << outcome.errors << "\n";
                return false;
            }
        }

        // ...a singleton comparison is disproved exactly without materializing...
        const std::string unequal = "uint8[<=20000] payload\n@assert _offset_ == {0}\n@sealed\n";
        {
            const auto outcome = analyzeOffsets(unequal);
            if (outcome.succeeded || outcome.errors.find("assertion failed") == std::string::npos)
            {
                std::cerr << "singleton offset comparison must be disproved exactly, not deferred "
                             "to materialization\n";
                return false;
            }
        }

        // ...elementwise addition of a non-negative scalar stays symbolic (it is the algebra's
        // own Add node), so it is exact beyond the materialization capacity instead of failing
        // (the 16-bit array-length prefix puts the minimum offset at 16; +8 gives 24)...
        const std::string shifted = "uint8[<=20000] payload\n@assert (_offset_ + 8).min == 24\n@sealed\n";
        {
            const auto outcome = analyzeOffsets(shifted);
            if (!outcome.succeeded)
            {
                std::cerr << "scalar addition to a beyond-capacity offset set must stay symbolic; errors: "
                          << outcome.errors << "\n";
                return false;
            }
        }

        // ...and an operation that genuinely requires materializing the full contents (an
        // elementwise transform of a beyond-capacity set) is a hard error, never an approximate
        // answer (and never a mere warning).
        const std::string unmaterializable =
            "uint8[<=20000] payload\n@assert (_offset_ * 2).max == 2 * (16 + 20000 * 8)\n@sealed\n";
        {
            const auto outcome = analyzeOffsets(unmaterializable);
            if (outcome.succeeded || outcome.errors.find("cannot be materialized exactly") == std::string::npos)
            {
                std::cerr << "an elementwise offset transform beyond exact capacity must hard-fail; errors: "
                          << outcome.errors << "\n";
                return false;
            }
        }

        // A modulo whose exact residue set is itself beyond every budget (huge divisor on a
        // 70001-element set: the residues ARE the set) must hard-fail — this is the path the
        // old modulo() degrade would have answered with a silently truncated set.
        const std::string hugeMod = "uint8[<=70000] payload\n@assert (_offset_ % 1000000007).count == 70001\n@sealed\n";
        {
            const auto outcome = analyzeOffsets(hugeMod);
            if (outcome.succeeded || outcome.errors.find("cannot be materialized exactly") == std::string::npos)
            {
                std::cerr << "a beyond-budget modulo must hard-fail, never truncate; errors: " << outcome.errors
                          << "\n";
                return false;
            }
        }

        // A small type's offset set evaluates concretely; still no warning channel.
        const std::string small = "uint8 a\nuint16 b\n@assert _offset_.max >= 0\n@sealed\n";
        {
            const auto outcome = analyzeOffsets(small);
            if (!outcome.succeeded || outcome.anyOffsetWarning)
            {
                std::cerr << "small offset set must analyze cleanly with no warning\n";
                return false;
            }
        }
    }

    // Capacity overflow guard (P1 #1): an adversarial INT64_MAX array capacity must not overflow the
    // length-prefix width computation (previously `ceilLog2(capacity + 1)`) or the bit-length-set
    // repeat math. The analyzer must terminate cleanly with diagnostics, never crash, hang, or
    // invoke signed-overflow UB (the latter is caught by the UBSan lane, which exercises this path).
    {
        const auto analyzeCompletes = [](const std::string& body) -> bool {
            llvmdsdl::DiagnosticEngine parse;
            llvmdsdl::Lexer            lex("uavcan.test.HugeArray.1.0.dsdl", body);
            auto                       toks = lex.lex();
            llvmdsdl::Parser           p("uavcan.test.HugeArray.1.0.dsdl", std::move(toks), parse);
            auto                       ast = p.parseDefinition();
            if (!ast)
            {
                llvm::consumeError(ast.takeError());
                return true;
            }
            if (parse.hasErrors())
            {
                return true;
            }
            llvmdsdl::DiscoveredDefinition disc;
            disc.filePath            = "uavcan/test/HugeArray.1.0.dsdl";
            disc.rootNamespacePath   = "uavcan";
            disc.fullName            = "uavcan.test.HugeArray";
            disc.shortName           = "HugeArray";
            disc.namespaceComponents = {"uavcan", "test"};
            disc.majorVersion        = 1;
            disc.minorVersion        = 0;
            disc.text                = body;
            llvmdsdl::ASTModule mod;
            mod.definitions.push_back(llvmdsdl::ParsedDefinition{disc, *ast});
            llvmdsdl::DiagnosticEngine sem;
            auto                       model = llvmdsdl::analyze(mod, sem);
            if (!model)
            {
                llvm::consumeError(model.takeError());
            }
            return true;  // reaching here means analysis terminated without crash/hang/UB
        };
        // INT64_MAX inclusive, exclusive, and fixed-length capacities.
        if (!analyzeCompletes("uint8[<=9223372036854775807] x\n@sealed\n") ||
            !analyzeCompletes("uint8[<9223372036854775807] y\n@sealed\n") ||
            !analyzeCompletes("uint8[9223372036854775807] z\n@sealed\n"))
        {
            std::cerr << "analyzer failed to complete on adversarial array capacity\n";
            return false;
        }
    }

    return true;
}
