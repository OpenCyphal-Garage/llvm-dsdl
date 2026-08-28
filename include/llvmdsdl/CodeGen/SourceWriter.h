//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Line-oriented source writer that owns block depth.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_SOURCE_WRITER_H
#define LLVMDSDL_CODEGEN_SOURCE_WRITER_H

#include <sstream>
#include <string>

namespace llvmdsdl
{

/// @brief The indentation unit of one target language.
class IndentPolicy final
{
public:
    /// @brief One horizontal tab per level, as gofmt requires.
    /// @return The policy.
    static IndentPolicy tabs();

    /// @brief @p width spaces per level.
    /// @param[in] width Spaces per level.
    /// @return The policy.
    static IndentPolicy spaces(unsigned width);

    /// @brief Renders the leading whitespace for @p depth levels.
    /// @param[in] depth Block depth; values below zero render as column zero.
    /// @return The prefix.
    std::string prefix(int depth) const;

private:
    explicit IndentPolicy(std::string unit)
        : unit_(std::move(unit))
    {
    }

    std::string unit_;
};

/// @brief Writes generated source one line at a time, tracking block depth itself.
///
/// Callers name blocks, never columns: @ref open and @ref close move the depth and
/// @ref line emits at whatever depth the writer currently holds. Because no emission
/// site can state a column, a body cannot be written at its enclosing block's depth.
///
/// @ref open and @ref close are separate calls rather than a scope guard because the
/// backends open and close a block from different methods -- an element loop begins in
/// one @c FieldStepSpelling method and ends in another.
class SourceWriter final
{
public:
    /// @brief Binds a writer to @p out.
    /// @param[in,out] out Destination stream, which must outlive the writer.
    /// @param[in] policy Indentation unit for the target language.
    SourceWriter(std::ostringstream& out, IndentPolicy policy)
        : out_(out)
        , policy_(std::move(policy))
    {
    }

    /// @brief Emits @p text at the current depth.
    /// @param[in] text Line content, without leading whitespace or a line terminator.
    void line(const std::string& text);

    /// @brief Emits an empty line, with no indentation.
    void blank();

    /// @brief Emits @p text at the current depth, then descends one level.
    /// @param[in] text The block-opening line.
    void open(const std::string& text);

    /// @brief Ascends one level, then emits @p text at the resulting depth.
    /// @param[in] text The block-closing line.
    void close(const std::string& text);

    /// @brief Descends one level without emitting anything.
    ///
    /// For blocks a language delimits by keyword rather than by a token of their
    /// own -- a Go @c case arm, a Python suite -- where @ref open and @ref close
    /// have no line to carry.
    void indent();

    /// @brief Ascends one level without emitting anything.
    void dedent();

    /// @brief The current block depth.
    /// @return Depth in levels, zero at file scope.
    int depth() const
    {
        return depth_;
    }

private:
    std::ostringstream& out_;
    IndentPolicy        policy_;
    int                 depth_{0};
};

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_SOURCE_WRITER_H
