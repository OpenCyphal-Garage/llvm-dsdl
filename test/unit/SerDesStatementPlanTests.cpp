//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "llvmdsdl/CodeGen/SerDesStatementPlan.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Model.h"

#include "UnitTests.h"

bool runSerDesStatementPlanTests()
{
    {
        llvmdsdl::SemanticSection section;

        llvmdsdl::SemanticField header;
        header.name                        = "header";
        header.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        header.resolvedType.bitLength      = 8;
        section.fields.push_back(header);

        llvmdsdl::SemanticField padding;
        padding.name                        = "_pad0";
        padding.isPadding                   = true;
        padding.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::Void;
        padding.resolvedType.bitLength      = 3;
        section.fields.push_back(padding);

        llvmdsdl::SemanticField payload;
        payload.name                               = "payload";
        payload.resolvedType.scalarCategory        = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        payload.resolvedType.bitLength             = 8;
        payload.resolvedType.arrayKind             = llvmdsdl::ArrayKind::VariableInclusive;
        payload.resolvedType.arrayCapacity         = 16;
        payload.resolvedType.arrayLengthPrefixBits = 8;
        section.fields.push_back(payload);

        llvmdsdl::LoweredSectionFacts facts;
        auto&                         payloadFacts = facts.fieldsByName["payload"];
        payloadFacts.arrayLengthPrefixBits         = 16;
        payloadFacts.serArrayLengthPrefixHelper    = "payload_prefix_ser";

        const auto plan = llvmdsdl::buildSectionStatementPlan(section, &facts);
        if (plan.orderedFields.size() != 3)
        {
            std::cerr << "ordered field count mismatch\n";
            return false;
        }
        if (plan.unionBranches.size() != 2)
        {
            std::cerr << "union branch count mismatch\n";
            return false;
        }
        if (plan.orderedFields[0].field == nullptr || plan.orderedFields[0].field->name != "header")
        {
            std::cerr << "ordered field[0] mismatch\n";
            return false;
        }
        if (plan.orderedFields[1].field == nullptr || !plan.orderedFields[1].field->isPadding)
        {
            std::cerr << "ordered field[1] should be padding\n";
            return false;
        }
        if (plan.orderedFields[2].field == nullptr || plan.orderedFields[2].field->name != "payload" ||
            !plan.orderedFields[2].arrayLengthPrefixBits || *plan.orderedFields[2].arrayLengthPrefixBits != 16)
        {
            std::cerr << "ordered field[2] array metadata mismatch\n";
            return false;
        }
        if (plan.orderedFields[2].fieldFacts == nullptr ||
            plan.orderedFields[2].fieldFacts->serArrayLengthPrefixHelper != "payload_prefix_ser")
        {
            std::cerr << "ordered field[2] lowered facts mismatch\n";
            return false;
        }
        if (plan.unionBranches[0].field == nullptr || plan.unionBranches[0].field->name != "header" ||
            plan.unionBranches[1].field == nullptr || plan.unionBranches[1].field->name != "payload")
        {
            std::cerr << "union branch ordering mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticSection section;
        section.isUnion = true;

        llvmdsdl::SemanticField option;
        option.name                        = "option";
        option.unionOptionIndex            = 1;
        option.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::SignedInt;
        option.resolvedType.bitLength      = 8;
        section.fields.push_back(option);

        const auto plan = llvmdsdl::buildSectionStatementPlan(section, nullptr);
        if (plan.orderedFields.size() != 1 || plan.unionBranches.size() != 1)
        {
            std::cerr << "union-only plan counts mismatch\n";
            return false;
        }
        if (plan.orderedFields.front().arrayLengthPrefixBits || plan.orderedFields.front().fieldFacts != nullptr)
        {
            std::cerr << "union-only plan should not have lowered facts\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticSection section;

        llvmdsdl::SemanticField first;
        first.name                        = "first";
        first.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        first.resolvedType.bitLength      = 8;
        section.fields.push_back(first);

        llvmdsdl::SemanticField second;
        second.name                        = "second";
        second.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        second.resolvedType.bitLength      = 8;
        section.fields.push_back(second);

        llvmdsdl::LoweredSectionFacts facts;
        facts.fieldsByName["first"].stepIndex  = 20;
        facts.fieldsByName["second"].stepIndex = 10;

        const auto plan = llvmdsdl::buildSectionStatementPlan(section, &facts);
        if (plan.orderedFields.size() != 2 || plan.unionBranches.size() != 2)
        {
            std::cerr << "step-index ordering plan counts mismatch\n";
            return false;
        }
        if (plan.orderedFields[0].field == nullptr || plan.orderedFields[0].field->name != "second" ||
            plan.orderedFields[1].field == nullptr || plan.orderedFields[1].field->name != "first")
        {
            std::cerr << "step-index ordering mismatch\n";
            return false;
        }
        if (plan.unionBranches[0].field == nullptr || plan.unionBranches[0].field->name != "second" ||
            plan.unionBranches[1].field == nullptr || plan.unionBranches[1].field->name != "first")
        {
            std::cerr << "union branch step-index ordering mismatch\n";
            return false;
        }
    }

    return true;
}
