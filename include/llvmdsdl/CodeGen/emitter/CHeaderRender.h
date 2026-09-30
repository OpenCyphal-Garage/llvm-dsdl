//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The guards a C or C++ header carries: its include guard, and the byte-order guard over a folded
/// body. C and C++ carry the same guards, which is why each is rendered once.
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

/// @brief Renders what opens a header guarded by @p macro: the test that skips a header already
///        read, and the macro's definition.
/// @param[in] macro The include guard the tree declares for the header.
/// @return Ordered preprocessor lines.
std::vector<std::string> renderIncludeGuardOpening(const std::string& macro);

/// @brief Renders what closes a header guarded by @p macro.
/// @param[in] macro The include guard the tree declares for the header.
/// @return The preprocessor line.
std::string renderIncludeGuardClosing(const std::string& macro);

}  // namespace llvmdsdl::emitter::c

#endif  // LLVMDSDL_CODEGEN_EMITTER_C_HEADER_RENDER_H
