//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The manifest of a generated Rust crate.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_EMITTER_RUST_PACKAGING_H
#define LLVMDSDL_CODEGEN_EMITTER_RUST_PACKAGING_H

#include <string>

#include "llvmdsdl/CodeGen/emitter/Rust.h"

namespace llvmdsdl::emitter::rust
{

/// @brief The crate's `Cargo.toml`, for @p options.
std::string renderCargoToml(const Options& options);

}  // namespace llvmdsdl::emitter::rust

#endif  // LLVMDSDL_CODEGEN_EMITTER_RUST_PACKAGING_H
