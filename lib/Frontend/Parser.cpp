//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements recursive-descent parsing for the DSDL frontend.
///
/// The parser constructs AST declarations and expressions while enforcing grammar rules and version constraints.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Frontend/Parser.h"

#include "llvmdsdl/Frontend/Discovery.h"
#include "llvmdsdl/Frontend/Lexer.h"
#include "llvmdsdl/Frontend/SourceLocation.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Support/Rational.h"

#include "llvm/Support/Error.h"

#include <cctype>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <regex>
#include <utility>
#include <variant>

namespace llvmdsdl
{
namespace
{

std::int64_t pow10i(int n)
{
    std::int64_t out = 1;
    for (int i = 0; i < n; ++i)
    {
        if (out > std::numeric_limits<std::int64_t>::max() / 10)
        {
            return std::numeric_limits<std::int64_t>::max();
        }
        out *= 10;
    }
    return out;
}

std::string removeUnderscores(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        if (c != '_')
        {
            out.push_back(c);
        }
    }
    return out;
}

std::optional<std::int64_t> parseIntegerLiteral(const std::string& in)
{
    std::string s     = removeUnderscores(in);
    int         base  = 10;
    std::size_t start = 0;

    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        base  = 16;
        start = 2;
    }
    else if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B'))
    {
        base  = 2;
        start = 2;
    }
    else if (s.size() > 2 && s[0] == '0' && (s[1] == 'o' || s[1] == 'O'))
    {
        base  = 8;
        start = 2;
    }

    std::int64_t out = 0;
    for (std::size_t i = start; i < s.size(); ++i)
    {
        const char c = s[i];
        int        v = -1;
        if (c >= '0' && c <= '9')
        {
            v = c - '0';
        }
        else if (c >= 'a' && c <= 'f')
        {
            v = 10 + (c - 'a');
        }
        else if (c >= 'A' && c <= 'F')
        {
            v = 10 + (c - 'A');
        }
        if (v < 0 || v >= base)
        {
            return std::nullopt;
        }
        if (out > (std::numeric_limits<std::int64_t>::max() - v) / base)
        {
            return std::nullopt;
        }
        out = out * base + v;
    }
    return out;
}

/// @brief True when an Integer token spelling is a plain decimal literal.
/// @details Hexadecimal/binary/octal literals (`0x..`, `0b..`, `0o..`) cannot
/// carry a fractional point, so only decimal spellings may participate in the
/// `.5` / `3.` real-literal reassembly performed by the expression parser.
bool isDecimalIntegerLiteral(const std::string& text)
{
    return !(text.size() >= 2 && text[0] == '0' &&
             (text[1] == 'x' || text[1] == 'X' || text[1] == 'b' || text[1] == 'B' || text[1] == 'o' ||
              text[1] == 'O'));
}

