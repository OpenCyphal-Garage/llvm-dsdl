//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// What one generated file names from outside itself.
///
/// A backend names an outside symbol through the set as it writes it, and the set records the
/// module that provides it. The import block is written from what was recorded once the file is
/// rendered, so a file imports what it names and nothing else. A backend states where its symbols
/// come from and how its language writes an import; which imports a file carries is not its to
/// decide again.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_IMPORT_SET_H
#define LLVMDSDL_CODEGEN_IMPORT_SET_H

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <llvm/ADT/StringRef.h>

namespace llvmdsdl
{

/// @brief Where a module a generated file imports comes from, which is the order of the import
///        block's groups.
enum class ImportOrigin : std::uint8_t
{
    /// @brief The target language's own standard library.
    Standard,

    /// @brief The runtime the generator writes beside the generated code.
    Runtime,

    /// @brief Another generated definition.
    Definition,
};

/// @brief One module a generated file imports, and what it names from it.
struct ImportedModule final
{
    ImportOrigin origin{ImportOrigin::Standard};

    /// @brief The module as the language's import names it.
    std::string path;

    /// @brief The local name the module itself is bound to, or empty where the file names only its
    ///        members.
    std::string binding;

    /// @brief Each member the file names, and the local name it spells the member by, in the order
    ///        of the members' names.
    std::vector<std::pair<std::string, std::string>> members;
};

/// @brief What one generated file names from outside itself.
class ImportSet final
{
public:
    /// @brief Records that the file names the module @p path itself, bound to @p binding.
    /// @return The binding, which is how the file spells the module.
    std::string module(ImportOrigin origin, llvm::StringRef path, llvm::StringRef binding);

    /// @brief Records that the file names @p name from the module @p path, spelled @p local, or
    ///        @p name where @p local is empty.
    /// @return How the file spells the member.
    std::string member(ImportOrigin origin, llvm::StringRef path, llvm::StringRef name, llvm::StringRef local = {});

    /// @brief The modules recorded, by origin and then by path.
    [[nodiscard]] std::vector<ImportedModule> modules() const;

private:
    struct Entry final
    {
        ImportOrigin                       origin{ImportOrigin::Standard};
        std::string                        binding;
        std::map<std::string, std::string> members;
    };

    /// @brief The entry for @p path, which is always named from one origin.
    Entry& entryFor(ImportOrigin origin, llvm::StringRef path);

    std::map<std::string, Entry> modules_;
};

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_IMPORT_SET_H
