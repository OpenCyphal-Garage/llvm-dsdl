//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Deterministic fuzz-style tests for the DSDL parser.
///
/// These tests drive the full untrusted-input pipeline -- raw source text is
/// lexed and then parsed via @ref llvmdsdl::Parser::parseDefinition -- over
/// large volumes of random, structured, and mutated inputs. The parser is a
/// hand-written recursive-descent parser with error recovery, so the failure
/// modes of interest are crashes, undefined behaviour (caught when built with
/// sanitizers / via the standalone libFuzzer harness), non-termination in the
/// statement / expression loops, and contract violations on the returned
/// @c llvm::Expected.
///
/// All generators are seeded with fixed values for full reproducibility. A
/// watchdog thread converts any single-input hang into an actionable abort
/// (printing the offending bytes) rather than an opaque CI timeout.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Frontend/Lexer.h"
#include "llvmdsdl/Frontend/Parser.h"
#include "llvmdsdl/Support/Diagnostics.h"

#include "llvm/Support/Error.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <thread>

#include "UnitTests.h"

namespace
{

using llvmdsdl::DiagnosticEngine;
using llvmdsdl::Lexer;
using llvmdsdl::Parser;
using llvmdsdl::Token;

/// @brief Heartbeat incremented after each parse; watched for stalls (hangs).
std::atomic<std::uint64_t> gHeartbeat{0};

/// @brief Signals the watchdog thread to stop once fuzzing is complete.
std::atomic<bool> gFinished{false};

/// @brief Snapshot of the input currently being parsed, for hang diagnostics.
std::string gCurrentInput;

/// @brief Renders bytes as hex for reproducible failure reports.
std::string toHex(std::string_view input)
{
    static const char* const kHex = "0123456789abcdef";
    std::string              out;
    out.reserve(input.size() * 3);
    for (const unsigned char ch : input)
    {
        out.push_back(kHex[ch >> 4]);
        out.push_back(kHex[ch & 0x0F]);
        out.push_back(' ');
    }
    return out;
}

/// @brief Lexes and parses @p input, verifying parser invariants.
/// @param[in] context Human-readable label for diagnostics on failure.
/// @param[in] input Source text to lex and parse.
/// @return True when every invariant holds; false (with a logged reason) otherwise.
bool parseAndCheck(std::string_view context, std::string_view input)
{
    gCurrentInput.assign(input);

    DiagnosticEngine   diagnostics;
    Lexer              lexer("fuzz.dsdl", std::string(input));
    std::vector<Token> tokens     = lexer.lex();
    const std::size_t  tokenCount = tokens.size();

    Parser                                  parser("fuzz.dsdl", std::move(tokens), diagnostics);
    llvm::Expected<llvmdsdl::DefinitionAST> definition = parser.parseDefinition();

    const bool succeeded = static_cast<bool>(definition);

    const auto reportFailure = [&](const char* reason) {
        std::cerr << "parser fuzz invariant violated [" << context << "]: " << reason << "\n";
        std::cerr << "  input (" << input.size() << " bytes): " << toHex(input) << "\n";
        return false;
    };

    bool ok = true;

    // Contract: parseDefinition() returns an error iff it recorded diagnostics.
    // (See Parser::parseDefinition, which returns an error exactly when
    // diagnostics_.hasErrors() is set.)
    if (succeeded == diagnostics.hasErrors())
    {
        ok = reportFailure("Expected success/failure disagrees with DiagnosticEngine::hasErrors()");
    }

    if (succeeded)
    {
        // A successful parse cannot have invented more statements than there
        // were tokens to build them from.
        if (definition->statements.size() > tokenCount)
        {
            ok = reportFailure("statement count exceeds token count");
        }
    }
    else
    {
        // llvm::Expected holds a checked error that must be consumed.
        llvm::consumeError(definition.takeError());
    }

    gHeartbeat.fetch_add(1, std::memory_order_relaxed);
    return ok;
}

/// @brief DSDL-like fragments spanning the parser's statement and expression forms.
constexpr auto kFragments = std::to_array<std::string_view>({
    // Primitive and composite type references.
    "uint8",
    "int16",
    "float32",
    "float64",
    "bool",
    "void3",
    "truncated uint7",
    "saturated int4",
    "uavcan.node.Heartbeat.1.0",
    "uavcan.file.Path.2.0",
    // Field and constant declarations.
    " field\n",
    " value = 42\n",
    " X = 0x1F\n",
    " name\n",
    // Array specifiers.
    "[4]",
    "[<=4]",
    "[<8]",
    "[>=1]",
    "[]",
    // Directives.
    "@union\n",
    "@sealed\n",
    "@extent 64\n",
    "@deprecated\n",
    "@assert ",
    "@print ",
    // Expressions and operators.
    "_offset_",
    "% 8",
    "== {0}",
    "<= 7",
    "&& true",
    "|| false",
    "+ 1",
    "- 2",
    "* 3",
    "/ 4",
    "** 2",
    "( 1 + 2 )",
    "{ 0, 8, 16 }",
    ".MAX_LENGTH",
    "!x",
    "1.5",
    "'str'",
    "\"dq\"",
    // Structural tokens.
    "---\n",
    "\n",
    "# comment\n",
    "@",
});

/// @brief Appends a random fragment, whitespace, or raw byte to @p out.
template <typename Rng>
void appendRandomPiece(Rng& rng, std::string& out)
{
    std::uniform_int_distribution<int> choice(0, 9);
    switch (choice(rng))
    {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4: {
        std::uniform_int_distribution<std::size_t> pick(0, kFragments.size() - 1);
        out.append(kFragments[pick(rng)]);
        break;
    }
    case 5:
        out.push_back(' ');
        break;
    case 6:
        out.push_back('\n');
        break;
    case 7: {
        static constexpr std::string_view          kPunct = "@()[]{},.=+-*/%|^&!<>#'\"\\";
        std::uniform_int_distribution<std::size_t> pick(0, kPunct.size() - 1);
        out.push_back(kPunct[pick(rng)]);
        break;
    }
    default: {
        std::uniform_int_distribution<int> byte(0, 255);
        out.push_back(static_cast<char>(byte(rng)));
        break;
    }
    }
}

/// @brief Applies a random in-place mutation to @p data.
template <typename Rng>
void mutate(Rng& rng, std::string& data)
{
    if (data.empty())
    {
        return;
    }
    std::uniform_int_distribution<int>         op(0, 3);
    std::uniform_int_distribution<std::size_t> pos(0, data.size() - 1);
    std::uniform_int_distribution<int>         byte(0, 255);
    switch (op(rng))
    {
    case 0:
        data[pos(rng)] = static_cast<char>(data[pos(rng)] ^ (1 << (byte(rng) & 7)));
        break;
    case 1:
        data[pos(rng)] = static_cast<char>(byte(rng));
        break;
    case 2:
        data.insert(data.begin() + static_cast<std::ptrdiff_t>(pos(rng)), static_cast<char>(byte(rng)));
        break;
    default:
        data.resize(pos(rng));
        break;
    }
}

/// @brief Curated adversarial inputs targeting specific parser edge cases.
bool runEdgeCaseCorpus()
{
    static const auto kCorpus = std::to_array<std::string_view>({
        std::string_view("", 0),                          // Empty definition.
        "\n\n\n",                                         // Only newlines.
        "# just a comment\n",                             // Only a comment.
        "@",                                              // Bare directive introducer.
        "@union",                                         // Directive, no newline, at EOF.
        "@extent",                                        // Directive missing its operand.
        "uint8",                                          // Type with no field name, at EOF.
        "uint8 ",                                         // Type then trailing space.
        "uint8 field",                                    // Field, no newline terminator.
        "uint8[ field\n",                                 // Unterminated array specifier.
        "uint8[<=] field\n",                              // Array bound with no expression.
        "x = \n",                                         // Constant with no value expression.
        "x = (((((\n",                                    // Deeply unbalanced parentheses.
        "x = {\n",                                        // Unterminated set literal.
        "x = 1 + + + + 2\n",                              // Repeated binary operators.
        "x = ----------\n",                               // Run of minus signs (unary stack).
        "@assert _offset_ % 8 == {0}\n",                  // Valid assertion expression.
        "---\n---\n---\n",                                // Multiple service markers.
        "uint8 a\n---\nbool b\n@sealed\n",                // Minimal valid service.
        "uavcan.node.Heartbeat.1.0 h\n@sealed\n",         // Composite-type field.
        ".5\n",                                           // Leading-dot real (see conformance gap).
        "@union\nuint99999999999999999999 a\n@sealed\n",  // Oversized bit length (regression: was an abort).
        "int999999999999999999999 a\n@sealed\n",          // Oversized signed-int bit length.
        "float88888888888888888888 a\n@sealed\n",         // Oversized float bit length.
        "void77777777777777777777\n@sealed\n",            // Oversized void bit length.
        "@@@@@@\n",                                       // Run of directive introducers.
        std::string_view("uint8\0field\n", 12),           // Embedded NUL.
        "\xff\xfe identifier\n",                          // High-bit bytes before a statement.
    });

    bool ok = true;
    for (auto kCorpu : kCorpus)
    {
        ok = parseAndCheck("edge-case", kCorpu) && ok;
    }
    return ok;
}

/// @brief Streams of fully random bytes of varied lengths.
bool runRandomByteFuzz()
{
    std::mt19937                       rng(0x0B5E55EDU);
    std::uniform_int_distribution<int> byte(0, 255);
    bool                               ok = true;

    for (int iteration = 0; iteration < 15000; ++iteration)
    {
        std::uniform_int_distribution<std::size_t> lengthDist(0, 256);
        const std::size_t                          length = lengthDist(rng);
        std::string                                data;
        data.reserve(length);
        for (std::size_t i = 0; i < length; ++i)
        {
            data.push_back(static_cast<char>(byte(rng)));
        }
        ok = parseAndCheck("random-bytes", data) && ok;
    }
    return ok;
}

/// @brief Token-aware inputs assembled from DSDL-like fragments.
bool runStructuredFuzz()
{
    std::mt19937 rng(0xDEFEC8EDU);
    bool         ok = true;

    for (int iteration = 0; iteration < 20000; ++iteration)
    {
        std::uniform_int_distribution<int> pieces(0, 40);
        const int                          count = pieces(rng);
        std::string                        data;
        for (int i = 0; i < count; ++i)
        {
            appendRandomPiece(rng, data);
        }
        ok = parseAndCheck("structured", data) && ok;
    }
    return ok;
}

/// @brief Mutational fuzzing seeded from valid-looking fragments.
bool runMutationalFuzz()
{
    std::mt19937 rng(0xF0CACC1AU);
    bool         ok = true;

    for (int iteration = 0; iteration < 20000; ++iteration)
    {
        std::string data;
        {
            std::uniform_int_distribution<int> pieces(1, 14);
            const int                          count = pieces(rng);
            for (int i = 0; i < count; ++i)
            {
                appendRandomPiece(rng, data);
            }
        }
        std::uniform_int_distribution<int> rounds(1, 10);
        const int                          mutations = rounds(rng);
        for (int i = 0; i < mutations; ++i)
        {
            mutate(rng, data);
        }
        ok = parseAndCheck("mutational", data) && ok;
    }
    return ok;
}

}  // namespace