std::optional<Rational> parseRealLiteral(const std::string& in)
{
    std::string s    = removeUnderscores(in);
    const auto  ePos = s.find_first_of("eE");

    std::string significand = s;
    int         exp         = 0;
    if (ePos != std::string::npos)
    {
        significand        = s.substr(0, ePos);
        const auto expPart = s.substr(ePos + 1);
        try
        {
            exp = std::stoi(expPart);
        } catch (...)
        {
            return std::nullopt;
        }
    }

    const auto  dot        = significand.find('.');
    std::string digits     = significand;
    int         fracDigits = 0;
    if (dot != std::string::npos)
    {
        fracDigits = static_cast<int>(significand.size() - dot - 1);
        digits     = significand;
        digits.erase(dot, 1);
    }

    if (digits.empty() || digits == "+" || digits == "-")
    {
        return std::nullopt;
    }

    bool neg = false;
    if (digits[0] == '+' || digits[0] == '-')
    {
        neg = digits[0] == '-';
        digits.erase(digits.begin());
    }

    std::int64_t num = 0;
    for (char c : digits)
    {
        if (!std::isdigit(static_cast<unsigned char>(c)))
        {
            return std::nullopt;
        }
        if (num > (std::numeric_limits<std::int64_t>::max() - (c - '0')) / 10)
        {
            return std::nullopt;
        }
        num = num * 10 + (c - '0');
    }
    if (neg)
    {
        num = -num;
    }

    std::int64_t den = pow10i(fracDigits);
    if (exp > 0)
    {
        const auto p = pow10i(exp);
        if (p == std::numeric_limits<std::int64_t>::max())
        {
            return std::nullopt;
        }
        if (num > 0 && num > std::numeric_limits<std::int64_t>::max() / p)
        {
            return std::nullopt;
        }
        if (num < 0 && num < std::numeric_limits<std::int64_t>::min() / p)
        {
            return std::nullopt;
        }
        num *= p;
    }
    else if (exp < 0)
    {
        const auto p = pow10i(-exp);
        if (p == std::numeric_limits<std::int64_t>::max())
        {
            return std::nullopt;
        }
        if (den > std::numeric_limits<std::int64_t>::max() / p)
        {
            return std::nullopt;
        }
        den *= p;
    }

    return Rational(num, den);
}

std::optional<std::pair<std::uint32_t, std::uint32_t>> parseVersionTokenAsMajorMinor(const std::string& text)
{
    const auto dot = text.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= text.size())
    {
        return std::nullopt;
    }
    if (text.find('.', dot + 1) != std::string::npos)
    {
        return std::nullopt;
    }

    const auto major = parseIntegerLiteral(text.substr(0, dot));
    const auto minor = parseIntegerLiteral(text.substr(dot + 1));
    if (!major || !minor || *major < 0 || *minor < 0)
    {
        return std::nullopt;
    }
    return std::make_pair(static_cast<std::uint32_t>(*major), static_cast<std::uint32_t>(*minor));
}

std::string normalizeCommentTokenText(const std::string& text)
{
    if (text.empty())
    {
        return "";
    }

    std::size_t start = 0U;
    if (text[start] == '#')
    {
        ++start;
    }
    if (start < text.size() && text[start] == ' ')
    {
        ++start;
    }
    return text.substr(start);
}

std::uint32_t statementLine(const StatementAST& statement)
{
    return std::visit([](const auto& node) { return node.location.line; }, statement);
}

void attachDocToStatement(StatementAST& statement, AttachedDoc doc)
{
    std::visit([&](auto& node) { node.doc = std::move(doc); }, statement);
}

void appendDocToStatement(StatementAST& statement, AttachedDoc&& doc)
{
    std::visit(
        [&](auto& node) {
            node.doc.lines.insert(node.doc.lines.end(),
                                  std::make_move_iterator(doc.lines.begin()),
                                  std::make_move_iterator(doc.lines.end()));
        },
        statement);
}

struct CollectedDocTrivia final
{
    AttachedDoc doc;
    std::size_t newlinesBeforeFirstComment{0U};
    std::size_t newlinesAfterLastComment{0U};
    bool        sawComment{false};
};

}  // namespace

Parser::Parser(std::string filePath, std::vector<Token> tokens, DiagnosticEngine& diagnostics)
    : filePath_(std::move(filePath))
    , tokens_(std::move(tokens))
    , diagnostics_(diagnostics)
{
}

const Token& Parser::current() const
{
    return tokens_[cursor_];
}

const Token& Parser::previous() const
{
    return tokens_[cursor_ - 1];
}

bool Parser::isAtEnd() const
{
    return current().kind == TokenKind::Eof;
}

bool Parser::check(TokenKind kind) const
{
    return current().kind == kind;
}

bool Parser::match(TokenKind kind)
{
    if (!check(kind))
    {
        return false;
    }
    (void) advance();
    return true;
}

bool Parser::matchAny(std::initializer_list<TokenKind> kinds)
{
    for (TokenKind k : kinds)
    {
        if (check(k))
        {
            (void) advance();
            return true;
        }
    }
    return false;
}

