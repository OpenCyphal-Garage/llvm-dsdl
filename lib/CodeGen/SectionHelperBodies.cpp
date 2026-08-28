//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include "llvmdsdl/SerDes/HelperBodyPlan.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"
#include "llvmdsdl/CodeGen/SerDesHelperDescriptors.h"

namespace llvmdsdl
{
namespace
{

/// @brief Maps a section's scalar descriptor onto the shared shape decision.
HelperScalarKind scalarKindOf(const ScalarHelperKind kind)
{
    switch (kind)
    {
    case ScalarHelperKind::Unsigned:
        return HelperScalarKind::Unsigned;
    case ScalarHelperKind::Signed:
        return HelperScalarKind::Signed;
    case ScalarHelperKind::Float:
        return HelperScalarKind::Float;
    }
    return HelperScalarKind::Unsigned;
}

}  // namespace

std::vector<HelperBody> buildSectionHelperBodies(
    const SectionHelperBindingPlan&                       plan,
    const HelperDirection                                 scalarDirection,
    const std::function<std::string(const std::string&)>& helperNameResolver,
    const bool                                            emitCapacityCheck)
{
    std::vector<HelperBody> bodies;

    if (emitCapacityCheck && plan.capacityCheck)
    {
        HelperBody body;
        body.kind         = HelperBodyKind::StatusGuard;
        body.guard        = HelperGuardKind::CapacityTooSmall;
        body.signature    = HelperSignature::ValueToStatus;
        body.symbol       = helperNameResolver(plan.capacityCheck->symbol);
        body.requiredBits = plan.capacityCheck->requiredBits;
        bodies.push_back(std::move(body));
    }
    if (plan.unionTagValidate)
    {
        HelperBody body;
        body.kind        = HelperBodyKind::TagMembership;
        body.signature   = HelperSignature::ValueToStatus;
        body.symbol      = helperNameResolver(plan.unionTagValidate->symbol);
        body.allowedTags = plan.unionTagValidate->allowedTags;
        bodies.push_back(std::move(body));
    }
    if (plan.unionTagMask)
    {
        HelperBody body;
        body.kind      = HelperBodyKind::Mask;
        body.signature = HelperSignature::UnsignedToUnsigned;
        body.symbol    = helperNameResolver(plan.unionTagMask->symbol);
        body.bits      = plan.unionTagMask->bits;
        bodies.push_back(std::move(body));
    }
    for (const auto& binding : plan.scalarBindings)
    {
        auto body   = helperBodyForScalar(scalarKindOf(binding.descriptor.kind),
                                          binding.descriptor.bitLength,
                                          binding.descriptor.castMode == CastMode::Saturated,
                                          scalarDirection);
        body.symbol = helperNameResolver(binding.symbol);
        bodies.push_back(std::move(body));
    }
    for (const auto& binding : plan.delimiterValidateBindings)
    {
        HelperBody body;
        body.kind      = HelperBodyKind::StatusGuard;
        body.guard     = HelperGuardKind::DelimiterOutOfRange;
        body.signature = HelperSignature::PairToStatus;
        body.symbol    = helperNameResolver(binding.symbol);
        bodies.push_back(std::move(body));
    }
    for (const auto& binding : plan.arrayPrefixBindings)
    {
        HelperBody body;
        body.kind      = HelperBodyKind::Mask;
        body.signature = HelperSignature::UnsignedToUnsigned;
        body.symbol    = helperNameResolver(binding.symbol);
        body.bits      = binding.bits;
        bodies.push_back(std::move(body));
    }
    for (const auto& binding : plan.arrayValidateBindings)
    {
        HelperBody body;
        body.kind      = HelperBodyKind::StatusGuard;
        body.guard     = HelperGuardKind::ArrayLengthOutOfRange;
        body.signature = HelperSignature::ValueToStatus;
        body.symbol    = helperNameResolver(binding.symbol);
        body.capacity  = binding.capacity;
        bodies.push_back(std::move(body));
    }

    return bodies;
}

}  // namespace llvmdsdl
