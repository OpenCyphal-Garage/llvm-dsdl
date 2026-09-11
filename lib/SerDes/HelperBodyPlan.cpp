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

}  // namespace llvmdsdl
