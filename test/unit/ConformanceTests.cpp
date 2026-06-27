//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Grammar-conformance regression tests for the DSDL lexer + parser.
///
/// Each case asserts whether a whole-file DSDL definition is accepted or
/// rejected, derived directly from the spec v1.0 section 3.2 PEG. These guard
/// the fixes produced by the production-by-production conformance audit:
/// operator precedence, numeric-literal validation, version specifiers, cast
/// modes, line endings, string content, and statement/token boundaries.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Frontend/Lexer.h"
#include "llvmdsdl/Frontend/Parser.h"
#include "llvmdsdl/Support/Diagnostics.h"

#include "llvm/Support/Error.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{

/// @brief Lexes and parses @p source as a whole definition.
/// @return True when it parses with no lexical or parse diagnostics.
bool accepts(const std::string& source)
{
    llvmdsdl::DiagnosticEngine diagnostics;
    llvmdsdl::Lexer            lexer("conformance.dsdl", source);
    auto                       tokens = lexer.lex();
    for (const llvmdsdl::LexerError& lexError : lexer.errors())
    {
        diagnostics.error(lexError.location, lexError.message);
    }
    llvmdsdl::Parser parser("conformance.dsdl", std::move(tokens), diagnostics);
    auto             definition = parser.parseDefinition();
    const bool       ok         = static_cast<bool>(definition);
    if (!definition)
    {
        llvm::consumeError(definition.takeError());
    }
    return ok && !diagnostics.hasErrors();
}

struct Case
{
    const char* body;
    bool        shouldParse;
};

}  // namespace

