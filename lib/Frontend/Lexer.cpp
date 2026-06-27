//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements lexical analysis for DSDL source text.
///
/// The lexer converts source characters into parser tokens while preserving precise source-location diagnostics.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Frontend/Lexer.h"

#include <cctype>
#include <cstdint>
#include <optional>
#include <regex>
#include <string>
#include <utility>

namespace llvmdsdl
{

namespace
{

/// @brief Returns true when @p text exactly matches the grammar for a numeric
/// literal of the indicated kind (DSDL spec v1.0 productions 121-132).
/// @param[in] text Full numeric token spelling (including any 0x/0b/0o prefix).
/// @param[in] isReal True to validate against literal_real, false for an integer.
bool numericLiteralConforms(const std::string& text, bool isReal)
{
    // digits = [0-9](_?[0-9])* — a single optional underscore separates digits.
    static const std::string kDigits   = "[0-9](?:_?[0-9])*";
    static const std::string kPoint    = "(?:(?:" + kDigits + ")?\\." + kDigits + "|" + kDigits + "\\.)";
    static const std::string kExponent = "[eE][+-]?" + kDigits;

    static const std::regex kBinary("0[bB](?:_?[01])+");
    static const std::regex kOctal("0[oO](?:_?[0-7])+");
    static const std::regex kHex("0[xX](?:_?[0-9a-fA-F])+");
    static const std::regex kDecimal("(?:0(?:_?0)*)+|(?:[1-9](?:_?[0-9])*)");
    // literal_real = (point|digits) exponent  |  point
    static const std::regex kReal("(?:(?:" + kPoint + "|" + kDigits + ")" + kExponent + ")|" + kPoint);

    if (isReal)
    {
        return std::regex_match(text, kReal);
    }
    if (text.size() >= 2 && text[0] == '0')
    {
        switch (text[1])
        {
        case 'b':
        case 'B':
            return std::regex_match(text, kBinary);
        case 'o':
        case 'O':
            return std::regex_match(text, kOctal);
        case 'x':
        case 'X':
            return std::regex_match(text, kHex);
        default:
            break;
        }
    }
    return std::regex_match(text, kDecimal);
}

/// @brief Appends a Unicode scalar value to @p out encoded as UTF-8.
void appendUtf8(std::string& out, std::uint32_t codePoint)
{
    if (codePoint <= 0x7Fu)
    {
        out.push_back(static_cast<char>(codePoint));
    }
    else if (codePoint <= 0x7FFu)
    {
        out.push_back(static_cast<char>(0xC0u | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    }
    else if (codePoint <= 0xFFFFu)
    {
        out.push_back(static_cast<char>(0xE0u | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0u | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    }
}

}  // namespace

Lexer::Lexer(std::string file, std::string text)
    : file_(std::move(file))
    , text_(std::move(text))
{
}

const std::vector<LexerError>& Lexer::errors() const
{
    return errors_;
}

void Lexer::recordError(std::uint32_t line, std::uint32_t column, std::string message)
{
    errors_.push_back(LexerError{SourceLocation{file_, line, column}, std::move(message)});
}

void Lexer::decodeUnicodeEscape(std::string& value, int hexDigits, std::uint32_t line, std::uint32_t column)
{
    std::uint32_t codePoint = 0;
    for (int i = 0; i < hexDigits; ++i)
    {
        const char digit = peek();
        int        nibble;
        if (digit >= '0' && digit <= '9')
        {
            nibble = digit - '0';
        }
        else if (digit >= 'a' && digit <= 'f')
        {
            nibble = 10 + (digit - 'a');
        }
        else if (digit >= 'A' && digit <= 'F')
        {
            nibble = 10 + (digit - 'A');
        }
        else
        {
            recordError(line, column, "incomplete unicode escape sequence in string literal");
            return;
        }
        codePoint = (codePoint << 4) | static_cast<std::uint32_t>(nibble);
        (void) advance();
    }

    // Reject values outside the Unicode scalar range (surrogates and > U+10FFFF).
    if (codePoint > 0x10FFFFu || (codePoint >= 0xD800u && codePoint <= 0xDFFFu))
    {
        recordError(line, column, "invalid unicode code point in string literal");
        return;
    }
    appendUtf8(value, codePoint);
}

bool Lexer::isAtEnd() const
{
    return index_ >= text_.size();
}

char Lexer::peek(std::size_t lookahead) const
{
    const std::size_t i = index_ + lookahead;
    return i < text_.size() ? text_[i] : '\0';
}

char Lexer::advance()
{
    if (isAtEnd())
    {
        return '\0';
    }
    const char c = text_[index_++];
    if (c == '\n')
    {
        ++line_;
        column_ = 1;
    }
    else
    {
        ++column_;
    }
    return c;
}

void Lexer::emit(TokenKind kind, std::string text, std::uint32_t line, std::uint32_t column)
{
    tokens_.push_back(Token{kind, std::move(text), SourceLocation{file_, line, column}});
}

void Lexer::lexIdentifierOrKeyword(std::uint32_t line, std::uint32_t column)
{
    std::string text;
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')
    {
        text.push_back(advance());
    }

    if (text == "true")
    {
        emit(TokenKind::True, text, line, column);
        return;
    }
    if (text == "false")
    {
        emit(TokenKind::False, text, line, column);
        return;
    }
    emit(TokenKind::Identifier, text, line, column);
}

void Lexer::lexNumber(std::uint32_t line, std::uint32_t column)
{
    std::string text;
    bool        hasDot = false;
    bool        hasExp = false;

    if (peek() == '0' &&
        (peek(1) == 'x' || peek(1) == 'X' || peek(1) == 'b' || peek(1) == 'B' || peek(1) == 'o' || peek(1) == 'O'))
    {
        text.push_back(advance());
        text.push_back(advance());
        while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')
        {
            text.push_back(advance());
        }
        if (!numericLiteralConforms(text, false))
        {
            recordError(line, column, "malformed integer literal '" + text + "'");
        }
        emit(TokenKind::Integer, text, line, column);
        return;
    }

    // Note (DSDL spec v1.0, section 3.2.4): the grammar's
    // `literal_real_point_notation` makes the integer part optional and allows a
    // bare trailing dot, so `.5` and `3.` are both valid real literals. This
    // leading-dot branch is dead in practice: `lex()` only dispatches into
    // lexNumber() on a leading digit, so `.` is emitted as a standalone Dot token
    // and `.5` lexes as `Dot Integer(5)` (and `3.` as `Integer(3) Dot`). The
    // reference grammar is a scannerless PEG that disambiguates `.` between real
    // literals, `type_version_specifier` (`Type.1.0`), and `op2_attrib`
    // (`x.field`) by context, which this context-free lexer cannot. The two
    // dotted real forms are therefore reassembled in Parser::parsePrimary, in
    // expression-atom position. See Parser.cpp and test/unit/ParserTests.cpp.
    if (peek() == '.')
    {
        hasDot = true;
        text.push_back(advance());
    }

    while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_')
    {
        text.push_back(advance());
    }

    if (!hasDot && peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1))))
    {
        hasDot = true;
        text.push_back(advance());
        while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_')
        {
            text.push_back(advance());
        }
    }

    if (peek() == 'e' || peek() == 'E')
    {
        hasExp = true;
        text.push_back(advance());
        if (peek() == '+' || peek() == '-')
        {
            text.push_back(advance());
        }
        while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_')
        {
            text.push_back(advance());
        }
    }

    const bool isReal = hasDot || hasExp;
    if (!numericLiteralConforms(text, isReal))
    {
        recordError(line, column, "malformed " + std::string(isReal ? "real" : "integer") + " literal '" + text + "'");
    }
    emit(isReal ? TokenKind::Real : TokenKind::Integer, text, line, column);
}

void Lexer::lexString(std::uint32_t line, std::uint32_t column, char quote)
{
    std::string value;
    (void) advance();
    // The string content class is [^'\\] / [^"\\] (productions 136-137): it
    // excludes only the closing quote and backslash, so a raw CR or LF is valid
    // string content and a string literal may span multiple physical lines.
    while (!isAtEnd() && peek() != quote)
    {
        const std::uint32_t charLine   = line_;
        const std::uint32_t charColumn = column_;
        const char          c          = advance();
        if (c != '\\')
        {
            value.push_back(c);
            continue;
        }

        // Escape sequence. DSDL spec v1.0 table 3.4 defines the only valid forms:
        // \\ \r \n \t \' \" \u???? \U????????. A backslash before EOF or a line
        // break (grammar: \\[^\r\n]) and any other escape are errors.
        if (isAtEnd() || peek() == '\n' || peek() == '\r')
        {
            recordError(charLine, charColumn, "unterminated escape sequence in string literal");
            break;
        }
        const char esc = advance();
        switch (esc)
        {
        case 'n':
            value.push_back('\n');
            break;
        case 'r':
            value.push_back('\r');
            break;
        case 't':
            value.push_back('\t');
            break;
        case '\\':
        case '\'':
        case '"':
            value.push_back(esc);
            break;
        case 'u':
            decodeUnicodeEscape(value, 4, charLine, charColumn);
            break;
        case 'U':
            decodeUnicodeEscape(value, 8, charLine, charColumn);
            break;
        default:
            recordError(charLine, charColumn, std::string("invalid escape sequence '\\") + esc + "' in string literal");
            value.push_back(esc);
            break;
        }
    }
    if (peek() == quote)
    {
        (void) advance();
    }
    else
    {
        recordError(line, column, "unterminated string literal");
    }
    emit(TokenKind::String, value, line, column);
}

std::vector<Token> Lexer::lex()
{
    while (!isAtEnd())
    {
        const std::uint32_t tokLine = line_;
        const std::uint32_t tokCol  = column_;
        const char          c       = peek();

        if (c == '\r')
        {
            // end_of_line = ~r"\r?\n" (production 4): a CR is only valid as the
            // optional prefix of a line feed. Consume `\r\n` as one line break
            // (the `\n` case below emits the Newline); a lone CR is invalid.
            if (peek(1) == '\n')
            {
                (void) advance();
                continue;
            }
            recordError(tokLine, tokCol, "stray carriage return (line endings must be '\\n' or '\\r\\n')");
            (void) advance();
            continue;
        }
        if (c == '\n')
        {
            (void) advance();
            emit(TokenKind::Newline, "\\n", tokLine, tokCol);
            continue;
        }
        if (c == ' ' || c == '\t')
        {
            (void) advance();
            continue;
        }
        if (c == '#')
        {
            std::string text;
            while (!isAtEnd() && peek() != '\n' && peek() != '\r')
            {
                text.push_back(advance());
            }
            emit(TokenKind::Comment, std::move(text), tokLine, tokCol);
            continue;
        }

        if (c == '-' && peek(1) == '-' && peek(2) == '-')
        {
            (void) advance();
            (void) advance();
            (void) advance();
            while (peek() == '-')
            {
                (void) advance();
            }
            emit(TokenKind::ServiceResponseMarker, "---", tokLine, tokCol);
            continue;
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
        {
            lexIdentifierOrKeyword(tokLine, tokCol);
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            lexNumber(tokLine, tokCol);
            continue;
        }

        if (c == '\'' || c == '"')
        {
            lexString(tokLine, tokCol, c);
            continue;
        }

        const char n = peek(1);
        if (c == '<' && n == '=')
        {
            (void) advance();
            (void) advance();
            emit(TokenKind::LessEqual, "<=", tokLine, tokCol);
            continue;
        }
        if (c == '>' && n == '=')
        {
            (void) advance();
            (void) advance();
            emit(TokenKind::GreaterEqual, ">=", tokLine, tokCol);
            continue;
        }
        if (c == '=' && n == '=')
        {
            (void) advance();
            (void) advance();
            emit(TokenKind::EqualEqual, "==", tokLine, tokCol);
            continue;
        }
        if (c == '!' && n == '=')
        {
            (void) advance();
            (void) advance();
            emit(TokenKind::BangEqual, "!=", tokLine, tokCol);
            continue;
        }
        if (c == '|' && n == '|')
        {
            (void) advance();
            (void) advance();
            emit(TokenKind::PipePipe, "||", tokLine, tokCol);
            continue;
        }
        if (c == '&' && n == '&')
        {
            (void) advance();
            (void) advance();
            emit(TokenKind::AmpAmp, "&&", tokLine, tokCol);
            continue;
        }
        if (c == '*' && n == '*')
        {
            (void) advance();
            (void) advance();
            emit(TokenKind::StarStar, "**", tokLine, tokCol);
            continue;
        }

        const std::optional<TokenKind> kind = [&]() -> std::optional<TokenKind> {
            switch (c)
            {
            case '@':
                return TokenKind::At;
            case '(':
                return TokenKind::LParen;
            case ')':
                return TokenKind::RParen;
            case '[':
                return TokenKind::LBracket;
            case ']':
                return TokenKind::RBracket;
            case '{':
                return TokenKind::LBrace;
            case '}':
                return TokenKind::RBrace;
            case ',':
                return TokenKind::Comma;
            case '.':
                return TokenKind::Dot;
            case '=':
                return TokenKind::Equal;
            case '+':
                return TokenKind::Plus;
            case '-':
                return TokenKind::Minus;
            case '*':
                return TokenKind::Star;
            case '/':
                return TokenKind::Slash;
            case '%':
                return TokenKind::Percent;
            case '|':
                return TokenKind::Pipe;
            case '^':
                return TokenKind::Caret;
            case '&':
                return TokenKind::Amp;
            case '!':
                return TokenKind::Bang;
            case '<':
                return TokenKind::Less;
            case '>':
                return TokenKind::Greater;
            default:
                return std::nullopt;
            }
        }();

        (void) advance();
        if (kind)
        {
            emit(*kind, std::string(1, c), tokLine, tokCol);
        }
        else
        {
            // A byte that begins no DSDL token. The grammar is ASCII-only
            // (identifier = ~r"[a-zA-Z_]..."), so non-ASCII bytes and stray
            // ASCII punctuation (?, $, \, `, ~, etc.) are lexical errors rather
            // than single-character identifiers.
            static const char* const kHex = "0123456789ABCDEF";
            const unsigned char      byte = static_cast<unsigned char>(c);
            std::string              desc;
            if (byte >= 0x20 && byte < 0x7F)
            {
                desc = std::string("'") + c + "'";
            }
            else
            {
                desc = std::string("0x") + kHex[byte >> 4] + kHex[byte & 0x0F];
            }
            recordError(tokLine, tokCol, "unexpected character " + desc + " in source");
        }
    }

    emit(TokenKind::Eof, "", line_, column_);
    return tokens_;
}

}  // namespace llvmdsdl
