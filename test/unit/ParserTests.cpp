//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <llvm/Support/Error.h>
#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "llvmdsdl/Frontend/Lexer.h"
#include "llvmdsdl/Frontend/Parser.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Frontend/AST.h"

#include "UnitTests.h"

bool runParserTests()
{
    const std::string text = "@union\n"
                             "uint8 a\n"
                             "uint16 b\n"
                             "@extent 64\n"
                             "---\n"
                             "bool ok\n"
                             "@sealed\n";

    llvmdsdl::DiagnosticEngine diag;
    llvmdsdl::Lexer            lexer("test.dsdl", text);
    auto                       tokens = lexer.lex();

    llvmdsdl::Parser parser("test.dsdl", std::move(tokens), diag);
    auto             def = parser.parseDefinition();
    if (!def)
    {
        std::cerr << "parser failed unexpectedly\n";
        return false;
    }

    if (!def->isService())
    {
        std::cerr << "expected service definition\n";
        return false;
    }

    if (def->statements.size() != 7)
    {
        std::cerr << "unexpected statement count: " << def->statements.size() << "\n";
        return false;
    }

    const std::string typeLiteralText = "uint8[<=uavcan.file.Path.2.0.MAX_LENGTH] parameter\n"
                                        "@sealed\n";

    llvmdsdl::DiagnosticEngine typeLiteralDiag;
    llvmdsdl::Lexer            typeLiteralLexer("type_literal_expr.dsdl", typeLiteralText);
    auto                       typeLiteralTokens = typeLiteralLexer.lex();
    llvmdsdl::Parser typeLiteralParser("type_literal_expr.dsdl", std::move(typeLiteralTokens), typeLiteralDiag);
    auto             typeLiteralDef = typeLiteralParser.parseDefinition();
    if (!typeLiteralDef)
    {
        std::cerr << "type-literal parser failed unexpectedly\n";
        return false;
    }
    if (typeLiteralDef->statements.size() != 2)
    {
        std::cerr << "unexpected type-literal statement count: " << typeLiteralDef->statements.size() << "\n";
        return false;
    }

    const auto* field = std::get_if<llvmdsdl::FieldDeclAST>(typeLiteralDef->statements.data());
    if (!field)
    {
        std::cerr << "expected first statement to be field declaration\n";
        return false;
    }
    if (field->type.arrayKind != llvmdsdl::ArrayKind::VariableInclusive)
    {
        std::cerr << "expected variable inclusive array kind\n";
        return false;
    }
    if (!field->type.arrayCapacity)
    {
        std::cerr << "expected array capacity expression\n";
        return false;
    }

    const auto* capacityBinary = std::get_if<llvmdsdl::ExprAST::Binary>(&field->type.arrayCapacity->value);
    if (!capacityBinary || capacityBinary->op != llvmdsdl::BinaryOp::Attribute)
    {
        std::cerr << "expected attribute expression in array capacity\n";
        return false;
    }

    const auto* lhsType = std::get_if<llvmdsdl::ExprAST::TypeLiteral>(&capacityBinary->lhs->value);
    if (!lhsType)
    {
        std::cerr << "expected type literal on attribute LHS\n";
        return false;
    }
    const auto* rhsId = std::get_if<llvmdsdl::ExprAST::Identifier>(&capacityBinary->rhs->value);
    if (!rhsId || rhsId->name != "MAX_LENGTH")
    {
        std::cerr << "expected MAX_LENGTH on attribute RHS\n";
        return false;
    }

    const auto* versioned = std::get_if<llvmdsdl::VersionedTypeExprAST>(&lhsType->type.scalar);
    if (!versioned)
    {
        std::cerr << "expected versioned type literal\n";
        return false;
    }
    if (versioned->nameComponents != std::vector<std::string>{"uavcan", "file", "Path"} || versioned->major != 2 ||
        versioned->minor != 0)
    {
        std::cerr << "unexpected type literal target\n";
        return false;
    }

    const std::string          commentText = "# file comment\n"
                                             "uint8 a # field comment\n"
                                             "uint16 B = 1 # constant comment\n"
                                             "@sealed # directive comment\n";
    llvmdsdl::DiagnosticEngine commentDiag;
    llvmdsdl::Lexer            commentLexer("comments.dsdl", commentText);
    auto                       commentTokens     = commentLexer.lex();
    const auto                 commentTokenCount = static_cast<std::size_t>(
        std::count_if(commentTokens.begin(), commentTokens.end(), [](const llvmdsdl::Token& token) {
            return token.kind == llvmdsdl::TokenKind::Comment;
        }));
    if (commentTokenCount < 3)
    {
        std::cerr << "expected lexer to preserve comment tokens\n";
        return false;
    }

    llvmdsdl::Parser commentParser("comments.dsdl", std::move(commentTokens), commentDiag);
    auto             commentDef = commentParser.parseDefinition();
    if (!commentDef)
    {
        std::cerr << "parser failed unexpectedly on comments fixture\n";
        return false;
    }
    if (commentDef->statements.size() != 3)
    {
        std::cerr << "unexpected statement count in comments fixture: " << commentDef->statements.size() << "\n";
        return false;
    }

    const auto* commentField = std::get_if<llvmdsdl::FieldDeclAST>(commentDef->statements.data());
    if (!commentField || commentField->nameLocation.line != 2 || commentField->nameLocation.column != 7)
    {
        std::cerr << "field symbol location was not captured correctly\n";
        return false;
    }
    const auto* commentConstant = std::get_if<llvmdsdl::ConstantDeclAST>(&commentDef->statements[1]);
    if (!commentConstant || commentConstant->nameLocation.line != 3 || commentConstant->nameLocation.column != 8)
    {
        std::cerr << "constant symbol location was not captured correctly\n";
        return false;
    }
    if (commentDef->doc.lines.size() != 1 || commentDef->doc.lines[0].text != "file comment")
    {
        std::cerr << "expected definition doc attachment from leading comment\n";
        return false;
    }
    if (commentField->doc.lines.size() != 1 || commentField->doc.lines[0].text != "field comment")
    {
        std::cerr << "expected trailing field comment attachment\n";
        return false;
    }
    if (commentConstant->doc.lines.size() != 1 || commentConstant->doc.lines[0].text != "constant comment")
    {
        std::cerr << "expected trailing constant comment attachment\n";
        return false;
    }

    const std::string          attachmentText = "# orphaned before blank line\n"
                                                "\n"
                                                "# first field docs line 1\n"
                                                "# first field docs line 2\n"
                                                "uint8 first\n"
                                                "\n"
                                                "# split block old\n"
                                                "\n"
                                                "# second field docs\n"
                                                "uint8 second # second trailing\n";
    llvmdsdl::DiagnosticEngine attachmentDiag;
    llvmdsdl::Lexer            attachmentLexer("attachment.dsdl", attachmentText);
    auto                       attachmentTokens = attachmentLexer.lex();
    llvmdsdl::Parser           attachmentParser("attachment.dsdl", std::move(attachmentTokens), attachmentDiag);
    auto                       attachmentDef = attachmentParser.parseDefinition();
    if (!attachmentDef)
    {
        std::cerr << "parser failed unexpectedly on attachment fixture\n";
        return false;
    }
    if (attachmentDef->statements.size() != 2)
    {
        std::cerr << "unexpected statement count in attachment fixture: " << attachmentDef->statements.size() << "\n";
        return false;
    }
    const auto* firstField  = std::get_if<llvmdsdl::FieldDeclAST>(attachmentDef->statements.data());
    const auto* secondField = std::get_if<llvmdsdl::FieldDeclAST>(&attachmentDef->statements[1]);
    if (!firstField || !secondField)
    {
        std::cerr << "expected field declarations in attachment fixture\n";
        return false;
    }
    if (attachmentDef->doc.lines.size() != 4 || attachmentDef->doc.lines[0].text != "orphaned before blank line" ||
        !attachmentDef->doc.lines[1].text.empty() || attachmentDef->doc.lines[2].text != "first field docs line 1" ||
        attachmentDef->doc.lines[3].text != "first field docs line 2")
    {
        std::cerr << "expected definition docs to preserve comment lines and blank separators\n";
        return false;
    }
    if (!firstField->doc.empty())
    {
        std::cerr << "expected first field docs to be promoted to definition docs only\n";
        return false;
    }
    if (secondField->doc.lines.size() != 4 || secondField->doc.lines[0].text != "split block old" ||
        !secondField->doc.lines[1].text.empty() || secondField->doc.lines[2].text != "second field docs" ||
        secondField->doc.lines[3].text != "second trailing")
    {
        std::cerr << "expected second field docs to preserve split block and trailing comment\n";
        return false;
    }

    const std::string          orphanText = "uint8 kept\n"
                                            "\n"
                                            "# orphan comment\n";
    llvmdsdl::DiagnosticEngine orphanDiag;
    llvmdsdl::Lexer            orphanLexer("orphan.dsdl", orphanText);
    auto                       orphanTokens = orphanLexer.lex();
    llvmdsdl::Parser           orphanParser("orphan.dsdl", std::move(orphanTokens), orphanDiag);
    auto                       orphanDef = orphanParser.parseDefinition();
    if (!orphanDef)
    {
        std::cerr << "parser failed unexpectedly on orphan fixture\n";
        return false;
    }
    if (!orphanDef->doc.empty())
    {
        std::cerr << "unexpected definition docs from orphan trailing comment\n";
        return false;
    }
    const auto* orphanField = std::get_if<llvmdsdl::FieldDeclAST>(orphanDef->statements.data());
    if (!orphanField || !orphanField->doc.empty())
    {
        std::cerr << "unexpected field docs from orphan trailing comment\n";
        return false;
    }

    const std::string          windowsText = "uint8 a # windows comment\r\n@sealed\r\n";
    llvmdsdl::DiagnosticEngine windowsDiag;
    llvmdsdl::Lexer            windowsLexer("windows.dsdl", windowsText);
    auto                       windowsTokens    = windowsLexer.lex();
    auto                       windowsCommentIt = std::ranges::find_if(windowsTokens, [](const llvmdsdl::Token& token) {
        return token.kind == llvmdsdl::TokenKind::Comment;
    });
    if (windowsCommentIt == windowsTokens.end())
    {
        std::cerr << "expected windows fixture comment token\n";
        return false;
    }
    if (!windowsCommentIt->text.empty() && windowsCommentIt->text.back() == '\r')
    {
        std::cerr << "windows fixture comment token unexpectedly contains carriage return\n";
        return false;
    }

    llvmdsdl::Parser windowsParser("windows.dsdl", std::move(windowsTokens), windowsDiag);
    auto             windowsDef = windowsParser.parseDefinition();
    if (!windowsDef || windowsDef->statements.size() != 2)
    {
        std::cerr << "windows fixture parse failed unexpectedly\n";
        return false;
    }
    const auto* windowsField = std::get_if<llvmdsdl::FieldDeclAST>(windowsDef->statements.data());
    if (!windowsField || windowsField->doc.lines.size() != 1 || windowsField->doc.lines[0].text != "windows comment")
    {
        std::cerr << "windows fixture failed to preserve trailing comment text\n";
        return false;
    }

    const std::string          reservedText = "#[vendor(extension)]\nuint8 value\n";
    llvmdsdl::DiagnosticEngine reservedDiag;
    llvmdsdl::Lexer            reservedLexer("reserved.dsdl", reservedText);
    auto                       reservedTokens = reservedLexer.lex();
    llvmdsdl::Parser           reservedParser("reserved.dsdl", std::move(reservedTokens), reservedDiag);
    auto                       reservedDef = reservedParser.parseDefinition();
    if (!reservedDef || reservedDef->statements.size() != 1)
    {
        std::cerr << "reserved-comment fixture parse failed unexpectedly\n";
        return false;
    }
    if (reservedDef->doc.lines.size() != 1 || reservedDef->doc.lines[0].text != "[vendor(extension)]")
    {
        std::cerr << "reserved-comment fixture comment lexing/parsing mismatch\n";
        return false;
    }

    // Regression (DSDL spec v1.0 section 3.2.4): real literals with an omitted
    // integer part (".5") or a bare trailing point ("3.") must parse as reals,
    // while the '.' used for attribute access and version specifiers stays
    // unaffected.
    {
        const auto constValueStr = [](const std::string& body) -> std::string {
            const std::string          text = body + "\n@sealed\n";
            llvmdsdl::DiagnosticEngine diag;
            llvmdsdl::Lexer            lexer("reallit.dsdl", text);
            auto                       tokens = lexer.lex();
            llvmdsdl::Parser           parser("reallit.dsdl", std::move(tokens), diag);
            auto                       def = parser.parseDefinition();
            if (!def || def->statements.empty())
            {
                return "";
            }
            const auto* constant = std::get_if<llvmdsdl::ConstantDeclAST>(def->statements.data());
            return (constant && constant->value) ? constant->value->str() : "";
        };

        const std::pair<std::string, std::string> realCases[] = {
            {"float32 A = .5", "1/2"},    // leading point
            {"float32 B = 3.", "3"},      // trailing point
            {"float32 C = .5e3", "500"},  // leading point with exponent
            {"float32 D = 1.5", "3/2"},   // unchanged single-token form
        };
        for (const auto& realCase : realCases)
        {
            const std::string actual = constValueStr(realCase.first);
            if (actual != realCase.second)
            {
                std::cerr << "real-literal regression: '" << realCase.first << "' expected " << realCase.second
                          << " got '" << actual << "'\n";
                return false;
            }
        }

        // Attribute access must not be consumed by the trailing-point rule.
        const std::string          attrText = "@assert reserved.x == 0\n@sealed\n";
        llvmdsdl::DiagnosticEngine attrDiag;
        llvmdsdl::Lexer            attrLexer("attr.dsdl", attrText);
        auto                       attrTokens = attrLexer.lex();
        llvmdsdl::Parser           attrParser("attr.dsdl", std::move(attrTokens), attrDiag);
        auto                       attrDef = attrParser.parseDefinition();
        if (!attrDef || attrDiag.hasErrors())
        {
            std::cerr << "attribute-access regression: '@assert reserved.x == 0' failed to parse\n";
            return false;
        }
    }

    // Operator precedence & associativity must mirror the spec grammar
    // (productions 64-107). The directive expression is rendered fully
    // parenthesized by ExprAST::str(), so we can assert exact structure.
    {
        const auto exprStr = [](const std::string& expr) -> std::string {
            const std::string          text = "@assert " + expr + "\n@sealed\n";
            llvmdsdl::DiagnosticEngine diag;
            llvmdsdl::Lexer            lexer("prec.dsdl", text);
            auto                       tokens = lexer.lex();
            llvmdsdl::Parser           parser("prec.dsdl", std::move(tokens), diag);
            auto                       def = parser.parseDefinition();
            if (!def || def->statements.empty())
            {
                return "<parse-failed>";
            }
            const auto* directive = std::get_if<llvmdsdl::DirectiveAST>(def->statements.data());
            return (directive && directive->expression) ? directive->expression->str() : "<no-expr>";
        };

        const std::pair<std::string, std::string> precedenceCases[] = {
            // Unary `!` is the loosest unary (binds looser than comparison/arithmetic).
            {"!a == b", "(!(a == b))"},
            {"!a + b", "(!(a + b))"},
            {"!a.b", "(!(a . b))"},
            {"!!a", "(!(!a))"},
            // Unary +/- bind looser than ** but tighter than * (inversion level).
            {"-2 ** 2", "(-(2 ** 2))"},
            {"-a ** b", "(-(a ** b))"},
            {"+2 ** 2", "(+(2 ** 2))"},
            {"-a * b", "((-a) * b)"},
            {"2 * -3", "(2 * (-3))"},
            // Binary precedence ladder and associativity.
            {"1 + 2 * 3", "(1 + (2 * 3))"},
            {"a == b | c", "(a == (b | c))"},
            {"a || b && c", "((a || b) && c)"},  // || and && share a level (left-assoc)
            {"a ** b ** c", "(a ** (b ** c))"},  // ** is right-associative
            {"a - b - c", "((a - b) - c)"},      // additive is left-associative
            {"a.b + c", "((a . b) + c)"},        // attribute binds tightest
        };
        for (const auto& precedenceCase : precedenceCases)
        {
            const std::string actual = exprStr(precedenceCase.first);
            if (actual != precedenceCase.second)
            {
                std::cerr << "operator-precedence regression: '" << precedenceCase.first << "' expected "
                          << precedenceCase.second << " got '" << actual << "'\n";
                return false;
            }
        }
    }

    // Cyphal Specification primitive bit-length ranges must be enforced. Per the
    // "Serializable types" section: signed integers [2,64], unsigned integers
    // [1,64], floats exactly {16,32,64}, void [1,64]. Note `int1` is lexically a
    // signed-integer name but semantically out of range (the single-bit case is
    // `bool`), so it must be rejected. A field whose only type is out of range
    // must fail to parse *and* raise a diagnostic; in-range boundaries must parse.
    {
        const auto fieldParses = [](const std::string& fieldType) -> bool {
            const std::string          text = fieldType + " f\n@sealed\n";
            llvmdsdl::DiagnosticEngine diag;
            llvmdsdl::Lexer            lexer("width.dsdl", text);
            auto                       tokens = lexer.lex();
            llvmdsdl::Parser           parser("width.dsdl", std::move(tokens), diag);
            auto                       def = parser.parseDefinition();
            return def && !def->statements.empty() && !diag.hasErrors();
        };

        const char* const rejectedTypes[] = {
            "uint0",
            "uint65",
            "uint100",
            "int0",
            "int1",
            "int65",
            "int128",
            "float8",
            "float17",
            "float0",
            "float128",
            "void0",
            "void65",
            "void100",
        };
        for (const char* rejected : rejectedTypes)
        {
            if (fieldParses(rejected))
            {
                std::cerr << "primitive-width regression: '" << rejected << "' should have been rejected\n";
                return false;
            }
        }

        const char* const acceptedTypes[] = {
            "uint1",
            "uint64",
            "int2",
            "int64",
            "float16",
            "float32",
            "float64",
            "void1",
            "void64",
        };
        for (const char* accepted : acceptedTypes)
        {
            if (!fieldParses(accepted))
            {
                std::cerr << "primitive-width regression: '" << accepted << "' should have parsed cleanly\n";
                return false;
            }
        }
    }

    return true;
}
