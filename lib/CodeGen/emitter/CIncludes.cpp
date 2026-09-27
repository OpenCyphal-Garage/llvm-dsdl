//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/emitter/CIncludes.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/ErrorHandling.h>

#include "llvmdsdl/CodeGen/ImportSet.h"

namespace llvmdsdl::emitter::c
{
namespace
{

/// @brief The header each standard name the generated C writes is declared in.
const llvm::StringMap<std::string>& standardHeaders()
{
    static const llvm::StringMap<std::string> headers{
        {"NULL", "<stddef.h>"},
        {"bool", "<stdbool.h>"},
        {"false", "<stdbool.h>"},
        {"int16_t", "<stdint.h>"},
        {"int32_t", "<stdint.h>"},
        {"int64_t", "<stdint.h>"},
        {"int8_t", "<stdint.h>"},
        {"offsetof", "<stddef.h>"},
        {"ptrdiff_t", "<stddef.h>"},
        {"size_t", "<stddef.h>"},
        {"true", "<stdbool.h>"},
        {"uint16_t", "<stdint.h>"},
        {"uint32_t", "<stdint.h>"},
        {"uint64_t", "<stdint.h>"},
        {"uint8_t", "<stdint.h>"},
    };
    return headers;
}

/// @brief The group of an include block that a header from @p origin is written in.
std::size_t includeGroup(const ImportOrigin origin)
{
    switch (origin)
    {
    case ImportOrigin::Standard:
        return 0U;
    case ImportOrigin::Library:
        return 1U;
    case ImportOrigin::Runtime:
    case ImportOrigin::Definition:
        return 2U;
    }
    llvm::report_fatal_error("include block: an include from no origin");
}

}  // namespace

CFileNames::CFileNames(ImportSet& includes, std::string ownHeader)
    : includes_(includes)
    , ownHeader_(std::move(ownHeader))
{
}

std::string CFileNames::standard(const llvm::StringRef name) const
{
    const auto header = standardHeaders().find(name);
    if (header == standardHeaders().end())
    {
        llvm::report_fatal_error(llvm::Twine("C backend: no header is known to declare '") + name + "'");
    }
    return includes_.member(ImportOrigin::Standard, header->second, name);
}

std::string CFileNames::runtime(const llvm::StringRef name) const
{
    return includes_.member(ImportOrigin::Runtime, "\"dsdl_runtime.h\"", name);
}

std::string CFileNames::declaredIn(const llvm::StringRef header, std::string name) const
{
    if (header.empty() || (header == ownHeader_))
    {
        return name;
    }
    return includes_.member(ImportOrigin::Definition, "\"" + header.str() + "\"", name);
}

std::string renderIncludeLines(const ImportSet& includes)
{
    std::array<std::vector<std::string>, 3> groups;
    for (const ImportedModule& module : includes.modules())
    {
        groups.at(includeGroup(module.origin)).push_back(module.path);
    }
    std::string lines;
    for (std::vector<std::string>& group : groups)
    {
        if (group.empty())
        {
            continue;
        }
        std::ranges::sort(group);
        lines += lines.empty() ? "" : "\n";
        for (const std::string& path : group)
        {
            lines += "#include " + path + "\n";
        }
    }
    return lines;
}

}  // namespace llvmdsdl::emitter::c
