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

#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/CodeGen/SerDesHelperDescriptors.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Model.h"

#include "UnitTests.h"

bool runSectionHelperBindingPlanTests()
{
    {
        llvmdsdl::SemanticSection section;
        section.serializationBufferSizeBits = 24;

        llvmdsdl::SemanticField scalarField;
        scalarField.name                        = "x";
        scalarField.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::SignedInt;
        scalarField.resolvedType.bitLength      = 7;
        scalarField.resolvedType.arrayKind      = llvmdsdl::ArrayKind::None;
        section.fields.push_back(scalarField);

        llvmdsdl::SemanticField arrayField;
        arrayField.name                               = "arr";
        arrayField.resolvedType.scalarCategory        = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        arrayField.resolvedType.bitLength             = 8;
        arrayField.resolvedType.arrayKind             = llvmdsdl::ArrayKind::VariableInclusive;
        arrayField.resolvedType.arrayCapacity         = 5;
        arrayField.resolvedType.arrayLengthPrefixBits = 8;
        section.fields.push_back(arrayField);

        llvmdsdl::SemanticField compositeField;
        compositeField.name                         = "c";
        compositeField.resolvedType.scalarCategory  = llvmdsdl::SemanticScalarCategory::Composite;
        compositeField.resolvedType.compositeSealed = false;
        section.fields.push_back(compositeField);

        llvmdsdl::LoweredSectionFacts facts;
        facts.capacityCheckHelper               = "cap";
        auto& scalarFacts                       = facts.fieldsByName["x"];
        scalarFacts.serSignedHelper             = "signed_ser";
        scalarFacts.deserSignedHelper           = "signed_des";
        auto& arrayFacts                        = facts.fieldsByName["arr"];
        arrayFacts.arrayLengthPrefixBits        = 8;
        arrayFacts.serArrayLengthPrefixHelper   = "arr_prefix_ser";
        arrayFacts.deserArrayLengthPrefixHelper = "arr_prefix_des";
        arrayFacts.arrayLengthValidateHelper    = "arr_validate";
        auto& compositeFacts                    = facts.fieldsByName["c"];
        compositeFacts.delimiterValidateHelper  = "delimiter_validate";

        const auto serPlan =
            llvmdsdl::buildSectionHelperBindingPlan(section, &facts, llvmdsdl::HelperBindingDirection::Serialize);
        const auto desPlan =
            llvmdsdl::buildSectionHelperBindingPlan(section, &facts, llvmdsdl::HelperBindingDirection::Deserialize);

        if (!serPlan.capacityCheck || serPlan.capacityCheck->symbol != "cap" ||
            serPlan.capacityCheck->requiredBits != 24)
        {
            std::cerr << "serialize plan capacity check mismatch\n";
            return false;
        }
        if (serPlan.scalarBindings.size() != 1 || serPlan.scalarBindings.front().symbol != "signed_ser")
        {
            std::cerr << "serialize plan scalar helper mismatch\n";
            return false;
        }
        if (desPlan.scalarBindings.size() != 1 || desPlan.scalarBindings.front().symbol != "signed_des")
        {
            std::cerr << "deserialize plan scalar helper mismatch\n";
            return false;
        }
        if (serPlan.arrayPrefixBindings.size() != 1 || serPlan.arrayPrefixBindings.front().symbol != "arr_prefix_ser" ||
            serPlan.arrayPrefixBindings.front().bits != 8)
        {
            std::cerr << "serialize plan array prefix helper mismatch\n";
            return false;
        }
        if (desPlan.arrayPrefixBindings.size() != 1 || desPlan.arrayPrefixBindings.front().symbol != "arr_prefix_des" ||
            desPlan.arrayPrefixBindings.front().bits != 8)
        {
            std::cerr << "deserialize plan array prefix helper mismatch\n";
            return false;
        }
        if (serPlan.arrayValidateBindings.size() != 1 ||
            serPlan.arrayValidateBindings.front().symbol != "arr_validate" ||
            serPlan.arrayValidateBindings.front().capacity != 5)
        {
            std::cerr << "serialize plan array validate helper mismatch\n";
            return false;
        }
        if (serPlan.delimiterValidateBindings.size() != 1 ||
            serPlan.delimiterValidateBindings.front().symbol != "delimiter_validate")
        {
            std::cerr << "serialize plan delimiter helper mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticSection section;
        section.isUnion                     = true;
        section.serializationBufferSizeBits = 16;

        llvmdsdl::SemanticField a;
        a.name                        = "a";
        a.unionOptionIndex            = 2;
        a.unionTagBits                = 8;
        a.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        a.resolvedType.bitLength      = 8;
        section.fields.push_back(a);

        llvmdsdl::SemanticField b;
        b.name                        = "b";
        b.unionOptionIndex            = 0;
        b.unionTagBits                = 8;
        b.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        b.resolvedType.bitLength      = 8;
        section.fields.push_back(b);

        llvmdsdl::LoweredSectionFacts facts;
        facts.capacityCheckHelper    = "cap_u";
        facts.unionTagValidateHelper = "union_validate";
        facts.serUnionTagHelper      = "union_tag_ser";
        facts.deserUnionTagHelper    = "union_tag_des";
        facts.unionTagBits           = 8;

        const auto serPlan =
            llvmdsdl::buildSectionHelperBindingPlan(section, &facts, llvmdsdl::HelperBindingDirection::Serialize);
        const auto desPlan =
            llvmdsdl::buildSectionHelperBindingPlan(section, &facts, llvmdsdl::HelperBindingDirection::Deserialize);

        if (!serPlan.unionTagValidate || serPlan.unionTagValidate->symbol != "union_validate" ||
            serPlan.unionTagValidate->allowedTags.size() != 2 || serPlan.unionTagValidate->allowedTags[0] != 0 ||
            serPlan.unionTagValidate->allowedTags[1] != 2)
        {
            std::cerr << "union tag validate helper mismatch\n";
            return false;
        }
        if (!serPlan.unionTagMask || serPlan.unionTagMask->symbol != "union_tag_ser" || serPlan.unionTagMask->bits != 8)
        {
            std::cerr << "serialize union tag mask helper mismatch\n";
            return false;
        }
        if (!desPlan.unionTagMask || desPlan.unionTagMask->symbol != "union_tag_des" || desPlan.unionTagMask->bits != 8)
        {
            std::cerr << "deserialize union tag mask helper mismatch\n";
            return false;
        }
    }

    return true;
}
