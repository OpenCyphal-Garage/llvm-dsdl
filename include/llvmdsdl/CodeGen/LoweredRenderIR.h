//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Language-agnostic render IR declarations shared by backend emitters.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_LOWERED_RENDER_IR_H
#define LLVMDSDL_CODEGEN_LOWERED_RENDER_IR_H

#include <cstdint>
#include <functional>
#include <vector>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"
#include "llvmdsdl/CodeGen/SerDesStatementPlan.h"
#include "llvmdsdl/CodeGen/WireOperationContract.h"

namespace llvmdsdl
{
struct LoweredSectionFacts;
struct SemanticSection;

/// @file
/// @brief Language-agnostic lowered render IR used by backend emitters.

/// @brief Kind of render step represented in @ref LoweredRenderStep.
enum class LoweredRenderStepKind
{

    /// @brief Regular field step.
    Field,

    /// @brief Padding/void advancement step.
    Padding,

    /// @brief Union dispatch step with branch payloads.
    UnionDispatch,
};

/// @brief Single render-IR step for serialise/deserialize body generation.
struct LoweredRenderStep final
{
    /// @brief Step kind.
    LoweredRenderStepKind kind{LoweredRenderStepKind::Field};

    /// @brief Field payload for `Field` and `Padding` kinds.
    PlannedFieldStep fieldStep;

    /// @brief Branch payload for `UnionDispatch` kind.
    std::vector<PlannedFieldStep> unionBranches;
};

/// @brief Complete render-IR body plus helper bindings.
struct LoweredBodyRenderIR final
{
    /// @brief Wire-operation contract version consumed by this render plan.
    std::int64_t contractVersion{kWireOperationContractVersion};

    /// @brief Ordered render steps.
    std::vector<LoweredRenderStep> steps;

    /// @brief Helper bindings required by these steps.
    SectionHelperBindingPlan helperBindings;
};

/// @brief Callback bundle for traversing lowered render steps.
struct LoweredRenderStepCallbacks final
{
    /// @brief Called for union-dispatch steps.
    std::function<void(const std::vector<PlannedFieldStep>&)> onUnionDispatch;

    /// @brief Called for non-padding field steps.
    std::function<void(const PlannedFieldStep&)> onField;

    /// @brief Called for padding steps.
    std::function<void(const PlannedFieldStep&)> onPadding;
};

/// @brief Builds render IR from semantic section and lowered metadata.
/// @param[in] section Semantic section.
/// @param[in] sectionFacts Lowered section facts.
/// @param[in] direction Helper-binding direction.
/// @return Render-IR bundle with deterministic step order.
LoweredBodyRenderIR buildLoweredBodyRenderIR(const SemanticSection&     section,
                                             const LoweredSectionFacts* sectionFacts,
                                             HelperBindingDirection     direction);

/// @brief Validates wire-operation contract version for lowered render IR.
/// @param[in] renderIR Render IR produced/consumed by shared planners.
/// @param[in] consumerLabel Deterministic consumer label for diagnostics.
/// @return Success on supported major version; error otherwise.
llvm::Error validateLoweredBodyRenderIRContract(const LoweredBodyRenderIR& renderIR, llvm::StringRef consumerLabel);

/// @brief Traverses lowered render steps with shared dispatch semantics.
/// @param[in] renderIR Lowered render body.
/// @param[in] callbacks Callback bundle invoked per step kind.
void forEachLoweredRenderStep(const LoweredBodyRenderIR& renderIR, const LoweredRenderStepCallbacks& callbacks);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_LOWERED_RENDER_IR_H
