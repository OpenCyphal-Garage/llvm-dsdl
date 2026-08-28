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

/// @brief Marker attribute for lowered section plans.
inline constexpr char kLoweredPlanMarkerAttr[] = "lowered";

/// @brief Attribute for minimum section bit length.
inline constexpr char kLoweredMinBitsAttr[] = "lowered_min_bits";

/// @brief Attribute for maximum section bit length.
inline constexpr char kLoweredMaxBitsAttr[] = "lowered_max_bits";

/// @brief Attribute for lowered step count.
inline constexpr char kLoweredStepCountAttr[] = "lowered_step_count";

/// @brief Attribute for lowered field count.
inline constexpr char kLoweredFieldCountAttr[] = "lowered_field_count";

/// @brief Attribute for lowered padding count.
inline constexpr char kLoweredPaddingCountAttr[] = "lowered_padding_count";

/// @brief Attribute for lowered alignment-operation count.
inline constexpr char kLoweredAlignCountAttr[] = "lowered_align_count";

/// @brief Attribute for section capacity-check helper symbol.
inline constexpr char kLoweredCapacityCheckHelperAttr[] = "lowered_capacity_check_helper";

/// @brief Attribute for union-tag validation helper symbol.
inline constexpr char kLoweredUnionTagValidateHelperAttr[] = "lowered_union_tag_validate_helper";

/// @brief Attribute for serialize union-tag helper symbol.
inline constexpr char kLoweredSerUnionTagHelperAttr[] = "lowered_ser_union_tag_helper";

/// @brief Attribute for deserialize union-tag helper symbol.
inline constexpr char kLoweredDeserUnionTagHelperAttr[] = "lowered_deser_union_tag_helper";

}  // namespace llvmdsdl

#endif  // LLVMDSDL_TRANSFORMS_LOWERED_SERDES_CONTRACT_H
