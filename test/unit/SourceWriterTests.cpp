//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <sstream>
#include <string>

#include "llvmdsdl/CodeGen/SourceWriter.h"

#include "UnitTests.h"

namespace
{

/// @brief Whether @p out holds @p expected, saying which case it was where it does not.
bool holds(const std::ostringstream& out, const std::string& expected, const char* const what)
{
    if (out.str() == expected)
    {
        return true;
    }
    std::cerr << "source writer, " << what << ": wrote\n" << out.str() << "expected\n" << expected;
    return false;
}

}  // namespace

bool runSourceWriterTests()
{
    bool ok = true;

    // One empty line between two units, none ahead of the first and none after the last.
    {
        std::ostringstream     out;
        llvmdsdl::SourceWriter w(out, llvmdsdl::IndentPolicy::spaces(2));
        w.separate();
        w.line("a");
        w.separate();
        w.open("b {");
        w.line("c");
        w.close("}");
        w.separate();
        ok = holds(out, "a\n\nb {\n  c\n}\n", "units") && ok;
    }

    // A unit that writes nothing adds no empty line, and several separations make one.
    {
        std::ostringstream     out;
        llvmdsdl::SourceWriter w(out, llvmdsdl::IndentPolicy::spaces(2));
        w.line("a");
        w.separate();
        w.separate();
        w.separate();
        w.line("b");
        ok = holds(out, "a\n\nb\n", "empty units") && ok;
    }

    // A separation after an empty line the writer was asked for adds none of its own.
    {
        std::ostringstream     out;
        llvmdsdl::SourceWriter w(out, llvmdsdl::IndentPolicy::spaces(2));
        w.line("a");
        w.blank();
        w.separate();
        w.line("b");
        ok = holds(out, "a\n\nb\n", "after an empty line") && ok;
    }

    return ok;
}
