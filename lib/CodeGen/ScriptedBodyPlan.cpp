//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Builds shared scripted-backend body plans from lowered helper metadata.
///
/// The resulting plan centralizes field helper lookup and section helper
/// wiring for TypeScript/Python emitters.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/ScriptedBodyPlan.h"
#include "llvmdsdl/CodeGen/RuntimeHelperBindings.h"
#include "llvmdsdl/CodeGen/RuntimeLoweredPlan.h"

#include <utility>

namespace llvmdsdl
{

ScriptedSectionBodyPlan buildScriptedSectionBodyPlan(const SemanticSection&           section,
                                                     const RuntimeSectionPlan&        runtimePlan,
                                                     const LoweredSectionFacts*       sectionFacts,
                                                     const RuntimeHelperNameResolver& helperNameResolver)
{
    ScriptedSectionBodyPlan out;
    out.sectionHelpers = resolveRuntimeSectionHelperNames(sectionFacts, helperNameResolver);
    out.fields.reserve(runtimePlan.fields.size());
    for (const auto& field : runtimePlan.fields)
    {
        ScriptedFieldBodyPlan fieldPlan;
        fieldPlan.field               = field;
        fieldPlan.arrayPrefixOverride = runtimeArrayPrefixOverride(fieldPlan.field);
        fieldPlan.helpers             = resolveRuntimeFieldHelperNames(section,
                                                                       sectionFacts,
                                                                       fieldPlan.field,
                                                                       fieldPlan.arrayPrefixOverride,
                                                                       helperNameResolver);
        out.fields.push_back(std::move(fieldPlan));
    }
    return out;
}

}  // namespace llvmdsdl
