//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements the shared snake_case name canonicalization.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Support/NameCanonicalization.h"

#include <cctype>
#include <cstddef>

namespace llvmdsdl
{

std::string canonicalSnakeCase(const llvm::StringRef name)
{
    std::string out;
    out.reserve(name.size() + 8);

    bool prevUnderscore = false;
    for (std::size_t i = 0; i < name.size(); ++i)
    {
        const char c    = name[i];
        const char prev = (i > 0) ? name[i - 1] : '\0';
        const char next = (i + 1 < name.size()) ? name[i + 1] : '\0';
        if (!std::isalnum(static_cast<unsigned char>(c)))
        {
            if (!out.empty() && !prevUnderscore)
            {
                out.push_back('_');
                prevUnderscore = true;
            }
            continue;
        }

        if (std::isupper(static_cast<unsigned char>(c)))
        {
            const bool boundary =
                std::islower(static_cast<unsigned char>(prev)) ||
                (std::isupper(static_cast<unsigned char>(prev)) && std::islower(static_cast<unsigned char>(next)));
            if (!out.empty() && !prevUnderscore && boundary)
            {
                out.push_back('_');
            }
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            prevUnderscore = false;
        }
        else
        {
            out.push_back(c);
            prevUnderscore = (c == '_');
        }
    }
    return out;
}

}  // namespace llvmdsdl
