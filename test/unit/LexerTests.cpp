//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Lexer conformance tests, focused on string-literal escape sequences.
///
/// DSDL spec v1.0 section 3.4 / table 3.4 defines the only valid escape forms:
/// \\ \r \n \t \' \" \u???? \U????????. This suite checks that valid escapes
/// (including Unicode escapes encoded as UTF-8) decode correctly and that
/// malformed escapes are reported via @ref llvmdsdl::Lexer::errors.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Frontend/Lexer.h"

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace
{

using llvmdsdl::Lexer;
using llvmdsdl::Token;
using llvmdsdl::TokenKind;

/// @brief Lexes @p source and returns the value of its first String token.
std::string lexStringValue(const std::string& source, std::size_t& errorCount)
{
    Lexer                    lexer("lex.dsdl", source);
    const std::vector<Token> tokens = lexer.lex();
    errorCount                      = lexer.errors().size();
    for (const Token& token : tokens)
    {
        if (token.kind == TokenKind::String)
        {
            return token.text;
        }
    }
    return std::string();
}

bool expectValue(const char* label, const std::string& source, const std::string& expected)
{
    std::size_t       errorCount = 0;
    const std::string actual     = lexStringValue(source, errorCount);
    if (actual != expected || errorCount != 0)
    {
        std::cerr << "lexer string escape [" << label << "]: expected a clean decode; got " << errorCount
                  << " errors and value of length " << actual.size() << "\n";
        return false;
    }
    return true;
}

bool expectErrors(const char* label, const std::string& source)
{
    std::size_t errorCount = 0;
    (void) lexStringValue(source, errorCount);
    if (errorCount == 0)
    {
        std::cerr << "lexer string escape [" << label << "]: expected at least one lexical error, got none\n";
        return false;
    }
    return true;
}

}  // namespace

/// @brief Entry point invoked by the unit-test runner.
bool runLexerTests()
{
    bool ok = true;

    // Valid simple escapes (spec table 3.4).
    ok = expectValue("backslash", R"('\\')", "\\") && ok;
    ok = expectValue("newline", R"('\n')", "\n") && ok;
    ok = expectValue("carriage-return", R"('\r')", "\r") && ok;
    ok = expectValue("tab", R"('\t')", "\t") && ok;
    ok = expectValue("single-quote", R"('\'')", "'") && ok;
    ok = expectValue("double-quote", R"("\"")", "\"") && ok;

    // Unicode escapes decode to UTF-8.
    // A backslash built from its ASCII code keeps these test cases free of
    // source-level escape-encoding hazards while still exercising "\u"/"\U".
    const char        backslash    = static_cast<char>(92);
    const std::string uAsciiSpace  = std::string("'") + backslash + "u0020'";
    const std::string uBmpEAcute   = std::string("'") + backslash + "u00e9'";
    ok = expectValue("u-ascii-space", uAsciiSpace, " ") && ok;
    ok = expectValue("u-bmp-e-acute", uBmpEAcute, "\xc3\xa9") && ok;
    ok = expectValue("U-ascii-lf", R"('\U0000000a')", "\n") && ok;
    ok = expectValue("U-emoji", R"('\U0001F600')", "\xf0\x9f\x98\x80") && ok;

    // Spec example (section 3.2.4): the two literals must decode identically.
    {
        std::size_t       doubleErrors = 0;
        std::size_t       singleErrors = 0;
        const std::string fromDouble   = lexStringValue(R"("oh, hi\U0000000aMark")", doubleErrors);
        const std::string fromSingle   = lexStringValue(R"('oh, hi\nMark')", singleErrors);
        if (fromDouble != fromSingle || fromDouble != std::string("oh, hi\nMark") || doubleErrors != 0 ||
            singleErrors != 0)
        {
            std::cerr << "lexer string escape [spec-example]: the two spec literals did not decode identically\n";
            ok = false;
        }
    }

    // Malformed escapes must be reported.
    ok = expectErrors("invalid-escape-x", R"("a\xZZ")") && ok;
    ok = expectErrors("invalid-escape-q", R"("a\qb")") && ok;
    ok = expectErrors("incomplete-u", R"("a\u12")") && ok;
    ok = expectErrors("nonhex-u", R"("a\uZZZZ")") && ok;
    ok = expectErrors("surrogate", R"("\uD800")") && ok;
    ok = expectErrors("unterminated-string", "'abc") && ok;
    ok = expectErrors("backslash-newline", "'a\\\nb'") && ok;

    if (!ok)
    {
        std::cerr << "lexer tests failed\n";
    }
    return ok;
}
