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
#include <mlir/Support/LLVM.h>
#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

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

    for (mlir::Operation& op : loweredModule->getBodyRegion().front())
    {
        if (op.getName().getStringRef() != "dsdl.schema")
        {
            continue;
        }

        const auto fullName = op.getAttrOfType<mlir::StringAttr>("full_name");
        const auto major    = op.getAttrOfType<mlir::IntegerAttr>("major");
        const auto minor    = op.getAttrOfType<mlir::IntegerAttr>("minor");
        if (!fullName || !major || !minor)
        {
            diagnostics.error({"<mlir>", 1, 1}, "dsdl.schema missing identity attributes");
            return false;
        }

        const auto key      = loweredTypeKey(fullName.getValue().str(),
                                             static_cast<std::uint32_t>(major.getInt()),
                                             static_cast<std::uint32_t>(minor.getInt()));
        auto&      sections = keyToSections[key];

        if (op.getNumRegions() == 0 || op.getRegion(0).empty())
        {
            diagnostics.error({"<mlir>", 1, 1}, "dsdl.schema has no body region for " + fullName.getValue().str());
            return false;
        }

        for (mlir::Operation& child : op.getRegion(0).front())
        {
            if (child.getName().getStringRef() != "dsdl.serialization_plan")
            {
                continue;
            }
            if (const auto envelopeViolation = findLoweredContractEnvelopeViolation(&child))
            {
                switch (envelopeViolation->kind)
                {
                case LoweredContractEnvelopeViolationKind::MissingVersion:
                    diagnostics.error({"<mlir>", 1, 1},
                                      "serialization plan missing lowered contract "
                                      "attribute '" +
                                          std::string(kLoweredSerDesContractVersionAttr) + "' for " +
                                          fullName.getValue().str());
                    break;
                case LoweredContractEnvelopeViolationKind::UnsupportedMajorVersion:
                    diagnostics.error({"<mlir>", 1, 1},
                                      "serialization plan unsupported lowered contract major version for " +
                                          fullName.getValue().str() + ": " +
                                          loweredSerDesUnsupportedMajorVersionDiagnosticDetail(
                                              envelopeViolation->encodedVersion));
                    break;
                case LoweredContractEnvelopeViolationKind::ProducerMismatch:
                    diagnostics.error({"<mlir>", 1, 1},
                                      "serialization plan lowered contract producer mismatch for " +
                                          fullName.getValue().str() + ": expected '" +
                                          std::string(kLoweredSerDesContractProducer) + "'");
                    break;
                }
                return false;
            }
            std::string section;
            if (const auto sectionAttr = child.getAttrOfType<mlir::StringAttr>("section"))
            {
                section = sectionAttr.getValue().str();
            }
            auto& sectionFacts = loweredFacts[key][section];
            if (!sections.insert(section).second)
            {
                diagnostics.error({"<mlir>", 1, 1},
                                  "duplicate dsdl.serialization_plan section '" + section + "' for " +
                                      fullName.getValue().str());
                return false;
            }

            if (const auto violation = findLoweredPlanContractViolation(*loweredModule, &child))
            {
                diagnostics.error({"<mlir>", 1, 1},
                                  "serialization plan contract violation for " + fullName.getValue().str() + ": " +
                                      violation->message);
                return false;
            }

            sectionFacts.capacityCheckHelper =
                child.getAttrOfType<mlir::StringAttr>(kLoweredCapacityCheckHelperAttr).getValue().str();
            if (child.hasAttr("is_union"))
            {
                sectionFacts.unionTagBits =
                    static_cast<std::uint32_t>(child.getAttrOfType<mlir::IntegerAttr>("union_tag_bits").getInt());
                sectionFacts.unionTagValidateHelper =
                    child.getAttrOfType<mlir::StringAttr>(kLoweredUnionTagValidateHelperAttr).getValue().str();
                sectionFacts.serUnionTagHelper =
                    child.getAttrOfType<mlir::StringAttr>(kLoweredSerUnionTagHelperAttr).getValue().str();
                sectionFacts.deserUnionTagHelper =
                    child.getAttrOfType<mlir::StringAttr>(kLoweredDeserUnionTagHelperAttr).getValue().str();
            }
            sectionFacts.zohAliasEligible = child.hasAttr("zoh_alias_eligible");
            if (!sectionFacts.zohAliasEligible)
            {
                if (const auto reasonAttr = child.getAttrOfType<mlir::StringAttr>("zoh_alias_reason"))
                {
                    sectionFacts.zohAliasReason = reasonAttr.getValue().str();
                }
                if (sectionFacts.zohAliasReason.empty())
                {
                    sectionFacts.zohAliasReason = "not-proven";
                }
            }
            else
            {
                sectionFacts.zohAliasReason = "eligible";
            }

            for (mlir::Operation& step : child.getRegion(0).front())
            {
                const auto stepName = step.getName().getStringRef();
                if (stepName == "dsdl.align")
                {
                    continue;
                }
                if (stepName != "dsdl.io")
                {
                    diagnostics.error({"<mlir>", 1, 1},
                                      "unexpected operation in validated serialization plan body for " +
                                          fullName.getValue().str());
                    return false;
                }

                const auto arrayKind       = step.getAttrOfType<mlir::StringAttr>("array_kind");
                const auto arrayPrefixBits = step.getAttrOfType<mlir::IntegerAttr>("array_length_prefix_bits");
                if (const auto fieldNameAttr = step.getAttrOfType<mlir::StringAttr>("name"); fieldNameAttr)
                {
                    auto& fieldFacts = sectionFacts.fieldsByName[fieldNameAttr.getValue().str()];
                    if (const auto stepIndex = step.getAttrOfType<mlir::IntegerAttr>("step_index"))
                    {
                        fieldFacts.stepIndex = nonNegative(stepIndex.getInt());
                    }
                    if (arrayKind && arrayKind.getValue().starts_with("variable") && arrayPrefixBits &&
                        arrayPrefixBits.getInt() > 0)
                    {
                        fieldFacts.arrayLengthPrefixBits = static_cast<std::uint32_t>(arrayPrefixBits.getInt());
                        if (const auto serArrayPrefixHelper =
                                step.getAttrOfType<mlir::StringAttr>("lowered_ser_array_length_prefix_helper"))
                        {
                            fieldFacts.serArrayLengthPrefixHelper = serArrayPrefixHelper.getValue().str();
                        }
                        if (const auto deserArrayPrefixHelper =
                                step.getAttrOfType<mlir::StringAttr>("lowered_deser_array_length_prefix_helper"))
                        {
                            fieldFacts.deserArrayLengthPrefixHelper = deserArrayPrefixHelper.getValue().str();
                        }
                        if (const auto arrayValidateHelper =
                                step.getAttrOfType<mlir::StringAttr>("lowered_array_length_validate_helper"))
                        {
                            fieldFacts.arrayLengthValidateHelper = arrayValidateHelper.getValue().str();
                        }
                    }
                    if (const auto serUnsigned = step.getAttrOfType<mlir::StringAttr>("lowered_ser_unsigned_helper"))
                    {
                        fieldFacts.serUnsignedHelper = serUnsigned.getValue().str();
                    }
                    if (const auto deserUnsigned =
                            step.getAttrOfType<mlir::StringAttr>("lowered_deser_unsigned_helper"))
                    {
                        fieldFacts.deserUnsignedHelper = deserUnsigned.getValue().str();
                    }
                    if (const auto serSigned = step.getAttrOfType<mlir::StringAttr>("lowered_ser_signed_helper"))
                    {
                        fieldFacts.serSignedHelper = serSigned.getValue().str();
                    }
                    if (const auto deserSigned = step.getAttrOfType<mlir::StringAttr>("lowered_deser_signed_helper"))
                    {
                        fieldFacts.deserSignedHelper = deserSigned.getValue().str();
                    }
                    if (const auto serFloat = step.getAttrOfType<mlir::StringAttr>("lowered_ser_float_helper"))
                    {
                        fieldFacts.serFloatHelper = serFloat.getValue().str();
                    }
                    if (const auto deserFloat = step.getAttrOfType<mlir::StringAttr>("lowered_deser_float_helper"))
                    {
                        fieldFacts.deserFloatHelper = deserFloat.getValue().str();
                    }
                    if (const auto delimiterValidateHelper =
                            step.getAttrOfType<mlir::StringAttr>("lowered_delimiter_validate_helper"))
                    {
                        fieldFacts.delimiterValidateHelper = delimiterValidateHelper.getValue().str();
                    }
                }
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
