//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared scalar storage token rendering for native codegen backends.
///
/// This header provides backend-specific scalar token selection derived from
/// normalised storage widths.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_STORAGE_TYPE_TOKENS_H
#define LLVMDSDL_CODEGEN_STORAGE_TYPE_TOKENS_H

#include <cstdint>
#include <string>

namespace llvmdsdl
{

/// @brief Target language for scalar storage token rendering.
enum class StorageTokenLanguage
{
    /// @brief C token family (`uint32_t`, `int32_t`, ...).
    C,

    /// @brief C++ token family (`std::uint32_t`, `std::int32_t`, ...).
    Cpp,

    /// @brief Rust token family (`u32`, `i32`, ...).
    Rust,

    /// @brief Go token family (`uint32`, `int32`, ...).
    Go,
};

/// @brief Returns unsigned scalar storage token for a bit width.
/// @param[in] language Target language.
/// @param[in] bitLength Scalar bit width.
/// @return Backend token name.
std::string renderUnsignedStorageToken(StorageTokenLanguage language, std::uint32_t bitLength);

/// @brief Returns signed scalar storage token for a bit width.
/// @param[in] language Target language.
/// @param[in] bitLength Scalar bit width.
/// @return Backend token name.
std::string renderSignedStorageToken(StorageTokenLanguage language, std::uint32_t bitLength);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_STORAGE_TYPE_TOKENS_H
