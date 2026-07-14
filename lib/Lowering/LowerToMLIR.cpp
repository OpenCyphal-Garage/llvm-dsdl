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
#include "llvmdsdl/Support/Diagnostics.h"
#include "mlir/IR/BuiltinAttributes.h"

namespace llvmdsdl
{
namespace
{

std::string mangleSymbol(std::string fullName, std::uint32_t major, std::uint32_t minor)
{
    for (char& c : fullName)
    {
        if (c == '.')
        {
            c = '_';
        }
    }
    return fullName + "_" + std::to_string(major) + "_" + std::to_string(minor);
}

std::string fieldKind(const SemanticField& f)
{
    return f.isPadding ? "padding" : "field";
}

bool isCKeyword(const std::string& name)
{
    static const std::set<std::string> kKeywords =
        {"auto",       "break",     "case",           "char",          "const",    "continue", "default",  "do",
         "double",     "else",      "enum",           "extern",        "float",    "for",      "goto",     "if",
         "inline",     "int",       "long",           "register",      "restrict", "return",   "short",    "signed",
         "sizeof",     "static",    "struct",         "switch",        "typedef",  "union",    "unsigned", "void",
         "volatile",   "while",     "_Alignas",       "_Alignof",      "_Atomic",  "_Bool",    "_Complex", "_Generic",
         "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local", "true",     "false"};
    return kKeywords.contains(name);
}

std::string sanitizeIdentifier(std::string name)
{
    if (name.empty())
    {
        return "_";
    }
    for (char& c : name)
    {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
        {
            c = '_';
        }
    }
    if (std::isdigit(static_cast<unsigned char>(name.front())))
    {
        name.insert(name.begin(), '_');
    }
    if (isCKeyword(name))
    {
        name += '_';
    }
    return name;
}

std::string cTypeNameFromInfo(const DiscoveredDefinition& info)
{
    std::string out;
    for (std::size_t i = 0; i < info.namespaceComponents.size(); ++i)
    {
        if (i > 0)
        {
            out += "__";
        }
        out += sanitizeIdentifier(info.namespaceComponents[i]);
    }
    if (!out.empty())
    {
        out += "__";
    }
    out += sanitizeIdentifier(info.shortName);
    return out;
}

std::string cTypeNameFromRef(const SemanticTypeRef& ref)
{
    std::string out;
    for (std::size_t i = 0; i < ref.namespaceComponents.size(); ++i)
    {
        if (i > 0)
        {
            out += "__";
        }
        out += sanitizeIdentifier(ref.namespaceComponents[i]);
    }
    if (!out.empty())
    {
        out += "__";
    }
    out += sanitizeIdentifier(ref.shortName);
    return out;
}

std::string headerFileName(const DiscoveredDefinition& info)
{
    return info.shortName + "_" + std::to_string(info.majorVersion) + "_" + std::to_string(info.minorVersion) + ".h";
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
                                              DiagnosticEngine&     diagnostics)
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
                           builder.getStringAttr(
                               mangleSymbol(def.info.fullName, def.info.majorVersion, def.info.minorVersion)));
        state.addAttribute("c_type_name", builder.getStringAttr(cTypeNameFromInfo(def.info)));
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
            const std::string baseCTypeName    = cTypeNameFromInfo(def.info);
            std::string       sectionCTypeName = baseCTypeName;
            if (def.isService)
            {
                if (sectionName == "request")
                {
                    sectionCTypeName += "__Request";
                }
                else if (sectionName == "response")
                {
                    sectionCTypeName += "__Response";
                }
            }

            for (const auto& field : section.fields)
            {
                mlir::OperationState fieldState(loc, "dsdl.field");
                fieldState.addAttribute("name", builder.getStringAttr(field.name));
                fieldState.addAttribute("c_name", builder.getStringAttr(sanitizeIdentifier(field.name)));
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
                ioState.addAttribute("c_name", builder.getStringAttr(sanitizeIdentifier(field.name)));
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
                    ioState.addAttribute("composite_c_type_name", builder.getStringAttr(cTypeNameFromRef(ref)));
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
    {
        std::string              verifyError;
        llvm::raw_string_ostream verifyStream(verifyError);
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
