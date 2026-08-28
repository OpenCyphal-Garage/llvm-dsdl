//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <string>

#include "llvmdsdl/CodeGen/RuntimeLoweredPlan.h"
#include "llvmdsdl/CodeGen/ScriptedOperationPlan.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvmdsdl/CodeGen/WireOperationContract.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Model.h"

#include "UnitTests.h"

bool runScriptedOperationPlanTests()
{
    llvmdsdl::SemanticSection section;
    section.isUnion = true;

    llvmdsdl::SemanticField scalar;
    scalar.name                        = "scalar";
    scalar.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
    scalar.resolvedType.bitLength      = 16;
    section.fields.push_back(scalar);

    llvmdsdl::SemanticField fixedArray;
    fixedArray.name                        = "fixed";
    fixedArray.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::Bool;
    fixedArray.resolvedType.arrayKind      = llvmdsdl::ArrayKind::Fixed;
    fixedArray.resolvedType.arrayCapacity  = 8;
    section.fields.push_back(fixedArray);

    llvmdsdl::SemanticField varArray;
    varArray.name                               = "var";
    varArray.resolvedType.scalarCategory        = llvmdsdl::SemanticScalarCategory::Composite;
    varArray.resolvedType.arrayKind             = llvmdsdl::ArrayKind::VariableInclusive;
    varArray.resolvedType.arrayCapacity         = 4;
    varArray.resolvedType.arrayLengthPrefixBits = 8;
    section.fields.push_back(varArray);

    llvmdsdl::RuntimeSectionPlan runtimePlan;
    runtimePlan.isUnion      = true;
    runtimePlan.unionTagBits = 3;
    runtimePlan.maxBits      = 128;

    llvmdsdl::RuntimeFieldPlan scalarPlan;
    scalarPlan.semanticFieldName = "scalar";
    scalarPlan.fieldName         = "scalar";
    scalarPlan.kind              = llvmdsdl::RuntimeFieldKind::Unsigned;
    scalarPlan.arrayKind         = llvmdsdl::RuntimeArrayKind::None;
    runtimePlan.fields.push_back(scalarPlan);

    llvmdsdl::RuntimeFieldPlan fixedPlan;
    fixedPlan.semanticFieldName = "fixed";
    fixedPlan.fieldName         = "fixed";
    fixedPlan.kind              = llvmdsdl::RuntimeFieldKind::Bool;
    fixedPlan.arrayKind         = llvmdsdl::RuntimeArrayKind::Fixed;
    runtimePlan.fields.push_back(fixedPlan);

    llvmdsdl::RuntimeFieldPlan varPlan;
    varPlan.semanticFieldName     = "var";
    varPlan.fieldName             = "var";
    varPlan.kind                  = llvmdsdl::RuntimeFieldKind::Composite;
    varPlan.arrayKind             = llvmdsdl::RuntimeArrayKind::Variable;
    varPlan.arrayLengthPrefixBits = 12;
    runtimePlan.fields.push_back(varPlan);

    auto operationPlanOrErr =
        llvmdsdl::buildScriptedSectionOperationPlan(section, runtimePlan, nullptr, [](const std::string& symbol) {
            return symbol;
        });
    if (!operationPlanOrErr)
    {
        std::cerr << "scripted operation plan unexpectedly failed: " << llvm::toString(operationPlanOrErr.takeError())
                  << "\n";
        return false;
    }
    const auto& operationPlan = *operationPlanOrErr;

    if (operationPlan.contractVersion != llvmdsdl::kWireOperationContractVersion || !operationPlan.isUnion ||
        operationPlan.unionTagBits != 3U || operationPlan.maxBits != 128 || operationPlan.fields.size() != 3U)
    {
        std::cerr << "scripted operation section metadata mismatch\n";
        return false;
    }
    if (operationPlan.fields[0].cardinality != llvmdsdl::ScriptedFieldCardinality::Scalar ||
        operationPlan.fields[0].valueKind != llvmdsdl::ScriptedFieldValueKind::Unsigned)
    {
        std::cerr << "scripted operation scalar classification mismatch\n";
        return false;
    }
    if (operationPlan.fields[1].cardinality != llvmdsdl::ScriptedFieldCardinality::FixedArray ||
        operationPlan.fields[1].valueKind != llvmdsdl::ScriptedFieldValueKind::Bool)
    {
        std::cerr << "scripted operation fixed-array classification mismatch\n";
        return false;
    }
    if (operationPlan.fields[2].cardinality != llvmdsdl::ScriptedFieldCardinality::VariableArray ||
        operationPlan.fields[2].valueKind != llvmdsdl::ScriptedFieldValueKind::Composite)
    {
        std::cerr << "scripted operation variable-array classification mismatch\n";
        return false;
    }

    runtimePlan.contractVersion = llvmdsdl::kWireOperationContractVersion + 1;
    operationPlanOrErr =
        llvmdsdl::buildScriptedSectionOperationPlan(section, runtimePlan, nullptr, [](const std::string& symbol) {
            return symbol;
        });
    if (operationPlanOrErr)
    {
        std::cerr << "scripted operation plan should reject unsupported wire-operation major version\n";
        return false;
    }
    const std::string errText = llvm::toString(operationPlanOrErr.takeError());
    if (!llvm::StringRef(errText).contains("unsupported wire-operation contract major version"))
    {
        std::cerr << "unexpected scripted-operation contract error text: " << errText << "\n";
        return false;
    }

    return true;
}
