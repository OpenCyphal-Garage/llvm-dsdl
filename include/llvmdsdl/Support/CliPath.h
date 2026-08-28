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
/// One definition of what a path argument means, so that every option taking one agrees: a leading
/// `~` is the invoking user's home directory, `.` and `..` are folded away, an absolute path is
/// taken as given, and a relative path naming something the run writes lands under the output
/// directory that run was given.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SUPPORT_CLIPATH_H
#define LLVMDSDL_SUPPORT_CLIPATH_H

#include <string>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace llvmdsdl
{

/// @brief Replaces a leading `~` in @p argument with the invoking user's home directory.
///
/// @details
/// The shell does this for an argument it expands, and does not for one it quotes, one built by a
/// build system, or one read from a configuration file. Only `~` alone and `~` followed by a
/// separator are expanded; `~name` addresses another user's home, which is left as typed.
///
/// This is the one stage of @ref normalizeCliPath that is safe to apply on its own, and applying it
/// on its own is what an argument with structure around the path needs -- the `<root>:<relative>`
/// colon syntax of a target or lookup argument, where folding the whole token as a path would
/// misread the part after the colon.
///
/// @param[in] argument Path argument as typed.
/// @return The expanded path, or an error when a `~` needs a home directory the platform cannot
///         name.
[[nodiscard]] llvm::Expected<std::string> expandHomeDirectory(llvm::StringRef argument);

/// @brief Normalizes a path argument naming something the run reads.
///
/// @details
/// Expands a leading `~` (see @ref expandHomeDirectory), then folds away `.` and `..` components.
/// The folding is lexical, so it holds for a path that does not exist yet and reads `a/../b` as `b`
/// even when `a` is a symbolic link. A relative path stays relative and so continues to be measured
/// from the working directory the tool was started in. An empty argument stays empty.
///
/// @param[in] argument Path argument as typed.
/// @return The normalized path, or an error when a `~` needs a home directory the platform cannot
///         name.
[[nodiscard]] llvm::Expected<std::string> normalizeCliPath(llvm::StringRef argument);

/// @brief Normalizes a path argument naming something the run writes.
///
/// @details
/// As @ref normalizeCliPath, except that a relative path is rooted under @p outputDirectory. The
/// rooting happens before the `.` and `..` folding, so `path/../to/file.txt` names
/// `<outputDirectory>/to/file.txt`. An absolute path is taken as given, which is how a caller
/// writes outside the output directory. An empty @p outputDirectory leaves a relative path
/// relative.
///
/// @param[in] argument Path argument as typed.
/// @param[in] outputDirectory Output directory in effect for the run.
/// @return The normalized path, or an error when a `~` needs a home directory the platform cannot
///         name.
[[nodiscard]] llvm::Expected<std::string> normalizeCliOutputPath(llvm::StringRef argument,
                                                                 llvm::StringRef outputDirectory);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SUPPORT_CLIPATH_H
