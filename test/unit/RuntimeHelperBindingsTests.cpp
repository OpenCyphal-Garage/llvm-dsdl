//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <optional>

#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/CodeGen/RuntimeHelperBindings.h"
#include "llvmdsdl/CodeGen/RuntimeLoweredPlan.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Model.h"

#include "UnitTests.h"

bool runRuntimeHelperBindingsTests()
{
    {
        llvmdsdl::SemanticSection section;

        llvmdsdl::SemanticField payload;
        payload.name                               = "payload";
        payload.resolvedType.scalarCategory        = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        payload.resolvedType.bitLength             = 8;
        payload.resolvedType.arrayKind             = llvmdsdl::ArrayKind::VariableInclusive;
        payload.resolvedType.arrayCapacity         = 5;
        payload.resolvedType.arrayLengthPrefixBits = 8;
        section.fields.push_back(payload);

        llvmdsdl::SemanticField nested;
        nested.name                         = "nested";
        nested.resolvedType.scalarCategory  = llvmdsdl::SemanticScalarCategory::Composite;
        nested.resolvedType.compositeSealed = false;
        section.fields.push_back(nested);

        llvmdsdl::LoweredSectionFacts facts;
        facts.capacityCheckHelper    = "cap_check";
        facts.unionTagValidateHelper = "union_validate";
        facts.serUnionTagHelper      = "union_mask_ser";
        facts.deserUnionTagHelper    = "union_mask_deser";

        facts.fieldsByName["payload"].serUnsignedHelper            = "scalar_ser_u";
        facts.fieldsByName["payload"].deserUnsignedHelper          = "scalar_deser_u";
        facts.fieldsByName["payload"].serArrayLengthPrefixHelper   = "array_prefix_ser";
        facts.fieldsByName["payload"].deserArrayLengthPrefixHelper = "array_prefix_deser";
        facts.fieldsByName["payload"].arrayLengthValidateHelper    = "array_validate";
        facts.fieldsByName["nested"].delimiterValidateHelper       = "delimiter_validate";

        const llvmdsdl::RuntimeHelperNameResolver nameResolver = [](const std::string& symbol) {
            return "bound_" + symbol;
        };

        const auto sectionNames = llvmdsdl::resolveRuntimeSectionHelperNames(&facts, nameResolver);
        if (sectionNames.capacityCheck != "bound_cap_check" ||
            sectionNames.unionTagValidate != "bound_union_validate" ||
            sectionNames.serUnionTagMask != "bound_union_mask_ser" ||
            sectionNames.deserUnionTagMask != "bound_union_mask_deser")
        {
            std::cerr << "runtime section helper name resolution mismatch\n";
            return false;
        }

        llvmdsdl::RuntimeFieldPlan payloadPlan;
        payloadPlan.semanticFieldName     = "payload";
        payloadPlan.arrayKind             = llvmdsdl::RuntimeArrayKind::Variable;
        payloadPlan.arrayLengthPrefixBits = 12;

        const auto payloadPrefixOverride = llvmdsdl::runtimeArrayPrefixOverride(payloadPlan);
        if (!payloadPrefixOverride || *payloadPrefixOverride != 12U)
        {
            std::cerr << "runtime prefix override mismatch\n";
            return false;
        }

        const auto payloadHelpers =
            llvmdsdl::resolveRuntimeFieldHelperNames(section, &facts, payloadPlan, payloadPrefixOverride, nameResolver);
        if (payloadHelpers.serScalar != "bound_scalar_ser_u" || payloadHelpers.deserScalar != "bound_scalar_deser_u" ||
            payloadHelpers.serArrayPrefix != "bound_array_prefix_ser" ||
            payloadHelpers.deserArrayPrefix != "bound_array_prefix_deser" ||
            payloadHelpers.arrayValidate != "bound_array_validate" || !payloadHelpers.delimiterValidate.empty())
        {
            std::cerr << "runtime field helper name resolution mismatch for variable array\n";
            return false;
        }

        llvmdsdl::RuntimeFieldPlan nestedPlan;
        nestedPlan.semanticFieldName = "nested";
        nestedPlan.arrayKind         = llvmdsdl::RuntimeArrayKind::None;
        if (llvmdsdl::runtimeArrayPrefixOverride(nestedPlan))
        {
            std::cerr << "runtime prefix override unexpectedly produced value for non-variable field\n";
            return false;
        }

        const auto nestedHelpers =
            llvmdsdl::resolveRuntimeFieldHelperNames(section, &facts, nestedPlan, std::nullopt, nameResolver);
        if (nestedHelpers.delimiterValidate != "bound_delimiter_validate")
        {
            std::cerr << "runtime field delimiter helper resolution mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticSection section;
        llvmdsdl::SemanticField   only;
        only.name                        = "only";
        only.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        only.resolvedType.bitLength      = 8;
        section.fields.push_back(only);

        llvmdsdl::RuntimeFieldPlan missingPlan;
        missingPlan.semanticFieldName = "missing";
        missingPlan.arrayKind         = llvmdsdl::RuntimeArrayKind::None;

        llvmdsdl::LoweredSectionFacts facts;
        facts.fieldsByName["only"].serUnsignedHelper = "u_ser";
        const auto helpers =
            llvmdsdl::resolveRuntimeFieldHelperNames(section,
                                                     &facts,
                                                     missingPlan,
                                                     std::nullopt,
                                                     [](const std::string& symbol) { return "bound_" + symbol; });
        if (!helpers.serScalar.empty() || !helpers.deserScalar.empty() || !helpers.serArrayPrefix.empty() ||
            !helpers.deserArrayPrefix.empty() || !helpers.arrayValidate.empty() || !helpers.delimiterValidate.empty())
        {
            std::cerr << "runtime field helper resolution should be empty for unknown semantic field\n";
            return false;
        }
    }

    return true;
}
