//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements the final C naming of a lowered schema.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/SchemaNaming.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <optional>
#include <vector>

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/MLIRContext.h"

#include "llvmdsdl/CodeGen/SectionNaming.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvmdsdl/Support/NamingPolicy.h"

namespace llvmdsdl
{

std::string cTypeNameFromInfo(const DiscoveredDefinition& info, const TypeNameVersioning versioning)
{
    return renderDefinitionTypeName(CodegenNamingLanguage::C,
                                    info.namespaceComponents,
                                    info.shortName,
                                    info.majorVersion,
                                    info.minorVersion,
                                    versioning);
}

void stampCNames(mlir::dsdl::SchemaOp schema, const SemanticDefinition& def, const TypeNameVersioning versioning)
{
    const NamingScope requestScope = makeSectionFieldScope(CodegenNamingLanguage::C, def.request);
    const NamingScope responseScope =
        makeSectionFieldScope(CodegenNamingLanguage::C, def.response.has_value() ? *def.response : def.request);

    const auto scopeFor = [&](const std::optional<llvm::StringRef> section) -> const NamingScope& {
        return (section && *section == "response") ? responseScope : requestScope;
    };
    mlir::MLIRContext* const context = schema.getContext();

    schema.walk([&](mlir::dsdl::FieldOp field) {
        if (field.getPadding())
        {
            return;
        }
        field.setCNameAttr(
            mlir::StringAttr::get(context,
                                  scopeFor(field.getSection()).get(IdentifierRole::FieldName, field.getName())));
    });

    const std::string baseTypeName = cTypeNameFromInfo(def.info, versioning);
    schema.setCTypeNameAttr(mlir::StringAttr::get(context, baseTypeName));

    schema.walk([&](mlir::dsdl::SerializationPlanOp plan) {
        // A service section appends its suffix to the *type* name, so the version sits before
        // it rather than at the end. Composing from the base is what keeps that true.
        std::string sectionTypeName = baseTypeName;
        if (const auto section = plan.getSection())
        {
            if (*section == "request")
            {
                sectionTypeName += renderSectionTypeSuffix(CodegenNamingLanguage::C, "request");
            }
            else if (*section == "response")
            {
                sectionTypeName += renderSectionTypeSuffix(CodegenNamingLanguage::C, "response");
            }
        }
        plan.setCTypeNameAttr(mlir::StringAttr::get(context, sectionTypeName));
        plan.setCSerializeSymbolAttr(mlir::StringAttr::get(context, sectionTypeName + "__serialize_"));
        plan.setCDeserializeSymbolAttr(mlir::StringAttr::get(context, sectionTypeName + "__deserialize_"));
    });

    schema.walk([&](mlir::dsdl::IOOp io) {
        if (io.isPadding())
        {
            return;
        }
        // An io op names no section of its own; the plan that encloses it does.
        auto plan = io->getParentOfType<mlir::dsdl::SerializationPlanOp>();
        io.setCNameAttr(mlir::StringAttr::get(context,
                                              scopeFor(plan ? plan.getSection() : std::nullopt)
                                                  .get(IdentifierRole::FieldName, io.getName())));
        if (!io.isComposite())
        {
            return;
        }
        // A referenced composite is named by the same rule as the definition itself. The io op
        // carries the reference's identity, so this re-renders rather than patching the string
        // lowering left.
        llvm::SmallVector<llvm::StringRef, 8> parts;
        io.getCompositeFullName()->split(parts, '.');
        if (parts.empty())
        {
            return;
        }
        const llvm::StringRef    shortName = parts.back();
        std::vector<std::string> namespaceComponents;
        namespaceComponents.reserve(parts.size() - 1U);
        for (std::size_t i = 0; (i + 1U) < parts.size(); ++i)
        {
            namespaceComponents.emplace_back(parts[i].str());
        }
        io.setCompositeCTypeNameAttr(
            mlir::StringAttr::get(context,
                                  renderDefinitionTypeName(CodegenNamingLanguage::C,
                                                           namespaceComponents,
                                                           shortName,
                                                           static_cast<std::uint32_t>(*io.getCompositeMajor()),
                                                           static_cast<std::uint32_t>(*io.getCompositeMinor()),
                                                           versioning)));
    });
}

std::size_t stampCNames(mlir::ModuleOp module, const SemanticModule& semantic, const TypeNameVersioning versioning)
{
    std::size_t stamped = 0;
    for (mlir::dsdl::SchemaOp schema : module.getBodyRegion().front().getOps<mlir::dsdl::SchemaOp>())
    {
        for (const auto& def : semantic.definitions)
        {
            if ((def.info.fullName == schema.getFullName()) && (def.info.majorVersion == schema.getMajor()) &&
                (def.info.minorVersion == schema.getMinor()))
            {
                stampCNames(schema, def, versioning);
                ++stamped;
                break;
            }
        }
    }
    return stamped;
}

}  // namespace llvmdsdl
