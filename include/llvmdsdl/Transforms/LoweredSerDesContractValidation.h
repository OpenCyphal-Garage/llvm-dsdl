//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared validation helpers for lowered-serdes contract plans.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_TRANSFORMS_LOWERED_SERDES_CONTRACT_VALIDATION_H
#define LLVMDSDL_TRANSFORMS_LOWERED_SERDES_CONTRACT_VALIDATION_H

#include <cstdint>
#include <optional>
#include <string>

namespace mlir
{
class ModuleOp;
class Operation;
}  // namespace mlir

namespace llvmdsdl
{

/// @brief One lowered-plan contract violation.
struct LoweredPlanContractViolation final
{
    /// @brief Operation that triggered the violation.
    mlir::Operation* operation{nullptr};

    /// @brief Deterministic violation text.
    std::string message;
};

/// @brief Classification of lowered-contract envelope violations.
enum class LoweredContractEnvelopeViolationKind
{
    MissingVersion,
    UnsupportedMajorVersion,
    ProducerMismatch,
};

/// @brief One lowered-contract envelope violation.
struct LoweredContractEnvelopeViolation final
{
    /// @brief Violation kind.
    LoweredContractEnvelopeViolationKind kind{LoweredContractEnvelopeViolationKind::MissingVersion};

    /// @brief Encoded version when `kind == UnsupportedMajorVersion`.
    std::int64_t encodedVersion{0};
};

/// @brief Validates lowered-contract version/producer envelope attributes.
/// @param[in] operation Operation expected to carry lowered-contract envelope attributes.
/// @return Violation details on failure; `std::nullopt` on success.
std::optional<LoweredContractEnvelopeViolation> findLoweredContractEnvelopeViolation(mlir::Operation* operation);

/// @brief Validates lowered metadata and helper references for a serialization plan.
/// @param[in] module Module containing helper symbols.
/// @param[in] operation The `dsdl.serialization_plan` to validate.
/// @return Violation details on failure; `std::nullopt` on success.
std::optional<LoweredPlanContractViolation> findLoweredPlanContractViolation(mlir::ModuleOp   module,
                                                                             mlir::Operation* operation);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_TRANSFORMS_LOWERED_SERDES_CONTRACT_VALIDATION_H
