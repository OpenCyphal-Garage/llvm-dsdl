//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Builds lowered body plans from semantic sections and lowering facts.
///
/// The planner produces a compact structure that binders and emitters consume to drive section-level serdes generation.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/LoweredBodyPlan.h"
#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"
#include "llvmdsdl/CodeGen/SerDesStatementPlan.h"

namespace llvmdsdl
{
struct LoweredSectionFacts;
struct SemanticSection;

LoweredBodyPlan buildLoweredBodyPlan(const SemanticSection&       section,
                                     const LoweredSectionFacts*   sectionFacts,
                                     const HelperBindingDirection direction)
{
    LoweredBodyPlan out;
    out.statements     = buildSectionStatementPlan(section, sectionFacts);
    out.helperBindings = buildSectionHelperBindingPlan(section, sectionFacts, direction);
    return out;
}

}  // namespace llvmdsdl
