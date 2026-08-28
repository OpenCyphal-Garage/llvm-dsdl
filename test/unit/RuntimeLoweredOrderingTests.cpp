//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <string>
#include <optional>
#include <unordered_map>
#include <vector>

#include "llvmdsdl/CodeGen/RuntimeLoweredPlan.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Model.h"

#include "UnitTests.h"

bool runRuntimeLoweredOrderingTests()
{
    {
        llvmdsdl::SemanticSection section;

        llvmdsdl::SemanticField first;
        first.name                        = "first";
        first.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        first.resolvedType.bitLength      = 8;
        section.fields.push_back(first);

        llvmdsdl::SemanticField payload;
        payload.name                               = "payload";
        payload.resolvedType.scalarCategory        = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        payload.resolvedType.bitLength             = 8;
        payload.resolvedType.arrayKind             = llvmdsdl::ArrayKind::VariableInclusive;
        payload.resolvedType.arrayCapacity         = 8;
        payload.resolvedType.arrayLengthPrefixBits = 8;
        section.fields.push_back(payload);

        llvmdsdl::SemanticField pad;
        pad.name                        = "_pad0";
        pad.isPadding                   = true;
        pad.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::Void;
        pad.resolvedType.bitLength      = 3;
        section.fields.push_back(pad);

        llvmdsdl::LoweredSectionFacts facts;
        facts.fieldsByName["first"].stepIndex               = 20;
        facts.fieldsByName["payload"].stepIndex             = 10;
        facts.fieldsByName["payload"].arrayLengthPrefixBits = 16;
        facts.fieldsByName["_pad0"].stepIndex               = 30;

        auto orderedOrErr = llvmdsdl::buildRuntimeOrderedFieldSteps(section, &facts);
        if (!orderedOrErr)
        {
            std::cerr << "runtime lowered ordering non-union unexpectedly failed: "
                      << llvm::toString(orderedOrErr.takeError()) << "\n";
            return false;
        }
        const auto& ordered = *orderedOrErr;
        if (ordered.size() != 3)
        {
            std::cerr << "runtime lowered ordering non-union size mismatch\n";
            return false;
        }
        if (ordered[0].field == nullptr || ordered[0].field->name != "payload" || !ordered[0].arrayLengthPrefixBits ||
            *ordered[0].arrayLengthPrefixBits != 16U)
        {
            std::cerr << "runtime lowered ordering non-union reordered payload mismatch\n";
            return false;
        }
        if (ordered[1].field == nullptr || ordered[1].field->name != "first")
        {
            std::cerr << "runtime lowered ordering non-union first field mismatch\n";
            return false;
        }
        if (ordered[2].field == nullptr || !ordered[2].field->isPadding)
        {
            std::cerr << "runtime lowered ordering non-union padding mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticSection section;
        section.isUnion = true;

        llvmdsdl::SemanticField alpha;
        alpha.name                        = "alpha";
        alpha.unionOptionIndex            = 1;
        alpha.unionTagBits                = 2;
        alpha.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        alpha.resolvedType.bitLength      = 8;
        section.fields.push_back(alpha);

        llvmdsdl::SemanticField beta;
        beta.name                               = "beta";
        beta.unionOptionIndex                   = 0;
        beta.unionTagBits                       = 2;
        beta.resolvedType.scalarCategory        = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        beta.resolvedType.bitLength             = 8;
        beta.resolvedType.arrayKind             = llvmdsdl::ArrayKind::VariableInclusive;
        beta.resolvedType.arrayCapacity         = 4;
        beta.resolvedType.arrayLengthPrefixBits = 8;
        section.fields.push_back(beta);

        llvmdsdl::LoweredSectionFacts facts;
        facts.fieldsByName["alpha"].stepIndex            = 20;
        facts.fieldsByName["beta"].stepIndex             = 10;
        facts.fieldsByName["beta"].arrayLengthPrefixBits = 12;

        auto orderedOrErr = llvmdsdl::buildRuntimeOrderedFieldSteps(section, &facts);
        if (!orderedOrErr)
        {
            std::cerr << "runtime lowered ordering union unexpectedly failed: "
                      << llvm::toString(orderedOrErr.takeError()) << "\n";
            return false;
        }
        const auto& ordered = *orderedOrErr;
        if (ordered.size() != 2)
        {
            std::cerr << "runtime lowered ordering union size mismatch\n";
            return false;
        }
        if (ordered[0].field == nullptr || ordered[0].field->name != "beta" || !ordered[0].arrayLengthPrefixBits ||
            *ordered[0].arrayLengthPrefixBits != 12U)
        {
            std::cerr << "runtime lowered ordering union beta ordering mismatch\n";
            return false;
        }
        if (ordered[1].field == nullptr || ordered[1].field->name != "alpha")
        {
            std::cerr << "runtime lowered ordering union alpha ordering mismatch\n";
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

        auto orderedOrErr = llvmdsdl::buildRuntimeOrderedFieldSteps(section, nullptr);
        if (orderedOrErr)
        {
            std::cerr << "runtime lowered ordering should fail without section facts\n";
            return false;
        }
        const std::string errText = llvm::toString(orderedOrErr.takeError());
        if (!llvm::StringRef(errText).contains("missing lowered section facts"))
        {
            std::cerr << "unexpected missing-facts error text: " << errText << "\n";
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
        facts.fieldsByName["first"].stepIndex              = 1;
        facts.fieldsByName["second"].arrayLengthPrefixBits = 8;

        auto orderedOrErr = llvmdsdl::buildRuntimeOrderedFieldSteps(section, &facts);
        if (orderedOrErr)
        {
            std::cerr << "runtime lowered ordering should fail for missing step index\n";
            return false;
        }
        const std::string errText = llvm::toString(orderedOrErr.takeError());
        if (!llvm::StringRef(errText).contains("missing lowered step index"))
        {
            std::cerr << "unexpected missing-step-index error text: " << errText << "\n";
            return false;
        }
    }

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

        auto runtimePlanOrErr = llvmdsdl::buildRuntimeSectionPlan(section, &facts);
        if (!runtimePlanOrErr)
        {
            std::cerr << "runtime section plan non-union unexpectedly failed: "
                      << llvm::toString(runtimePlanOrErr.takeError()) << "\n";
            return false;
        }
        const auto& runtimePlan = *runtimePlanOrErr;
        if (runtimePlan.isUnion || runtimePlan.fields.size() != 2U)
        {
            std::cerr << "runtime section plan non-union shape mismatch\n";
            return false;
        }
        if (runtimePlan.fields[0].fieldName != "payload" || runtimePlan.fields[0].semanticFieldName != "payload" ||
            runtimePlan.fields[0].arrayKind != llvmdsdl::RuntimeArrayKind::Variable ||
            runtimePlan.fields[0].arrayLengthPrefixBits != 12)
        {
            std::cerr << "runtime section plan non-union array metadata mismatch\n";
            return false;
        }
        if (runtimePlan.fields[1].fieldName != "counter" || runtimePlan.fields[1].semanticFieldName != "counter" ||
            runtimePlan.fields[1].kind != llvmdsdl::RuntimeFieldKind::Unsigned)
        {
            std::cerr << "runtime section plan non-union scalar metadata mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticSection section;
        section.isUnion      = true;
        section.maxBitLength = 24;

        llvmdsdl::SemanticField alpha;
        alpha.name                        = "alpha";
        alpha.unionOptionIndex            = 2;
        alpha.unionTagBits                = 3;
        alpha.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::UnsignedInt;
        alpha.resolvedType.bitLength      = 8;
        section.fields.push_back(alpha);

        llvmdsdl::SemanticField beta;
        beta.name                        = "beta";
        beta.unionOptionIndex            = 1;
        beta.unionTagBits                = 3;
        beta.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::SignedInt;
        beta.resolvedType.bitLength      = 8;
        section.fields.push_back(beta);

        llvmdsdl::LoweredSectionFacts facts;
        facts.fieldsByName["alpha"].stepIndex = 20;
        facts.fieldsByName["beta"].stepIndex  = 10;

        auto runtimePlanOrErr = llvmdsdl::buildRuntimeSectionPlan(section, &facts);
        if (!runtimePlanOrErr)
        {
            std::cerr << "runtime section plan union unexpectedly failed: "
                      << llvm::toString(runtimePlanOrErr.takeError()) << "\n";
            return false;
        }
        const auto& runtimePlan = *runtimePlanOrErr;
        if (!runtimePlan.isUnion || runtimePlan.unionTagBits != 3 || runtimePlan.fields.size() != 2U)
        {
            std::cerr << "runtime section plan union shape mismatch\n";
            return false;
        }
        if (runtimePlan.fields[0].unionOptionIndex != 1 || runtimePlan.fields[1].unionOptionIndex != 2)
        {
            std::cerr << "runtime section plan union option sorting mismatch\n";
            return false;
        }
        if (runtimePlan.fields[0].semanticFieldName != "beta" || runtimePlan.fields[1].semanticFieldName != "alpha")
        {
            std::cerr << "runtime section plan semantic field names mismatch\n";
            return false;
        }
    }

    return true;
}
