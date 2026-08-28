//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/SourceWriter.h"

#include <cstddef>
#include <string>
#include <utility>

namespace llvmdsdl
{

IndentPolicy IndentPolicy::tabs()
{
    return IndentPolicy{"\t"};
}

IndentPolicy IndentPolicy::spaces(const unsigned width)
{
    return IndentPolicy{std::string(static_cast<std::size_t>(width), ' ')};
}

std::string IndentPolicy::prefix(const int depth) const
{
    if (depth <= 0)
    {
        return {};
    }
    std::string out;
    out.reserve(unit_.size() * static_cast<std::size_t>(depth));
    for (int level = 0; level < depth; ++level)
    {
        out += unit_;
    }
    return out;
}

void SourceWriter::line(const std::string& text)
{
    out_ << policy_.prefix(depth_) << text << '\n';
}

void SourceWriter::blank()
{
    out_ << '\n';
}

void SourceWriter::indent()
{
    ++depth_;
}

void SourceWriter::dedent()
{
    if (depth_ > 0)
    {
        --depth_;
    }
}

void SourceWriter::open(const std::string& text)
{
    line(text);
    indent();
}

void SourceWriter::close(const std::string& text)
{
    dedent();
    line(text);
}

void SourceWriter::midway(const std::string& text)
{
    dedent();
    line(text);
    indent();
}

}  // namespace llvmdsdl
