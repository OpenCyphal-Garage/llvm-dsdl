//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Builds ordered serialization and deserialization statement plans.
///
/// The planner establishes a deterministic field traversal order for both linear and union sections.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/SerDesStatementPlan.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/Semantics/Model.h"

namespace llvmdsdl
{

SectionStatementPlan buildSectionStatementPlan(const SemanticSection& section, const LoweredSectionFacts* sectionFacts)
{
    SectionStatementPlan out;
    struct OrderedStep final
    {
        PlannedFieldStep step;
        std::size_t      sequence{0};
        std::int64_t     orderKey{0};
    };
    std::vector<OrderedStep> ordered;
    ordered.reserve(section.fields.size());

    std::size_t sequence = 0;
    for (const auto& field : section.fields)
    {
        const auto* const      fieldFacts = findLoweredFieldFacts(sectionFacts, field.name);
        const auto             prefixBits = loweredFieldArrayPrefixBits(sectionFacts, field.name);
        PlannedFieldStep const step{&field, prefixBits, fieldFacts};
        const auto orderKey = (fieldFacts && fieldFacts->stepIndex) ? *fieldFacts->stepIndex
                                                                    : (std::numeric_limits<std::int64_t>::max() / 2);
        ordered.push_back(OrderedStep{step, sequence++, orderKey});
    }

    std::ranges::sort(ordered, [](const OrderedStep& lhs, const OrderedStep& rhs) {
        if (lhs.orderKey != rhs.orderKey)
        {
            return lhs.orderKey < rhs.orderKey;
        }
        return lhs.sequence < rhs.sequence;
    });

    out.orderedFields.reserve(ordered.size());
    out.unionBranches.reserve(ordered.size());
    for (const auto& entry : ordered)
    {
        out.orderedFields.push_back(entry.step);
        if (!entry.step.field->isPadding)
        {
            out.unionBranches.push_back(entry.step);
        }
    }

    return out;
}

}  // namespace llvmdsdl
