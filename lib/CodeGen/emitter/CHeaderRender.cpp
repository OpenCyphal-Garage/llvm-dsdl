//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements the byte-order guard of CHeaderRender.h.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/emitter/CHeaderRender.h"

#include <string>
#include <vector>

namespace llvmdsdl::emitter::c
{

std::vector<std::string> renderLittleEndianGuardLines(const std::string& typeName)
{
    const std::string moves = typeName + ": its serialisation moves the object as the wire's bytes, which holds ";
    const std::string fix   = " Regenerate with --target-triple naming this target.\"";
    // The targets MSVC compiles for, which it states no byte order for.
    const std::string msvc = std::string("defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64)") +
                             " || defined(_M_ARM) || defined(_M_ARM64) || defined(_M_ARM64EC))";
    return {
        "#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)",
        "#  if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__",
        "#    error \"" + moves + "only on a little-endian host." + fix,
        "#  endif",
        "#elif !(" + msvc + ")",
        "#  error \"" + moves + "only on a little-endian host, and this compiler states no byte order." + fix,
        "#endif",
    };
}

}  // namespace llvmdsdl::emitter::c
