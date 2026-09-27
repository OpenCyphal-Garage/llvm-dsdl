//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Records what one generated file names from outside itself.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/ImportSet.h"

#include <algorithm>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/ErrorHandling.h>

namespace llvmdsdl
{

ImportSet::Entry& ImportSet::entryFor(const ImportOrigin origin, const llvm::StringRef path)
{
    const auto [it, inserted] = modules_.try_emplace(path.str());
    if (inserted)
    {
        it->second.origin = origin;
    }
    else if (it->second.origin != origin)
    {
        llvm::report_fatal_error(llvm::Twine("import set: the module ") + path + " is named from two origins");
    }
    return it->second;
}

std::string ImportSet::module(const ImportOrigin origin, const llvm::StringRef path, const llvm::StringRef binding)
{
    Entry& entry = entryFor(origin, path);
    if (entry.binding.empty())
    {
        entry.binding = binding.str();
    }
    else if (entry.binding != binding)
    {
        llvm::report_fatal_error(llvm::Twine("import set: the module ") + path + " is bound to two names");
    }
    return entry.binding;
}

std::string ImportSet::member(const ImportOrigin    origin,
                              const llvm::StringRef path,
                              const llvm::StringRef name,
                              const llvm::StringRef local,
                              const ImportUse       use)
{
    Entry&            entry    = entryFor(origin, path);
    const std::string spelling = local.empty() ? name.str() : local.str();
    const auto [it, inserted]  = entry.members.try_emplace(name.str(), ImportedMember{name.str(), spelling, use});
    if (!inserted)
    {
        if (it->second.local != spelling)
        {
            llvm::report_fatal_error(llvm::Twine("import set: ") + name + " from " + path + " is spelled two ways");
        }
        if (use == ImportUse::Value)
        {
            it->second.use = ImportUse::Value;
        }
    }
    return it->second.local;
}

std::vector<ImportedModule> ImportSet::modules() const
{
    std::vector<ImportedModule> out;
    out.reserve(modules_.size());
    for (const auto& [path, entry] : modules_)
    {
        ImportedModule module;
        module.origin  = entry.origin;
        module.path    = path;
        module.binding = entry.binding;
        for (const auto& [name, member] : entry.members)
        {
            module.members.push_back(member);
        }
        out.push_back(std::move(module));
    }
    std::ranges::stable_sort(out, [](const ImportedModule& a, const ImportedModule& b) {
        return std::tie(a.origin, a.path) < std::tie(b.origin, b.path);
    });
    return out;
}

}  // namespace llvmdsdl
