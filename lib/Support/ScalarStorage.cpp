//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements the storage widths a generated type holds a DSDL scalar in.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Support/ScalarStorage.h"

#include <cstdint>

namespace llvmdsdl
{

std::uint32_t scalarStorageBits(const std::uint32_t bitLength)
{
    if (bitLength <= 8U)
    {
        return 8U;
    }
    if (bitLength <= 16U)
    {
        return 16U;
    }
    if (bitLength <= 32U)
    {
        return 32U;
    }
    return 64U;
}

std::uint32_t floatStorageBits(const std::uint32_t bitLength)
{
    return (bitLength == 64U) ? 64U : 32U;
}

}  // namespace llvmdsdl
