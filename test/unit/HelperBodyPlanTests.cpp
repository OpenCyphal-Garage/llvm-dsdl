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
/// What is worth pinning here is which shape a descriptor calls for, because that
/// decision is shared by every backend: a mistake in it is a mistake in five
/// languages at once. How a shape is spelled is per backend and is covered by the
/// generated-corpus comparisons instead.
///
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "UnitTests.h"
#include "llvmdsdl/SerDes/HelperBodyPlan.h"
#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"
#include "llvmdsdl/CodeGen/SerDesHelperDescriptors.h"

namespace
{

/// @brief Builds a one-scalar plan and returns the body chosen for it.
llvmdsdl::HelperBody scalarBody(const llvmdsdl::ScalarHelperKind kind,
                                const std::uint32_t              bits,
                                const llvmdsdl::CastMode         castMode,
                                const llvmdsdl::HelperDirection  direction)
{
    llvmdsdl::ScalarHelperDescriptor descriptor;
    descriptor.kind      = kind;
    descriptor.bitLength = bits;
    descriptor.castMode  = castMode;

    llvmdsdl::SectionHelperBindingPlan plan;
    plan.scalarBindings.push_back(llvmdsdl::ScalarBindingDescriptor{"scalar", descriptor});

    const auto bodies =
        llvmdsdl::buildSectionHelperBodies(plan, direction, [](const std::string& s) { return s; }, false);
    return bodies.empty() ? llvmdsdl::HelperBody{} : bodies.front();
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
using llvmdsdl::ScalarHelperKind;

/// @brief Scalar shapes, which are the only ones that vary by direction and cast mode.
bool scalarShapes()
{
    bool ok = true;

    // Saturation is asked for by the field and only applies on the way out.
    ok = expectKind("saturated unsigned serialize",
                    scalarBody(ScalarHelperKind::Unsigned, 8, CastMode::Saturated, HelperDirection::Serialize),
                    HelperBodyKind::SaturateUnsigned,
                    HelperSignature::UnsignedToUnsigned) &&
         ok;
    ok = expectKind("saturated unsigned deserialize",
                    scalarBody(ScalarHelperKind::Unsigned, 8, CastMode::Saturated, HelperDirection::Deserialize),
                    HelperBodyKind::Mask,
                    HelperSignature::UnsignedToUnsigned) &&
         ok;
    ok = expectKind("truncated unsigned serialize",
                    scalarBody(ScalarHelperKind::Unsigned, 8, CastMode::Truncated, HelperDirection::Serialize),
                    HelperBodyKind::Mask,
                    HelperSignature::UnsignedToUnsigned) &&
         ok;

    // A full-width value already occupies the register: nothing to mask or clamp.
    ok = expectKind("64-bit unsigned",
                    scalarBody(ScalarHelperKind::Unsigned, 64, CastMode::Saturated, HelperDirection::Serialize),
                    HelperBodyKind::Identity,
                    HelperSignature::UnsignedToUnsigned) &&
         ok;
    ok = expectKind("64-bit signed",
                    scalarBody(ScalarHelperKind::Signed, 64, CastMode::Saturated, HelperDirection::Deserialize),
                    HelperBodyKind::Identity,
                    HelperSignature::SignedToSigned) &&
         ok;

    // A narrow signed field clamps outbound and regains its sign bit inbound.
    const auto signedOut = scalarBody(ScalarHelperKind::Signed, 13, CastMode::Saturated, HelperDirection::Serialize);
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
                    scalarBody(ScalarHelperKind::Signed, 13, CastMode::Truncated, HelperDirection::Deserialize),
                    HelperBodyKind::SignExtend,
                    HelperSignature::SignedToSigned) &&
         ok;
    ok = expectKind("truncated signed serialize",
                    scalarBody(ScalarHelperKind::Signed, 13, CastMode::Truncated, HelperDirection::Serialize),
                    HelperBodyKind::Identity,
                    HelperSignature::SignedToSigned) &&
         ok;

    // Floats are carried at their own width and never normalised.
    ok = expectKind("float32",
                    scalarBody(ScalarHelperKind::Float, 32, CastMode::Saturated, HelperDirection::Serialize),
                    HelperBodyKind::Identity,
                    HelperSignature::Float32) &&
         ok;
    ok = expectKind("float64",
                    scalarBody(ScalarHelperKind::Float, 64, CastMode::Saturated, HelperDirection::Deserialize),
                    HelperBodyKind::Identity,
                    HelperSignature::Float64) &&
         ok;
    return ok;
}

/// @brief The non-scalar helpers, and the order a section emits them in.
bool sectionShapes()
{
    llvmdsdl::SectionHelperBindingPlan plan;
    plan.capacityCheck    = llvmdsdl::CapacityCheckHelperDescriptor{"cap", 16};
    plan.unionTagValidate = llvmdsdl::UnionTagValidateHelperDescriptor{"validate_tag", {0, 1}};
    plan.unionTagMask     = llvmdsdl::UnionTagMaskBindingDescriptor{"mask_tag", 8};
    plan.delimiterValidateBindings.push_back(llvmdsdl::DelimiterValidateBindingDescriptor{"delim"});
    plan.arrayPrefixBindings.push_back(llvmdsdl::ArrayPrefixBindingDescriptor{"arr_prefix", 8});
    plan.arrayValidateBindings.push_back(llvmdsdl::ArrayValidateBindingDescriptor{"arr_validate", 32});

    const auto bodies = llvmdsdl::buildSectionHelperBodies(
        plan, HelperDirection::Serialize, [](const std::string& s) { return "mlir_" + s; }, true);

    const std::vector<std::pair<std::string, HelperBodyKind>> expected{
        {"mlir_cap", HelperBodyKind::StatusGuard},
        {"mlir_validate_tag", HelperBodyKind::TagMembership},
        {"mlir_mask_tag", HelperBodyKind::Mask},
        {"mlir_delim", HelperBodyKind::StatusGuard},
        {"mlir_arr_prefix", HelperBodyKind::Mask},
        {"mlir_arr_validate", HelperBodyKind::StatusGuard},
    };
    if (bodies.size() != expected.size())
    {
        std::cerr << "section helper body count " << bodies.size() << ", expected " << expected.size() << "\n";
        return false;
    }
    bool ok = true;
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        if (bodies[i].symbol != expected[i].first || bodies[i].kind != expected[i].second)
        {
            std::cerr << "section helper body " << i << " is " << bodies[i].symbol << " kind "
                      << static_cast<int>(bodies[i].kind) << ", expected " << expected[i].first << "\n";
            ok = false;
        }
    }
    // Each guard has to know which condition it trips on; they share a shape.
    if (bodies[0].guard != llvmdsdl::HelperGuardKind::CapacityTooSmall || bodies[0].requiredBits != 16 ||
        bodies[3].guard != llvmdsdl::HelperGuardKind::DelimiterOutOfRange ||
        bodies[5].guard != llvmdsdl::HelperGuardKind::ArrayLengthOutOfRange || bodies[5].capacity != 32)
    {
        std::cerr << "section helper guard operands wrong\n";
        ok = false;
    }
    if (bodies[3].signature != HelperSignature::PairToStatus)
    {
        std::cerr << "delimiter helper should take a pair\n";
        ok = false;
    }
    return ok;
}

