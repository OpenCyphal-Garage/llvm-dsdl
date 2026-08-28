//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements scalar storage-width resolution helpers for emitters.
///
/// These helpers normalize bit-length decisions into portable storage classes shared across language backends.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/TypeStorage.h"

#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Model.h"
#include <cstdint>
#include <optional>
#include <utility>

namespace llvmdsdl
{

std::uint32_t scalarStorageBits(const std::uint32_t bitLength)
{
    if (bitLength <= 8)
    {
        return 8;
    }
    if (bitLength <= 16)
    {
        return 16;
    }
    if (bitLength <= 32)
    {
        return 32;
    }
    return 64;
}

const char* scalarWidthSuffix(const std::uint32_t bitLength)
{
    switch (scalarStorageBits(bitLength))
    {
    case 8:
        return "8";
    case 16:
        return "16";
    case 32:
        return "32";
    default:
        return "64";
    }
}

bool isVariableArray(const ArrayKind kind)
{
    return kind == ArrayKind::VariableInclusive || kind == ArrayKind::VariableExclusive;
}

SemanticFieldType arrayElementType(const SemanticFieldType& type)
{
    auto out                  = type;
    out.arrayKind             = ArrayKind::None;
    out.arrayCapacity         = 0;
    out.arrayLengthPrefixBits = 0;
    return out;
}

std::optional<std::uint64_t> resolveUnsignedSaturationMax(const std::uint32_t bitLength)
{
    if (bitLength >= 64U)
    {
        return std::nullopt;
    }
    if (bitLength == 0U)
    {
        return std::uint64_t{0U};
    }
    return (std::uint64_t{1U} << bitLength) - std::uint64_t{1U};
}

std::optional<std::pair<std::int64_t, std::int64_t>> resolveSignedSaturationRange(const std::uint32_t bitLength)
{
    if (bitLength == 0U || bitLength >= 64U)
    {
        return std::nullopt;
    }
    const auto minValue = -(std::int64_t{1} << (bitLength - 1U));
    const auto maxValue = (std::int64_t{1} << (bitLength - 1U)) - 1;
    return std::pair<std::int64_t, std::int64_t>{minValue, maxValue};
}

}  // namespace llvmdsdl