const Token& Parser::advance()
{
    if (!isAtEnd())
    {
        ++cursor_;
    }
    return previous();
}

bool Parser::expect(TokenKind kind, const std::string& message)
{
    if (check(kind))
    {
        (void) advance();
        return true;
    }
    diagnostics_.error(current().location, message);
    return false;
}

void Parser::syncToNextLine()
{
    while (!isAtEnd() && !check(TokenKind::Newline))
    {
        (void) advance();
    }
    while (match(TokenKind::Newline) || match(TokenKind::Comment))
    {
    }
}

llvm::Expected<DefinitionAST> Parser::parseDefinition()
{
    DefinitionAST def;
    def.location = SourceLocation{filePath_, 1, 1};

    auto collectDocTrivia = [&]() -> CollectedDocTrivia {
        CollectedDocTrivia out;

        while (check(TokenKind::Newline) || check(TokenKind::Comment))
        {
            if (check(TokenKind::Comment))
            {
                const Token token = advance();
                if (out.sawComment && out.newlinesAfterLastComment > 1U)
                {
                    out.doc.lines.push_back(AttachedDocLine{token.location, ""});
                }
                out.doc.lines.push_back(AttachedDocLine{token.location, normalizeCommentTokenText(token.text)});
                out.sawComment               = true;
                out.newlinesAfterLastComment = 0U;
                continue;
            }

            (void) advance();
            if (out.sawComment)
            {
                ++out.newlinesAfterLastComment;
            }
            else
            {
                ++out.newlinesBeforeFirstComment;
            }
        }

        return out;
    };

    auto collectTrailingDoc = [&](const std::uint32_t line) -> AttachedDoc {
        AttachedDoc trailingDoc;
        if (check(TokenKind::Comment) && current().location.line == line)
        {
            const Token token = advance();
            trailingDoc.lines.push_back(AttachedDocLine{token.location, normalizeCommentTokenText(token.text)});
        }
        return trailingDoc;
    };

    while (!isAtEnd())
    {
        CollectedDocTrivia leadingTrivia = collectDocTrivia();
        if (def.statements.empty() && def.doc.empty() && !leadingTrivia.doc.empty())
        {
            def.doc = std::move(leadingTrivia.doc);
        }

        if (!def.statements.empty() && !leadingTrivia.doc.empty() && leadingTrivia.newlinesBeforeFirstComment <= 1U &&
            leadingTrivia.newlinesAfterLastComment > 1U)
        {
            appendDocToStatement(def.statements.back(), std::move(leadingTrivia.doc));
        }

        if (isAtEnd())
        {
            if (!def.statements.empty() && !leadingTrivia.doc.empty() && leadingTrivia.newlinesBeforeFirstComment <= 1U)
            {
                appendDocToStatement(def.statements.back(), std::move(leadingTrivia.doc));
            }
            break;
        }

        auto stmt = parseStatement();
        if (!stmt)
        {
            syncToNextLine();
            continue;
        }

        AttachedDoc trailingDoc = collectTrailingDoc(statementLine(*stmt));
        if (!trailingDoc.empty())
        {
            leadingTrivia.doc.lines.insert(leadingTrivia.doc.lines.end(),
                                           std::make_move_iterator(trailingDoc.lines.begin()),
                                           std::make_move_iterator(trailingDoc.lines.end()));
        }
        attachDocToStatement(*stmt, std::move(leadingTrivia.doc));

        def.statements.push_back(*stmt);
    }

    if (diagnostics_.hasErrors())
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "DSDL parse failed");
    }
    return def;
}

std::optional<StatementAST> Parser::parseStatement()
{
    if (check(TokenKind::At))
    {
        auto d = parseDirective();
        if (d)
        {
            return StatementAST(*d);
        }
        return std::nullopt;
    }
    if (check(TokenKind::ServiceResponseMarker))
    {
        ServiceResponseMarkerAST m;
        m.location = current().location;
        (void) advance();
        return StatementAST(m);
    }
    return parseAttribute();
}

