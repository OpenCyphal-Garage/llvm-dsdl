//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <optional>
#include <string>

#include "llvmdsdl/CodeGen/HelperSymbolResolver.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"
#include "llvmdsdl/CodeGen/SerDesHelperDescriptors.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Model.h"

#include "UnitTests.h"

bool runHelperSymbolResolverTests()
{
    {
        if (!llvmdsdl::resolveSectionCapacityCheckHelperSymbol(nullptr).empty() ||
            !llvmdsdl::resolveSectionUnionTagValidateHelperSymbol(nullptr).empty() ||
            !llvmdsdl::resolveSectionUnionTagMaskHelperSymbol(nullptr, llvmdsdl::HelperBindingDirection::Serialize)
                 .empty() ||
            !llvmdsdl::resolveSectionUnionTagMaskHelperSymbol(nullptr, llvmdsdl::HelperBindingDirection::Deserialize)
                 .empty())
        {
            std::cerr << "null section helper resolution should be empty\n";
            return false;
        }
    }

    {
        llvmdsdl::LoweredSectionFacts facts;
        facts.capacityCheckHelper    = "cap";
        facts.unionTagValidateHelper = "tag_validate";
        facts.serUnionTagHelper      = "tag_ser";
        facts.deserUnionTagHelper    = "tag_des";

        if (llvmdsdl::resolveSectionCapacityCheckHelperSymbol(&facts) != "cap" ||
            llvmdsdl::resolveSectionUnionTagValidateHelperSymbol(&facts) != "tag_validate" ||
            llvmdsdl::resolveSectionUnionTagMaskHelperSymbol(&facts, llvmdsdl::HelperBindingDirection::Serialize) !=
                "tag_ser" ||
            llvmdsdl::resolveSectionUnionTagMaskHelperSymbol(&facts, llvmdsdl::HelperBindingDirection::Deserialize) !=
                "tag_des")
        {
            std::cerr << "section helper symbol resolution mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticFieldType type;
        type.scalarCategory = llvmdsdl::SemanticScalarCategory::SignedInt;
        type.bitLength      = 5;
        type.castMode       = llvmdsdl::CastMode::Saturated;

        llvmdsdl::LoweredFieldFacts facts;
        facts.serSignedHelper   = "signed_ser";
        facts.deserSignedHelper = "signed_des";

        if (llvmdsdl::resolveScalarHelperSymbol(type, &facts, llvmdsdl::HelperBindingDirection::Serialize) !=
                "signed_ser" ||
            llvmdsdl::resolveScalarHelperSymbol(type, &facts, llvmdsdl::HelperBindingDirection::Deserialize) !=
                "signed_des")
        {
            std::cerr << "scalar helper symbol resolution mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticFieldType type;
        type.scalarCategory = llvmdsdl::SemanticScalarCategory::SignedInt;
        type.bitLength      = 5;
        type.castMode       = llvmdsdl::CastMode::Saturated;
        if (!llvmdsdl::resolveScalarHelperSymbol(type, nullptr, llvmdsdl::HelperBindingDirection::Serialize).empty() ||
            !llvmdsdl::resolveScalarHelperSymbol(type, nullptr, llvmdsdl::HelperBindingDirection::Deserialize).empty())
        {
            std::cerr << "scalar helper resolution without lowered facts should be empty\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticFieldType type;
        type.arrayKind             = llvmdsdl::ArrayKind::VariableInclusive;
        type.arrayCapacity         = 32;
        type.arrayLengthPrefixBits = 8;

        llvmdsdl::LoweredFieldFacts facts;
        facts.serArrayLengthPrefixHelper   = "prefix_ser";
        facts.deserArrayLengthPrefixHelper = "prefix_des";
        facts.arrayLengthValidateHelper    = "len_validate";

        const auto ser =
            llvmdsdl::resolveArrayLengthHelperDescriptor(type, &facts, 8U, llvmdsdl::HelperBindingDirection::Serialize);
        const auto des = llvmdsdl::resolveArrayLengthHelperDescriptor(type,
                                                                      &facts,
                                                                      8U,
                                                                      llvmdsdl::HelperBindingDirection::Deserialize);
        if (!ser || !des || ser->prefixSymbol != "prefix_ser" || des->prefixSymbol != "prefix_des" ||
            ser->validateSymbol != "len_validate" || des->capacity != 32)
        {
            std::cerr << "array helper descriptor resolution mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticFieldType type;
        type.arrayKind             = llvmdsdl::ArrayKind::Fixed;
        type.arrayCapacity         = 32;
        type.arrayLengthPrefixBits = 8;
        llvmdsdl::LoweredFieldFacts facts;
        facts.serArrayLengthPrefixHelper = "prefix_ser";
        facts.arrayLengthValidateHelper  = "len_validate";
        if (llvmdsdl::resolveArrayLengthHelperDescriptor(type, &facts, 8U, llvmdsdl::HelperBindingDirection::Serialize)
                .has_value())
        {
            std::cerr << "fixed array should not have a length helper descriptor\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticFieldType type;
        type.scalarCategory  = llvmdsdl::SemanticScalarCategory::Composite;
        type.compositeSealed = false;
        llvmdsdl::LoweredFieldFacts facts;
        facts.delimiterValidateHelper = "delim_validate";
        if (llvmdsdl::resolveDelimiterValidateHelperSymbol(type, &facts) != "delim_validate")
        {
            std::cerr << "delimiter helper symbol resolution mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticFieldType type;
        type.scalarCategory  = llvmdsdl::SemanticScalarCategory::Composite;
        type.compositeSealed = true;
        llvmdsdl::LoweredFieldFacts facts;
        facts.delimiterValidateHelper = "delim_validate";
        if (!llvmdsdl::resolveDelimiterValidateHelperSymbol(type, &facts).empty())
        {
            std::cerr << "sealed composite should not use delimiter helper symbol\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticFieldType type;
        type.scalarCategory  = llvmdsdl::SemanticScalarCategory::Composite;
        type.compositeSealed = false;
        if (!llvmdsdl::resolveDelimiterValidateHelperSymbol(type, nullptr).empty())
        {
            std::cerr << "delimiter helper without lowered facts should be empty\n";
            return false;
        }
    }

    return true;
}
