//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// How wide a generated type holds a DSDL scalar.
///
/// Every backend obeys the same widths, and the answer is needed below codegen: whether a
/// structure is a byte image of its own wire form turns on whether each field is stored in
/// exactly the width the wire carries it in.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SUPPORT_SCALAR_STORAGE_H
#define LLVMDSDL_SUPPORT_SCALAR_STORAGE_H

#include <cstdint>

namespace llvmdsdl
{

/// @brief Storage width a generated type holds an integer scalar in.
/// @param[in] bitLength Scalar bit width on the wire.
/// @return Storage width in bits: the smallest of 8, 16, 32 or 64 that holds it.
std::uint32_t scalarStorageBits(std::uint32_t bitLength);

/// @brief Storage width a generated type holds a floating-point scalar in.
/// @param[in] bitLength Scalar bit width on the wire.
/// @return 64 for a 64-bit float, otherwise 32: a `float16` is held in single precision.
std::uint32_t floatStorageBits(std::uint32_t bitLength);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SUPPORT_SCALAR_STORAGE_H