std::optional<DirectiveAST> Parser::parseDirective()
{
    DirectiveAST out;
    out.location = current().location;

    if (!expect(TokenKind::At, "expected '@' to begin directive"))
    {
        return std::nullopt;
    }
    if (!check(TokenKind::Identifier))
    {
        diagnostics_.error(current().location, "expected directive name");
        return std::nullopt;
    }

    const std::string name = advance().text;
    out.rawName            = name;
    if (name == "union")
    {
        out.kind = DirectiveKind::Union;
    }
    else if (name == "extent")
    {
        out.kind = DirectiveKind::Extent;
    }
    else if (name == "sealed")
    {
        out.kind = DirectiveKind::Sealed;
    }
    else if (name == "deprecated")
    {
        out.kind = DirectiveKind::Deprecated;
    }
    else if (name == "assert")
    {
        out.kind = DirectiveKind::Assert;
    }
    else if (name == "print")
    {
        out.kind = DirectiveKind::Print;
    }
    else
    {
        out.kind = DirectiveKind::Unknown;
    }

    if (!check(TokenKind::Newline) && !check(TokenKind::Comment) && !check(TokenKind::Eof))
    {
        out.expression = parseExpression();
    }
    return out;
}

std::optional<StatementAST> Parser::parseAttribute()
{
    auto type = parseTypeExpr(false);
    if (!type)
    {
        diagnostics_.error(current().location, "expected type expression");
        return std::nullopt;
    }

    if (check(TokenKind::Identifier))
    {
        const Token nameTok = advance();
        if (match(TokenKind::Equal))
        {
            auto value = parseExpression();
            if (!value)
            {
                diagnostics_.error(current().location, "expected expression after '=' in constant");
                return std::nullopt;
            }
            ConstantDeclAST c;
            c.location     = nameTok.location;
            c.type         = *type;
            c.name         = nameTok.text;
            c.nameLocation = nameTok.location;
            c.value        = value;
            return StatementAST(c);
        }

        FieldDeclAST f;
        f.location     = nameTok.location;
        f.type         = *type;
        f.name         = nameTok.text;
        f.nameLocation = nameTok.location;
        f.isPadding    = false;
        return StatementAST(f);
    }

    if (type->isVoid())
    {
        FieldDeclAST f;
        f.location     = type->location;
        f.type         = *type;
        f.name         = "";
        f.nameLocation = type->location;
        f.isPadding    = true;
        return StatementAST(f);
    }

    diagnostics_.error(current().location, "expected field name or constant initializer");
    return std::nullopt;
}

