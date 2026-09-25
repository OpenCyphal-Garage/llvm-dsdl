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

#include "llvmdsdl/CodeGen/EmbeddedSources.h"

#include <optional>
#include <string_view>

// Generated at build time by tools/generate_embedded_sources.py from the files its manifest
// names; see lib/CodeGen/CMakeLists.txt. Defines llvmdsdl::embedded_sources::detail::kEmbeddedFiles.
#include "EmbeddedSources.inc"

namespace llvmdsdl::embedded_sources
{

std::optional<std::string_view> find(const std::string_view key)
{
    for (const auto& entry : detail::kEmbeddedFiles)
    {
        if (entry.key == key)
        {
            return entry.data;
        }
    }
    return std::nullopt;
}

}  // namespace llvmdsdl::embedded_sources
