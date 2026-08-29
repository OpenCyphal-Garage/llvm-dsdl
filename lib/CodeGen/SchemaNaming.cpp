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
#include <vector>

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/MLIRContext.h"

#include "llvmdsdl/CodeGen/SectionNaming.h"
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

void stampCNames(mlir::Operation& schema, const SemanticDefinition& def, const TypeNameVersioning versioning)
{
    const NamingScope requestScope = makeSectionFieldScope(CodegenNamingLanguage::C, def.request);
    const NamingScope responseScope =
        makeSectionFieldScope(CodegenNamingLanguage::C, def.response.has_value() ? *def.response : def.request);

    const auto scopeFor = [&](mlir::Operation* const op) -> const NamingScope& {
        const auto sectionAttr = op->getAttrOfType<mlir::StringAttr>("section");
        return (sectionAttr && sectionAttr.getValue() == "response") ? responseScope : requestScope;
    };

    schema.walk([&](mlir::Operation* const op) {
        const llvm::StringRef opName = op->getName().getStringRef();
        if ((opName != "dsdl.field") && (opName != "dsdl.io"))
        {
            return;
        }
        if (op->hasAttr("padding") || (op->getAttrOfType<mlir::StringAttr>("kind") &&
                                       op->getAttrOfType<mlir::StringAttr>("kind").getValue() == "padding"))
        {
            return;
        }
        const auto nameAttr = op->getAttrOfType<mlir::StringAttr>("name");
        if (!nameAttr)
        {
            return;
        }
        // An io op names no section of its own; the plan that encloses it does.
        mlir::Operation* const sectionCarrier = (opName == "dsdl.io") ? op->getParentOp() : op;
        op->setAttr("c_name",
                    mlir::StringAttr::get(op->getContext(),
                                          scopeFor(sectionCarrier)
                                              .get(IdentifierRole::FieldName, nameAttr.getValue())));
    });

    mlir::MLIRContext* const context      = schema.getContext();
    const std::string        baseTypeName = cTypeNameFromInfo(def.info, versioning);
    schema.setAttr("c_type_name", mlir::StringAttr::get(context, baseTypeName));

    schema.walk([&](mlir::Operation* const op) {
        const llvm::StringRef opName = op->getName().getStringRef();
        if (opName == "dsdl.serialization_plan")
        {
            // A service section appends its suffix to the *type* name, so the version sits before
            // it rather than at the end. Composing from the base is what keeps that true.
            std::string sectionTypeName = baseTypeName;
            if (const auto sectionAttr = op->getAttrOfType<mlir::StringAttr>("section"))
            {
                if (sectionAttr.getValue() == "request")
                {
                    sectionTypeName += renderSectionTypeSuffix(CodegenNamingLanguage::C, "request");
                }
                else if (sectionAttr.getValue() == "response")
                {
                    sectionTypeName += renderSectionTypeSuffix(CodegenNamingLanguage::C, "response");
                }
            }
            op->setAttr("c_type_name", mlir::StringAttr::get(context, sectionTypeName));
            op->setAttr("c_serialize_symbol", mlir::StringAttr::get(context, sectionTypeName + "__serialize_"));
            op->setAttr("c_deserialize_symbol", mlir::StringAttr::get(context, sectionTypeName + "__deserialize_"));
            return;
        }
        if (opName != "dsdl.io")
        {
            return;
        }
        // A referenced composite is named by the same rule as the definition itself. The io op
        // carries the reference's identity, so this re-renders rather than patching the string
        // lowering left.
        const auto fullNameAttr = op->getAttrOfType<mlir::StringAttr>("composite_full_name");
        const auto majorAttr    = op->getAttrOfType<mlir::IntegerAttr>("composite_major");
        const auto minorAttr    = op->getAttrOfType<mlir::IntegerAttr>("composite_minor");
        if (!fullNameAttr || !majorAttr || !minorAttr)
        {
            return;
        }
        llvm::SmallVector<llvm::StringRef, 8> parts;
        fullNameAttr.getValue().split(parts, '.');
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
        op->setAttr("composite_c_type_name",
                    mlir::StringAttr::get(context,
                                          renderDefinitionTypeName(CodegenNamingLanguage::C,
                                                                   namespaceComponents,
                                                                   shortName,
                                                                   static_cast<std::uint32_t>(majorAttr.getInt()),
                                                                   static_cast<std::uint32_t>(minorAttr.getInt()),
                                                                   versioning)));
    });
}

std::size_t stampCNames(mlir::ModuleOp module, const SemanticModule& semantic, const TypeNameVersioning versioning)
{
    std::size_t stamped = 0;
    for (mlir::Operation& schema : module.getBodyRegion().front())
    {
        if (schema.getName().getStringRef() != "dsdl.schema")
        {
            continue;
        }
        const auto fullName = schema.getAttrOfType<mlir::StringAttr>("full_name");
        const auto major    = schema.getAttrOfType<mlir::IntegerAttr>("major");
        const auto minor    = schema.getAttrOfType<mlir::IntegerAttr>("minor");
        if (!fullName || !major || !minor)
        {
            continue;
        }
        for (const auto& def : semantic.definitions)
        {
            if ((def.info.fullName == fullName.getValue()) &&
                (static_cast<std::int64_t>(def.info.majorVersion) == major.getInt()) &&
                (static_cast<std::int64_t>(def.info.minorVersion) == minor.getInt()))
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