std::optional<TypeExprAST> Parser::parseTypeExpr(bool silent)
{
    const std::size_t start = cursor_;

    auto fail = [&]() -> std::optional<TypeExprAST> {
        cursor_ = start;
        return std::nullopt;
    };

    TypeExprAST type;
    type.location = current().location;

    CastMode castMode = CastMode::Saturated;
    if (check(TokenKind::Identifier) && (current().text == "saturated" || current().text == "truncated"))
    {
        castMode = current().text == "truncated" ? CastMode::Truncated : CastMode::Saturated;
        (void) advance();
    }

    if (!check(TokenKind::Identifier))
    {
        return fail();
    }

    const Token        baseTok = advance();
    const std::string& name    = baseTok.text;

    // Safely parses a primitive/void bit-length suffix. The leading regex
    // guarantees the suffix is all digits, so the only failure mode is a value
    // too large for the field: std::stoul would throw std::out_of_range (which,
    // uncaught, aborts the process), and even an in-range unsigned long can
    // overflow the std::uint32_t bit-length field. Both cases are reported as a
    // diagnostic and turned into a parse failure instead of a crash.
    auto parseBitLength = [&](const std::string& digits) -> std::optional<std::uint32_t> {
        try
        {
            const unsigned long value = std::stoul(digits);
            if (value <= std::numeric_limits<std::uint32_t>::max())
            {
                return static_cast<std::uint32_t>(value);
            }
        } catch (...)
        {
        }
        if (!silent)
        {
            diagnostics_.error(baseTok.location, "primitive type bit length is out of range");
        }
        return std::nullopt;
    };

    if (name == "bool" || name == "byte" || name == "utf8" ||
        std::regex_match(name, std::regex(R"(^u?int[1-9][0-9]*$)")) ||
        std::regex_match(name, std::regex(R"(^float[1-9][0-9]*$)")) ||
        std::regex_match(name, std::regex(R"(^void[1-9][0-9]*$)")))
    {
        if (name.rfind("void", 0) == 0)
        {
            VoidTypeExprAST v;
            const auto      bits = parseBitLength(name.substr(4));
            if (!bits)
            {
                return fail();
            }
            v.bitLength = *bits;
            type.scalar = v;
        }
        else
        {
            PrimitiveTypeExprAST p;
            p.castMode = castMode;

            if (name == "bool")
            {
                p.kind      = PrimitiveKind::Bool;
                p.bitLength = 1;
            }
            else if (name == "byte")
            {
                p.kind      = PrimitiveKind::Byte;
                p.castMode  = CastMode::Truncated;
                p.bitLength = 8;
            }
            else if (name == "utf8")
            {
                p.kind      = PrimitiveKind::Utf8;
                p.castMode  = CastMode::Truncated;
                p.bitLength = 8;
            }
            else if (name.rfind("uint", 0) == 0)
            {
                p.kind          = PrimitiveKind::UnsignedInt;
                const auto bits = parseBitLength(name.substr(4));
                if (!bits)
                {
                    return fail();
                }
                p.bitLength = *bits;
            }
            else if (name.rfind("int", 0) == 0)
            {
                p.kind          = PrimitiveKind::SignedInt;
                const auto bits = parseBitLength(name.substr(3));
                if (!bits)
                {
                    return fail();
                }
                p.bitLength = *bits;
            }
            else if (name.rfind("float", 0) == 0)
            {
                p.kind          = PrimitiveKind::Float;
                const auto bits = parseBitLength(name.substr(5));
                if (!bits)
                {
                    return fail();
                }
                p.bitLength = *bits;
            }
            type.scalar = p;
        }
    }
    else
    {
        VersionedTypeExprAST v;
        v.nameComponents.push_back(name);

        bool seenVersion = false;
        while (check(TokenKind::Dot))
        {
            if (cursor_ + 3 < tokens_.size() && tokens_[cursor_ + 1].kind == TokenKind::Integer &&
                tokens_[cursor_ + 2].kind == TokenKind::Dot && tokens_[cursor_ + 3].kind == TokenKind::Integer)
            {
                (void) advance();
                const auto maybeMajor = parseIntegerLiteral(advance().text);
                (void) expect(TokenKind::Dot, "expected '.' between major/minor version");
                const auto maybeMinor = parseIntegerLiteral(advance().text);
                if (!maybeMajor || !maybeMinor || *maybeMajor < 0 || *maybeMinor < 0)
                {
                    if (!silent)
                    {
                        diagnostics_.error(baseTok.location, "invalid version specifier");
                    }
                    return fail();
                }
                v.major     = static_cast<std::uint32_t>(*maybeMajor);
                v.minor     = static_cast<std::uint32_t>(*maybeMinor);
                seenVersion = true;
                break;
            }

            if (cursor_ + 1 < tokens_.size() && tokens_[cursor_ + 1].kind == TokenKind::Real)
            {
                (void) advance();
                const auto version = parseVersionTokenAsMajorMinor(advance().text);
                if (!version)
                {
                    if (!silent)
                    {
                        diagnostics_.error(baseTok.location, "invalid version specifier");
                    }
                    return fail();
                }
                v.major     = version->first;
                v.minor     = version->second;
                seenVersion = true;
                break;
            }

            (void) advance();
            if (!check(TokenKind::Identifier))
            {
                if (!silent)
                {
                    diagnostics_.error(current().location, "expected identifier in versioned type name");
                }
                return fail();
            }
            v.nameComponents.push_back(advance().text);
        }

        if (!seenVersion)
        {
            if (!silent)
            {
                diagnostics_.error(baseTok.location, "expected version suffix '.major.minor' for composite type");
            }
            return fail();
        }

        type.scalar = v;
    }

    if (match(TokenKind::LBracket))
    {
        if (match(TokenKind::LessEqual))
        {
            type.arrayKind = ArrayKind::VariableInclusive;
        }
        else if (match(TokenKind::Less))
        {
            type.arrayKind = ArrayKind::VariableExclusive;
        }
        else
        {
            type.arrayKind = ArrayKind::Fixed;
        }

        type.arrayCapacity = parseExpression();
        if (!expect(TokenKind::RBracket, "expected closing ']' in array type"))
        {
            return fail();
        }
    }

    return type;
}

