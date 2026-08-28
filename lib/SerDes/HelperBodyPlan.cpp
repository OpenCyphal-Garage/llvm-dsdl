//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include "llvmdsdl/SerDes/HelperBodyPlan.h"

#include <cstdint>
#include <string>

namespace llvmdsdl
{

std::string renderMaskLiteral(const HelperSpellingLanguage language, const std::uint32_t bits)
{
    switch (language)
    {
    case HelperSpellingLanguage::Cpp:
        if (bits == 0U)
        {
            return "0ULL";
        }
        if (bits >= 64U)
        {
            return "18446744073709551615ULL";
        }
        return std::to_string((1ULL << bits) - 1ULL) + "ULL";
    case HelperSpellingLanguage::Rust:
        if (bits == 0U)
        {
            return "0u64";
        }
        if (bits >= 64U)
        {
            return "u64::MAX";
        }
        return std::to_string((1ULL << bits) - 1ULL) + "u64";
    case HelperSpellingLanguage::Go:
        if (bits == 0U)
        {
            return "uint64(0)";
        }
        if (bits >= 64U)
        {
            return "^uint64(0)";
        }
        return "uint64(" + std::to_string((1ULL << bits) - 1ULL) + ")";
    case HelperSpellingLanguage::TypeScript:
        if (bits == 0U)
        {
            return "0n";
        }
        if (bits >= 64U)
        {
            return "18446744073709551615n";
        }
        return std::to_string((1ULL << bits) - 1ULL) + "n";
    case HelperSpellingLanguage::Python:
        if (bits == 0U)
        {
            return "0";
        }
        if (bits >= 64U)
        {
            return "18446744073709551615";
        }
        return std::to_string((1ULL << bits) - 1ULL);
    }
    return "0";
}

HelperBody helperBodyForScalar(const HelperScalarKind kind,
                               const std::uint32_t    bits,
                               const bool             saturated,
                               const HelperDirection  direction)
{
    HelperBody body;
    body.bits = bits;

    const bool serialize = direction == HelperDirection::Serialize;
    const bool narrow    = bits < 64U;

    switch (kind)
    {
    case HelperScalarKind::Unsigned:
        body.signature = HelperSignature::UnsignedToUnsigned;
        if (serialize && saturated && narrow)
        {
            body.kind = HelperBodyKind::SaturateUnsigned;
        }
        else if (narrow)
        {
            body.kind = HelperBodyKind::Mask;
        }
        else
        {
            body.kind = HelperBodyKind::Identity;
        }
        break;

    case HelperScalarKind::Signed:
        body.signature = HelperSignature::SignedToSigned;
        if (serialize && saturated && bits > 0U && narrow)
        {
            body.kind     = HelperBodyKind::SaturateSigned;
            body.minValue = -(std::int64_t{1} << (bits - 1U));
            body.maxValue = (std::int64_t{1} << (bits - 1U)) - 1;
        }
        else if (!serialize && bits > 0U && narrow)
        {
            body.kind = HelperBodyKind::SignExtend;
        }
        else
        {
            body.kind = HelperBodyKind::Identity;
        }
        break;

    case HelperScalarKind::Float:
        body.signature = bits == 64U ? HelperSignature::Float64 : HelperSignature::Float32;
        body.kind      = HelperBodyKind::Identity;
        break;
    }
    return body;
}

void renderHelperBody(const HelperBody& body, HelperBodySpelling& spelling)
{
    switch (body.kind)
    {
    case HelperBodyKind::Identity:
        spelling.spellIdentity(body);
        break;
    case HelperBodyKind::Mask:
        spelling.spellMask(body);
        break;
    case HelperBodyKind::SaturateUnsigned:
        spelling.spellSaturateUnsigned(body);
        break;
    case HelperBodyKind::SaturateSigned:
        spelling.spellSaturateSigned(body);
        break;
    case HelperBodyKind::SignExtend:
        spelling.spellSignExtend(body);
        break;
    case HelperBodyKind::StatusGuard:
        spelling.spellStatusGuard(body);
        break;
    case HelperBodyKind::TagMembership:
        spelling.spellTagMembership(body);
        break;
    }
}

}  // namespace llvmdsdl
