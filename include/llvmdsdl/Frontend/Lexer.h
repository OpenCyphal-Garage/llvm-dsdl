//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Token and lexer declarations for transforming DSDL text into token streams.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_FRONTEND_LEXER_H
#define LLVMDSDL_FRONTEND_LEXER_H

#include "llvmdsdl/Frontend/SourceLocation.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace llvmdsdl
{

/// @file
/// @brief Tokenization interfaces for DSDL source text.

/// @brief Token categories recognized by the lexer.
enum class TokenKind
{

    /// @brief End-of-file sentinel.
    Eof,

    /// @brief Line break token preserving statement boundaries.
    Newline,

    /// @brief End-of-line comment token beginning with `#`.
    Comment,

    /// @brief Identifier token.
    Identifier,

    /// @brief Integer literal token.
    Integer,

    /// @brief Real literal token.
    Real,

    /// @brief String literal token.
    String,

    /// @brief `true` keyword token.
    True,

    /// @brief `false` keyword token.
    False,

    /// @brief `@` directive introducer.
    At,

    /// @brief `---` marker separating service request/response.
    ServiceResponseMarker,

    /// @brief `(` token.
    LParen,

    /// @brief `)` token.
    RParen,

    /// @brief `[` token.
    LBracket,

    /// @brief `]` token.
    RBracket,

    /// @brief `{` token.
    LBrace,

    /// @brief `}` token.
    RBrace,

    /// @brief `,` token.
    Comma,

    /// @brief `.` token.
    Dot,

    /// @brief `=` token.
    Equal,

    /// @brief `+` token.
    Plus,

    /// @brief `-` token.
    Minus,

    /// @brief `*` token.
    Star,

    /// @brief `/` token.
    Slash,

    /// @brief `%` token.
    Percent,

    /// @brief `|` token.
    Pipe,

    /// @brief `^` token.
    Caret,

    /// @brief `&` token.
    Amp,

    /// @brief `!` token.
    Bang,

    /// @brief `<` token.
    Less,

    /// @brief `>` token.
    Greater,

    /// @brief `<=` token.
    LessEqual,

    /// @brief `>=` token.
    GreaterEqual,

    /// @brief `==` token.
    EqualEqual,

    /// @brief `!=` token.
    BangEqual,

    /// @brief `||` token.
    PipePipe,

    /// @brief `&&` token.
    AmpAmp,

    /// @brief `**` token.
    StarStar,
};

/// @brief Single lexical token emitted by @ref Lexer.
struct Token
{
    /// @brief Token category.
    TokenKind kind{TokenKind::Eof};

    /// @brief Original token spelling.
    std::string text;

    /// @brief Start location of the token.
    SourceLocation location;
};

/// @brief Lexical error recovered during tokenization (e.g. a malformed string
/// escape). The lexer is otherwise permissive; callers drain @ref Lexer::errors
/// into their diagnostic engine to surface these to the user.
struct LexerError
{
    /// @brief Location of the offending construct.
    SourceLocation location;

    /// @brief Human-readable description.
    std::string message;
};

/// @brief Converts DSDL source text into a token stream.
class Lexer final
{
public:
    /// @brief Constructs a lexer for one source file.
    /// @param[in] file Logical file name used in token locations.
    /// @param[in] text Full source text to tokenize.
    Lexer(std::string file, std::string text);

    /// @brief Tokenizes the input source.
    /// @return Token sequence terminated by @ref TokenKind::Eof.
    [[nodiscard]] std::vector<Token> lex();

    /// @brief Lexical errors discovered during the most recent @ref lex call.
    /// @return Recorded errors, in source order.
    [[nodiscard]] const std::vector<LexerError>& errors() const;

private:
    /// @brief Returns true when all input characters are consumed.
    [[nodiscard]] bool isAtEnd() const;

    /// @brief Peeks at the current or lookahead character without consuming it.
    /// @param[in] lookahead Character lookahead distance.
    /// @return Character at the requested lookahead position.
    [[nodiscard]] char peek(std::size_t lookahead = 0) const;

    /// @brief Consumes and returns the next character.
    char advance();

    /// @brief Emits one token into the output stream.
    /// @param[in] kind Token kind.
    /// @param[in] text Token spelling.
    /// @param[in] line Token start line.
    /// @param[in] column Token start column.
    void emit(TokenKind kind, std::string text, std::uint32_t line, std::uint32_t column);

    /// @brief Lexes an identifier or keyword token.
    void lexIdentifierOrKeyword(std::uint32_t line, std::uint32_t column);

    /// @brief Lexes an integer or real literal token.
    void lexNumber(std::uint32_t line, std::uint32_t column);

    /// @brief Lexes a quoted string literal token.
    void lexString(std::uint32_t line, std::uint32_t column, char quote);

    /// @brief Decodes a `\u????` / `\U????????` escape into @p value as UTF-8.
    /// @param[in,out] value Destination string receiving the decoded bytes.
    /// @param[in] hexDigits Number of hexadecimal digits to consume (4 or 8).
    /// @param[in] line Source line of the escape, for diagnostics.
    /// @param[in] column Source column of the escape, for diagnostics.
    void decodeUnicodeEscape(std::string& value, int hexDigits, std::uint32_t line, std::uint32_t column);

    /// @brief Records a lexical error.
    void recordError(std::uint32_t line, std::uint32_t column, std::string message);

    /// @brief Logical file name for token locations.
    std::string file_;

    /// @brief Full source text.
    std::string text_;

    /// @brief Current byte offset.
    std::size_t index_{0};

    /// @brief Current 1-based source line.
    std::uint32_t line_{1};

    /// @brief Current 1-based source column.
    std::uint32_t column_{1};

    /// @brief Output token buffer.
    std::vector<Token> tokens_;

    /// @brief Lexical errors recorded during tokenization.
    std::vector<LexerError> errors_;
};

}  // namespace llvmdsdl

#endif  // LLVMDSDL_FRONTEND_LEXER_H