std::shared_ptr<ExprAST> Parser::parseExpression(int precedence)
{
    auto lhs = parseUnary();
    if (!lhs)
    {
        return nullptr;
    }

    while (true)
    {
        const TokenKind tk     = current().kind;
        const int       opPrec = precedenceOf(tk);
        if (opPrec < precedence)
        {
            break;
        }
        const auto maybeOp = toBinaryOp(tk);
        if (!maybeOp)
        {
            break;
        }
        const SourceLocation loc = current().location;
        (void) advance();

        const int rhsPrec = opPrec + (isRightAssociative(tk) ? 0 : 1);
        auto      rhs     = parseExpression(rhsPrec);
        if (!rhs)
        {
            diagnostics_.error(loc, "expected right-hand side expression");
            return lhs;
        }

        auto node      = std::make_shared<ExprAST>();
        node->location = loc;
        node->value    = ExprAST::Binary{*maybeOp, lhs, rhs};
        lhs            = node;
    }

    return lhs;
}

std::shared_ptr<ExprAST> Parser::parseUnary()
{
    if (matchAny({TokenKind::Plus, TokenKind::Minus, TokenKind::Bang}))
    {
        const Token opTok   = previous();
        auto        operand = parseUnary();
        if (!operand)
        {
            diagnostics_.error(opTok.location, "expected expression after unary operator");
            return nullptr;
        }

        UnaryOp op = UnaryOp::Plus;
        if (opTok.kind == TokenKind::Minus)
        {
            op = UnaryOp::Minus;
        }
        else if (opTok.kind == TokenKind::Bang)
        {
            op = UnaryOp::LogicalNot;
        }

        auto node      = std::make_shared<ExprAST>();
        node->location = opTok.location;
        node->value    = ExprAST::Unary{op, operand};
        return node;
    }
    return parsePrimary();
}

