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

#include "llvmdsdl/Support/Language.h"

namespace llvmdsdl
{

/// @brief Returns unsigned scalar storage token for a bit width.
/// @param[in] language Target language.
/// @param[in] bitLength Scalar bit width.
/// @return Backend token name.
std::string renderUnsignedStorageToken(Language language, std::uint32_t bitLength);

/// @brief Returns signed scalar storage token for a bit width.
/// @param[in] language Target language.
/// @param[in] bitLength Scalar bit width.
/// @return Backend token name.
std::string renderSignedStorageToken(Language language, std::uint32_t bitLength);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_STORAGE_TYPE_TOKENS_H
