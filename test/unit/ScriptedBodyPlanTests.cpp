//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>

#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/CodeGen/RuntimeLoweredPlan.h"
#include "llvmdsdl/CodeGen/ScriptedBodyPlan.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Model.h"

#include "UnitTests.h"

bool runScriptedBodyPlanTests()
{
    {
        llvmdsdl::SemanticSection section;

        llvmdsdl::SemanticField payload;
        payload.name                               = "payload";
        payload.resolvedType.scalarCategory        = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        payload.resolvedType.bitLength             = 8;
        payload.resolvedType.arrayKind             = llvmdsdl::ArrayKind::VariableInclusive;
        payload.resolvedType.arrayCapacity         = 4;
        payload.resolvedType.arrayLengthPrefixBits = 8;
        section.fields.push_back(payload);

        llvmdsdl::SemanticField value;
        value.name                        = "value";
        value.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::SignedInt;
        value.resolvedType.bitLength      = 16;
        section.fields.push_back(value);

        llvmdsdl::RuntimeSectionPlan runtimePlan;
        runtimePlan.isUnion = false;
        runtimePlan.maxBits = 64;

        llvmdsdl::RuntimeFieldPlan payloadPlan;
        payloadPlan.semanticFieldName     = "payload";
        payloadPlan.fieldName             = "payload";
        payloadPlan.kind                  = llvmdsdl::RuntimeFieldKind::Unsigned;
        payloadPlan.arrayKind             = llvmdsdl::RuntimeArrayKind::Variable;
        payloadPlan.arrayLengthPrefixBits = 12;
        payloadPlan.arrayCapacity         = 4;
        runtimePlan.fields.push_back(payloadPlan);

        llvmdsdl::RuntimeFieldPlan valuePlan;
        valuePlan.semanticFieldName = "value";
        valuePlan.fieldName         = "value";
        valuePlan.kind              = llvmdsdl::RuntimeFieldKind::Signed;
        valuePlan.arrayKind         = llvmdsdl::RuntimeArrayKind::None;
        runtimePlan.fields.push_back(valuePlan);

        llvmdsdl::LoweredSectionFacts facts;
        facts.capacityCheckHelper                                  = "capacity";
        facts.unionTagValidateHelper                               = "union_validate";
        facts.serUnionTagHelper                                    = "union_ser";
        facts.deserUnionTagHelper                                  = "union_deser";
        facts.fieldsByName["payload"].serUnsignedHelper            = "payload_scalar_ser";
        facts.fieldsByName["payload"].deserUnsignedHelper          = "payload_scalar_deser";
        facts.fieldsByName["payload"].serArrayLengthPrefixHelper   = "payload_prefix_ser";
        facts.fieldsByName["payload"].deserArrayLengthPrefixHelper = "payload_prefix_deser";
        facts.fieldsByName["payload"].arrayLengthValidateHelper    = "payload_validate";
        facts.fieldsByName["value"].serSignedHelper                = "value_scalar_ser";
        facts.fieldsByName["value"].deserSignedHelper              = "value_scalar_deser";

        const auto scripted =
            llvmdsdl::buildScriptedSectionBodyPlan(section, runtimePlan, &facts, [](const std::string& symbol) {
                return "bound_" + symbol;
            });

        if (scripted.sectionHelpers.capacityCheck != "bound_capacity" ||
            scripted.sectionHelpers.unionTagValidate != "bound_union_validate" ||
            scripted.sectionHelpers.serUnionTagMask != "bound_union_ser" ||
            scripted.sectionHelpers.deserUnionTagMask != "bound_union_deser")
        {
            std::cerr << "scripted body section helper mapping mismatch\n";
            return false;
        }
        if (scripted.fields.size() != 2U)
        {
            std::cerr << "scripted body field count mismatch\n";
            return false;
        }
        if (scripted.fields[0].field.semanticFieldName != "payload" || !scripted.fields[0].arrayPrefixOverride ||
            *scripted.fields[0].arrayPrefixOverride != 12U ||
            scripted.fields[0].helpers.serScalar != "bound_payload_scalar_ser" ||
            scripted.fields[0].helpers.deserScalar != "bound_payload_scalar_deser" ||
            scripted.fields[0].helpers.serArrayPrefix != "bound_payload_prefix_ser" ||
            scripted.fields[0].helpers.deserArrayPrefix != "bound_payload_prefix_deser" ||
            scripted.fields[0].helpers.arrayValidate != "bound_payload_validate")
        {
            std::cerr << "scripted body variable-array field helper mismatch\n";
            return false;
        }
        if (scripted.fields[1].field.semanticFieldName != "value" || scripted.fields[1].arrayPrefixOverride ||
            scripted.fields[1].helpers.serScalar != "bound_value_scalar_ser" ||
            scripted.fields[1].helpers.deserScalar != "bound_value_scalar_deser")
        {
            std::cerr << "scripted body scalar field helper mismatch\n";
            return false;
        }
    }

    return true;
}