std::shared_ptr<ExprAST> Parser::parsePrimary()
{
    const auto peekKind = [&](std::size_t ahead) -> TokenKind {
        const std::size_t index = cursor_ + ahead;
        return index < tokens_.size() ? tokens_[index].kind : TokenKind::Eof;
    };
    const auto startsAtom = [](TokenKind kind) {
        switch (kind)
        {
        case TokenKind::Integer:
        case TokenKind::Real:
        case TokenKind::Identifier:
        case TokenKind::True:
        case TokenKind::False:
        case TokenKind::String:
        case TokenKind::LParen:
        case TokenKind::LBrace:
            return true;
        default:
            return false;
        }
    };

    // DSDL spec v1.0 section 3.2.4 permits real literals with an omitted integer
    // part (".5") or a bare trailing point ("3."): literal_real_point_notation =
    // (literal_real_digits? literal_real_fraction) / (literal_real_digits ".").
    // The context-free lexer cannot fold these into single Real tokens without
    // breaking the '.' used by attribute access (op2_attrib = "." identifier) and
    // type version specifiers (Type.major.minor), so they are reassembled here,
    // in expression-atom position, where a '.' adjacent to digits is
    // unambiguously fractional. Forms with an explicit integer part (1.5, 1e9,
    // 1.5e-3) already arrive as single Real tokens and are handled below.
    //
    // Leading point: '.' Integer  or  '.' Real   (".5", ".5e3").
    if (check(TokenKind::Dot) && (peekKind(1) == TokenKind::Integer || peekKind(1) == TokenKind::Real) &&
        (peekKind(1) != TokenKind::Integer || isDecimalIntegerLiteral(tokens_[cursor_ + 1].text)))
    {
        const SourceLocation loc = current().location;
        (void) advance();  // consume '.'
        const Token fraction = advance();
        const auto  value    = parseRealLiteral("." + fraction.text);
        auto        node     = std::make_shared<ExprAST>();
        node->location       = loc;
        node->value          = value.value_or(Rational(0, 1));
        return node;
    }

    if (match(TokenKind::True) || match(TokenKind::False))
    {
        auto n      = std::make_shared<ExprAST>();
        n->location = previous().location;
        n->value    = previous().kind == TokenKind::True;
        return n;
    }

    if (match(TokenKind::Integer))
    {
        const Token intToken = previous();

        // Trailing point: Integer '.' that forms a real "3.". The attribute
        // operator is always '.' followed by an identifier, so a '.' here that is
        // not followed by any atom (it precedes an operator, newline, bracket, or
        // EOF) cannot be attribute access and is the fractional point of a real.
        // Restricted to decimal integer spellings; hex/bin/oct cannot take a '.'.
        if (check(TokenKind::Dot) && isDecimalIntegerLiteral(intToken.text) && !startsAtom(peekKind(1)))
        {
            (void) advance();  // consume '.'
            const auto value = parseRealLiteral(intToken.text + ".");
            auto       node  = std::make_shared<ExprAST>();
            node->location   = intToken.location;
            node->value      = value.value_or(Rational(0, 1));
            return node;
        }

        const auto maybe = parseIntegerLiteral(intToken.text);
        auto       n     = std::make_shared<ExprAST>();
        n->location      = intToken.location;
        n->value         = Rational(maybe.value_or(0), 1);
        return n;
    }

    if (match(TokenKind::Real))
    {
        const auto maybe = parseRealLiteral(previous().text);
        auto       n     = std::make_shared<ExprAST>();
        n->location      = previous().location;
        n->value         = maybe.value_or(Rational(0, 1));
        return n;
    }

    if (match(TokenKind::String))
    {
        auto n      = std::make_shared<ExprAST>();
        n->location = previous().location;
        n->value    = previous().text;
        return n;
    }

    if (match(TokenKind::LParen))
    {
        auto e = parseExpression();
        (void) expect(TokenKind::RParen, "expected ')' after expression");
        return e;
    }

    if (match(TokenKind::LBrace))
    {
        return parseSetLiteral(previous().location);
    }

    if (check(TokenKind::Identifier))
    {
        const std::size_t save = cursor_;
        if (auto t = parseTypeExpr(true))
        {
            auto n      = std::make_shared<ExprAST>();
            n->location = t->location;
            n->value    = ExprAST::TypeLiteral{*t};
            return n;
        }
        cursor_ = save;
    }

    if (match(TokenKind::Identifier))
    {
        auto n      = std::make_shared<ExprAST>();
        n->location = previous().location;
        n->value    = ExprAST::Identifier{previous().text};
        return n;
    }

    diagnostics_.error(current().location, "expected expression atom");
    return nullptr;
}

