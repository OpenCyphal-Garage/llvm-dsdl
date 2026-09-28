//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The packaging files of a generated Python package.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_EMITTER_PYTHON_PACKAGING_H
#define LLVMDSDL_CODEGEN_EMITTER_PYTHON_PACKAGING_H

#include <string>

#include <llvm/ADT/StringRef.h>

#include "llvmdsdl/CodeGen/emitter/Python.h"

namespace llvmdsdl::emitter::python
{

/// @brief The generator's metadata written beside the generated package, for @p options.
std::string renderPackageMetadata(const Options& options);

/// @brief The package's `pyproject.toml`.
/// @param[in] packageName The generated package's name.
/// @param[in] rootPackageName The top-level package component that package discovery starts from.
std::string renderPyProjectToml(llvm::StringRef packageName, llvm::StringRef rootPackageName);

}  // namespace llvmdsdl::emitter::python

#endif  // LLVMDSDL_CODEGEN_EMITTER_PYTHON_PACKAGING_H
