//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Backend-neutral shapes of the serialisation helper bodies.
///
/// A helper binding is a small function the generated code calls to normalise a
/// value or to check one: mask a scalar to its wire width, saturate it, sign-extend
/// it, or answer whether a length is in range. Each backend used to decide which of
/// those a descriptor called for and spell it in one step, from a table of canned
/// lines -- so the decision was stated five times and the emitted body carried its
/// own indentation as text.
///
/// @ref buildSectionHelperBodies makes that decision once. A backend supplies a
/// @ref HelperBodySpelling that renders each shape in its own idiom, through a
/// @ref SourceWriter that owns the block depth.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_HELPER_BODY_PLAN_H
#define LLVMDSDL_CODEGEN_HELPER_BODY_PLAN_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace llvmdsdl
{
struct SectionHelperBindingPlan;
enum class ScalarBindingRenderDirection;

/// @brief What a helper body does.
enum class HelperBodyKind
{
    /// @brief Returns its argument. A float helper, or an integer already at wire width.
    Identity,

    /// @brief Returns the argument masked to @ref HelperBody::bits.
    Mask,

    /// @brief Clamps an unsigned argument to the widest value @ref HelperBody::bits holds.
    SaturateUnsigned,

    /// @brief Clamps a signed argument to [@ref HelperBody::minValue, @ref HelperBody::maxValue].
    SaturateSigned,

    /// @brief Masks to @ref HelperBody::bits, then propagates the sign bit.
    SignExtend,

    /// @brief Answers a status: an error when the guard trips, success otherwise.
    StatusGuard,

    /// @brief Answers a status: success when the tag is one of @ref HelperBody::allowedTags.
    TagMembership,
};

/// @brief The condition a @ref HelperBodyKind::StatusGuard trips on.
enum class HelperGuardKind
{
    /// @brief The buffer holds fewer bits than the section needs.
    CapacityTooSmall,

    /// @brief An array length is negative or above the array's capacity.
    ArrayLengthOutOfRange,

    /// @brief A delimiter header's payload size is negative or exceeds what remains.
    DelimiterOutOfRange,
};

/// @brief A helper's parameter and return types, in wire terms rather than any language's.
enum class HelperSignature
{
    /// @brief Unsigned 64-bit in, unsigned 64-bit out.
    UnsignedToUnsigned,

    /// @brief Signed 64-bit in, signed 64-bit out.
    SignedToSigned,

    /// @brief 32-bit float in and out.
    Float32,

    /// @brief 64-bit float in and out.
    Float64,

    /// @brief A value in, a pass/fail answer out.
    ///
    /// How the answer is spelled is the backend's choice: C, C++, Go and Rust return
    /// a runtime status code, while TypeScript and Python return a boolean.
    ValueToStatus,

    /// @brief Two values in, a pass/fail answer out.
    PairToStatus,
};

/// @brief One helper binding: its name, its signature, and the shape of its body.
struct HelperBody final
{
    /// @brief What the body does.
    HelperBodyKind kind{HelperBodyKind::Identity};

    /// @brief Parameter and return types.
    HelperSignature signature{HelperSignature::UnsignedToUnsigned};

    /// @brief The helper's identifier, already projected for the target language.
    std::string symbol;

    /// @brief Wire width for @ref HelperBodyKind::Mask, @ref HelperBodyKind::SaturateUnsigned
    ///        and @ref HelperBodyKind::SignExtend.
    std::uint32_t bits{0};

    /// @brief Lower bound for @ref HelperBodyKind::SaturateSigned.
    std::int64_t minValue{0};

    /// @brief Upper bound for @ref HelperBodyKind::SaturateSigned.
    std::int64_t maxValue{0};

    /// @brief Bits the section needs, for @ref HelperGuardKind::CapacityTooSmall.
    std::int64_t requiredBits{0};

    /// @brief Array capacity, for @ref HelperGuardKind::ArrayLengthOutOfRange.
    std::int64_t capacity{0};

    /// @brief Which condition a @ref HelperBodyKind::StatusGuard trips on.
    HelperGuardKind guard{HelperGuardKind::CapacityTooSmall};

    /// @brief Accepted union tags, for @ref HelperBodyKind::TagMembership. Empty accepts none.
    std::vector<std::int64_t> allowedTags;
};

/// @brief Per-backend spelling of the helper body shapes.
///
/// Each method renders one shape in the backend's idiom and is free to spell it
/// however the language reads best -- a guard, a ternary, a clamp expression. What
/// a spelling cannot do is choose a different shape: that decision belongs to
/// @ref buildSectionHelperBodies and is shared by every backend.
class HelperBodySpelling
{
public:
    HelperBodySpelling()                                     = default;
    HelperBodySpelling(const HelperBodySpelling&)            = delete;
    HelperBodySpelling(HelperBodySpelling&&)                 = delete;
    HelperBodySpelling& operator=(const HelperBodySpelling&) = delete;
    HelperBodySpelling& operator=(HelperBodySpelling&&)      = delete;
    virtual ~HelperBodySpelling()                            = default;

    /// @brief Returns the argument unchanged.
    /// @param[in] body The binding.
    virtual void spellIdentity(const HelperBody& body) = 0;

    /// @brief Returns the argument masked to its wire width.
    /// @param[in] body The binding.
    virtual void spellMask(const HelperBody& body) = 0;

    /// @brief Clamps an unsigned argument to its wire width.
    /// @param[in] body The binding.
    virtual void spellSaturateUnsigned(const HelperBody& body) = 0;

    /// @brief Clamps a signed argument to its wire range.
    /// @param[in] body The binding.
    virtual void spellSaturateSigned(const HelperBody& body) = 0;

    /// @brief Masks to the wire width, then propagates the sign bit.
    /// @param[in] body The binding.
    virtual void spellSignExtend(const HelperBody& body) = 0;

    /// @brief Answers a status, erroring when the guard trips.
    /// @param[in] body The binding.
    virtual void spellStatusGuard(const HelperBody& body) = 0;

    /// @brief Answers a status, succeeding on an accepted union tag.
    /// @param[in] body The binding.
    virtual void spellTagMembership(const HelperBody& body) = 0;
};

/// @brief Dispatches one helper body to its spelling.
/// @param[in] body The binding.
/// @param[in,out] spelling Backend spelling.
void renderHelperBody(const HelperBody& body, HelperBodySpelling& spelling);

/// @brief Builds the helper bodies a section needs, in emission order.
///
/// THE single in-code decision of which shape each descriptor calls for: masking
/// versus saturation, sign extension versus identity, and the width and bounds each
/// carries. Every backend renders the result.
///
/// @param[in] plan Helper bindings the section requires.
/// @param[in] scalarDirection Serialize or deserialize; scalar shapes differ by direction.
/// @param[in] helperNameResolver Projects a helper symbol to the target language's identifier.
/// @param[in] emitCapacityCheck Controls whether the capacity check is included.
/// @return The bodies, in the order they are emitted.
std::vector<HelperBody> buildSectionHelperBodies(
    const SectionHelperBindingPlan&                       plan,
    ScalarBindingRenderDirection                          scalarDirection,
    const std::function<std::string(const std::string&)>& helperNameResolver,
    bool                                                  emitCapacityCheck);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_HELPER_BODY_PLAN_H
