//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements the reserved identifier predicate (DSDL spec v1.0 section 3.2.5).
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Support/ReservedIdentifiers.h"

#include <cctype>
#include <regex>
#include <string>
#include <unordered_set>

namespace llvmdsdl
{

bool isReservedIdentifier(const std::string& identifier)
{
    // The patterns are specified case-insensitively over the ASCII set
    // (table 3.5 caption), so compare against a lowercased copy.
    std::string lowered = identifier;
    for (char& c : lowered)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    // Fixed reserved words (cast modes, boolean literals, primitive type names
    // without a category pattern, future-use keywords, and the MS-DOS device
    // names that some filesystems reserve).
    static const std::unordered_set<std::string> kExactWords = {
        "truncated", "saturated", "true", "false", "bool",   "byte", "utf8", "optional", "aligned",
        "const",     "struct",    "super", "template", "enum", "self", "and",  "or",       "not",
        "auto",      "type",      "con",   "prn",      "aux",  "nul",
    };
    if (kExactWords.find(lowered) != kExactWords.end())
    {
        return true;
    }

    // Pattern-based reserved names: primitive/void/fixed-point type categories,
    // the MS-DOS numbered device names, and intrinsic entities of the form
    // `_<anything>_` (e.g. `_offset_`).
    static const std::regex kPatterns(
        "^(?:u?int[0-9]*|float[0-9]*|void[0-9]*|q[0-9]+_[0-9]+|com[0-9]|lpt[0-9]|_.*_)$");
    return std::regex_match(lowered, kPatterns);
}

}  // namespace llvmdsdl
