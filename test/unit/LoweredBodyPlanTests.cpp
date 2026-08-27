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

#include "llvmdsdl/CodeGen/LoweredBodyPlan.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"
#include "llvmdsdl/CodeGen/SerDesHelperDescriptors.h"
#include "llvmdsdl/CodeGen/SerDesStatementPlan.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Model.h"

#include "UnitTests.h"

bool runLoweredBodyPlanTests()
{
    {
        llvmdsdl::SemanticSection section;
        section.serializationBufferSizeBits = 80;

        llvmdsdl::SemanticField payload;
        payload.name                               = "payload";
        payload.resolvedType.scalarCategory        = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        payload.resolvedType.bitLength             = 8;
        payload.resolvedType.arrayKind             = llvmdsdl::ArrayKind::VariableInclusive;
        payload.resolvedType.arrayCapacity         = 32;
        payload.resolvedType.arrayLengthPrefixBits = 8;
        section.fields.push_back(payload);

        llvmdsdl::SemanticField scalar;
        scalar.name                        = "scalar";
        scalar.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::SignedInt;
        scalar.resolvedType.bitLength      = 7;
        section.fields.push_back(scalar);

        llvmdsdl::LoweredSectionFacts facts;
        facts.capacityCheckHelper                 = "capacity_check";
        auto& payloadFacts                        = facts.fieldsByName["payload"];
        payloadFacts.stepIndex                    = 20;
        payloadFacts.arrayLengthPrefixBits        = 16;
        payloadFacts.serArrayLengthPrefixHelper   = "payload_prefix_ser";
        payloadFacts.deserArrayLengthPrefixHelper = "payload_prefix_des";
        payloadFacts.arrayLengthValidateHelper    = "payload_validate";
        auto& scalarFacts                         = facts.fieldsByName["scalar"];
        scalarFacts.stepIndex                     = 10;
        scalarFacts.serSignedHelper               = "scalar_ser";
        scalarFacts.deserSignedHelper             = "scalar_des";

        const auto serPlan =
            llvmdsdl::buildLoweredBodyPlan(section, &facts, llvmdsdl::HelperBindingDirection::Serialize);
        const auto desPlan =
            llvmdsdl::buildLoweredBodyPlan(section, &facts, llvmdsdl::HelperBindingDirection::Deserialize);

        if (!serPlan.helperBindings.capacityCheck || serPlan.helperBindings.capacityCheck->symbol != "capacity_check" ||
            serPlan.helperBindings.capacityCheck->requiredBits != 80)
        {
            std::cerr << "serialize lowered body capacity helper mismatch\n";
            return false;
        }
        if (serPlan.statements.orderedFields.size() != 2 || serPlan.statements.orderedFields[0].field == nullptr ||
            serPlan.statements.orderedFields[0].field->name != "scalar" ||
            serPlan.statements.orderedFields[1].field == nullptr ||
            serPlan.statements.orderedFields[1].field->name != "payload")
        {
            std::cerr << "serialize lowered body statement ordering mismatch\n";
            return false;
        }
        if (serPlan.helperBindings.scalarBindings.size() != 1 ||
            serPlan.helperBindings.scalarBindings[0].symbol != "scalar_ser")
        {
            std::cerr << "serialize lowered body scalar binding mismatch\n";
            return false;
        }
        if (desPlan.helperBindings.scalarBindings.size() != 1 ||
            desPlan.helperBindings.scalarBindings[0].symbol != "scalar_des")
        {
            std::cerr << "deserialize lowered body scalar binding mismatch\n";
            return false;
        }
        if (serPlan.helperBindings.arrayPrefixBindings.size() != 1 ||
            serPlan.helperBindings.arrayPrefixBindings[0].symbol != "payload_prefix_ser" ||
            serPlan.helperBindings.arrayPrefixBindings[0].bits != 16)
        {
            std::cerr << "serialize lowered body array prefix binding mismatch\n";
            return false;
        }
        if (desPlan.helperBindings.arrayPrefixBindings.size() != 1 ||
            desPlan.helperBindings.arrayPrefixBindings[0].symbol != "payload_prefix_des" ||
            desPlan.helperBindings.arrayPrefixBindings[0].bits != 16)
        {
            std::cerr << "deserialize lowered body array prefix binding mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticSection section;
        section.isUnion                     = true;
        section.serializationBufferSizeBits = 16;

        llvmdsdl::SemanticField alpha;
        alpha.name                        = "alpha";
        alpha.unionOptionIndex            = 2;
        alpha.unionTagBits                = 8;
        alpha.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        alpha.resolvedType.bitLength      = 8;
        section.fields.push_back(alpha);

        llvmdsdl::SemanticField beta;
        beta.name                        = "beta";
        beta.unionOptionIndex            = 0;
        beta.unionTagBits                = 8;
        beta.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        beta.resolvedType.bitLength      = 8;
        section.fields.push_back(beta);

        llvmdsdl::LoweredSectionFacts facts;
        facts.unionTagBits                    = 8;
        facts.unionTagValidateHelper          = "union_validate";
        facts.serUnionTagHelper               = "union_tag_ser";
        facts.deserUnionTagHelper             = "union_tag_des";
        facts.fieldsByName["alpha"].stepIndex = 20;
        facts.fieldsByName["beta"].stepIndex  = 10;

        const auto serPlan =
            llvmdsdl::buildLoweredBodyPlan(section, &facts, llvmdsdl::HelperBindingDirection::Serialize);
        const auto desPlan =
            llvmdsdl::buildLoweredBodyPlan(section, &facts, llvmdsdl::HelperBindingDirection::Deserialize);

        if (serPlan.statements.unionBranches.size() != 2 || serPlan.statements.unionBranches[0].field == nullptr ||
            serPlan.statements.unionBranches[0].field->name != "beta" ||
            serPlan.statements.unionBranches[1].field == nullptr ||
            serPlan.statements.unionBranches[1].field->name != "alpha")
        {
            std::cerr << "lowered body union branch ordering mismatch\n";
            return false;
        }
        if (!serPlan.helperBindings.unionTagValidate ||
            serPlan.helperBindings.unionTagValidate->symbol != "union_validate" ||
            serPlan.helperBindings.unionTagValidate->allowedTags.size() != 2 ||
            serPlan.helperBindings.unionTagValidate->allowedTags[0] != 0 ||
            serPlan.helperBindings.unionTagValidate->allowedTags[1] != 2)
        {
            std::cerr << "lowered body union tag validate binding mismatch\n";
            return false;
        }
        if (!serPlan.helperBindings.unionTagMask || serPlan.helperBindings.unionTagMask->symbol != "union_tag_ser" ||
            serPlan.helperBindings.unionTagMask->bits != 8)
        {
            std::cerr << "serialize lowered body union tag mask mismatch\n";
            return false;
        }
        if (!desPlan.helperBindings.unionTagMask || desPlan.helperBindings.unionTagMask->symbol != "union_tag_des" ||
            desPlan.helperBindings.unionTagMask->bits != 8)
        {
            std::cerr << "deserialize lowered body union tag mask mismatch\n";
            return false;
        }
    }

    return true;
}
