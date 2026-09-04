//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Extracts lowered helper facts from MLIR contracts.
///
/// This implementation runs transform pipelines and collects helper metadata required by language-specific code
/// generators.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"

#include <cstdint>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/OperationSupport.h>
#include <mlir/IR/OwningOpRef.h>
#include <mlir/IR/Region.h>
#include <mlir/IR/Verifier.h>
#include <mlir/Support/LLVM.h>
#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/Transforms/Passes.h"
#include "llvmdsdl/Transforms/LoweredSerDesContract.h"
#include "llvmdsdl/Transforms/LoweredSerDesContractValidation.h"
#include "mlir/Pass/Pass.h"  // IWYU pragma: keep
#include "mlir/Pass/PassManager.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "mlir/IR/BuiltinOps.h"

namespace llvmdsdl
{

namespace
{

std::int64_t nonNegative(const std::int64_t value)
{
    return std::max<std::int64_t>(value, 0);
}

}  // namespace

std::string loweredTypeKey(const std::string& name, std::uint32_t major, std::uint32_t minor)
{
    return name + ":" + std::to_string(major) + ":" + std::to_string(minor);
}

const LoweredFieldFacts* findLoweredFieldFacts(const LoweredSectionFacts* const sectionFacts,
                                               const std::string&               fieldName)
{
    if (sectionFacts == nullptr)
    {
        return nullptr;
    }
    const auto it = sectionFacts->fieldsByName.find(fieldName);
    if (it == sectionFacts->fieldsByName.end())
    {
        return nullptr;
    }
    return &it->second;
}

std::optional<std::uint32_t> loweredFieldArrayPrefixBits(const LoweredSectionFacts* const sectionFacts,
                                                         const std::string&               fieldName)
{
    const auto* const fieldFacts = findLoweredFieldFacts(sectionFacts, fieldName);
    if (fieldFacts == nullptr)
    {
        return std::nullopt;
    }
    return fieldFacts->arrayLengthPrefixBits;
}

bool collectLoweredFactsFromMlir(const SemanticModule&  semantic,
                                 mlir::ModuleOp         module,
                                 DiagnosticEngine&      diagnostics,
                                 const std::string&     backendLabel,
                                 LoweredFactsMap* const outFacts,
                                 const bool             optimizeLoweredSerDes)
{
    std::unordered_map<std::string, std::set<std::string>> keyToSections;
    LoweredFactsMap                                        loweredFacts;
    auto              loweredModule = mlir::OwningOpRef<mlir::ModuleOp>(mlir::cast<mlir::ModuleOp>(module->clone()));
    mlir::PassManager pm(module.getContext());
    pm.addPass(createLowerDSDLExecPass());
    pm.addPass(createDSDLAnnotateAliasabilityPass());
    if (optimizeLoweredSerDes)
    {
        addOptimizeLoweredSerDesPipeline(pm);
    }
    // The passes read the dialect's attributes through their accessors, which is only sound over
    // IR the verifier has accepted.
    if (mlir::failed(mlir::verify(loweredModule->getOperation())))
    {
        diagnostics.error({"<mlir>", 1, 1},
                          "failed to run lower-dsdl-exec for " + backendLabel +
                              " backend validation: the module does not verify");
        return false;
    }
    // The passes read the dialect's attributes through their accessors, which is only sound over
    // IR the verifier has accepted.
    if (mlir::failed(mlir::verify(loweredModule->getOperation())))
    {
        diagnostics.error({"<mlir>", 1, 1},
                          "failed to run lower-dsdl-exec for " + backendLabel +
                              " backend validation: the module does not verify");
        return false;
    }
    if (mlir::failed(pm.run(*loweredModule)))
    {
        diagnostics.error({"<mlir>", 1, 1},
                          "failed to run lower-dsdl-exec for " + backendLabel + " backend validation");
        return false;
    }
    if (const auto envelopeViolation = findLoweredContractEnvelopeViolation(loweredModule->getOperation()))
    {
        switch (envelopeViolation->kind)
        {
        case LoweredContractEnvelopeViolationKind::MissingVersion:
            diagnostics.error({"<mlir>", 1, 1},
                              "lowered SerDes contract missing module attribute '" +
                                  std::string(kLoweredSerDesContractVersionAttr) + "' for " + backendLabel +
                                  " backend validation");
            break;
        case LoweredContractEnvelopeViolationKind::UnsupportedMajorVersion:
            diagnostics.error({"<mlir>", 1, 1},
                              "unsupported lowered SerDes contract major version for " + backendLabel +
                                  " backend validation: " +
                                  loweredSerDesUnsupportedMajorVersionDiagnosticDetail(
                                      envelopeViolation->encodedVersion));
            break;
        case LoweredContractEnvelopeViolationKind::ProducerMismatch:
            diagnostics.error({"<mlir>", 1, 1},
                              "lowered SerDes contract producer mismatch for " + backendLabel +
                                  " backend validation: expected '" + std::string(kLoweredSerDesContractProducer) +
                                  "'");
            break;
        }
        return false;
    }

    for (mlir::dsdl::SchemaOp op : loweredModule->getBodyRegion().front().getOps<mlir::dsdl::SchemaOp>())
    {
        const std::string fullName = op.getFullName().str();
        const auto        key      = loweredTypeKey(fullName, op.getMajor(), op.getMinor());
        auto&             sections = keyToSections[key];

        if (op.getBody().empty())
        {
            diagnostics.error({"<mlir>", 1, 1}, "dsdl.schema has no body region for " + fullName);
            return false;
        }

        for (mlir::dsdl::SerializationPlanOp child : op.getBody().front().getOps<mlir::dsdl::SerializationPlanOp>())
        {
            if (const auto envelopeViolation = findLoweredContractEnvelopeViolation(child.getOperation()))
            {
                switch (envelopeViolation->kind)
                {
                case LoweredContractEnvelopeViolationKind::MissingVersion:
                    diagnostics.error({"<mlir>", 1, 1},
                                      "serialization plan missing lowered contract "
                                      "attribute '" +
                                          std::string(kLoweredSerDesContractVersionAttr) + "' for " + fullName);
                    break;
                case LoweredContractEnvelopeViolationKind::UnsupportedMajorVersion:
                    diagnostics.error({"<mlir>", 1, 1},
                                      "serialization plan unsupported lowered contract major version for " + fullName +
                                          ": " +
                                          loweredSerDesUnsupportedMajorVersionDiagnosticDetail(
                                              envelopeViolation->encodedVersion));
                    break;
                case LoweredContractEnvelopeViolationKind::ProducerMismatch:
                    diagnostics.error({"<mlir>", 1, 1},
                                      "serialization plan lowered contract producer mismatch for " + fullName +
                                          ": expected '" + std::string(kLoweredSerDesContractProducer) + "'");
                    break;
                }
                return false;
            }
            const std::string section      = child.getSection().value_or(llvm::StringRef{}).str();
            auto&             sectionFacts = loweredFacts[key][section];
            if (!sections.insert(section).second)
            {
                std::string duplicate = "duplicate dsdl.serialization_plan section '";
                duplicate.append(section).append("' for ").append(fullName);
                diagnostics.error({"<mlir>", 1, 1}, duplicate);
                return false;
            }

            if (const auto violation = findLoweredPlanContractViolation(*loweredModule, child.getOperation()))
            {
                diagnostics.error({"<mlir>", 1, 1},
                                  "serialization plan contract violation for " + op.getFullName().str() + ": " +
                                      violation->message);
                return false;
            }

            // The contract validation above has established every helper the plan needs.
            sectionFacts.capacityCheckHelper = child.getLoweredCapacityCheckHelper()->str();
            if (child.getIsUnion())
            {
                sectionFacts.unionTagBits           = static_cast<std::uint32_t>(*child.getUnionTagBits());
                sectionFacts.unionTagValidateHelper = child.getLoweredUnionTagValidateHelper()->str();
                sectionFacts.serUnionTagHelper      = child.getLoweredSerUnionTagHelper()->str();
                sectionFacts.deserUnionTagHelper    = child.getLoweredDeserUnionTagHelper()->str();
            }
            sectionFacts.zohAliasEligible = child.getZohAliasEligible();
            if (!sectionFacts.zohAliasEligible)
            {
                sectionFacts.zohAliasReason = child.getZohAliasReason().value_or(llvm::StringRef{}).str();
                if (sectionFacts.zohAliasReason.empty())
                {
                    sectionFacts.zohAliasReason = "not-proven";
                }
            }
            else
            {
                sectionFacts.zohAliasReason = "eligible";
            }

            const auto valueOrEmpty = [](const std::optional<llvm::StringRef> value) {
                return value ? value->str() : std::string{};
            };
            for (mlir::dsdl::IOOp step : child.getBody().front().getOps<mlir::dsdl::IOOp>())
            {
                auto& fieldFacts     = sectionFacts.fieldsByName[step.getName().str()];
                fieldFacts.stepIndex = nonNegative(step.getStepIndex().value_or(0));
                if (step.isVariableArray() && step.getArrayLengthPrefixBits() > 0)
                {
                    fieldFacts.arrayLengthPrefixBits      = static_cast<std::uint32_t>(step.getArrayLengthPrefixBits());
                    fieldFacts.serArrayLengthPrefixHelper = valueOrEmpty(step.getLoweredSerArrayLengthPrefixHelper());
                    fieldFacts.deserArrayLengthPrefixHelper =
                        valueOrEmpty(step.getLoweredDeserArrayLengthPrefixHelper());
                    fieldFacts.arrayLengthValidateHelper = valueOrEmpty(step.getLoweredArrayLengthValidateHelper());
                }
                fieldFacts.serUnsignedHelper       = valueOrEmpty(step.getLoweredSerUnsignedHelper());
                fieldFacts.deserUnsignedHelper     = valueOrEmpty(step.getLoweredDeserUnsignedHelper());
                fieldFacts.serSignedHelper         = valueOrEmpty(step.getLoweredSerSignedHelper());
                fieldFacts.deserSignedHelper       = valueOrEmpty(step.getLoweredDeserSignedHelper());
                fieldFacts.serFloatHelper          = valueOrEmpty(step.getLoweredSerFloatHelper());
                fieldFacts.deserFloatHelper        = valueOrEmpty(step.getLoweredDeserFloatHelper());
                fieldFacts.delimiterValidateHelper = valueOrEmpty(step.getLoweredDelimiterValidateHelper());
            }
        }
    }

    for (const auto& def : semantic.definitions)
    {
        const auto key = loweredTypeKey(def.info.fullName, def.info.majorVersion, def.info.minorVersion);
        const auto it  = keyToSections.find(key);
        if (it == keyToSections.end())
        {
            diagnostics.error({"<mlir>", 1, 1}, "missing dsdl.schema for " + def.info.fullName);
            return false;
        }

        std::set<std::string> expectedSections;
        if (def.isService)
        {
            expectedSections.insert("request");
            expectedSections.insert("response");
        }
        else
        {
            expectedSections.insert("");
        }

        for (const auto& sectionName : expectedSections)
        {
            if (!it->second.contains(sectionName))
            {
                diagnostics.error({"<mlir>", 1, 1},
                                  "missing dsdl.serialization_plan section '" + sectionName + "' for " +
                                      def.info.fullName);
                return false;
            }
        }
    }

    if (outFacts != nullptr)
    {
        *outFacts = std::move(loweredFacts);
    }
    return true;
}

}  // namespace llvmdsdl
