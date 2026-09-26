//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// What a target's generated interface lets the lowering assume of a body.
///
/// The lowering names no language. It folds a body against these facts, and the driver takes them
/// from the target's row in `LanguageTraits.h`. A default-constructed interface assumes nothing,
/// which is what `mlir` prints: the neutral body every backend translates.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SUPPORT_BODY_INTERFACE_H
#define LLVMDSDL_SUPPORT_BODY_INTERFACE_H

namespace llvmdsdl
{

/// @brief Which of a plan body's pointer arguments a target can present as null.
///
/// A body opens by testing the three it is handed, and answers `-2` when any is null. A target
/// whose references cannot be null never reaches that answer, so the test is a constant and the
/// guard is a branch nothing takes. Each defaults to the answer C gives, which is that any of them
/// may be null, so a caller that says nothing keeps the guard.
struct TargetNullability final
{
    /// @brief Whether the object a body serialises can arrive null.
    ///
    /// True where the target is handed a pointer, or an object a caller may still omit. False
    /// where it is handed a reference.
    bool objectPointer{true};

    /// @brief Whether the buffer and the slot holding its size can arrive null.
    ///
    /// True only where they are pointers. A slice, a view and a local cannot be null.
    bool rawPointer{true};

    /// @brief Whether a field accessor's buffer can arrive null, where `rawPointer` says a body's can.
    ///
    /// False for a target whose accessors take a span while its serialise and deserialise still
    /// take a pointer.
    bool accessorBuffer{true};

    /// @brief Whether every argument is still nullable, so the guard has nothing to fold.
    [[nodiscard]] constexpr bool allNullable() const
    {
        return objectPointer && rawPointer && accessorBuffer;
    }
};

/// @brief How a target stores a bool array.
enum class BoolArrayStorage
{
    /// @brief Packed, eight to a byte, as on the wire.
    Packed,

    /// @brief Packed where the array's length is fixed, and a bool per element where it varies.
    PackedWhenFixed,

    /// @brief A bool per element.
    PerElement,
};

/// @brief What a target's generated interface hands a body, and what its objects are.
struct BodyInterface final
{
    /// @brief What the target can present as null, which decides whether the entry guard survives.
    TargetNullability nullability{};

    /// @brief Whether the target's composite getter answers a view that carries its own length.
    ///
    /// Such a getter's caller reads the length from the view, so the length the plan writes back is
    /// observed by nothing. A target whose getter answers a pointer passes the length back through
    /// another, which the caller reads.
    bool accessorsReturnViews{false};

    /// @brief Whether the target's objects can be byte images of the wire.
    ///
    /// Such a target folds a host-image section's bodies into one move. Where a structure has no
    /// layout to speak of, it cannot. The row states what the language can do; the driver clears it
    /// for a host that orders bytes other than as the wire does, since the swap back is per scalar.
    bool objectsAreByteImages{false};

    /// @brief Whether a serialise or deserialise entry point is handed the bytes available as the
    ///        length of its buffer, and answers the bytes it used beside its error.
    ///
    /// A target whose entry point reads and writes the size through a pointer takes the plan's
    /// bodies and nested calls as they are. One whose buffer carries its own length has each body
    /// folded to take the buffer alone and answer the size as a second result, and each nested call
    /// folded to `dsdl.call_serdes_sized`, which state the adaptation once rather than in each
    /// spelling.
    bool bodiesAnswerSize{false};

    /// @brief How the target stores a bool array.
    ///
    /// The plan moves a bool array as one run of wire bits. A run whose array the target stores a
    /// bool per element is expanded by `dsdl-expand-bool-runs` into a loop over the elements, which
    /// every backend already translates, rather than each spelling looping on its own.
    BoolArrayStorage boolArrays{BoolArrayStorage::Packed};
};

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SUPPORT_BODY_INTERFACE_H
