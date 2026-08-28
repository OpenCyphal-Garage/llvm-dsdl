//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Tests for command-line path normalization.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Support/CliPath.h"

#include <filesystem>
#include <iostream>
#include <string>

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Path.h"

#include "UnitTests.h"

namespace
{

struct TestContext final
{
    bool ok{true};

    void expectEq(const std::string& actual, const std::string& expected, const std::string& what)
    {
        if (actual != expected)
        {
            ok = false;
            std::cerr << "CliPath test failed: " << what << "\n  expected: " << expected << "\n  actual:   " << actual
                      << '\n';
        }
    }
};

/// @brief The value of an @ref llvm::Expected, or the empty string once its error is reported.
std::string value(TestContext& t, llvm::Expected<std::string> result, const std::string& what)
{
    if (!result)
    {
        t.ok = false;
        std::cerr << "CliPath test failed: " << what << "\n  error: " << llvm::toString(result.takeError()) << '\n';
        return {};
    }
    return *result;
}

/// @brief The home directory the expansion under test resolves `~` to.
std::string homeDirectory()
{
    llvm::SmallString<128> home;
    if (!llvm::sys::path::home_directory(home))
    {
        return {};
    }
    return home.str().str();
}

std::string under(const std::string& root, const std::string& relative)
{
    return (std::filesystem::path(root) / relative).lexically_normal().string();
}

void testOutputPathsAreRootedUnderTheOutputDirectory(TestContext& t)
{
    const std::string out = "out";

    t.expectEq(value(t, llvmdsdl::normalizeCliOutputPath("filename.txt", out), "bare file name"),
               under(out, "filename.txt"),
               "a bare file name lands in the output directory");
    t.expectEq(value(t, llvmdsdl::normalizeCliOutputPath("path/to/filename.txt", out), "relative path"),
               under(out, "path/to/filename.txt"),
               "a relative path keeps its shape under the output directory");
    t.expectEq(value(t, llvmdsdl::normalizeCliOutputPath("path/../to/filename.txt", out), "relative path with .."),
               under(out, "to/filename.txt"),
               "'..' is folded after rooting");
    t.expectEq(value(t, llvmdsdl::normalizeCliOutputPath("./filename.txt", out), "relative path with ."),
               under(out, "filename.txt"),
               "'.' is folded after rooting");

    // The escape hatch: an output that belongs beside the output directory rather than inside it.
    t.expectEq(value(t, llvmdsdl::normalizeCliOutputPath("../beside.txt", out), "relative path leaving the root"),
               "beside.txt",
               "a relative path may fold its way back out of the output directory");

    const auto absolute = std::filesystem::path("/absolute/path/to/filename.txt").lexically_normal().string();
    t.expectEq(value(t, llvmdsdl::normalizeCliOutputPath(absolute, out), "absolute path"),
               absolute,
               "an absolute path is taken as given");

    t.expectEq(value(t, llvmdsdl::normalizeCliOutputPath("filename.txt", ""), "no output directory"),
               "filename.txt",
               "an empty output directory leaves a relative path relative");
    t.expectEq(value(t, llvmdsdl::normalizeCliOutputPath("", out), "empty argument"),
               "",
               "an empty argument stays empty");

    // A multi-segment output directory is joined whole, not just its last component.
    t.expectEq(value(t, llvmdsdl::normalizeCliOutputPath("names.json", "build/./gen"), "nested output directory"),
               under("build/gen", "names.json"),
               "the output directory is itself folded");
}

void testInputPathsStayWhereTheCallerPutThem(TestContext& t)
{
    t.expectEq(value(t, llvmdsdl::normalizeCliPath("path/../to/filename.txt"), "relative input"),
               std::filesystem::path("to/filename.txt").lexically_normal().string(),
               "a relative input path is folded and stays relative");
    t.expectEq(value(t, llvmdsdl::normalizeCliPath("./dsdl_out"), "relative input with ."),
               "dsdl_out",
               "a leading './' is folded away");
    t.expectEq(value(t, llvmdsdl::normalizeCliPath("../sibling"), "relative input leaving the tree"),
               std::filesystem::path("../sibling").lexically_normal().string(),
               "a leading '..' has nothing to fold against and survives");

    const auto absolute = std::filesystem::path("/absolute/path/to/filename.txt").lexically_normal().string();
    t.expectEq(value(t, llvmdsdl::normalizeCliPath(absolute), "absolute input"),
               absolute,
               "an absolute input path is taken as given");
    t.expectEq(value(t, llvmdsdl::normalizeCliPath(""), "empty input"), "", "an empty argument stays empty");
}

void testHomeExpansion(TestContext& t)
{
    const std::string home = homeDirectory();
    if (home.empty())
    {
        // Nothing to hold the expansion against; the error path is the tested behaviour here.
        if (static_cast<bool>(llvmdsdl::expandHomeDirectory("~/path")))
        {
            t.ok = false;
            std::cerr << "CliPath test failed: '~' expanded with no home directory to expand it to\n";
        }
        return;
    }

    t.expectEq(value(t, llvmdsdl::expandHomeDirectory("~"), "bare tilde"), home, "'~' alone is the home directory");
    t.expectEq(value(t, llvmdsdl::expandHomeDirectory("~/path/to/filename.txt"), "tilde path"),
               under(home, "path/to/filename.txt"),
               "'~/' is replaced by the home directory");
    t.expectEq(value(t, llvmdsdl::normalizeCliPath("~/path/../to/filename.txt"), "tilde path with .."),
               under(home, "to/filename.txt"),
               "a home-relative path is folded after expansion");

    // An expanded '~' is absolute, so the output directory has no claim on it.
    t.expectEq(value(t, llvmdsdl::normalizeCliOutputPath("~/names.json", "out"), "tilde output path"),
               under(home, "names.json"),
               "a home-relative output path ignores the output directory");

    // `~name` addresses another user's home. Left as typed, it is an ordinary relative path.
    t.expectEq(value(t, llvmdsdl::expandHomeDirectory("~other/path"), "other user's home"),
               "~other/path",
               "'~name' is left as typed");
    t.expectEq(value(t, llvmdsdl::normalizeCliPath("dsdl~out/file.txt"), "tilde inside a name"),
               std::filesystem::path("dsdl~out/file.txt").lexically_normal().string(),
               "a '~' that does not lead the argument is an ordinary character");
}

}  // namespace

bool runCliPathTests()
{
    TestContext t;
    testOutputPathsAreRootedUnderTheOutputDirectory(t);
    testInputPathsStayWhereTheCallerPutThem(t);
    testHomeExpansion(t);
    return t.ok;
}
