//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The facts a generated declaration states.
///
/// A generated name is spelt differently in each language -- `FULL_NAME`, `ListRequestFullName`,
/// `ns__List__Request_FULL_NAME_` -- and a surface tree records what each one states beside it, so
/// a reader finds the declaration by the fact rather than by its spelling.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SUPPORT_GENERATED_FACT_H
#define LLVMDSDL_SUPPORT_GENERATED_FACT_H

#include <cstdint>

namespace llvmdsdl
{

/// @brief What a generated declaration states, where its kind and the entity it stands for do not
///        tell it apart from another.
enum class GeneratedFact : std::uint8_t
{
    /// @brief The definition's full name.
    FullName,

    /// @brief The definition's full name and version.
    FullNameAndVersion,

    /// @brief Whether the definition is deprecated.
    IsDeprecated,

    /// @brief The section's extent, in bytes.
    ExtentBytes,

    /// @brief The size of a buffer that holds any serialised object of the section.
    SerializationBufferSizeBytes,

    /// @brief Whether the section's wire image is flat.
    WireFlat,

    /// @brief Why the section's wire image is flat or is not.
    WireFlatReason,

    /// @brief Whether the section's object is its wire image on the host.
    HostImage,

    /// @brief Why the section's object is its wire image on the host or is not.
    HostImageReason,

    /// @brief How many options a union has.
    UnionOptionCount,

    /// @brief Whether a message or service has a fixed port-ID.
    HasFixedPortId,

    /// @brief A message's or service's fixed port-ID.
    FixedPortId,

    /// @brief How a variable-length array's elements are held in memory.
    MemoryMode,

    /// @brief The largest array held inline, in bytes.
    InlineThresholdBytes,

    /// @brief The allocation class of one variable-length array field.
    PoolClass,

    /// @brief One array field's capacity.
    ArrayCapacity,

    /// @brief Whether one array field's length varies.
    ArrayIsVariableLength,

    /// @brief The member holding a union's tag.
    UnionTag,

    /// @brief The member a structure with no fields holds.
    Placeholder,

    /// @brief The macro that keeps a header from being read twice.
    IncludeGuard,

    /// @brief The macro saying a translation unit holds a version of the definition, under a type
    ///        name the versions share.
    SelectedType,

    /// @brief The macro saying a translation unit holds this version of the definition.
    SelectedVersion,

    /// @brief Whether a union holds one option.
    OptionTest,

    /// @brief Makes a union hold one option.
    OptionSelect,

    /// @brief A service's serialisation, which is its request's.
    Serialize,

    /// @brief A service's deserialisation, which is its request's.
    Deserialize,

    /// @brief A service's initialisation, which is its request's.
    Initialize,

    /// @brief Appends an object's wire image to a buffer the caller owns.
    AppendWireImage,

    /// @brief Answers an object's wire image.
    WireImage,

    /// @brief Reads an object from its wire image.
    FromWireImage,
};

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SUPPORT_GENERATED_FACT_H
