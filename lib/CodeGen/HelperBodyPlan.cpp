//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/HelperBodyPlan.h"

#include <cstdint>
#include <string>
#include <vector>

#include "llvmdsdl/CodeGen/HelperBindingRender.h"
#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"
#include "llvmdsdl/CodeGen/SerDesHelperDescriptors.h"

namespace llvmdsdl
{
namespace
{

/// @brief The body a scalar descriptor calls for, in the given direction.
///
/// The decision every backend used to repeat. A width of 64 leaves nothing to do:
/// the value already occupies the whole register, so masking and saturation are
/// both identities. Below that, serialisation saturates only when the field asked
/// for it and otherwise truncates by masking, while deserialisation of a signed
/// field has to put the sign bit back.
HelperBody buildScalarBody(const std::string&                 symbol,
                           const ScalarHelperDescriptor&      descriptor,
                           const ScalarBindingRenderDirection direction)
{
    HelperBody body;
    body.symbol = symbol;
    body.bits   = descriptor.bitLength;

    const bool serialize = direction == ScalarBindingRenderDirection::Serialize;
    const bool saturated = descriptor.castMode == CastMode::Saturated;
    const bool narrow    = descriptor.bitLength < 64U;

    switch (descriptor.kind)
    {
    case ScalarHelperKind::Unsigned:
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

    case ScalarHelperKind::Signed:
        body.signature = HelperSignature::SignedToSigned;
        if (serialize && saturated && descriptor.bitLength > 0U && narrow)
        {
            body.kind     = HelperBodyKind::SaturateSigned;
            body.minValue = -(std::int64_t{1} << (descriptor.bitLength - 1U));
            body.maxValue = (std::int64_t{1} << (descriptor.bitLength - 1U)) - 1;
        }
        else if (!serialize && descriptor.bitLength > 0U && narrow)
        {
            body.kind = HelperBodyKind::SignExtend;
        }
        else
        {
            body.kind = HelperBodyKind::Identity;
        }
        break;

    case ScalarHelperKind::Float:
        body.signature = descriptor.bitLength == 64U ? HelperSignature::Float64 : HelperSignature::Float32;
        body.kind      = HelperBodyKind::Identity;
        break;
    }
    return body;
}

}  // namespace

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

std::vector<HelperBody> buildSectionHelperBodies(
    const SectionHelperBindingPlan&                       plan,
    const ScalarBindingRenderDirection                    scalarDirection,
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
        bodies.push_back(buildScalarBody(helperNameResolver(binding.symbol), binding.descriptor, scalarDirection));
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
