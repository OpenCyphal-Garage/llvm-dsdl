//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Access to the language-runtime support sources embedded in the binary.
///
/// The C/C++/Rust/Go/Python emitters ship a hand-written runtime scaffold with
/// every generated package. Those sources are embedded verbatim at build time
/// (tools/runtime/generate_embedded_runtime.py) so an installed `dsdlc`
/// resolves them from itself rather than from a source path baked in at compile
/// time -- which is what lets the tool run outside a source checkout.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_EMBEDDED_RUNTIME_SOURCES_H
#define LLVMDSDL_CODEGEN_EMBEDDED_RUNTIME_SOURCES_H

#include <optional>
#include <string_view>

namespace llvmdsdl::embedded_runtime
{

/// @brief Looks up an embedded runtime support source by its path.
/// @param[in] relativePath Path relative to the repository `runtime/` directory
///   (e.g. "dsdl_runtime.h", "rust/dsdl_runtime.rs"), matching the manifest in
///   tools/runtime/generate_embedded_runtime.py.
/// @return The file's verbatim contents, or std::nullopt if no such file is
///   embedded. The returned view points at static storage and outlives any
///   caller.
std::optional<std::string_view> find(std::string_view relativePath);

}  // namespace llvmdsdl::embedded_runtime

#endif  // LLVMDSDL_CODEGEN_EMBEDDED_RUNTIME_SOURCES_H
