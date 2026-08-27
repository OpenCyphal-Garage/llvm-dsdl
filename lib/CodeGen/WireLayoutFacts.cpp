//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Computes wire-layout facts needed by generated serdes code.
///
/// The implementation resolves section-level details such as union-tag width and related layout constraints.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/WireLayoutFacts.h"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/Semantics/Model.h"

namespace llvmdsdl
{

std::uint32_t resolveUnionTagBits(const SemanticSection& section, const LoweredSectionFacts* const sectionFacts)
{
    if (sectionFacts && sectionFacts->unionTagBits)
    {
        return *sectionFacts->unionTagBits;
    }
    std::uint32_t tagBits = 8;
    for (const auto& f : section.fields)
    {
        if (!f.isPadding)
        {
            tagBits = std::max<std::uint32_t>(8U, f.unionTagBits);
            break;
        }
    }
    return tagBits;
}

}  // namespace llvmdsdl
