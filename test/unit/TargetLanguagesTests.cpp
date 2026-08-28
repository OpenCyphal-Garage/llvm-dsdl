//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Holds dsdlc's `--target-language` table against the library's naming languages.
///
/// The tool accepts a wider set than the library knows about: `ast` and `mlir` print an
/// intermediate representation, and `obj` produces object code through the C and C++ backends. What
/// must hold is the containment -- every naming language the library offers has to be reachable
/// from the command line, or a backend exists that nobody can select.
///
/// The rendered forms are checked against the table rather than against fixed text. A test that
/// pinned the help string would fail on every new lane and be repaired by pasting the new string
/// in, proving nothing.
///
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "llvmdsdl/Frontend/Discovery.h"

#include "TargetLanguages.h"
#include "UnitTests.h"

namespace
{

struct TestContext final
{
    bool ok{true};

    void expect(const bool condition, const std::string& what)
    {
        if (!condition)
        {
            ok = false;
            std::cerr << "TargetLanguages test failed: " << what << '\n';
        }
    }
};

}  // namespace

bool runTargetLanguagesTests()
{
    using namespace llvmdsdl::dsdlc;

    TestContext t;

    // Every naming language the library offers must be selectable on the command line.
    for (const auto& [language, name] : llvmdsdl::allOutputLanguages())
    {
        (void) language;
        const std::string spelled(name);
        t.expect(isKnownLanguage(spelled), "library language '" + spelled + "' is accepted by --target-language");
        t.expect(isCodegenLanguage(spelled), "library language '" + spelled + "' is a codegen lane");
    }

    // The table is the only place the set is written down, so nothing in it may repeat.
    std::vector<std::string> names;
    for (const auto& entry : allTargetLanguages())
    {
        names.emplace_back(entry.name);
    }
    std::vector<std::string> sorted = names;
    std::ranges::sort(sorted);
    t.expect(std::ranges::adjacent_find(sorted) == sorted.end(), "the table lists each language once");
    t.expect(!names.empty(), "the table is not empty");

    // The rendered forms are the table, joined -- not a second copy of it.
    std::string expected;
    for (const auto& name : names)
    {
        if (!expected.empty())
        {
            expected += " | ";
        }
        expected += name;
    }
    t.expect(renderTargetLanguages(" | ") == expected, "the help list renders every table entry in order");

    // A dump lane emits no source tree, and an unknown value is nothing at all.
    t.expect(!isCodegenLanguage("ast") && !isCodegenLanguage("mlir"), "the dump lanes are not codegen");
    t.expect(!emitsSourceTree("obj"), "obj publishes artefacts rather than a source tree");
    t.expect(!isKnownLanguage("definitely-not-a-language"), "an unknown value is refused");
    t.expect(findTargetLanguage("definitely-not-a-language") == nullptr, "an unknown value has no row");

    return t.ok;
}
