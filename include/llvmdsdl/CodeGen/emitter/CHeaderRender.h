//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The byte-order guard a C or C++ header carries over a folded body.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_EMITTER_C_HEADER_RENDER_H
#define LLVMDSDL_CODEGEN_EMITTER_C_HEADER_RENDER_H

#include <string>
#include <vector>

namespace llvmdsdl::emitter::c
{

/// @brief Renders the guard a folded body carries, refusing a host that does not order bytes as
///        the wire does.
///
/// It refuses a host it cannot place as well as one it places as big-endian: a compiler stating no
/// byte order says nothing about the target, and the fold is only correct where the two orders
/// agree. MSVC states none and every target it compiles for is little-endian, so it is named.
/// C and C++ carry the same guard, which is why it is rendered once.
/// @param[in] typeName Generated type name, which the refusal names.
/// @return Ordered preprocessor lines.
std::vector<std::string> renderLittleEndianGuardLines(const std::string& typeName);

}  // namespace llvmdsdl::emitter::c

#endif  // LLVMDSDL_CODEGEN_EMITTER_C_HEADER_RENDER_H
