//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Embedded language-runtime support source accessor.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/EmbeddedRuntimeSources.h"

#include <optional>
#include <string_view>

// Generated at build time from the files under runtime/ by
// tools/runtime/generate_embedded_runtime.py; see lib/CodeGen/CMakeLists.txt.
// Defines llvmdsdl::embedded_runtime::detail::kEmbeddedRuntimeFiles.
#include "EmbeddedRuntimeSources.inc"

namespace llvmdsdl::embedded_runtime
{

std::optional<std::string_view> find(const std::string_view relativePath)
{
    for (const auto& entry : detail::kEmbeddedRuntimeFiles)
    {
        if (entry.path == relativePath)
        {
            return entry.data;
        }
    }
    return std::nullopt;
}

}  // namespace llvmdsdl::embedded_runtime
