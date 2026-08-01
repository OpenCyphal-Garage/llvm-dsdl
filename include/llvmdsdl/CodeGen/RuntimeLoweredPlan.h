//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Backend-neutral runtime lowering plans derived from shared lowered metadata.
///
/// These declarations provide a language-agnostic runtime field/body planning
/// surface used by scripted backends (currently TypeScript and Python).
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_RUNTIME_LOWERED_PLAN_H
#define LLVMDSDL_CODEGEN_RUNTIME_LOWERED_PLAN_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/CodeGen/WireOperationContract.h"

namespace llvmdsdl
{
struct LoweredSectionFacts;

/// @brief Planned field step in lowered execution order.
struct RuntimeOrderedFieldStep final
{
    /// @brief Semantic field reference.
    const SemanticField* field{nullptr};

    /// @brief Optional lowered array-prefix width override.
    std::optional<std::uint32_t> arrayLengthPrefixBits;
};

/// @brief Runtime field category.
enum class RuntimeFieldKind
{
    /// @brief Explicit padding field.
    Padding,

    /// @brief Boolean scalar.
    Bool,

    /// @brief Unsigned/byte/utf8 scalar.
    Unsigned,

    /// @brief Signed scalar.
    Signed,

    /// @brief Floating-point scalar.
    Float,

    /// @brief Nested composite field.
    Composite,
};

/// @brief Runtime array category.
enum class RuntimeArrayKind
{
    /// @brief Scalar/non-array field.
    None,

    /// @brief Fixed-length array field.
    Fixed,

    /// @brief Variable-length array field.
    Variable,
};

/// @brief Runtime plan entry for one field.
struct RuntimeFieldPlan final
{
    /// @brief Original semantic field identifier used for lowered-facts lookup.
    std::string semanticFieldName;

    /// @brief Backend-neutral field identifier (semantic name by default).
    ///
    /// Emitters should project this identifier through a language-specific naming
    /// policy before rendering source code.
    std::string fieldName;

    /// @brief Runtime field kind.
    RuntimeFieldKind kind{RuntimeFieldKind::Unsigned};

    /// @brief Cast behaviour for numeric write paths.
    CastMode castMode{CastMode::Saturated};

    /// @brief Scalar/composite bit length for one element.
    std::int64_t bitLength{0};

    /// @brief Alignment in bits.
    std::int64_t alignmentBits{1};

    /// @brief True when bigint runtime APIs are required.
    bool useBigInt{false};

    /// @brief Optional composite type reference.
    std::optional<SemanticTypeRef> compositeType;

    /// @brief Composite sealed flag.
    bool compositeSealed{true};

    /// @brief Maximum payload bits for delimited composites.
    std::int64_t compositePayloadMaxBits{0};

    /// @brief Union option index for union sections.
    std::uint32_t unionOptionIndex{0};

    /// @brief Array category.
    RuntimeArrayKind arrayKind{RuntimeArrayKind::None};

    /// @brief Array capacity for fixed/variable arrays.
    std::int64_t arrayCapacity{0};

    /// @brief Prefix width for variable arrays.
    std::int64_t arrayLengthPrefixBits{0};
};

/// @brief Runtime plan for one semantic section.
struct RuntimeSectionPlan final
{
    /// @brief Wire-operation contract version consumed by this runtime plan.
    std::int64_t contractVersion{kWireOperationContractVersion};

    /// @brief True for union sections.
    bool isUnion{false};

    /// @brief Union tag bit width for union sections.
    std::int64_t unionTagBits{0};

    /// @brief Ordered runtime field steps.
    std::vector<RuntimeFieldPlan> fields;

    /// @brief Maximum serialized bit length used for allocation.
    std::int64_t maxBits{0};
};

/// @brief Builds deterministic runtime field ordering from lowered facts.
/// @param[in] section Semantic section to plan.
/// @param[in] sectionFacts Lowered section facts for the same section.
/// @return Ordered field steps or a contract-validation error.
llvm::Expected<std::vector<RuntimeOrderedFieldStep>> buildRuntimeOrderedFieldSteps(
    const SemanticSection&     section,
    const LoweredSectionFacts* sectionFacts);

/// @brief Builds runtime section plan from lowered facts.
/// @param[in] section Semantic section to plan.
/// @param[in] sectionFacts Lowered section facts for the same section.
/// @return Runtime section plan or a contract-validation error.
llvm::Expected<RuntimeSectionPlan> buildRuntimeSectionPlan(const SemanticSection&     section,
                                                           const LoweredSectionFacts* sectionFacts);

/// @brief Validates wire-operation contract version for runtime section plans.
/// @param[in] plan Runtime section plan.
/// @param[in] consumerLabel Deterministic consumer label for diagnostics.
/// @return Success on supported major version; error otherwise.
llvm::Error validateRuntimeSectionPlanContract(const RuntimeSectionPlan& plan, llvm::StringRef consumerLabel);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_RUNTIME_LOWERED_PLAN_H
