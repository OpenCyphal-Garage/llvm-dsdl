//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared version constants and diagnostics for wire-operation contract plans.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_WIRE_OPERATION_CONTRACT_H
#define LLVMDSDL_CODEGEN_WIRE_OPERATION_CONTRACT_H

#include <cstdint>
#include <string>

namespace llvmdsdl
{

/// @brief Current wire-operation contract major version.
inline constexpr std::int64_t kWireOperationContractMajor = 2;

/// @brief Current wire-operation contract minor version.
inline constexpr std::int64_t kWireOperationContractMinor = 0;

/// @brief Current encoded wire-operation contract version.
inline constexpr std::int64_t kWireOperationContractVersion = kWireOperationContractMajor;

/// @brief Decodes major version from wire-operation contract encoding.
constexpr std::int64_t wireOperationContractMajorFromEncoded(const std::int64_t encodedVersion)
{
    return encodedVersion;
}

/// @brief True when the encoded contract version is supported by this build.
constexpr bool isSupportedWireOperationContractVersion(const std::int64_t encodedVersion)
{
    return wireOperationContractMajorFromEncoded(encodedVersion) == kWireOperationContractMajor;
}

/// @brief Deterministic diagnostic detail for unknown wire-operation contract majors.
inline std::string wireOperationUnsupportedMajorVersionDiagnosticDetail(const std::int64_t encodedVersion)
{
    return "expected " + std::to_string(kWireOperationContractMajor) + ", got " +
           std::to_string(wireOperationContractMajorFromEncoded(encodedVersion)) + " (encoded version " +
           std::to_string(encodedVersion) + ")";
}

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_WIRE_OPERATION_CONTRACT_H
