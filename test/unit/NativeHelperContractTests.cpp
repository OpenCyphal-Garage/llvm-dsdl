//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <string>

#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/CodeGen/NativeHelperContract.h"
#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"
#include "llvmdsdl/Semantics/Model.h"

#include "UnitTests.h"

bool runNativeHelperContractTests()
{
    {
        llvmdsdl::SemanticSection const    section;
        llvmdsdl::SectionHelperBindingPlan helpers;
        helpers.capacityCheck = llvmdsdl::CapacityCheckHelperDescriptor{"helper_capacity", 32};

        std::string missing;
        if (!llvmdsdl::validateNativeSectionHelperContract(section,
                                                           nullptr,
                                                           llvmdsdl::HelperBindingDirection::Serialize,
                                                           helpers,
                                                           &missing))
        {
            std::cerr << "native helper contract should accept non-union section with capacity helper\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticSection const          section;
        llvmdsdl::SectionHelperBindingPlan const helpers;

        std::string missing;
        if (llvmdsdl::validateNativeSectionHelperContract(section,
                                                          nullptr,
                                                          llvmdsdl::HelperBindingDirection::Serialize,
                                                          helpers,
                                                          &missing))
        {
            std::cerr << "native helper contract should reject missing capacity helper\n";
            return false;
        }
        if (missing != "capacity-check")
        {
            std::cerr << "native helper contract missing-requirement mismatch for capacity helper\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticSection          section;
        llvmdsdl::SectionHelperBindingPlan helpers;
        section.isUnion          = true;
        helpers.capacityCheck    = llvmdsdl::CapacityCheckHelperDescriptor{"helper_capacity", 32};
        helpers.unionTagValidate = llvmdsdl::UnionTagValidateHelperDescriptor{"helper_union_validate", {0, 1}};
        helpers.unionTagMask     = llvmdsdl::UnionTagMaskBindingDescriptor{"helper_union_mask", 8};

        std::string missing;
        if (!llvmdsdl::validateNativeSectionHelperContract(section,
                                                           nullptr,
                                                           llvmdsdl::HelperBindingDirection::Serialize,
                                                           helpers,
                                                           &missing))
        {
            std::cerr << "native helper contract should accept complete union helper set\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticSection          section;
        llvmdsdl::SectionHelperBindingPlan helpers;
        section.isUnion       = true;
        helpers.capacityCheck = llvmdsdl::CapacityCheckHelperDescriptor{"helper_capacity", 32};
        helpers.unionTagMask  = llvmdsdl::UnionTagMaskBindingDescriptor{"helper_union_mask", 8};

        std::string missing;
        if (llvmdsdl::validateNativeSectionHelperContract(section,
                                                          nullptr,
                                                          llvmdsdl::HelperBindingDirection::Serialize,
                                                          helpers,
                                                          &missing))
        {
            std::cerr << "native helper contract should reject missing union-tag validate helper\n";
            return false;
        }
        if (missing != "union-tag-validate")
        {
            std::cerr << "native helper contract missing-requirement mismatch for union-tag validate helper\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticSection          section;
        llvmdsdl::SectionHelperBindingPlan helpers;
        section.isUnion          = true;
        helpers.capacityCheck    = llvmdsdl::CapacityCheckHelperDescriptor{"helper_capacity", 32};
        helpers.unionTagValidate = llvmdsdl::UnionTagValidateHelperDescriptor{"helper_union_validate", {0, 1}};

        std::string missing;
        if (llvmdsdl::validateNativeSectionHelperContract(section,
                                                          nullptr,
                                                          llvmdsdl::HelperBindingDirection::Serialize,
                                                          helpers,
                                                          &missing))
        {
            std::cerr << "native helper contract should reject missing union-tag mask helper\n";
            return false;
        }
        if (missing != "union-tag-mask")
        {
            std::cerr << "native helper contract missing-requirement mismatch for union-tag mask helper\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticSection section;
        llvmdsdl::SemanticField   field;
        field.name                        = "value";
        field.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        field.resolvedType.bitLength      = 8;
        section.fields.push_back(field);

        llvmdsdl::LoweredSectionFacts sectionFacts;
        sectionFacts.fieldsByName["value"].serUnsignedHelper = "helper_ser_u8";

        llvmdsdl::SectionHelperBindingPlan helpers;
        helpers.capacityCheck = llvmdsdl::CapacityCheckHelperDescriptor{"helper_capacity", 8};

        std::string missing;
        if (llvmdsdl::validateNativeSectionHelperContract(section,
                                                          &sectionFacts,
                                                          llvmdsdl::HelperBindingDirection::Serialize,
                                                          helpers,
                                                          &missing))
        {
            std::cerr << "native helper contract should reject missing scalar helper bindings\n";
            return false;
        }
        if (missing != "scalar-binding:value:helper_ser_u8")
        {
            std::cerr << "native helper contract missing-requirement mismatch for scalar binding\n";
            return false;
        }

        helpers.scalarBindings.push_back(
            llvmdsdl::ScalarBindingDescriptor{"helper_ser_u8",
                                              llvmdsdl::ScalarHelperDescriptor{llvmdsdl::ScalarHelperKind::Unsigned,
                                                                               "helper_ser_u8",
                                                                               "helper_des_u8",
                                                                               8,
                                                                               llvmdsdl::CastMode::Saturated}});
        if (!llvmdsdl::validateNativeSectionHelperContract(section,
                                                           &sectionFacts,
                                                           llvmdsdl::HelperBindingDirection::Serialize,
                                                           helpers,
                                                           &missing))
        {
            std::cerr << "native helper contract should accept satisfied scalar helper bindings\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticSection section;
        llvmdsdl::SemanticField   field;
        field.name                               = "samples";
        field.resolvedType.scalarCategory        = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        field.resolvedType.bitLength             = 8;
        field.resolvedType.arrayKind             = llvmdsdl::ArrayKind::VariableInclusive;
        field.resolvedType.arrayLengthPrefixBits = 8;
        field.resolvedType.arrayCapacity         = 32;
        section.fields.push_back(field);

        llvmdsdl::LoweredSectionFacts sectionFacts;
        sectionFacts.fieldsByName["samples"].serUnsignedHelper          = "helper_ser_u8";
        sectionFacts.fieldsByName["samples"].serArrayLengthPrefixHelper = "helper_arr_prefix_ser";
        sectionFacts.fieldsByName["samples"].arrayLengthValidateHelper  = "helper_arr_validate";

        llvmdsdl::SectionHelperBindingPlan helpers;
        helpers.capacityCheck = llvmdsdl::CapacityCheckHelperDescriptor{"helper_capacity", 8};
        helpers.scalarBindings.push_back(
            llvmdsdl::ScalarBindingDescriptor{"helper_ser_u8",
                                              llvmdsdl::ScalarHelperDescriptor{llvmdsdl::ScalarHelperKind::Unsigned,
                                                                               "helper_ser_u8",
                                                                               "helper_des_u8",
                                                                               8,
                                                                               llvmdsdl::CastMode::Saturated}});
        helpers.arrayPrefixBindings.push_back(llvmdsdl::ArrayPrefixBindingDescriptor{"helper_arr_prefix_ser", 8});

        std::string missing;
        if (llvmdsdl::validateNativeSectionHelperContract(section,
                                                          &sectionFacts,
                                                          llvmdsdl::HelperBindingDirection::Serialize,
                                                          helpers,
                                                          &missing))
        {
            std::cerr << "native helper contract should reject missing array validate helper binding\n";
            return false;
        }
        if (missing != "array-validate-binding:samples:helper_arr_validate")
        {
            std::cerr << "native helper contract missing-requirement mismatch for array validate binding\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticSection section;
        llvmdsdl::SemanticField   field;
        field.name                         = "nested";
        field.resolvedType.scalarCategory  = llvmdsdl::SemanticScalarCategory::Composite;
        field.resolvedType.compositeSealed = false;
        section.fields.push_back(field);

        llvmdsdl::LoweredSectionFacts sectionFacts;
        sectionFacts.fieldsByName["nested"].delimiterValidateHelper = "helper_delim";

        llvmdsdl::SectionHelperBindingPlan helpers;
        helpers.capacityCheck = llvmdsdl::CapacityCheckHelperDescriptor{"helper_capacity", 8};

        std::string missing;
        if (llvmdsdl::validateNativeSectionHelperContract(section,
                                                          &sectionFacts,
                                                          llvmdsdl::HelperBindingDirection::Serialize,
                                                          helpers,
                                                          &missing))
        {
            std::cerr << "native helper contract should reject missing delimiter helper binding\n";
            return false;
        }
        if (missing != "delimiter-binding:nested:helper_delim")
        {
            std::cerr << "native helper contract missing-requirement mismatch for delimiter binding\n";
            return false;
        }
    }

    return true;
}
