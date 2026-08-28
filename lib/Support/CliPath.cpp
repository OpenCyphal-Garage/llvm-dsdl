//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Normalization of the filesystem paths a command line carries.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Support/CliPath.h"

#include <filesystem>
#include <string>

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Path.h"

namespace llvmdsdl
{
namespace
{

std::string foldPath(const std::filesystem::path& path)
{
    return path.lexically_normal().string();
}

}  // namespace

llvm::Expected<std::string> expandHomeDirectory(const llvm::StringRef argument)
{
    if (!argument.starts_with('~'))
    {
        return argument.str();
    }
    const llvm::StringRef tail = argument.drop_front();
    if (!tail.empty() && !llvm::sys::path::is_separator(tail.front()))
    {
        return argument.str();
    }

    llvm::SmallString<128> home;
    if (!llvm::sys::path::home_directory(home))
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "cannot expand '~' in '%s': this platform names no home directory",
                                       argument.str().c_str());
    }

    std::filesystem::path expanded(home.c_str());
    if (!tail.empty())
    {
        expanded /= std::filesystem::path(tail.drop_front().str());
    }
    return expanded.string();
}

llvm::Expected<std::string> normalizeCliPath(const llvm::StringRef argument)
{
    if (argument.empty())
    {
        return std::string{};
    }
    auto expanded = expandHomeDirectory(argument);
    if (!expanded)
    {
        return expanded.takeError();
    }
    return foldPath(std::filesystem::path(*expanded));
}

llvm::Expected<std::string> normalizeCliOutputPath(const llvm::StringRef argument,
                                                   const llvm::StringRef outputDirectory)
{
    if (argument.empty())
    {
        return std::string{};
    }
    auto expanded = expandHomeDirectory(argument);
    if (!expanded)
    {
        return expanded.takeError();
    }

    const std::filesystem::path path(*expanded);
    if (path.is_absolute() || outputDirectory.empty())
    {
        return foldPath(path);
    }
    return foldPath(std::filesystem::path(outputDirectory.str()) / path);
}

}  // namespace llvmdsdl
