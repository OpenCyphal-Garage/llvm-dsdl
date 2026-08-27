//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements source-location rendering helpers.
///
/// This file contains utility formatting logic for presenting file, line, and column coordinates.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Frontend/SourceLocation.h"

#include <sstream>
#include <string>

namespace llvmdsdl
{

std::string SourceLocation::str() const
{
    std::ostringstream out;
    out << file << ':' << line << ':' << column;
    return out.str();
}

}  // namespace llvmdsdl
