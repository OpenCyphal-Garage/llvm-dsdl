//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements lowering from semantic models to DSDL MLIR.
///
/// The lowering pipeline maps analyzed types and sections into dialect operations suitable for downstream transforms.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Lowering/LowerToMLIR.h"

#include <llvm/ADT/StringRef.h>
#include <mlir/IR/Attributes.h>
#include <mlir/IR/Block.h>
#include <mlir/IR/Location.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/OperationSupport.h>
#include <mlir/IR/OwningOpRef.h>
#include <mlir/IR/Diagnostics.h>
#include <mlir/IR/Region.h>
#include <mlir/IR/Verifier.h>
#include <algorithm>
#include <cctype>
#include <set>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mlir/IR/Builders.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/BitLengthSet.h"
#include "llvmdsdl/Semantics/Evaluator.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "mlir/IR/BuiltinAttributes.h"

namespace llvmdsdl
{
namespace
{

std::string fieldKind(const SemanticField& f)
{
    return f.isPadding ? "padding" : "field";
}

std::string cTypeNameFromInfo(const DiscoveredDefinition& info, const TypeNameVersioning versioning)
{
    return renderDefinitionTypeName(CodegenNamingLanguage::C,
                                    info.namespaceComponents,
                                    info.shortName,
                                    info.majorVersion,
                                    info.minorVersion,
                                    versioning);
}

std::string cTypeNameFromRef(const SemanticTypeRef& ref, const TypeNameVersioning versioning)
{
    std::string out;
    for (std::size_t i = 0; i < ref.namespaceComponents.size(); ++i)
    {
        if (i > 0)
        {
            out += "__";
        }
        out += codegenProjectIdentifier(CodegenNamingLanguage::C,
                                        IdentifierRole::NamespaceName,
                                        ref.namespaceComponents[i]);
    }
    if (!out.empty())
    {
        out += "__";
    }
    out += codegenProjectIdentifier(CodegenNamingLanguage::C, IdentifierRole::TypeName, ref.shortName);
    return out;
}

std::string headerFileName(const DiscoveredDefinition& info)
{
    return renderDefinitionFileStem(CodegenNamingLanguage::C, info.shortName, info.majorVersion, info.minorVersion) +
           ".h";
}

std::string relativeHeaderPath(const DiscoveredDefinition& info)
{
    std::string path;
    for (const auto& ns : info.namespaceComponents)
    {
        if (!path.empty())
        {
            path += "/";
        }
        path += ns;
    }
    if (!path.empty())
    {
        path += "/";
    }
    path += headerFileName(info);
    return path;
}

llvm::StringRef scalarCategoryName(SemanticScalarCategory category)
{
    switch (category)
    {
    case SemanticScalarCategory::Bool:
        return "bool";
    case SemanticScalarCategory::Byte:
        return "byte";
    case SemanticScalarCategory::Utf8:
        return "utf8";
    case SemanticScalarCategory::UnsignedInt:
        return "unsigned";
    case SemanticScalarCategory::SignedInt:
        return "signed";
    case SemanticScalarCategory::Float:
        return "float";
    case SemanticScalarCategory::Void:
        return "void";
    case SemanticScalarCategory::Composite:
        return "composite";
    }
    return "void";
}

llvm::StringRef castModeName(CastMode castMode)
{
    switch (castMode)
    {
    case CastMode::Saturated:
        return "saturated";
    case CastMode::Truncated:
        return "truncated";
    }
    return "saturated";
}

llvm::StringRef arrayKindName(ArrayKind arrayKind)
{
    switch (arrayKind)
    {
    case ArrayKind::None:
        return "none";
    case ArrayKind::Fixed:
        return "fixed";
    case ArrayKind::VariableInclusive:
        return "variable_inclusive";
    case ArrayKind::VariableExclusive:
        return "variable_exclusive";
    }
    return "none";
}

std::optional<std::string> docAttrText(const AttachedDoc& doc)
{
    if (doc.empty())
    {
        return std::nullopt;
    }
    return doc.str();
}

}  // namespace

mlir::OwningOpRef<mlir::ModuleOp> lowerToMLIR(const SemanticModule& module,
                                              mlir::MLIRContext&    context,
                                              DiagnosticEngine&     diagnostics,
                                              const bool            verifyModule)
{
    mlir::OpBuilder builder(&context);
    auto            m = mlir::ModuleOp::create(builder.getUnknownLoc());
    builder.setInsertionPointToStart(&m->getRegion(0).front());

    for (const auto& def : module.definitions)
    {
        builder.setInsertionPointToEnd(&m->getRegion(0).front());
        const auto loc = builder.getUnknownLoc();

        mlir::OperationState state(loc, "dsdl.schema");
        state.addAttribute("sym_name",
                           builder.getStringAttr(renderDefinitionSymbolBase(def.info.fullName,
                                                                            def.info.majorVersion,
                                                                            def.info.minorVersion)));
        state.addAttribute("c_type_name",
                           builder.getStringAttr(cTypeNameFromInfo(def.info, TypeNameVersioning::Unversioned)));
        state.addAttribute("header_path", builder.getStringAttr(relativeHeaderPath(def.info)));
        state.addAttribute("full_name", builder.getStringAttr(def.info.fullName));
        state.addAttribute("major", builder.getI32IntegerAttr(def.info.majorVersion));
        state.addAttribute("minor", builder.getI32IntegerAttr(def.info.minorVersion));
        if (def.request.sealed)
        {
            state.addAttribute("sealed", builder.getUnitAttr());
        }
        if (def.request.extentBits)
        {
            state.addAttribute("extent_bits", builder.getI64IntegerAttr(*def.request.extentBits));
        }
        if (def.info.fixedPortId)
        {
            state.addAttribute("fixed_port_id", builder.getI64IntegerAttr(*def.info.fixedPortId));
        }
        if (def.isService)
        {
            state.addAttribute("service", builder.getUnitAttr());
        }
        if (def.request.deprecated)
        {
            state.addAttribute("deprecated", builder.getUnitAttr());
        }
        if (const auto doc = docAttrText(def.doc))
        {
            state.addAttribute("doc", builder.getStringAttr(*doc));
        }
        state.addRegion();

        auto* schema     = builder.create(state);
        auto& schemaBody = schema->getRegion(0);
        schemaBody.push_back(new mlir::Block());

        builder.setInsertionPointToStart(&schemaBody.front());

        auto emitSection = [&](const SemanticSection& section, llvm::StringRef sectionName) {
            const std::string baseCTypeName    = cTypeNameFromInfo(def.info, TypeNameVersioning::Unversioned);
            std::string       sectionCTypeName = baseCTypeName;
            if (def.isService)
            {
                if (sectionName == "request")
                {
                    sectionCTypeName += renderSectionTypeSuffix(CodegenNamingLanguage::C, "request");
                }
                else if (sectionName == "response")
                {
                    sectionCTypeName += renderSectionTypeSuffix(CodegenNamingLanguage::C, "response");
                }
            }

            for (const auto& field : section.fields)
            {
                mlir::OperationState fieldState(loc, "dsdl.field");
                fieldState.addAttribute("name", builder.getStringAttr(field.name));
                // The unscoped default. The C backend stamps the scoped name over this before it
                // converts to EmitC, because only it knows what the struct declaration spells; what
                // stays here is what keeps hand-driven `dsdl-opt` runs able to name a member at all.
                fieldState.addAttribute("c_name",
                                        builder.getStringAttr(codegenProjectIdentifier(CodegenNamingLanguage::C,
                                                                                       IdentifierRole::FieldName,
                                                                                       field.name)));
                fieldState.addAttribute("type_name", builder.getStringAttr(field.type.str()));
                if (field.isPadding)
                {
                    fieldState.addAttribute("padding", builder.getUnitAttr());
                }
                if (const auto doc = docAttrText(field.doc))
                {
                    fieldState.addAttribute("doc", builder.getStringAttr(*doc));
                }
                if (!sectionName.empty())
                {
                    fieldState.addAttribute("section", builder.getStringAttr(sectionName));
                }
                (void) builder.create(fieldState);
            }

            for (const auto& constant : section.constants)
            {
                mlir::OperationState constState(loc, "dsdl.constant");
                constState.addAttribute("name", builder.getStringAttr(constant.name));
                constState.addAttribute("type_name", builder.getStringAttr(constant.type.str()));
                constState.addAttribute("value_text", builder.getStringAttr(constant.value.str()));
                if (const auto doc = docAttrText(constant.doc))
                {
                    constState.addAttribute("doc", builder.getStringAttr(*doc));
                }
                if (!sectionName.empty())
                {
                    constState.addAttribute("section", builder.getStringAttr(sectionName));
                }
                (void) builder.create(constState);
            }

            mlir::OperationState planState(loc, "dsdl.serialization_plan");
            if (!sectionName.empty())
            {
                planState.addAttribute("section", builder.getStringAttr(sectionName));
            }
            planState.addAttribute("c_type_name", builder.getStringAttr(sectionCTypeName));
            planState.addAttribute("c_serialize_symbol", builder.getStringAttr(sectionCTypeName + "__serialize_"));
            planState.addAttribute("c_deserialize_symbol", builder.getStringAttr(sectionCTypeName + "__deserialize_"));
            planState.addAttribute("min_bits", builder.getI64IntegerAttr(section.minBitLength));
            planState.addAttribute("max_bits", builder.getI64IntegerAttr(section.maxBitLength));
            if (section.sealed)
            {
                planState.addAttribute("sealed", builder.getUnitAttr());
            }
            if (section.extentBits)
            {
                planState.addAttribute("extent_bits", builder.getI64IntegerAttr(*section.extentBits));
            }
            if (section.isUnion)
            {
                planState.addAttribute("is_union", builder.getUnitAttr());
                if (!section.fields.empty())
                {
                    planState.addAttribute("union_tag_bits",
                                           builder.getI64IntegerAttr(section.fields.front().unionTagBits));
                }
                planState.addAttribute("union_option_count",
                                       builder.getI64IntegerAttr(
                                           static_cast<std::int64_t>(std::count_if(section.fields.begin(),
                                                                                   section.fields.end(),
                                                                                   [](const SemanticField& field) {
                                                                                       return !field.isPadding;
                                                                                   }))));
            }
            if (section.fixedSize)
            {
                planState.addAttribute("fixed_size", builder.getUnitAttr());
            }
            planState.addRegion();
            auto* plan       = builder.create(planState);
            auto& planRegion = plan->getRegion(0);
            planRegion.push_back(new mlir::Block());

            builder.setInsertionPointToStart(&planRegion.front());
            bool emittedPlanStep = false;
            for (const auto& field : section.fields)
            {
                mlir::OperationState alignState(loc, "dsdl.align");
                alignState.addAttribute("bits",
                                        builder.getI32IntegerAttr(
                                            static_cast<std::int32_t>(field.resolvedType.alignmentBits)));
                (void) builder.create(alignState);
                emittedPlanStep = true;

                mlir::OperationState ioState(loc, "dsdl.io");
                ioState.addAttribute("kind", builder.getStringAttr(fieldKind(field)));
                ioState.addAttribute("name", builder.getStringAttr(field.name));
                // The unscoped default, as on `dsdl.field` above.
                ioState.addAttribute("c_name",
                                     builder.getStringAttr(codegenProjectIdentifier(CodegenNamingLanguage::C,
                                                                                    IdentifierRole::FieldName,
                                                                                    field.name)));
                ioState.addAttribute("type_name", builder.getStringAttr(field.type.str()));
                if (const auto doc = docAttrText(field.doc))
                {
                    ioState.addAttribute("doc", builder.getStringAttr(*doc));
                }
                ioState.addAttribute("scalar_category",
                                     builder.getStringAttr(scalarCategoryName(field.resolvedType.scalarCategory)));
                ioState.addAttribute("cast_mode", builder.getStringAttr(castModeName(field.resolvedType.castMode)));
                ioState.addAttribute("array_kind", builder.getStringAttr(arrayKindName(field.resolvedType.arrayKind)));
                ioState.addAttribute("bit_length",
                                     builder.getI64IntegerAttr(
                                         static_cast<std::int64_t>(field.resolvedType.bitLength)));
                ioState.addAttribute("array_capacity", builder.getI64IntegerAttr(field.resolvedType.arrayCapacity));
                ioState.addAttribute("array_length_prefix_bits",
                                     builder.getI64IntegerAttr(field.resolvedType.arrayLengthPrefixBits));
                ioState.addAttribute("alignment_bits", builder.getI64IntegerAttr(field.resolvedType.alignmentBits));
                ioState.addAttribute("union_option_index",
                                     builder.getI64IntegerAttr(static_cast<std::int64_t>(field.unionOptionIndex)));
                ioState.addAttribute("union_tag_bits",
                                     builder.getI64IntegerAttr(static_cast<std::int64_t>(field.unionTagBits)));
                if (field.resolvedType.compositeType)
                {
                    const auto& ref = *field.resolvedType.compositeType;
                    ioState.addAttribute("composite_full_name", builder.getStringAttr(ref.fullName));
                    ioState.addAttribute("composite_major",
                                         builder.getI64IntegerAttr(static_cast<std::int64_t>(ref.majorVersion)));
                    ioState.addAttribute("composite_minor",
                                         builder.getI64IntegerAttr(static_cast<std::int64_t>(ref.minorVersion)));
                    ioState.addAttribute("composite_c_type_name",
                                         builder.getStringAttr(cTypeNameFromRef(ref, TypeNameVersioning::Unversioned)));
                    ioState.addAttribute("composite_sealed", builder.getBoolAttr(field.resolvedType.compositeSealed));
                    ioState.addAttribute("composite_extent_bits",
                                         builder.getI64IntegerAttr(field.resolvedType.compositeExtentBits));
                }
                ioState.addAttribute("min_bits", builder.getI64IntegerAttr(field.resolvedType.bitLengthSet.min()));
                ioState.addAttribute("max_bits", builder.getI64IntegerAttr(field.resolvedType.bitLengthSet.max()));
                (void) builder.create(ioState);
                emittedPlanStep = true;
            }

            if (!emittedPlanStep)
            {
                // Keep the plan region structurally non-empty for valid empty
                // request/response sections. This no-op alignment is removed by
                // lower-dsdl-serialization.
                mlir::OperationState alignState(loc, "dsdl.align");
                alignState.addAttribute("bits", builder.getI32IntegerAttr(1));
                (void) builder.create(alignState);
            }

            builder.setInsertionPointAfter(plan);
        };

        emitSection(def.request, def.isService ? "request" : "");
        if (def.response)
        {
            emitSection(*def.response, "response");
        }
    }

    if (diagnostics.hasErrors())
    {
        return nullptr;
    }

    // Proactively verify the lowered module here, in the lowering itself, so every op
    // verifier (serialization plan / io / align / field / constant) fires for ALL backends
    // — not only the C path, which happens to verify via its pass manager. Malformed IR is
    // rejected loudly at construction rather than flowing silently into a backend.
    //
    // Skipped for the LSP introspection path (verifyModule == false), which deliberately
    // lowers error-tolerant / partially-resolved semantic models to render a best-effort
    // debug snapshot — a partial module there is an acceptable view, not a codegen contract.
    if (verifyModule)
    {
        std::string                   verifyError;
        llvm::raw_string_ostream      verifyStream(verifyError);
        mlir::ScopedDiagnosticHandler handler(&context, [&](mlir::Diagnostic& diag) {
            verifyStream << diag.str() << '\n';
            return mlir::success();
        });
        if (mlir::failed(mlir::verify(m.getOperation())))
        {
            verifyStream.flush();
            diagnostics.error({"<mlir>", 1, 1}, "lowered MLIR failed verification: " + verifyError);
            return nullptr;
        }
    }
    return m;
}

}  // namespace llvmdsdl