std::shared_ptr<ExprAST> Parser::parseSetLiteral(const SourceLocation& location)
{
    std::vector<std::shared_ptr<ExprAST>> elements;

    if (!check(TokenKind::RBrace))
    {
        while (true)
        {
            auto item = parseExpression();
            if (!item)
            {
                break;
            }
            elements.push_back(item);
            if (!match(TokenKind::Comma))
            {
                break;
            }
        }
    }

    (void) expect(TokenKind::RBrace, "expected '}' to close set literal");

    auto n      = std::make_shared<ExprAST>();
    n->location = location;
    n->value    = ExprAST::SetLiteral{elements};
    return n;
}

int Parser::precedenceOf(TokenKind kind)
{
    switch (kind)
    {
    case TokenKind::Dot:
        return 90;
    case TokenKind::StarStar:
        return 80;
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent:
        return 70;
    case TokenKind::Plus:
    case TokenKind::Minus:
        return 60;
    case TokenKind::Pipe:
    case TokenKind::Caret:
    case TokenKind::Amp:
        return 50;
    case TokenKind::EqualEqual:
    case TokenKind::BangEqual:
    case TokenKind::LessEqual:
    case TokenKind::GreaterEqual:
    case TokenKind::Less:
    case TokenKind::Greater:
        return 40;
    case TokenKind::PipePipe:
    case TokenKind::AmpAmp:
        return 30;
    default:
        return -1;
    }
}

bool Parser::isRightAssociative(TokenKind kind)
{
    return kind == TokenKind::StarStar;
}

std::optional<BinaryOp> Parser::toBinaryOp(TokenKind kind)
{
    switch (kind)
    {
    case TokenKind::Dot:
        return BinaryOp::Attribute;
    case TokenKind::StarStar:
        return BinaryOp::Pow;
    case TokenKind::Star:
        return BinaryOp::Mul;
    case TokenKind::Slash:
        return BinaryOp::Div;
    case TokenKind::Percent:
        return BinaryOp::Mod;
    case TokenKind::Plus:
        return BinaryOp::Add;
    case TokenKind::Minus:
        return BinaryOp::Sub;
    case TokenKind::Pipe:
        return BinaryOp::BitOr;
    case TokenKind::Caret:
        return BinaryOp::BitXor;
    case TokenKind::Amp:
        return BinaryOp::BitAnd;
    case TokenKind::EqualEqual:
        return BinaryOp::Eq;
    case TokenKind::BangEqual:
        return BinaryOp::Ne;
    case TokenKind::LessEqual:
        return BinaryOp::Le;
    case TokenKind::GreaterEqual:
        return BinaryOp::Ge;
    case TokenKind::Less:
        return BinaryOp::Lt;
    case TokenKind::Greater:
        return BinaryOp::Gt;
    case TokenKind::PipePipe:
        return BinaryOp::LogicalOr;
    case TokenKind::AmpAmp:
        return BinaryOp::LogicalAnd;
    default:
        return std::nullopt;
    }
}

llvm::Expected<ASTModule> parseDefinitions(const std::vector<std::string>& rootNamespaceDirs,
                                           const std::vector<std::string>& lookupDirs,
                                           DiagnosticEngine&               diagnostics)
{
    ASTModule module;
    auto      defs = discoverDefinitions(rootNamespaceDirs, lookupDirs, diagnostics);

    for (const auto& def : defs)
    {
        Lexer lexer(def.filePath, def.text);
        auto  tokens = lexer.lex();
        for (const LexerError& lexError : lexer.errors())
        {
            diagnostics.error(lexError.location, lexError.message);
        }
        Parser parser(def.filePath, std::move(tokens), diagnostics);

        auto parsed = parser.parseDefinition();
        if (!parsed)
        {
            llvm::consumeError(parsed.takeError());
            continue;
        }
        module.definitions.push_back(ParsedDefinition{def, *parsed});
    }

    if (diagnostics.hasErrors())
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "parsing failed");
    }
    return module;
}

}  // namespace llvmdsdl
