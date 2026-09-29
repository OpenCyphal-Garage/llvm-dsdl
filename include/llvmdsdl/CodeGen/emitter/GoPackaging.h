//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The module file of a generated Go module.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_EMITTER_GO_PACKAGING_H
#define LLVMDSDL_CODEGEN_EMITTER_GO_PACKAGING_H

#include <string>

#include <llvm/ADT/StringRef.h>

#include "llvmdsdl/CodeGen/emitter/Go.h"

namespace llvmdsdl::emitter::go
{

/// @brief The module's `go.mod`, for @p options, opening with the comment line @p generatedComment.
std::string renderGoMod(const Options& options, llvm::StringRef generatedComment);

}  // namespace llvmdsdl::emitter::go

#endif  // LLVMDSDL_CODEGEN_EMITTER_GO_PACKAGING_H