/// @brief The capacity check is the one helper a caller can suppress.
bool capacityCheckIsOptional()
{
    llvmdsdl::SectionHelperBindingPlan plan;
    plan.capacityCheck = llvmdsdl::CapacityCheckHelperDescriptor{"cap", 16};

    const auto with = llvmdsdl::
        buildSectionHelperBodies(plan, HelperDirection::Serialize, [](const std::string& s) { return s; }, true);
    const auto without = llvmdsdl::
        buildSectionHelperBodies(plan, HelperDirection::Deserialize, [](const std::string& s) { return s; }, false);
    if (with.size() != 1 || !without.empty())
    {
        std::cerr << "capacity check suppression wrong: " << with.size() << " with, " << without.size() << " without\n";
        return false;
    }
    return true;
}

/// @brief An empty allowed-tag set accepts nothing rather than everything.
bool emptyTagSetAcceptsNothing()
{
    llvmdsdl::SectionHelperBindingPlan plan;
    plan.unionTagValidate = llvmdsdl::UnionTagValidateHelperDescriptor{"validate_tag", {}};

    const auto bodies = llvmdsdl::
        buildSectionHelperBodies(plan, HelperDirection::Serialize, [](const std::string& s) { return s; }, false);
    if (bodies.size() != 1 || bodies.front().kind != HelperBodyKind::TagMembership ||
        !bodies.front().allowedTags.empty())
    {
        std::cerr << "empty union tag set did not survive planning\n";
        return false;
    }
    return true;
}

}  // namespace

bool runHelperBodyPlanTests()
{
    bool ok = true;
    ok      = scalarShapes() && ok;
    ok      = sectionShapes() && ok;
    ok      = capacityCheckIsOptional() && ok;
    ok      = emptyTagSetAcceptsNothing() && ok;
    return ok;
}
