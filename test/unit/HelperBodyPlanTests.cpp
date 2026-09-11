//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Tests for the helper body shape decision.
///
/// These pin which shape a descriptor calls for, a decision shared by every backend. How a
/// shape is spelled is per backend and is covered by the generated-corpus comparisons.
///
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <iostream>

#include "UnitTests.h"
#include "llvmdsdl/SerDes/HelperBodyPlan.h"
#include "llvmdsdl/Frontend/AST.h"

namespace
{

/// @brief The body chosen for one scalar field.
llvmdsdl::HelperBody scalarBody(const llvmdsdl::HelperScalarKind kind,
                                const std::uint32_t              bits,
                                const llvmdsdl::CastMode         castMode,
                                const llvmdsdl::HelperDirection  direction)
{
    return llvmdsdl::helperBodyForScalar(kind, bits, castMode == llvmdsdl::CastMode::Saturated, direction);
}

bool expectKind(const char*                     what,
                const llvmdsdl::HelperBody&     body,
                const llvmdsdl::HelperBodyKind  expectedKind,
                const llvmdsdl::HelperSignature expectedSignature)
{
    if (body.kind != expectedKind || body.signature != expectedSignature)
    {
        std::cerr << "helper body shape mismatch for " << what << ": kind " << static_cast<int>(body.kind)
                  << " signature " << static_cast<int>(body.signature) << "\n";
        return false;
    }
    return true;
}

using llvmdsdl::CastMode;
using llvmdsdl::HelperBodyKind;
using llvmdsdl::HelperDirection;
using llvmdsdl::HelperSignature;
using llvmdsdl::HelperScalarKind;

/// @brief Scalar shapes; only these vary by direction and cast mode.
bool scalarShapes()
{
    bool ok = true;

    // Saturation is asked for by the field and only applies on the way out.
    ok = expectKind("saturated unsigned serialize",
                    scalarBody(HelperScalarKind::Unsigned, 8, CastMode::Saturated, HelperDirection::Serialize),
                    HelperBodyKind::SaturateUnsigned,
                    HelperSignature::UnsignedToUnsigned) &&
         ok;
    ok = expectKind("saturated unsigned deserialize",
                    scalarBody(HelperScalarKind::Unsigned, 8, CastMode::Saturated, HelperDirection::Deserialize),
                    HelperBodyKind::Mask,
                    HelperSignature::UnsignedToUnsigned) &&
         ok;
    ok = expectKind("truncated unsigned serialize",
                    scalarBody(HelperScalarKind::Unsigned, 8, CastMode::Truncated, HelperDirection::Serialize),
                    HelperBodyKind::Mask,
                    HelperSignature::UnsignedToUnsigned) &&
         ok;

    // A full-width value already occupies the register: nothing to mask or clamp.
    ok = expectKind("64-bit unsigned",
                    scalarBody(HelperScalarKind::Unsigned, 64, CastMode::Saturated, HelperDirection::Serialize),
                    HelperBodyKind::Identity,
                    HelperSignature::UnsignedToUnsigned) &&
         ok;
    ok = expectKind("64-bit signed",
                    scalarBody(HelperScalarKind::Signed, 64, CastMode::Saturated, HelperDirection::Deserialize),
                    HelperBodyKind::Identity,
                    HelperSignature::SignedToSigned) &&
         ok;

    // A narrow signed field clamps outbound and regains its sign bit inbound.
    const auto signedOut = scalarBody(HelperScalarKind::Signed, 13, CastMode::Saturated, HelperDirection::Serialize);
    ok                   = expectKind("saturated signed serialize",
                                      signedOut,
                                      HelperBodyKind::SaturateSigned,
                                      HelperSignature::SignedToSigned) &&
                           ok;
    if (signedOut.minValue != -4096 || signedOut.maxValue != 4095)
    {
        std::cerr << "13-bit signed bounds wrong: " << signedOut.minValue << ".." << signedOut.maxValue << "\n";
        ok = false;
    }
    ok = expectKind("signed deserialize",
                    scalarBody(HelperScalarKind::Signed, 13, CastMode::Truncated, HelperDirection::Deserialize),
                    HelperBodyKind::SignExtend,
                    HelperSignature::SignedToSigned) &&
         ok;
    ok = expectKind("truncated signed serialize",
                    scalarBody(HelperScalarKind::Signed, 13, CastMode::Truncated, HelperDirection::Serialize),
                    HelperBodyKind::Identity,
                    HelperSignature::SignedToSigned) &&
         ok;

    // Floats are carried at their own width and never normalised.
    ok = expectKind("float32",
                    scalarBody(HelperScalarKind::Float, 32, CastMode::Saturated, HelperDirection::Serialize),
                    HelperBodyKind::Identity,
                    HelperSignature::Float32) &&
         ok;
    ok = expectKind("float64",
                    scalarBody(HelperScalarKind::Float, 64, CastMode::Saturated, HelperDirection::Deserialize),
                    HelperBodyKind::Identity,
                    HelperSignature::Float64) &&
         ok;
    return ok;
}

}  // namespace

bool runHelperBodyPlanTests()
{
    bool ok = true;
    ok      = scalarShapes() && ok;
    return ok;
}