/// @brief Entry point invoked by the unit-test runner.
bool runConformanceTests()
{
    // Every snippet is completed into a whole definition body. `@sealed` makes a
    // grammatically complete file; cases that test whole-file shapes provide it
    // themselves.
    const Case cases[] = {
        // ---- Numeric literals (productions 121-132) ----
        {"uint64 X = 0\n@sealed\n", true},
        {"uint64 X = 00\n@sealed\n", true},          // all-zeros decimal
        {"uint64 X = 1_000\n@sealed\n", true},
        {"uint64 X = 0xDEAD_BEEF\n@sealed\n", true},
        {"uint64 X = 0b1010\n@sealed\n", true},
        {"uint64 X = 0o755\n@sealed\n", true},
        {"uint64 X = 01\n@sealed\n", false},         // decimal leading zero
        {"uint64 X = 1__2\n@sealed\n", false},       // doubled underscore
        {"uint64 X = 1_\n@sealed\n", false},         // trailing underscore
        {"uint64 X = 0b2\n@sealed\n", false},        // bad binary digit
        {"uint64 X = 0o8\n@sealed\n", false},        // bad octal digit
        {"uint64 X = 0xG\n@sealed\n", false},        // bad hex digit
        {"uint64 X = 0x\n@sealed\n", false},         // empty hex
        {"float64 X = 1.5\n@sealed\n", true},
        {"float64 X = .5\n@sealed\n", true},
        {"float64 X = 3.\n@sealed\n", true},
        {"float64 X = 1e9\n@sealed\n", true},
        {"float64 X = 1.5e-3\n@sealed\n", true},
        {"float64 X = 1e\n@sealed\n", false},        // exponent without digits
        {"float64 X = 1e+\n@sealed\n", false},
        {"float64 X = 1_.5\n@sealed\n", false},      // underscore before point
        {"float64 X = 1.5_\n@sealed\n", false},      // trailing underscore
        {"uint64 X = 99999999999999999999999999\n@sealed\n", false},  // not representable

        // ---- Operator precedence is exercised by ParserTests; here just sanity ----
        {"@assert !a == b\n@sealed\n", true},
        {"@assert -2 ** 2 == -4\n@sealed\n", true},

        // ---- Identifiers / bytes (production 6, ASCII-only) ----
        {"uint8 field_1\n@sealed\n", true},
        {"uint8 _x\n@sealed\n", true},
        {"@assert ? < 1\n@sealed\n", false},         // stray ASCII punctuation

        // ---- Primitive types and cast modes (productions 36-53) ----
        {"truncated uint7 x\n@sealed\n", true},
        {"saturated int8 x\n@sealed\n", true},
        {"bool x\n@sealed\n", true},
        {"byte x\n@sealed\n", true},
        {"truncated bool x\n@sealed\n", false},      // cast on non-numeric
        {"saturated utf8 x\n@sealed\n", false},
        {"truncated void8\n@sealed\n", false},

        // ---- Versioned types (productions 31-35) ----
        {"uavcan.node.Heartbeat.1.0 h\n@sealed\n", true},
        {"Name.0x1.0 x\n@sealed\n", false},          // non-decimal version
        {"Name.01.0 x\n@sealed\n", false},           // leading-zero version
        {"saturated.Foo.1.0 x\n@sealed\n", true},    // 'saturated' is a namespace

        // ---- Array types (productions 23-30) ----
        {"uint8[4] a\n@sealed\n", true},
        {"uint8[<=4] a\n@sealed\n", true},
        {"uint8[<8] a\n@sealed\n", true},

        // ---- Line endings (production 4) ----
        {"uint8 a\r\n@sealed\r\n", true},            // CRLF
        {"uint8 a\r@sealed\r", false},               // lone CR

        // ---- String literals (productions 134-137, table 3.4) ----
        {"@assert 'a b' == 'a b'\n@sealed\n", true},
        {"@assert \"x\\nz\" == \"x\\nz\"\n@sealed\n", true},
        {"@assert 'bad\\q' == 'x'\n@sealed\n", false},  // invalid escape

        // ---- Statement / token boundaries (productions 1-2, 14-21) ----
        {"uint8 a\n@sealed\n", true},
        {"uint8 a uint8 b\n@sealed\n", false},       // two statements per line
        {"uint8 a @sealed\n", false},                // trailing tokens
        {"uint8[2]name\n@sealed\n", false},          // missing type/name whitespace
        {"uint8[2] name\n@sealed\n", true},
        {"void8\n@sealed\n", true},                  // padding field
        {"void8[2]\n@sealed\n", false},              // padding cannot be array
        {"@union\nuint8 a\n@sealed\n", true},
        {"@ union\n@sealed\n", false},               // space after '@'
        {"@assert(1 == 1)\n@sealed\n", false},       // missing whitespace before expr
        {"@assert (1 == 1)\n@sealed\n", true},

        // ---- Top-level structure (productions 1-2) ----
        {"", true},                                  // empty file is valid
        {"\n\n\n", true},                            // only blank lines
        {"# just a comment\n", true},
        {"   # indented comment\n@sealed\n", true},  // leading ws before comment is ok
        {"  uint8 a\n@sealed\n", false},             // leading ws before a statement is not

        // ---- Whitespace adjacency inside literals and type names ----
        {"float64 X = . 5\n@sealed\n", false},       // space between '.' and digits
        {"float64 X = 3 .\n@sealed\n", false},       // space between digits and '.'
        {"a.b.C.1.0 x\n@sealed\n", true},
        {"Name .1.0 x\n@sealed\n", false},           // space before version dot
        {"Name. 1.0 x\n@sealed\n", false},           // space after namespace dot
        {"Name.1 .0 x\n@sealed\n", false},           // space inside version
        {"@assert 3 . foo == 0\n@sealed\n", true},   // '.' + identifier is attribute access

        // ---- Reserved identifiers (§3.2.5 / table 3.5) must not name entities ----
        {"uint8.Foo.1.0 x\n@sealed\n", false},       // 'uint8' is a reserved pattern
        {"uint8 true\n@sealed\n", false},            // 'true' is reserved

        // ---- Trailing-dot real with exponent (productions 128-129) ----
        {"@assert 3.e2 == 300.0\n@sealed\n", true},  // 3.e2 is a single real
        {"@assert 3.E+2 == 300.0\n@sealed\n", true},
        // Possessive PEG: an integer adjacent to '.' is a real `3.`, so the
        // trailing identifier dangles; only a space makes it attribute access.
        {"@assert 3.field == 0\n@sealed\n", false},
        {"@assert 3 . field == 0\n@sealed\n", true},
        // ---- Boolean keywords are valid directive names (productions 6, 21) ----
        {"@true\n@sealed\n", true},
        {"@false\n@sealed\n", true},
    };

    bool ok = true;
    for (const Case& testCase : cases)
    {
        const bool actual = accepts(testCase.body);
        if (actual != testCase.shouldParse)
        {
            std::cerr << "conformance regression: \"" << testCase.body << "\" expected "
                      << (testCase.shouldParse ? "ACCEPT" : "REJECT") << " but got "
                      << (actual ? "ACCEPT" : "REJECT") << "\n";
            ok = false;
        }
    }
    if (!ok)
    {
        std::cerr << "conformance tests failed\n";
    }
    return ok;
}
