//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The manifest of a generated TypeScript package.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_EMITTER_TS_PACKAGING_H
#define LLVMDSDL_CODEGEN_EMITTER_TS_PACKAGING_H

#include <string>

#include "llvmdsdl/CodeGen/emitter/Ts.h"

namespace llvmdsdl::emitter::ts
{

/// @brief The package's `package.json`, for @p options.
std::string renderPackageJson(const Options& options);

}  // namespace llvmdsdl::emitter::ts

#endif  // LLVMDSDL_CODEGEN_EMITTER_TS_PACKAGING_H
