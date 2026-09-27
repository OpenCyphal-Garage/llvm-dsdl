//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// What a generated C file names from other headers, and the include block a generated C or C++
/// file writes from what it named.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_EMITTER_C_INCLUDES_H
#define LLVMDSDL_CODEGEN_EMITTER_C_INCLUDES_H

#include <string>

#include <llvm/ADT/StringRef.h>

#include "llvmdsdl/CodeGen/ImportSet.h"

namespace llvmdsdl::emitter::c
{

/// @brief How one generated C file names what it takes from other headers, recording each include.
///
/// Every name the file writes from another header is named here, so the includes written from the
/// set once the file is rendered hold what the file names and nothing else.
class CFileNames final
{
public:
    /// @param[in] includes The set the file's includes are recorded in.
    /// @param[in] ownHeader The header the file is, or the one that declares what it defines; a name
    ///            it declares is no include of the file.
    CFileNames(ImportSet& includes, std::string ownHeader);

    /// @brief The standard library's @p name, from the header that declares it.
    [[nodiscard]] std::string standard(llvm::StringRef name) const;

    /// @brief The runtime's @p name.
    [[nodiscard]] std::string runtime(llvm::StringRef name) const;

    /// @brief @p name, which @p header declares; that header is included unless it is empty or the
    ///        file's own.
    [[nodiscard]] std::string declaredIn(llvm::StringRef header, std::string name) const;

private:
    ImportSet&  includes_;
    std::string ownHeader_;
};

/// @brief The `#include` lines of a C or C++ file that names @p includes: the standard library's
///        headers, then a bound library's, then the generated tree's own, each group in the order of
///        its paths and set apart from the next by a blank line.
std::string renderIncludeLines(const ImportSet& includes);

}  // namespace llvmdsdl::emitter::c

#endif  // LLVMDSDL_CODEGEN_EMITTER_C_INCLUDES_H
