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

#include <cstddef>
#include <functional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

/// @brief How a language breaks a line longer than its output keeps a line to.
struct LineBreaking final
{
    /// @brief One line of a broken line: its text, and the levels of indentation it takes beyond
    ///        the broken line's.
    struct Piece final
    {
        int         level{};
        std::string text;
    };

    /// @brief The length a line is kept to, its indentation included.
    std::size_t length{};

    /// @brief The lines a line breaks into, given its text, the column it starts at and the columns
    ///        one level of indentation takes; none where the language has no break for it.
    std::function<std::vector<Piece>(const std::string& text, std::size_t column, std::size_t unit)> pieces;
};

/// @brief Writes generated source one line at a time, tracking block depth itself.
///
/// Callers name blocks, never columns: @ref open and @ref close move the depth and
/// @ref line emits at whatever depth the writer currently holds. Indentation follows
/// the block structure, so a body sits one level inside the line that opened it.
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
    /// @param[in] breaking How the language breaks a line longer than it keeps one to, which must
    ///            outlive the writer; null where it keeps no length.
    SourceWriter(std::ostringstream& out, IndentPolicy policy, const LineBreaking* breaking = nullptr)
        : out_(out)
        , policy_(std::move(policy))
        , breaking_(breaking)
    {
    }

    /// @brief Emits @p text at the current depth.
    /// @param[in] text Line content, without leading whitespace or a line terminator.
    void line(const std::string& text);

    /// @brief Emits an empty line, with no indentation.
    void blank();

    /// @brief Begins a new unit: the next line is preceded by an empty one, unless it is the first
    ///        line written or already follows an empty one.
    ///
    /// Separation takes effect only where a line follows, so a unit that writes nothing adds no
    /// empty line and none follows the last.
    void separate();

    /// @brief Emits @p text at the current depth, then descends one level.
    /// @param[in] text The block-opening line.
    void open(const std::string& text);

    /// @brief Ascends one level, then emits @p text at the resulting depth. A block's last unit is
    ///        followed by its closing line directly, so a separation still pending is dropped.
    /// @param[in] text The block-closing line.
    void close(const std::string& text);

    /// @brief Emits @p text one level out, then descends again.
    ///
    /// For the line that ends one arm of a construct and begins the next, which is at
    /// the depth of neither: a preprocessor @c \#elif, a @c "} else {".
    /// @param[in] text The arm-separating line.
    void midway(const std::string& text);

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
    /// @brief Writes the empty line a separation asked for, where one is due.
    void begin();

    std::ostringstream&       out_;
    IndentPolicy              policy_;
    const LineBreaking* const breaking_;
    int                       depth_{0};
    /// @brief Whether a line has been written.
    bool written_{false};
    /// @brief Whether the last line written was empty.
    bool afterBlank_{false};
    /// @brief Whether the next line begins a unit.
    bool separating_{false};
};

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_SOURCE_WRITER_H
