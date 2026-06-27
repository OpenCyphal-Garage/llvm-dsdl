//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Deterministic fuzz-style tests for the DSDL lexer.
///
/// The lexer ingests untrusted `.dsdl` source text, so it is the compiler's
/// outermost attack surface. This suite feeds large volumes of random,
/// mutated, and adversarial byte sequences through @ref llvmdsdl::Lexer and
/// asserts a set of structural invariants on every result. The generators are
/// seeded with fixed values so failures are fully reproducible in CI.
///
/// The invariant core (@ref lexAndCheck) is intentionally identical in spirit
/// to the standalone libFuzzer harness, so coverage-guided fuzzing and the
/// in-tree CTest run exercise the same properties.
///
/// Design note discovered while building these tests: unlike every other
/// token, the @ref llvmdsdl::TokenKind::Newline token does not carry its
/// verbatim source spelling. The lexer emits the synthetic two-character
/// literal "\n" (backslash + 'n') for it, so its @c text is longer than the
/// single source byte it consumed. The text-length invariant below therefore
/// exempts Newline tokens explicitly.
///
/// Known conformance gap (documented, not yet fixed): DSDL spec v1.0 section
/// 3.2.4 permits `.5` and `3.` as real literals, but this context-free lexer
/// emits `Dot Integer(5)` and `Integer(3) Dot` respectively (see lexNumber in
/// lib/Frontend/Lexer.cpp). These fuzz invariants are structural only, so they
/// do not assert literal semantics; the decision is to resolve `.5` / `3.` in
/// the parser, where literal-vs-version-vs-attribute context is available.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Frontend/Lexer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using llvmdsdl::Lexer;
using llvmdsdl::Token;
using llvmdsdl::TokenKind;

/// @brief Runs the lexer over @p input and verifies structural invariants.
/// @param[in] context Human-readable label for diagnostics on failure.
/// @param[in] input Source text to tokenize.
/// @return True when every invariant holds; false (with a logged reason) otherwise.
bool lexAndCheck(std::string_view context, std::string_view input)
{
    Lexer                    lexer("fuzz.dsdl", std::string(input));
    const std::vector<Token> tokens = lexer.lex();

    const auto reportFailure = [&](const char* reason) {
        std::cerr << "lexer fuzz invariant violated [" << context << "]: " << reason << "\n";
        std::cerr << "  input (" << input.size() << " bytes):";
        for (const unsigned char ch : input)
        {
            static const char* const hex = "0123456789abcdef";
            std::cerr << ' ' << hex[ch >> 4] << hex[ch & 0x0F];
        }
        std::cerr << "\n";
        return false;
    };

    // The token stream must always be terminated by exactly one Eof at the end.
    if (tokens.empty())
    {
        return reportFailure("empty token vector (missing Eof)");
    }
    if (tokens.back().kind != TokenKind::Eof)
    {
        return reportFailure("last token is not Eof");
    }
    if (!tokens.back().text.empty())
    {
        return reportFailure("Eof token has non-empty text");
    }
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i)
    {
        if (tokens[i].kind == TokenKind::Eof)
        {
            return reportFailure("Eof token appears before end of stream");
        }
    }

    // Every non-Eof token consumes at least one input byte, so the lexer can
    // never emit more than input.size() + 1 tokens (the +1 being Eof). A
    // violation indicates a token emitted without consuming input, i.e. a
    // latent infinite-loop / runaway-emit regression.
    if (tokens.size() > input.size() + 1)
    {
        return reportFailure("token count exceeds input.size() + 1");
    }

    for (const Token& token : tokens)
    {
        if (token.location.line < 1)
        {
            return reportFailure("token line is < 1");
        }
        if (token.location.column < 1)
        {
            return reportFailure("token column is < 1");
        }
        // No single token's spelling can be longer than the whole input, EXCEPT
        // the Newline token, whose spelling is the synthetic two-character
        // literal "\n" rather than the consumed source byte(s). See the design
        // note in the file header / accompanying report.
        if (token.kind != TokenKind::Newline && token.text.size() > input.size())
        {
            return reportFailure("token text longer than entire input");
        }
    }

    return true;
}

/// @brief Token-like fragments covering every lexer branch, used as mutation seeds.
constexpr auto kFragments = std::to_array<std::string_view>({
    // Identifiers and keywords.
    "uint8", "float64", "bool", "void3", "_offset_", "saturated", "truncated", "true", "false", "x_1",
    // Numeric literals across every scanning path.
    "0", "42", "1_000", "0x1F", "0X0a", "0b1010", "0B11", "0o755", "0O17", "1.5", ".5", "3.", "1e9", "2.0e-3", "6.022E+23",
    // String literals and escapes.
    "'hello'", "\"world\"", "'a\\nb'", "'tab\\t'", "'q\\'q'", "\"esc\\\\\"",
    // Comments and directives.
    "# a comment", "@union", "@sealed", "@assert", "@extent",
    // Structural markers and operators.
    "---", "----", "<=", ">=", "==", "!=", "||", "&&", "**", "[", "]", "{", "}",
});

