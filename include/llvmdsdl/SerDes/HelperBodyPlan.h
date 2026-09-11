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
/// A helper is a small function the generated code calls to normalise a value: mask a
/// scalar to its wire width, saturate it, or sign-extend it. @ref helperBodyForScalar
/// decides which shape a field's helper takes, and `lower-dsdl-exec` synthesises the body
/// from that shape.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SERDES_HELPER_BODY_PLAN_H
#define LLVMDSDL_SERDES_HELPER_BODY_PLAN_H

#include <cstdint>
#include <string>
#include <vector>

namespace llvmdsdl
{
/// @brief Which direction a section's helpers are built for.
///
/// Scalar shapes differ by direction: serialisation saturates or truncates a value
/// on its way to the wire, deserialisation puts a signed field's sign bit back.
enum class HelperDirection
{
    /// @brief Helpers used on the way to the wire.
    Serialize,

    /// @brief Helpers used on the way back.
    Deserialize,
};

/// @brief The wire category of a scalar field, in terms the shape decision needs.
///
/// Deliberately not @c SemanticScalarCategory: the decision has to be reachable from
/// the MLIR lowering, which knows a field's category only as a string attribute and
/// cannot see the semantic model.
enum class HelperScalarKind
{
    /// @brief Unsigned, byte, or UTF-8.
    Unsigned,

    /// @brief Signed.
    Signed,

    /// @brief IEEE 754 binary16, binary32, or binary64.
    Float,
};

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

/// @brief The body shape a scalar field's helper takes.
///
/// THE single statement of the decision, and the reason this lives below both the
/// MLIR lowering and the string backends: each has to answer the same question about
/// the same field, and answering it twice is how the two drift apart.
///
/// A width of 64 leaves nothing to do -- the value already occupies the whole
/// register, so masking and saturation are both identities. Below that, serialisation
/// saturates when the field asked for it and otherwise truncates by masking, while
/// deserialisation of a signed field has to put the sign bit back.
///
/// @param[in] kind Wire category of the field.
/// @param[in] bits Wire width. A width of 64 leaves nothing to normalise, so every
///            kind answers @ref HelperBodyKind::Identity there.
/// @param[in] saturated Whether the field's cast mode is saturated.
/// @param[in] direction Which way the helper runs.
/// @return The shape, with the operands it needs. The symbol is left empty.
HelperBody helperBodyForScalar(HelperScalarKind kind, std::uint32_t bits, bool saturated, HelperDirection direction);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SERDES_HELPER_BODY_PLAN_H
