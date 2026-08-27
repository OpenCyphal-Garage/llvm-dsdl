//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <string>
#include <utility>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/CodeGen/RuntimeLoweredPlan.h"
#include "llvmdsdl/CodeGen/WireOperationContract.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Model.h"

#include "UnitTests.h"

bool runRuntimeLoweredPlanTests()
{
    {
        llvmdsdl::SemanticSection section;
        section.maxBitLength = 32;

        llvmdsdl::SemanticField counter;
        counter.name                        = "counter";
        counter.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        counter.resolvedType.bitLength      = 8;
        section.fields.push_back(counter);

        llvmdsdl::SemanticField payload;
        payload.name                               = "payload";
        payload.resolvedType.scalarCategory        = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        payload.resolvedType.bitLength             = 8;
        payload.resolvedType.arrayKind             = llvmdsdl::ArrayKind::VariableInclusive;
        payload.resolvedType.arrayCapacity         = 4;
        payload.resolvedType.arrayLengthPrefixBits = 8;
        section.fields.push_back(payload);

        llvmdsdl::LoweredSectionFacts facts;
        facts.fieldsByName["counter"].stepIndex             = 20;
        facts.fieldsByName["payload"].stepIndex             = 10;
        facts.fieldsByName["payload"].arrayLengthPrefixBits = 12;

        auto runtimeOrderedOrErr = llvmdsdl::buildRuntimeOrderedFieldSteps(section, &facts);
        if (!runtimeOrderedOrErr)
        {
            std::cerr << "runtime lowered ordered steps unexpectedly failed: "
                      << llvm::toString(runtimeOrderedOrErr.takeError()) << "\n";
            return false;
        }
        const auto& runtimeOrdered = *runtimeOrderedOrErr;
        if (runtimeOrdered.size() != 2U || runtimeOrdered[0].field == nullptr ||
            runtimeOrdered[0].field->name != "payload" || !runtimeOrdered[0].arrayLengthPrefixBits ||
            *runtimeOrdered[0].arrayLengthPrefixBits != 12U)
        {
            std::cerr << "runtime lowered ordered steps metadata mismatch\n";
            return false;
        }

        auto runtimePlanOrErr = llvmdsdl::buildRuntimeSectionPlan(section, &facts);
        if (!runtimePlanOrErr)
        {
            std::cerr << "runtime lowered section plan unexpectedly failed: "
                      << llvm::toString(runtimePlanOrErr.takeError()) << "\n";
            return false;
        }
        const auto& runtimePlan = *runtimePlanOrErr;
        if (runtimePlan.contractVersion != llvmdsdl::kWireOperationContractVersion || runtimePlan.isUnion ||
            runtimePlan.fields.size() != 2U || runtimePlan.fields[0].semanticFieldName != "payload" ||
            runtimePlan.fields[0].arrayKind != llvmdsdl::RuntimeArrayKind::Variable ||
            runtimePlan.fields[0].arrayLengthPrefixBits != 12)
        {
            std::cerr << "runtime lowered section plan metadata mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticSection section;
        llvmdsdl::SemanticField   first;
        first.name                        = "first";
        first.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        first.resolvedType.bitLength      = 8;
        section.fields.push_back(first);

        auto runtimeOrderedOrErr = llvmdsdl::buildRuntimeOrderedFieldSteps(section, nullptr);
        if (runtimeOrderedOrErr)
        {
            std::cerr << "runtime lowered ordered steps should fail without section facts\n";
            return false;
        }
        const std::string errText = llvm::toString(runtimeOrderedOrErr.takeError());
        if (!llvm::StringRef(errText).contains("missing lowered section facts"))
        {
            std::cerr << "unexpected runtime missing-facts error text: " << errText << "\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticSection section;
        section.maxBitLength = 8;

        llvmdsdl::SemanticField keyword;
        keyword.name                        = "class";
        keyword.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        keyword.resolvedType.bitLength      = 8;
        section.fields.push_back(keyword);

        llvmdsdl::LoweredSectionFacts facts;
        facts.fieldsByName["class"].stepIndex = 0;

        auto runtimePlanOrErr = llvmdsdl::buildRuntimeSectionPlan(section, &facts);
        if (!runtimePlanOrErr)
        {
            std::cerr << "runtime plan keyword-identifier case unexpectedly failed: "
                      << llvm::toString(runtimePlanOrErr.takeError()) << "\n";
            return false;
        }
        const auto& runtimePlan = *runtimePlanOrErr;
        if (runtimePlan.fields.size() != 1U || runtimePlan.fields[0].semanticFieldName != "class" ||
            runtimePlan.fields[0].fieldName != "class")
        {
            std::cerr << "runtime plan should preserve semantic field identifiers without language projection\n";
            return false;
        }
    }

    {
        llvmdsdl::RuntimeSectionPlan invalidPlan;
        invalidPlan.contractVersion = llvmdsdl::kWireOperationContractVersion + 1;
        auto err = llvmdsdl::validateRuntimeSectionPlanContract(invalidPlan, "runtime-lowered-plan-tests");
        if (!err)
        {
            std::cerr << "runtime section plan should reject unsupported wire-operation major version\n";
            return false;
        }
        const std::string errText = llvm::toString(std::move(err));
        if (!llvm::StringRef(errText).contains("unsupported wire-operation contract major version"))
        {
            std::cerr << "unexpected runtime plan contract error text: " << errText << "\n";
            return false;
        }
    }

    return true;
}