/// @brief Appends a random fragment, separator, or raw byte to @p out.
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
    {
        std::uniform_int_distribution<std::size_t> pick(0, kFragments.size() - 1);
        out.append(kFragments[pick(rng)]);
        break;
    }
    case 4:
        out.push_back(' ');
        break;
    case 5:
        out.push_back('\n');
        break;
    case 6:
        out.append("\r\n");
        break;
    case 7:
    {
        // Lone punctuation, including bytes with no two-char operator partner.
        static constexpr std::string_view kPunct = "@()[]{},.=+-*/%|^&!<>#'\"\\:;?~`";
        std::uniform_int_distribution<std::size_t> pick(0, kPunct.size() - 1);
        out.push_back(kPunct[pick(rng)]);
        break;
    }
    default:
    {
        // Any raw byte, including NUL and high-bit / non-ASCII bytes.
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
    case 0:  // Bit flip.
        data[pos(rng)] = static_cast<char>(data[pos(rng)] ^ (1 << (byte(rng) & 7)));
        break;
    case 1:  // Overwrite with a random byte.
        data[pos(rng)] = static_cast<char>(byte(rng));
        break;
    case 2:  // Insert a random byte.
        data.insert(data.begin() + static_cast<std::ptrdiff_t>(pos(rng)), static_cast<char>(byte(rng)));
        break;
    default:  // Truncate.
        data.resize(pos(rng));
        break;
    }
}

/// @brief Curated adversarial inputs that target specific lexer edge cases.
bool runEdgeCaseCorpus()
{
    static const auto kCorpus = std::to_array<std::string_view>({
        std::string_view("", 0),                            // Empty input.
        std::string_view("\0", 1),                          // Lone NUL byte.
        std::string_view("a\0b", 3),                        // Embedded NUL inside identifier run.
        "'unterminated",                                    // String with no closing quote at EOF.
        "\"unterminated",                                   // Double-quoted, unterminated.
        "'newline\nafter",                                  // String terminated by a newline.
        "'trailing backslash\\",                            // Backslash as final byte (escape at EOF).
        std::string_view("'\\\0'", 4),                      // Escaped NUL inside a string.
        "0x",                                               // Hex prefix with no digits.
        "0b",                                               // Binary prefix with no digits.
        "0o",                                               // Octal prefix with no digits.
        "0xGHI",                                            // Hex prefix, non-hex tail.
        "123_",                                             // Trailing digit separator.
        "1.2.3",                                            // Multiple dots.
        ".",                                                // Lone dot.
        ".e5",                                              // Dot then exponent, no mantissa digits.
        "1e",                                               // Exponent with no digits.
        "1e+",                                              // Exponent sign with no digits.
        "--",                                               // Two dashes (not a marker).
        "------",                                           // Many dashes (marker plus extras).
        "#",                                                // Bare comment introducer.
        "\r",                                               // Lone carriage return.
        "\xff\xfe\x80",                                     // High-bit / non-ASCII bytes.
        "@\xc3\xa9",                                        // Directive introducer plus UTF-8 bytes.
    });

    bool ok = true;
    for (std::size_t i = 0; i < kCorpus.size(); ++i)
    {
        ok = lexAndCheck("edge-case", kCorpus[i]) && ok;
    }
    return ok;
}

/// @brief Streams of fully random bytes of varied lengths.
bool runRandomByteFuzz()
{
    std::mt19937                       rng(0xC0FFEEu);
    std::uniform_int_distribution<int> byte(0, 255);
    bool                               ok = true;

    for (int iteration = 0; iteration < 20000; ++iteration)
    {
        std::uniform_int_distribution<std::size_t> lengthDist(0, 256);
        const std::size_t                          length = lengthDist(rng);
        std::string                                data;
        data.reserve(length);
        for (std::size_t i = 0; i < length; ++i)
        {
            data.push_back(static_cast<char>(byte(rng)));
        }
        ok = lexAndCheck("random-bytes", data) && ok;
    }
    return ok;
}

/// @brief Token-aware inputs assembled from DSDL-like fragments.
bool runStructuredFuzz()
{
    std::mt19937 rng(0x5EED1234u);
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
        ok = lexAndCheck("structured", data) && ok;
    }
    return ok;
}

/// @brief Mutational fuzzing seeded from valid fragments and edge cases.
bool runMutationalFuzz()
{
    std::mt19937 rng(0xABCDEF01u);
    bool         ok = true;

    for (int iteration = 0; iteration < 20000; ++iteration)
    {
        // Build a plausible seed, then corrupt it a handful of times.
        std::string data;
        {
            std::uniform_int_distribution<int> pieces(1, 12);
            const int                          count = pieces(rng);
            for (int i = 0; i < count; ++i)
            {
                appendRandomPiece(rng, data);
            }
        }
        std::uniform_int_distribution<int> rounds(1, 8);
        const int                          mutations = rounds(rng);
        for (int i = 0; i < mutations; ++i)
        {
            mutate(rng, data);
        }
        ok = lexAndCheck("mutational", data) && ok;
    }
    return ok;
}

}  // namespace

/// @brief Entry point invoked by the unit-test runner.
/// @return True when all lexer fuzz invariants hold across every strategy.
bool runLexerFuzzTests()
{
    bool ok = true;
    ok      = runEdgeCaseCorpus() && ok;
    ok      = runRandomByteFuzz() && ok;
    ok      = runStructuredFuzz() && ok;
    ok      = runMutationalFuzz() && ok;
    if (!ok)
    {
        std::cerr << "lexer fuzz tests failed\n";
    }
    return ok;
}
