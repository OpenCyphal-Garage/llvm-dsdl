//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Composite lowered body-plan declarations combining statements and helper bindings.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_LOWERED_BODY_PLAN_H
#define LLVMDSDL_CODEGEN_LOWERED_BODY_PLAN_H

#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"
#include "llvmdsdl/CodeGen/SerDesStatementPlan.h"

namespace llvmdsdl
{
struct LoweredSectionFacts;
struct SemanticSection;

/// @file
/// @brief Composite lowered body plan combining statements and helper bindings.

/// @brief Complete lowered plan required to emit one section body.
struct LoweredBodyPlan final
{
    /// @brief Ordered field/union statement plan.
    SectionStatementPlan statements;

    /// @brief Helper binding plan associated with statements.
    SectionHelperBindingPlan helperBindings;
};

/// @brief Builds combined lowered body plan.
/// @param[in] section Semantic section.
/// @param[in] sectionFacts Lowered section facts.
/// @param[in] direction Serialise/deserialize direction.
/// @return Lowered body plan.
LoweredBodyPlan buildLoweredBodyPlan(const SemanticSection&     section,
                                     const LoweredSectionFacts* sectionFacts,
                                     HelperBindingDirection     direction);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_LOWERED_BODY_PLAN_H
