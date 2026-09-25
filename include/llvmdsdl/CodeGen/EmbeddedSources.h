//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The sources compiled into dsdlc: the language runtimes it writes beside generated code, and
/// the built-in vocabulary bindings.
///
//===----------------------------------------------------------------------===//

#ifndef LLVMDSDL_CODEGEN_EMBEDDED_SOURCES_H
#define LLVMDSDL_CODEGEN_EMBEDDED_SOURCES_H

#include <optional>
#include <string_view>

namespace llvmdsdl::embedded_sources
{

/// @brief Looks up an embedded source by its key.
/// @param[in] key A runtime file's path relative to the repository `runtime/` directory
///   (`dsdl_runtime.h`, `rust/dsdl_runtime.rs`), or `vocabulary/<language>.yaml`, matching the
///   manifest in tools/generate_embedded_sources.py.
/// @return The file's verbatim contents, or std::nullopt if no such file is embedded. The returned
///   view points at static storage and outlives any caller.
std::optional<std::string_view> find(std::string_view key);

}  // namespace llvmdsdl::embedded_sources

#endif  // LLVMDSDL_CODEGEN_EMBEDDED_SOURCES_H
