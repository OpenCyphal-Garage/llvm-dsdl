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

/// @brief True when @p text exactly matches `literal_integer_decimal`
/// (production 124): either an all-zero run or a non-zero-led digit run, with
/// single optional underscores between digits. Used to validate version
/// specifier components, which the grammar restricts to decimal integers.
bool isDecimalIntegerToken(const std::string& text)
{
    static const std::regex kDecimal("(?:0(?:_?0)*)+|(?:[1-9](?:_?[0-9])*)");
    return std::regex_match(text, kDecimal);
}

/// @brief True when token @p b immediately follows token @p a with no
/// intervening whitespace (the lexer advances one column per byte, so adjacency
/// means same line and b starts exactly where a ends). Used to enforce the
/// grammar's mandatory/forbidden `_` (whitespace) between specific tokens.
bool tokensAdjacent(const Token& a, const Token& b)
{
    return a.location.line == b.location.line &&
           b.location.column == a.location.column + static_cast<std::uint32_t>(a.text.size());
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

    const std::string majorText = text.substr(0, dot);
    const std::string minorText = text.substr(dot + 1);
    // Version components must be decimal integers (production 35), so reject a
    // real-digit spelling with a leading zero such as "01.0".
    if (!isDecimalIntegerToken(majorText) || !isDecimalIntegerToken(minorText))
    {
        return std::nullopt;
    }
    const auto major = parseIntegerLiteral(majorText);
    const auto minor = parseIntegerLiteral(minorText);
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

        // line = statement? _? comment? (production 2): the optional whitespace
        // `_?` follows the statement, so a statement may not be preceded by
        // leading whitespace (the lexer discards leading whitespace, which shows
        // up as a first-token column greater than 1).
        if (current().location.column != 1)
        {
            diagnostics_.error(current().location, "a statement must begin at the start of the line");
            syncToNextLine();
            continue;
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

        // line = statement? _? comment? (production 2): at most one statement per
        // line. After a statement (and its optional trailing comment), the only
        // valid continuation is an end_of_line or end of file; anything else is a
        // second statement crammed onto the same physical line.
        if (!check(TokenKind::Newline) && !check(TokenKind::Eof) && !check(TokenKind::Comment))
        {
            diagnostics_.error(current().location, "expected end of line after statement");
            syncToNextLine();
        }
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
    // statement_directive_* = "@" identifier (productions 20-21). The directive
    // name is a plain identifier (production 6), which also matches the spellings
    // the lexer reserves as the boolean keywords true/false; those are only
    // special inside expressions, so accept them as directive names too.
    if (!check(TokenKind::Identifier) && !check(TokenKind::True) && !check(TokenKind::False))
    {
        diagnostics_.error(current().location, "expected directive name");
        return std::nullopt;
    }
    // The grammar concatenates "@" and the identifier with no whitespace.
    if (!tokensAdjacent(previous(), current()))
    {
        diagnostics_.error(current().location, "no whitespace allowed between '@' and the directive name");
        return std::nullopt;
    }

    const Token       nameTok = advance();
    const std::string name    = nameTok.text;
    out.rawName               = name;
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
        // statement_directive_with_expression = "@" identifier _ expression
        // (production 20): the `_` between the directive name and the expression
        // is mandatory whitespace.
        if (tokensAdjacent(nameTok, current()))
        {
            diagnostics_.error(current().location, "expected whitespace before directive expression");
            return std::nullopt;
        }
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
        // statement_field/constant = type _ identifier ... (productions 14-15):
        // the `_` is mandatory one-or-more whitespace, so the type and the name
        // may not be directly adjacent (e.g. `uint8[2]name`).
        if (tokensAdjacent(previous(), current()))
        {
            diagnostics_.error(current().location, "expected whitespace between type and name");
            return std::nullopt;
        }
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
        // statement_padding_field = type_void "" (production 16): a void padding
        // field is a bare scalar void with no array specifier.
        if (type->arrayKind != ArrayKind::None)
        {
            diagnostics_.error(type->location, "a void (padding) field cannot be an array");
            return std::nullopt;
        }
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

    // A cast prefix `truncated`/`saturated` only applies when it is immediately
    // followed by another identifier (the primitive type name). When followed by
    // a '.', the word is an ordinary namespace component of a versioned type
    // (e.g. `saturated.Foo.1.0`), not a cast keyword.
    CastMode castMode    = CastMode::Saturated;
    bool     castApplied = false;
    if (check(TokenKind::Identifier) && (current().text == "saturated" || current().text == "truncated") &&
        cursor_ + 1 < tokens_.size() && tokens_[cursor_ + 1].kind == TokenKind::Identifier)
    {
        castMode    = current().text == "truncated" ? CastMode::Truncated : CastMode::Saturated;
        castApplied = true;
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

    // A cast mode may only precede a numeric primitive (type_primitive_name =
    // uint/int/float, productions 44-51); it is invalid on bool/byte/utf8, void,
    // or composite (versioned) types.
    if (castApplied && !std::regex_match(name, std::regex(R"(^u?int[1-9][0-9]*$)")) &&
        !std::regex_match(name, std::regex(R"(^float[1-9][0-9]*$)")))
    {
        if (!silent)
        {
            diagnostics_.error(baseTok.location,
                               "a cast mode ('saturated'/'truncated') may only precede an integer or floating-point type");
        }
        return fail();
    }

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

        // type_versioned (production 34) and type_version_specifier (35) are pure
        // concatenation: no whitespace is permitted between namespace components,
        // around the separating dots, or between the major/minor version digits.
        const auto rejectVersioned = [&](const SourceLocation& location, const char* message) {
            if (!silent)
            {
                diagnostics_.error(location, message);
            }
        };

        bool  seenVersion = false;
        Token lastTok     = baseTok;
        while (check(TokenKind::Dot))
        {
            const Token dotTok = current();
            if (!tokensAdjacent(lastTok, dotTok))
            {
                rejectVersioned(dotTok.location, "no whitespace allowed in a versioned type name");
                return fail();
            }

            if (cursor_ + 3 < tokens_.size() && tokens_[cursor_ + 1].kind == TokenKind::Integer &&
                tokens_[cursor_ + 2].kind == TokenKind::Dot && tokens_[cursor_ + 3].kind == TokenKind::Integer)
            {
                (void) advance();
                const Token majorTok = advance();
                const Token midDot   = current();
                (void) expect(TokenKind::Dot, "expected '.' between major/minor version");
                const Token minorTok   = advance();
                const auto  maybeMajor = parseIntegerLiteral(majorTok.text);
                const auto  maybeMinor = parseIntegerLiteral(minorTok.text);
                // The version is `literal_integer_decimal "." literal_integer_decimal`
                // (production 35): decimal-only, character-adjacent components.
                if (!maybeMajor || !maybeMinor || *maybeMajor < 0 || *maybeMinor < 0 ||
                    !isDecimalIntegerToken(majorTok.text) || !isDecimalIntegerToken(minorTok.text) ||
                    !tokensAdjacent(dotTok, majorTok) || !tokensAdjacent(majorTok, midDot) ||
                    !tokensAdjacent(midDot, minorTok))
                {
                    rejectVersioned(baseTok.location, "invalid version specifier");
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
                const Token realTok = advance();
                const auto  version = parseVersionTokenAsMajorMinor(realTok.text);
                if (!version || !tokensAdjacent(dotTok, realTok))
                {
                    rejectVersioned(baseTok.location, "invalid version specifier");
                    return fail();
                }
                v.major     = version->first;
                v.minor     = version->second;
                seenVersion = true;
                break;
            }

            (void) advance();
            if (!check(TokenKind::Identifier) || !tokensAdjacent(dotTok, current()))
            {
                rejectVersioned(current().location, "expected identifier in versioned type name");
                return fail();
            }
            lastTok = current();
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

// Helper: builds a left-associative chain `next (op next)*` where the operator
// must be one of `ops`. Mirrors the PEG shape `level = lower (_? op2 _? lower)*`.
std::shared_ptr<ExprAST> Parser::parseExpression()
{
    return parseLogical();
}

namespace
{

std::shared_ptr<ExprAST> makeBinaryNode(BinaryOp op, std::shared_ptr<ExprAST> lhs, std::shared_ptr<ExprAST> rhs,
                                        const SourceLocation& location)
{
    auto node      = std::make_shared<ExprAST>();
    node->location = location;
    node->value    = ExprAST::Binary{op, std::move(lhs), std::move(rhs)};
    return node;
}

std::shared_ptr<ExprAST> makeUnaryNode(UnaryOp op, std::shared_ptr<ExprAST> operand, const SourceLocation& location)
{
    auto node      = std::make_shared<ExprAST>();
    node->location = location;
    node->value    = ExprAST::Unary{op, std::move(operand)};
    return node;
}

}  // namespace

// `ex_logical = ex_logical_not (_? op2_log _? ex_logical_not)*` (production 64).
// `||` and `&&` share this level and are left-associative.
std::shared_ptr<ExprAST> Parser::parseLogical()
{
    auto lhs = parseLogicalNot();
    if (!lhs)
    {
        return nullptr;
    }
    while (check(TokenKind::PipePipe) || check(TokenKind::AmpAmp))
    {
        const Token op  = advance();
        auto        rhs = parseLogicalNot();
        if (!rhs)
        {
            diagnostics_.error(op.location, "expected right-hand side expression");
            return lhs;
        }
        lhs = makeBinaryNode(*toBinaryOp(op.kind), lhs, rhs, op.location);
    }
    return lhs;
}

// `ex_logical_not = op1_form_log_not / ex_comparison`, `op1_form_log_not = "!"
// _? ex_logical_not` (productions 65, 74). `!` is the LOOSEST unary operator:
// its operand is a whole `ex_logical_not`, so `!a == b` parses as `!(a == b)`.
std::shared_ptr<ExprAST> Parser::parseLogicalNot()
{
    if (check(TokenKind::Bang))
    {
        const Token op      = advance();
        auto        operand = parseLogicalNot();
        if (!operand)
        {
            diagnostics_.error(op.location, "expected expression after unary operator");
            return nullptr;
        }
        return makeUnaryNode(UnaryOp::LogicalNot, operand, op.location);
    }
    return parseComparison();
}

// `ex_comparison = ex_bitwise (_? op2_cmp _? ex_bitwise)*` (production 66).
std::shared_ptr<ExprAST> Parser::parseComparison()
{
    auto lhs = parseBitwise();
    if (!lhs)
    {
        return nullptr;
    }
    while (check(TokenKind::EqualEqual) || check(TokenKind::BangEqual) || check(TokenKind::LessEqual) ||
           check(TokenKind::GreaterEqual) || check(TokenKind::Less) || check(TokenKind::Greater))
    {
        const Token op  = advance();
        auto        rhs = parseBitwise();
        if (!rhs)
        {
            diagnostics_.error(op.location, "expected right-hand side expression");
            return lhs;
        }
        lhs = makeBinaryNode(*toBinaryOp(op.kind), lhs, rhs, op.location);
    }
    return lhs;
}

// `ex_bitwise = ex_additive (_? op2_bit _? ex_additive)*` (production 67).
std::shared_ptr<ExprAST> Parser::parseBitwise()
{
    auto lhs = parseAdditive();
    if (!lhs)
    {
        return nullptr;
    }
    while (check(TokenKind::Pipe) || check(TokenKind::Caret) || check(TokenKind::Amp))
    {
        const Token op  = advance();
        auto        rhs = parseAdditive();
        if (!rhs)
        {
            diagnostics_.error(op.location, "expected right-hand side expression");
            return lhs;
        }
        lhs = makeBinaryNode(*toBinaryOp(op.kind), lhs, rhs, op.location);
    }
    return lhs;
}

// `ex_additive = ex_multiplicative (_? op2_add _? ex_multiplicative)*` (prod 68).
std::shared_ptr<ExprAST> Parser::parseAdditive()
{
    auto lhs = parseMultiplicative();
    if (!lhs)
    {
        return nullptr;
    }
    while (check(TokenKind::Plus) || check(TokenKind::Minus))
    {
        const Token op  = advance();
        auto        rhs = parseMultiplicative();
        if (!rhs)
        {
            diagnostics_.error(op.location, "expected right-hand side expression");
            return lhs;
        }
        lhs = makeBinaryNode(*toBinaryOp(op.kind), lhs, rhs, op.location);
    }
    return lhs;
}

// `ex_multiplicative = ex_inversion (_? op2_mul _? ex_inversion)*` (prod 69).
std::shared_ptr<ExprAST> Parser::parseMultiplicative()
{
    auto lhs = parseInversion();
    if (!lhs)
    {
        return nullptr;
    }
    while (check(TokenKind::Star) || check(TokenKind::Slash) || check(TokenKind::Percent))
    {
        const Token op  = advance();
        auto        rhs = parseInversion();
        if (!rhs)
        {
            diagnostics_.error(op.location, "expected right-hand side expression");
            return lhs;
        }
        lhs = makeBinaryNode(*toBinaryOp(op.kind), lhs, rhs, op.location);
    }
    return lhs;
}

// `ex_inversion = op1_form_inv_pos / op1_form_inv_neg / ex_exponential`, where
// `op1_form_inv_{pos,neg} = ("+"|"-") _? ex_exponential` (productions 70, 75,
// 76). Unary `+`/`-` bind TIGHTER than `*` but LOOSER than `**`: their operand
// is a whole `ex_exponential`, so `-2 ** 2` parses as `-(2 ** 2)`.
std::shared_ptr<ExprAST> Parser::parseInversion()
{
    if (check(TokenKind::Plus) || check(TokenKind::Minus))
    {
        const Token op      = advance();
        auto        operand = parseExponential();
        if (!operand)
        {
            diagnostics_.error(op.location, "expected expression after unary operator");
            return nullptr;
        }
        const UnaryOp unaryOp = op.kind == TokenKind::Minus ? UnaryOp::Minus : UnaryOp::Plus;
        return makeUnaryNode(unaryOp, operand, op.location);
    }
    return parseExponential();
}

// `ex_exponential = ex_attribute (_? op2_exp _? ex_inversion)?` (production 71).
// A single `**` whose right side recurses through `ex_inversion`, making `**`
// right-associative: `a ** b ** c` parses as `a ** (b ** c)`.
std::shared_ptr<ExprAST> Parser::parseExponential()
{
    auto lhs = parseAttributeAccess();
    if (!lhs)
    {
        return nullptr;
    }
    if (check(TokenKind::StarStar))
    {
        const Token op  = advance();
        auto        rhs = parseInversion();
        if (!rhs)
        {
            diagnostics_.error(op.location, "expected right-hand side expression");
            return lhs;
        }
        return makeBinaryNode(BinaryOp::Pow, lhs, rhs, op.location);
    }
    return lhs;
}

// `ex_attribute = expression_atom (_? op2_attrib _? identifier)*` (production
// 72). The attribute operator `.` is left-associative and its right operand is
// strictly an identifier.
std::shared_ptr<ExprAST> Parser::parseAttributeAccess()
{
    auto lhs = parsePrimary();
    if (!lhs)
    {
        return nullptr;
    }
    while (check(TokenKind::Dot) && cursor_ + 1 < tokens_.size() &&
           tokens_[cursor_ + 1].kind == TokenKind::Identifier)
    {
        const Token op    = advance();  // '.'
        const Token ident = advance();  // identifier
        auto        rhs   = std::make_shared<ExprAST>();
        rhs->location     = ident.location;
        rhs->value        = ExprAST::Identifier{ident.text};
        lhs               = makeBinaryNode(BinaryOp::Attribute, lhs, rhs, op.location);
    }
    return lhs;
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
        (peekKind(1) != TokenKind::Integer || isDecimalIntegerLiteral(tokens_[cursor_ + 1].text)) &&
        tokensAdjacent(current(), tokens_[cursor_ + 1]))
    {
        const SourceLocation loc = current().location;
        (void) advance();  // consume '.'
        const Token fraction = advance();
        const auto  value    = parseRealLiteral("." + fraction.text);
        if (!value)
        {
            diagnostics_.error(loc, "real literal cannot be represented");
        }
        auto node      = std::make_shared<ExprAST>();
        node->location = loc;
        node->value    = value.value_or(Rational(0, 1));
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
        if (check(TokenKind::Dot) && isDecimalIntegerLiteral(intToken.text) && !startsAtom(peekKind(1)) &&
            tokensAdjacent(intToken, current()))
        {
            (void) advance();  // consume '.'
            const auto value = parseRealLiteral(intToken.text + ".");
            if (!value)
            {
                diagnostics_.error(intToken.location, "real literal cannot be represented");
            }
            auto node      = std::make_shared<ExprAST>();
            node->location = intToken.location;
            node->value    = value.value_or(Rational(0, 1));
            return node;
        }

        const auto maybe = parseIntegerLiteral(intToken.text);
        if (!maybe)
        {
            diagnostics_.error(intToken.location, "integer literal '" + intToken.text + "' cannot be represented");
        }
        auto n      = std::make_shared<ExprAST>();
        n->location = intToken.location;
        n->value    = Rational(maybe.value_or(0), 1);
        return n;
    }

    if (match(TokenKind::Real))
    {
        const Token realToken = previous();
        const auto  maybe     = parseRealLiteral(realToken.text);
        if (!maybe)
        {
            diagnostics_.error(realToken.location, "real literal '" + realToken.text + "' cannot be represented");
        }
        auto n      = std::make_shared<ExprAST>();
        n->location = realToken.location;
        n->value    = maybe.value_or(Rational(0, 1));
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