/// @brief Entry point invoked by the unit-test runner.
/// @return True when all parser fuzz invariants hold across every strategy.
bool runParserFuzzTests()
{
    // Watchdog: if a single parse stalls (no heartbeat progress) for too long,
    // the parser is hung -- report the offending input and abort hard so CI
    // surfaces an actionable bug instead of an opaque timeout.
    std::thread watchdog([] {
        std::uint64_t last   = gHeartbeat.load(std::memory_order_relaxed);
        int           stalls = 0;
        while (!gFinished.load(std::memory_order_relaxed))
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            const std::uint64_t now = gHeartbeat.load(std::memory_order_relaxed);
            if (now == last)
            {
                if (++stalls >= 10)
                {
                    std::cerr << "parser fuzz: watchdog detected a hang (>10s on one input)\n";
                    std::cerr << "  input (" << gCurrentInput.size() << " bytes): " << toHex(gCurrentInput) << "\n";
                    std::abort();
                }
            }
            else
            {
                stalls = 0;
                last   = now;
            }
        }
    });

    bool ok = true;
    ok      = runEdgeCaseCorpus() && ok;
    ok      = runRandomByteFuzz() && ok;
    ok      = runStructuredFuzz() && ok;
    ok      = runMutationalFuzz() && ok;

    gFinished.store(true, std::memory_order_relaxed);
    watchdog.join();

    if (!ok)
    {
        std::cerr << "parser fuzz tests failed\n";
    }
    return ok;
}
