//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Contract constants for lowered serialization metadata exchanged between passes and backends.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_TRANSFORMS_LOWERED_SERDES_CONTRACT_H
#define LLVMDSDL_TRANSFORMS_LOWERED_SERDES_CONTRACT_H

#include <cstdint>
#include <string>

namespace llvmdsdl
{

/// @file
/// @brief Canonical attribute keys and constants for the lowered-serdes
/// @details Contract schema shared by lowering and backend code generators.

/// @brief Current lowered-serdes contract version.
inline constexpr std::int64_t kLoweredSerDesContractMajor = 2;

/// @brief Current lowered-serdes contract minor version.
inline constexpr std::int64_t kLoweredSerDesContractMinor = 0;

/// @brief Current lowered-serdes contract encoded version.
/// @details Version 1 encoding stores the major version directly.
inline constexpr std::int64_t kLoweredSerDesContractVersion = kLoweredSerDesContractMajor;

/// @brief Extracts the contract major version from encoded form.
/// @details Version 1 encoding stores major directly.
constexpr std::int64_t loweredSerDesContractMajorFromEncoded(const std::int64_t encodedVersion)
{
    return encodedVersion;
}

/// @brief True when the encoded contract version is supported by this build.
constexpr bool isSupportedLoweredSerDesContractVersion(const std::int64_t encodedVersion)
{
    return loweredSerDesContractMajorFromEncoded(encodedVersion) == kLoweredSerDesContractMajor;
}

/// @brief Deterministic diagnostic detail for unknown major-version failures.
inline std::string loweredSerDesUnsupportedMajorVersionDiagnosticDetail(const std::int64_t encodedVersion)
{
    return "expected " + std::to_string(kLoweredSerDesContractMajor) + ", got " +
           std::to_string(loweredSerDesContractMajorFromEncoded(encodedVersion)) + " (encoded version " +
           std::to_string(encodedVersion) + ")";
}

/// @brief Module-level attribute key for contract version.
inline constexpr char kLoweredSerDesContractVersionAttr[] = "llvmdsdl.lowered_contract_version";

/// @brief Module-level attribute key for contract producer.
inline constexpr char kLoweredSerDesContractProducerAttr[] = "llvmdsdl.lowered_contract_producer";

/// @brief Expected producer pass identifier.
inline constexpr char kLoweredSerDesContractProducer[] = "lower-dsdl-exec";

}  // namespace llvmdsdl

#endif  // LLVMDSDL_TRANSFORMS_LOWERED_SERDES_CONTRACT_H
