//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements C backend code emission from lowered DSDL modules.
///
/// This file orchestrates pass pipelines, helper synthesis, and translation-unit rendering for generated C artifacts.
///
/// The line-building concatenations here carry NOLINT for
/// performance-inefficient-string-concatenation. Each one spells out a line of generated
/// source, and an append sequence would cost the reader the line itself.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/EmitCommon.h"
#include "llvmdsdl/CodeGen/SchemaNaming.h"
#include "llvmdsdl/CodeGen/SectionNaming.h"
#include "llvmdsdl/CodeGen/emitter/C.h"
#include "llvmdsdl/CodeGen/EmbeddedSources.h"
#include "llvmdsdl/CodeGen/SchemaLookup.h"
#include "llvmdsdl/CodeGen/TypeMetadata.h"
#include "llvmdsdl/IR/DSDLOps.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <mlir/IR/Attributes.h>
#include <mlir/IR/Block.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/OperationSupport.h>
#include <mlir/IR/OwningOpRef.h>
#include <mlir/IR/Region.h>
#include <mlir/Support/LLVM.h>
#include <cctype>  // IWYU pragma: keep -- libstdc++ reaches this transitively; libc++ needs it named.
#include <filesystem>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <llvm/ADT/STLExtras.h>
#include <llvm/Support/ErrorHandling.h>
#include <mlir/IR/BuiltinAttributeInterfaces.h>
#include <mlir/IR/BuiltinTypes.h>
#include <set>
#include <utility>
#include "llvmdsdl/IR/DSDLTypes.h"
#include "llvmdsdl/CodeGen/BodyTranslator.h"
#include "llvmdsdl/CodeGen/SourceWriter.h"
#include "llvmdsdl/CodeGen/TypeStorage.h"
#include "llvmdsdl/Transforms/PlanSteps.h"
#include "llvmdsdl/CodeGen/emitter/CHeaderRender.h"
#include "llvmdsdl/CodeGen/ConstantLiteralRender.h"
#include "llvmdsdl/CodeGen/DefinitionDependencies.h"
#include "llvmdsdl/CodeGen/DefinitionIndex.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/CodeGen/StorageTypeTokens.h"
#include "llvmdsdl/Transforms/Passes.h"
#include "mlir/Conversion/Passes.h"  // IWYU pragma: keep
#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVMPass.h"
#include "mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Evaluator.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Version.h"
#include "llvmdsdl/Support/Language.h"
#include "mlir/IR/BuiltinAttributes.h"

namespace llvmdsdl::emitter::c
{
namespace
{

std::string headerFileName(const DiscoveredDefinition& info)
{
    return renderDefinitionFileStem(Language::C, info.shortName, info.majorVersion, info.minorVersion) + ".h";
}

std::string sectionIRFunctionStem(const SemanticDefinition& def, const std::string& sectionName)
{
    return renderDefinitionSymbolBase(def.info.fullName, def.info.majorVersion, def.info.minorVersion) +
           renderSectionSymbolSuffix(sectionName);
}

/// @brief The object a definition's serialisation is assembled into, beside its header.
std::string objectFileName(const DiscoveredDefinition& info)
{
    auto name = headerFileName(info);
    if ((name.size() >= 2U) && (name.substr(name.size() - 2U) == ".h"))
    {
        name.replace(name.size() - 2U, 2U, ".o");
        return name;
    }
    return name + ".o";
}

std::string implFileName(const DiscoveredDefinition& info)
{
    auto name = headerFileName(info);
    if (name.size() >= 2U && name.substr(name.size() - 2U) == ".h")
    {
        name.replace(name.size() - 2U, 2U, ".c");
    }
    else
    {
        name += ".c";
    }
    return name;
}

std::string headerGuard(const DiscoveredDefinition& info)
{
    return renderIncludeGuard(Language::C, "LLVMDSDL_", info.fullName, info.majorVersion, info.minorVersion, "_H");
}

std::string valueToCExpr(const TypeExprAST& type, const Value& value)
{
    return renderConstantLiteral(Language::C, value, makeConstantTypeInfo(type));
}

std::string unsignedStorageType(const std::uint32_t bitLength)
{
    return renderUnsignedStorageToken(Language::C, bitLength);
}

std::string signedStorageType(const std::uint32_t bitLength)
{
    return renderSignedStorageToken(Language::C, bitLength);
}

class EmitterContext final
{
public:
    EmitterContext(const SemanticModule&    semantic,
                   const bool               emitDeprecationAttributes,
                   const bool               hostImageFolded,
                   const bool               accessorsOnly,
                   const TypeNameVersioning typeNameVersioning)
        : index_(semantic)
        , emitDeprecationAttributes_(emitDeprecationAttributes)
        , hostImageFolded_(hostImageFolded)
        , accessorsOnly_(accessorsOnly)
        , typeNameVersioning_(typeNameVersioning)
    {
    }

    /// @brief Whether generated type names carry the definition's version.
    TypeNameVersioning typeNameVersioning() const
    {
        return typeNameVersioning_;
    }

    /// @brief True when `@deprecated` definitions should carry a language-native attribute.
    bool emitDeprecationAttributes() const
    {
        return emitDeprecationAttributes_;
    }

    /// @brief Whether a host-image section's bodies were folded into one move.
    bool hostImageFolded() const
    {
        return hostImageFolded_;
    }

    /// @brief Whether the run emits the field accessors and neither the object type nor the serdes.
    bool accessorsOnly() const
    {
        return accessorsOnly_;
    }

    const SemanticDefinition* find(const SemanticTypeRef& ref) const
    {
        return index_.find(ref);
    }

    std::string cTypeName(const SemanticDefinition& def) const
    {
        return cTypeNameFromInfo(def.info, typeNameVersioning_);
    }

    std::string cTypeName(const SemanticTypeRef& ref) const
    {
        if (const auto* def = find(ref))
        {
            return cTypeName(*def);
        }

        DiscoveredDefinition tmp;
        tmp.fullName            = ref.fullName;
        tmp.shortName           = ref.shortName;
        tmp.namespaceComponents = ref.namespaceComponents;
        tmp.majorVersion        = ref.majorVersion;
        tmp.minorVersion        = ref.minorVersion;
        return cTypeNameFromInfo(tmp, typeNameVersioning_);
    }

    static std::string relativeHeaderPath(const SemanticDefinition& def)
    {
        std::filesystem::path p;
        for (const auto& ns : def.info.namespaceComponents)
        {
            p /= ns;
        }
        p /= headerFileName(def.info);
        return p.generic_string();
    }

    std::string relativeHeaderPath(const SemanticTypeRef& ref) const
    {
        if (const auto* def = find(ref))
        {
            return relativeHeaderPath(*def);
        }
        std::filesystem::path p;
        for (const auto& ns : ref.namespaceComponents)
        {
            p /= ns;
        }
        DiscoveredDefinition tmp;
        tmp.shortName    = ref.shortName;
        tmp.majorVersion = ref.majorVersion;
        tmp.minorVersion = ref.minorVersion;
        p /= headerFileName(tmp);
        return p.generic_string();
    }

private:
    DefinitionIndex    index_;
    bool               emitDeprecationAttributes_{false};
    bool               hostImageFolded_{false};
    bool               accessorsOnly_{false};
    TypeNameVersioning typeNameVersioning_{TypeNameVersioning::Unversioned};
};

SourceWriter makeCWriter(std::ostringstream& out)
{
    return SourceWriter{out, IndentPolicy::spaces(2)};
}

std::string generatedCommentLine(llvm::StringRef detail)
{
    return llvm::formatv("/* Generated by llvmdsdl {0} ({1}). */", llvmdsdl::kVersionString, detail).str();
}

/// @brief Makes one line of documentation safe to place inside a C block comment.
///
/// @details
/// Both delimiters have to go, not just the closing one. `*/` ends the comment early and
/// spills the rest of the sentence into code position. `/*` is subtler: the comment still ends where
/// it should, so the file compiles -- but it warns under `-Wcomment`, which is an error under
/// `-Werror`, and generated code that cannot be compiled with warnings as errors is generated code
/// somebody has to work around. Definitions do write both: the standard namespace and the showroom
/// both carry doc comments that quote C and JSDoc syntax.
///
/// A space is inserted rather than a backslash or an entity.
std::string sanitizeCCommentText(std::string text)
{
    const auto separate = [&text](const char* delimiter, const char* replacement) {
        std::size_t pos = 0U;
        while ((pos = text.find(delimiter, pos)) != std::string::npos)
        {
            text.replace(pos, 2U, replacement);
            pos += 3U;
        }
    };
    separate("*/", "* /");
    separate("/*", "/ *");
    return text;
}

void emitAttachedDocC(SourceWriter& w, const AttachedDoc& doc)
{
    for (const auto& line : doc.lines)
    {
        w.line("/* " + sanitizeCCommentText(line.text) + " */");
    }
}

std::string cTypeFromFieldType(const SemanticFieldType& type, const EmitterContext& ctx)
{
    switch (type.scalarCategory)
    {
    case SemanticScalarCategory::Bool:
        return "bool";
    case SemanticScalarCategory::Byte:
    case SemanticScalarCategory::Utf8:
    case SemanticScalarCategory::UnsignedInt:
        return unsignedStorageType(type.bitLength);
    case SemanticScalarCategory::SignedInt:
        return signedStorageType(type.bitLength);
    case SemanticScalarCategory::Float:
        if (type.bitLength == 64U)
        {
            return "double";
        }
        return "float";
    case SemanticScalarCategory::Void:
        return "uint8_t";
    case SemanticScalarCategory::Composite:
        if (type.compositeType)
        {
            return renderCTagSpelling(ctx.cTypeName(*type.compositeType));
        }
        return "uint8_t";
    }
    return "uint8_t";
}

/// @brief Declares the tag value that selects each of a union's options.
void emitUnionOptionTagMacros(SourceWriter&          w,
                              const std::string&     typeName,
                              const SemanticSection& section,
                              const SectionMetadata& metadata)
{
    if (!metadata.isUnion)
    {
        return;
    }
    const NamingScope constScope = makeSectionConstantScope(Language::C, section, {});
    for (const auto& option : metadata.unionOptions)
    {
        w.line("#define " + typeName + "_" +
               constScope.get(IdentifierRole::MacroName, unionOptionTagName(Language::C, option.name)) + " " +
               std::to_string(option.tag) + "U");
    }
    w.blank();
}

void emitArrayMacros(SourceWriter& w, const std::string& typeName, const SemanticSection& section)
{
    const NamingScope constScope = makeSectionConstantScope(Language::C, section, {});
    for (const auto& field : section.fields)
    {
        if (field.isPadding || field.resolvedType.arrayKind == ArrayKind::None)
        {
            continue;
        }
        const auto named = [&](const ArrayMetadataKind kind) {
            return constScope.get(IdentifierRole::MacroName, arrayMetadataName(Language::C, field.name, kind));
        };
        w.line("#define " + typeName + "_" + named(ArrayMetadataKind::Capacity) + " " +
               std::to_string(field.resolvedType.arrayCapacity) + "U");
        w.line("#define " + typeName + "_" + named(ArrayMetadataKind::IsVariableLength) + " " +
               (isVariableArray(field.resolvedType.arrayKind) ? "true" : "false"));
    }
    if (!section.fields.empty())
    {
        w.blank();
    }
}

void emitSectionTypedef(SourceWriter&                         w,
                        const std::string&                    typeName,
                        const SemanticSection&                section,
                        const SectionMetadata&                metadata,
                        const EmitterContext&                 ctx,
                        const bool                            deprecatedAttribute,
                        const mlir::dsdl::SerializationPlanOp plan)
{
    // One scope for the whole section: the keyword and claimed-name escapes make the projection
    // many-to-one, so two distinct DSDL fields can otherwise land on one member. The serialiser
    // reads the same scope through the `c_name` attributes stamped in `emitCImplementations`.
    const NamingScope fieldScope = makeSectionFieldScope(Language::C, section);
    w.open("typedef struct " + typeName + " {");

    std::size_t emitted = 0;
    for (const auto& field : section.fields)
    {
        if (field.isPadding)
        {
            continue;
        }

        const auto cMember    = fieldScope.get(IdentifierRole::FieldName, field.name);
        const bool viewMember = std::ranges::find(metadata.viewMembers, field.name) != metadata.viewMembers.end();
        const auto baseType =
            viewMember ? std::string{"dsdl_runtime_view_t"} : cTypeFromFieldType(field.resolvedType, ctx);

        emitAttachedDocC(w, field.doc);
        if (viewMember)
        {
            // NOLINTNEXTLINE(performance-inefficient-string-concatenation)
            w.line("/* Held as a view: the bytes of the " + ctx.cTypeName(*field.resolvedType.compositeType) +
                   " this field carries, and their count" +
                   ((field.resolvedType.arrayKind == ArrayKind::None) ? "" : ", per element") + ". */");
        }

        if (field.resolvedType.arrayKind == ArrayKind::None)
        {
            // NOLINTNEXTLINE(performance-inefficient-string-concatenation)
            w.line(baseType + " " + cMember + ";");
            ++emitted;
            continue;
        }

        if (field.resolvedType.arrayKind == ArrayKind::Fixed)
        {
            if (field.resolvedType.scalarCategory == SemanticScalarCategory::Bool)
            {
                w.line("uint8_t " + cMember + "[(" + std::to_string(field.resolvedType.arrayCapacity) +
                       "U + 7U) / 8U];");
            }
            else
            {
                // NOLINTBEGIN(performance-inefficient-string-concatenation)
                w.line(baseType + " " + cMember + "[" + std::to_string(field.resolvedType.arrayCapacity) + "U];");
                // NOLINTEND(performance-inefficient-string-concatenation)
            }
            ++emitted;
            continue;
        }

        w.open("struct {");
        if (field.resolvedType.scalarCategory == SemanticScalarCategory::Bool)
        {
            w.line("uint8_t bitpacked[(" + std::to_string(field.resolvedType.arrayCapacity) + "U + 7U) / 8U];");
        }
        else
        {
            w.line(baseType + " elements[" + std::to_string(field.resolvedType.arrayCapacity) + "U];");
        }
        w.line("size_t count;");
        w.close("} " + cMember + ";");
        ++emitted;
    }

    if (section.isUnion)
    {
        // Tag storage must match the wire tag width (uint8 for <=256 options, uint16 for
        // 257..65536, etc.); a hardcoded uint8_t truncates a wide tag and mis-dispatches.
        w.line(unsignedStorageType(unionTagBits(plan)) + " _tag_;");
        ++emitted;
    }

    if (emitted == 0)
    {
        w.line("uint8_t _dummy_;");
    }

    if (deprecatedAttribute)
    {
        // After the typedef name, not between the closing brace and the name. The two positions are
        // not equivalent: GCC reads the earlier one as deprecating the struct type and warns once,
        // at the definition, and says nothing where the typedef is used. Placed after the name it
        // deprecates the typedef, and the diagnostic lands where it is useful: on the code that
        // names it. Clang warns either way, so this only shows up on GCC. Generated code never
        // names the typedef; it spells the type through its tag, see renderCTagSpelling.
        w.close("} " + typeName + " __attribute__((deprecated));");
    }
    else
    {
        w.close("} " + typeName + ";");
    }
    w.blank();

    // The verdict was decided under natural alignment; this pins the layout on the target the
    // header is compiled for. Through the tag, as the typedef may be deprecated, and through the
    // runtime's macro, as the header is included from C++ translation units too.
    if (metadata.hostImage.holds && !metadata.hostImageMembers.empty())
    {
        const std::string tag = renderCTagSpelling(typeName);
        // NOLINTBEGIN(performance-inefficient-string-concatenation)
        w.line("DSDL_RUNTIME_STATIC_ASSERT(sizeof(" + tag +
               ") == " + std::to_string(metadata.serializationBufferSizeBytes) + "U, \"" + typeName +
               ": the structure is not the byte image its serialisation assumes\");");
        for (const auto& member : metadata.hostImageMembers)
        {
            const std::string cMember = fieldScope.get(IdentifierRole::FieldName, member.fieldName);
            w.line("DSDL_RUNTIME_STATIC_ASSERT(offsetof(" + tag + ", " + cMember +
                   ") == " + std::to_string(member.offsetBytes) + "U, \"" + typeName + "." + cMember +
                   ": not at the offset its serialisation assumes\");");
        }
        // NOLINTEND(performance-inefficient-string-concatenation)
        w.blank();
    }

    if (metadata.isUnion)
    {
        w.line("#define " + typeName + "_UNION_OPTION_COUNT_ " + std::to_string(metadata.unionOptions.size()) + "U");
        w.blank();
    }
}

void emitSectionConstants(SourceWriter& w, const std::string& typeName, const SemanticSection& section)
{
    // Two DSDL constants can project onto one macro token -- `foo_bar` and `FOO_BAR` both upper-case
    // to FOO_BAR -- and a duplicate `#define` silently takes the second value. The scope keeps them
    // apart. It does not keep them off the generated metadata macros: those carry a trailing `_`,
    // which is a name a DSDL constant can reach rather than one it cannot, so they are claimed in
    // the policy tables and escaped by the projection this reads back.
    NamingScope const constScope = makeSectionConstantScope(Language::C, section, {});
    for (const auto& c : section.constants)
    {
        emitAttachedDocC(w, c.doc);
        w.line("#define " + typeName + "_" + constScope.get(IdentifierRole::ConstantName, c.name) + " (" +
               valueToCExpr(c.type, c.value) + ")");
    }
    if (!section.constants.empty())
    {
        w.blank();
    }
}

void emitSectionMetadata(SourceWriter& w, const std::string& typeName, const SectionMetadata& metadata)
{
    for (const auto& line : renderTypeMetadataMacros(typeName, metadata))
    {
        w.line(line);
    }
    w.blank();
}

/// @brief Wraps each of a union's option tags in a test and a selector.
///
/// The tag constants say what a tag value means; these say it in the two places a caller reaches
/// for. They read the constant rather than the number, so an option's tag is written down once.
void emitUnionOptionWrappers(SourceWriter&          w,
                             const std::string&     typeName,
                             const SemanticSection& section,
                             const SectionMetadata& metadata)
{
    if (!metadata.isUnion)
    {
        return;
    }
    const NamingScope fieldScope = makeSectionFieldScope(Language::C, section);
    const NamingScope tagScope   = makeSectionConstantScope(Language::C, section, {});
    const std::string objectType = renderCTagSpelling(typeName);
    for (const auto& option : metadata.unionOptions)
    {
        const std::string member = fieldScope.get(IdentifierRole::FieldName, option.name);
        const std::string tag =
            typeName + "_" + tagScope.get(IdentifierRole::MacroName, unionOptionTagName(Language::C, option.name));

        // NOLINTBEGIN(performance-inefficient-string-concatenation)
        w.line("static inline bool " + typeName + "__is_" + member + "_(const " + objectType + "* const obj)");
        // NOLINTEND(performance-inefficient-string-concatenation)
        w.open("{");
        w.line("return (obj != NULL) && (obj->_tag_ == " + tag + ");");
        w.close("}");
        w.blank();

        // NOLINTBEGIN(performance-inefficient-string-concatenation)
        w.line("static inline void " + typeName + "__select_" + member + "_(" + objectType + "* const obj)");
        // NOLINTEND(performance-inefficient-string-concatenation)
        w.open("{");
        w.open("if (obj != NULL) {");
        w.line("obj->_tag_ = " + tag + ";");
        w.close("}");
        w.close("}");
        w.blank();
    }
}

void emitSection(SourceWriter&              w,
                 const EmitterContext&      ctx,
                 const SemanticDefinition&  def,
                 const std::string&         typeName,
                 const std::string&         sectionName,
                 const SemanticSection&     section,
                 const AttachedDoc&         typeDoc,
                 const mlir::dsdl::SchemaOp schema)
{
    const SectionMetadata                 metadata = sectionMetadata(def.info, section, schema, sectionName);
    const mlir::dsdl::SerializationPlanOp plan     = sectionPlan(schema, sectionName);
    emitSectionMetadata(w, typeName, metadata);
    // A folded body moves the object as the wire's bytes, which holds only where the host orders
    // them as the wire does. This source is compiled for a target the generator did not see.
    if (ctx.hostImageFolded() && metadata.hostImage.holds && !ctx.accessorsOnly())
    {
        for (const auto& line : renderLittleEndianGuardLines(typeName))
        {
            w.line(line);
        }
        w.blank();
    }
    emitSectionConstants(w, typeName, section);
    const auto irStem = sectionIRFunctionStem(def, sectionName);
    // The object type and its serialisation, which an accessors-only run leaves out.
    if (!ctx.accessorsOnly())
    {
        emitArrayMacros(w, typeName, section);
        emitUnionOptionTagMacros(w, typeName, section, metadata);
        emitAttachedDocC(w,
                         docWithDeprecationNotice(typeDoc,
                                                  section.deprecated,
                                                  def.info.fullName,
                                                  def.info.majorVersion,
                                                  def.info.minorVersion));
        emitSectionTypedef(w,
                           typeName,
                           section,
                           metadata,
                           ctx,
                           section.deprecated && ctx.emitDeprecationAttributes(),
                           plan);

        const auto objectType = renderCTagSpelling(typeName);
        w.line("int8_t " + irStem + "__serialize_ir_(const " + objectType +
               "* obj, uint8_t* buffer, size_t* "
               "inout_buffer_size_bytes);");
        w.line("int8_t " + irStem + "__deserialize_ir_(" + objectType +
               "* out_obj, const uint8_t* buffer, size_t* "
               "inout_buffer_size_bytes);");
        w.line("int8_t " + irStem + "__initialize_ir_(" + objectType + "* out_obj);");
        w.blank();

        w.line("static inline int8_t " + typeName + "__serialize_(const " + objectType +
               "* const obj, uint8_t* const buffer, size_t* const "
               "inout_buffer_size_bytes)");
        w.open("{");
        w.line("return " + irStem + "__serialize_ir_(obj, buffer, inout_buffer_size_bytes);");
        w.close("}");
        w.blank();

        w.line("static inline int8_t " + typeName + "__deserialize_(" + objectType +
               "* const out_obj, const uint8_t* buffer, size_t* const "
               "inout_buffer_size_bytes)");
        w.open("{");
        w.line("return " + irStem + "__deserialize_ir_(out_obj, buffer, inout_buffer_size_bytes);");
        w.close("}");
        w.blank();

        w.line("static inline int8_t " + typeName + "__initialize_(" + objectType + "* const out_obj)");
        w.open("{");
        w.line("return " + irStem + "__initialize_ir_(out_obj);");
        w.close("}");
        w.blank();
    }
    // A wire-flat section's scalar fields, and the elements of its fixed arrays of scalars, have a
    // getter and a setter beside the bodies: one read or one write at the field's offset, through
    // the helpers the bodies normalise with. The lowered entry points take the size, and an index,
    // as an int64_t and hold an integer in one; the wrappers speak the member's own type.
    // A union whose options are all flat and of one length has them too, at the offset after its
    // tag, and the tag as a member named `_tag_`; the pass that builds the bodies decides which
    // unions those are, and the tag's getter is the sign that it did.
    auto       schemaModule = schema ? schema->getParentOfType<mlir::ModuleOp>() : mlir::ModuleOp{};
    const bool unionFlat    = section.isUnion && schemaModule &&
                              (schemaModule.lookupSymbol<mlir::func::FuncOp>(irStem + "__get__tag__ir_") != nullptr);
    if ((metadata.wireFlat.holds && !section.isUnion) || unionFlat)
    {
        const NamingScope fieldScope = makeSectionFieldScope(Language::C, section);
        struct Subject final
        {
            std::string       name;
            std::string       cMember;
            SemanticFieldType type;
        };
        std::vector<Subject> subjects;
        if (unionFlat)
        {
            SemanticFieldType tag;
            tag.scalarCategory = SemanticScalarCategory::UnsignedInt;
            tag.bitLength      = metadata.unionTagBits;
            subjects.push_back(Subject{"_tag_", "_tag_", tag});
        }
        for (const auto& field : section.fields)
        {
            if (!field.isPadding)
            {
                subjects.push_back(
                    Subject{field.name, fieldScope.get(IdentifierRole::FieldName, field.name), field.resolvedType});
            }
        }
        for (const auto& [name, cMember, type] : subjects)
        {
            const ArrayKind kind = type.arrayKind;
            if ((kind != ArrayKind::None) && (kind != ArrayKind::Fixed))
            {
                continue;
            }
            const bool        indexed   = kind == ArrayKind::Fixed;
            const std::string irIndex   = indexed ? ", int64_t index" : "";
            const std::string cIndex    = indexed ? ", const size_t index" : "";
            const std::string passIndex = indexed ? ", (int64_t) index" : "";
            const std::string size      = "(int64_t) buffer_size_bytes";
            if (type.scalarCategory == SemanticScalarCategory::Composite)
            {
                // A nested composite's getter answers the buffer from the field's offset and, through
                // the pointer, what remains of this one, for the nested type's own accessors.
                // NOLINTBEGIN(performance-inefficient-string-concatenation)
                const std::string irGet = irStem + "__get_" + name + "_ir_";
                w.line("const uint8_t* " + irGet + "(const uint8_t* buffer, int64_t buffer_size_bytes" + irIndex +
                       ", size_t* out_size);");
                w.blank();
                w.line("static inline const uint8_t* " + typeName + "__get_" + cMember +
                       "_(const uint8_t* const buffer, const size_t buffer_size_bytes" + cIndex +
                       ", size_t* const out_size)");
                w.open("{");
                w.line("size_t               sub_size = 0;");
                w.line("const uint8_t* const sub      = " + irGet + "(buffer, " + size + passIndex + ", &sub_size);");
                w.line("if (out_size != NULL)");
                w.open("{");
                w.line("*out_size = sub_size;");
                w.close("}");
                w.line("return sub;");
                w.close("}");
                w.blank();
                // NOLINTEND(performance-inefficient-string-concatenation)
                continue;
            }
            const bool  isFloat = type.scalarCategory == SemanticScalarCategory::Float;
            const bool  isBool  = type.scalarCategory == SemanticScalarCategory::Bool;
            std::string irType  = "int64_t";
            if (isFloat)
            {
                irType = (type.bitLength <= 32) ? "float" : "double";
            }
            const std::string cType = cTypeFromFieldType(type, ctx);
            // NOLINTBEGIN(performance-inefficient-string-concatenation)
            const std::string irGet = irStem + "__get_" + name + "_ir_";
            const std::string irSet = irStem + "__set_" + name + "_ir_";
            w.line(irType + " " + irGet + "(const uint8_t* buffer, int64_t buffer_size_bytes" + irIndex + ");");
            w.line("int8_t " + irSet + "(uint8_t* buffer, int64_t buffer_size_bytes" + irIndex + ", " + irType +
                   " value);");
            w.blank();
            w.line("static inline " + cType + " " + typeName + "__get_" + cMember +
                   "_(const uint8_t* const buffer, const size_t buffer_size_bytes" + cIndex + ")");
            w.open("{");
            if (isBool)
            {
                w.line("return " + irGet + "(buffer, " + size + passIndex + ") != 0;");
            }
            else
            {
                w.line("return (" + cType + ") " + irGet + "(buffer, " + size + passIndex + ");");
            }
            w.close("}");
            w.blank();
            w.line("static inline int8_t " + typeName + "__set_" + cMember +
                   "_(uint8_t* const buffer, const size_t buffer_size_bytes" + cIndex + ", const " + cType + " value)");
            w.open("{");
            w.line("return " + irSet + "(buffer, (int64_t) buffer_size_bytes" + passIndex + ", (" + irType + ") " +
                   (isBool ? std::string("(value ? 1 : 0)") : std::string("value")) + ");");
            w.close("}");
            w.blank();
            // NOLINTEND(performance-inefficient-string-concatenation)
        }
    }

    if (!ctx.accessorsOnly())
    {
        emitUnionOptionWrappers(w, typeName, section, metadata);
    }
}

llvm::Expected<std::string> loadRuntimeHeader()
{
    if (const auto data = embedded_sources::find("dsdl_runtime.h"))
    {
        return std::string(*data);
    }
    return llvm::createStringError(llvm::inconvertibleErrorCode(), "embedded runtime source missing: dsdl_runtime.h");
}

std::string renderHeader(const SemanticDefinition& def, const EmitterContext& ctx, mlir::ModuleOp module)
{
    const mlir::dsdl::SchemaOp schema = schemaOf(module, def);
    std::ostringstream         out;
    // The declarations are rendered first, so that the includes can be read off them.
    std::ostringstream body;
    SourceWriter       w            = makeCWriter(body);
    const auto         guard        = headerGuard(def.info);
    const auto         baseTypeName = ctx.cTypeName(def);

    out << generatedCommentLine("C backend") << "\n";
    out << "/* Source: " << def.info.fullName << "." << def.info.majorVersion << "." << def.info.minorVersion
        << " */\n\n";
    out << "#ifndef " << guard << "\n";
    out << "#define " << guard << "\n\n";

    // Under the unversioned scheme this type's name carries no version, so two versions of it are
    // one identifier. Generating both is fine -- they are separate headers -- but including both is
    // not, and saying so here beats a cascade of redefinitions from inside generated code.
    if (ctx.typeNameVersioning() == TypeNameVersioning::Unversioned)
    {
        const auto [anyVersion, thisVersion] =
            renderVersionSentinelMacros(Language::C, def.info.fullName, def.info.majorVersion, def.info.minorVersion);
        out << "#if defined(" << anyVersion << ") && !defined(" << thisVersion << ")\n";
        out << "#  error \"" << def.info.fullName
            << ": two versions of one type in one translation unit, but generated type names are "
               "unversioned. Regenerate with --versioned-type-names to use both.\"\n";
        out << "#endif\n";
        out << "#define " << anyVersion << "\n";
        out << "#define " << thisVersion << "\n\n";
    }

    if (def.isService)
    {
        const auto requestType  = renderSectionTypeName(Language::C, baseTypeName, "request");
        const auto responseType = renderSectionTypeName(Language::C, baseTypeName, "response");

        for (const auto& line : renderServiceAliasIdentityMacros(baseTypeName,
                                                                 def.info.fullName,
                                                                 def.info.majorVersion,
                                                                 def.info.minorVersion,
                                                                 def.info.fixedPortId))
        {
            w.line(line);
        }
        w.blank();

        emitSection(w, ctx, def, requestType, "request", def.request, def.doc, schema);
        if (def.response)
        {
            emitSection(w, ctx, def, responseType, "response", *def.response, def.doc, schema);
        }
        for (const auto& line :
             renderServiceAliasBridgeLines(baseTypeName,
                                           requestType,
                                           def.request.deprecated && ctx.emitDeprecationAttributes()))
        {
            w.line(line);
        }
        w.blank();

        // The wrappers call the request's serialisation, which an accessors-only run does not emit.
        if (!ctx.accessorsOnly())
        {
            for (const auto& line : renderServiceAliasWrapperLines(baseTypeName, requestType))
            {
                w.line(line);
            }
        }
    }
    else
    {
        emitSection(w, ctx, def, baseTypeName, "", def.request, def.doc, schema);
    }

    // Each header is included where the declarations take something from it. A nested type's
    // header is included where this header names the type: a field held as a view names none,
    // and an accessors-only header names none, since its composite getters answer bytes.
    const std::string                         declarations = body.str();
    static const std::vector<IncludeProvider> standardHeaders{
        {"<stdbool.h>", {"bool", "true", "false"}},
        {"<stddef.h>", {"size_t", "offsetof("}},
        {"<stdint.h>", {"int8_t", "int16_t", "int32_t", "int64_t"}},
        {"<string.h>", {"memcpy(", "memset(", "memcmp(", "memmove("}},
        {"\"dsdl_runtime.h\"", {"dsdl_runtime_", "DSDL_RUNTIME_"}},
    };
    out << includeLinesFor(declarations, standardHeaders);
    if (!ctx.accessorsOnly())
    {
        for (const auto& depRef : collectDefinitionCompositeDependencies(def, /*referencedOnly=*/true))
        {
            if (const auto* dep = ctx.find(depRef))
            {
                out << "#include \"" << EmitterContext::relativeHeaderPath(*dep) << "\"\n";
            }
        }
    }
    out << "\n" << declarations;
    out << "#endif /* " << guard << " */\n";
    return out.str();
}

/// @brief Clones the schemas @p target reaches, for their layout alone.
///
/// A C translation unit needs only the nested type's name, which its header supplies. An object
/// addresses members by position, so it needs the nested type's layout, and that lives in the
/// nested type's own schema. Their functions stay behind: the serialisation of a nested type
/// belongs to the nested type's object.
void cloneReachableSchemas(mlir::Operation*                         target,
                           mlir::ModuleOp                           destination,
                           const llvm::StringMap<mlir::Operation*>& byKey)
{
    llvm::SmallVector<mlir::Operation*, 8> pending{target};
    llvm::StringSet<>                      seen;
    while (!pending.empty())
    {
        mlir::Operation* const at = pending.pop_back_val();
        at->walk([&](mlir::dsdl::IOOp op) {
            if (!op.isComposite())
            {
                return;
            }
            const std::string key = op.getCompositeFullName()->str() + "." + std::to_string(*op.getCompositeMajor()) +
                                    "." + std::to_string(*op.getCompositeMinor());
            if (!seen.insert(key).second)
            {
                return;
            }
            const auto found = byKey.find(key);
            if (found == byKey.end())
            {
                return;
            }
            mlir::Operation* const clone = found->second->clone();
            destination.getBodyRegion().front().push_back(clone);
            pending.push_back(clone);
        });
    }
}

/// @brief Clones into @p destination the functions the pipeline built for @p schema: its helpers
///        and its two bodies, in the order @p source holds them.
void cloneFunctionsOf(mlir::dsdl::SchemaOp schema, mlir::ModuleOp source, mlir::ModuleOp destination)
{
    const llvm::StringRef owner = schema.getSymName();
    for (const mlir::func::FuncOp fn : source.getBodyRegion().front().getOps<mlir::func::FuncOp>())
    {
        const auto tag = fn->getAttrOfType<mlir::StringAttr>("llvmdsdl.schema_sym");
        if (tag && tag.getValue() == owner)
        {
            destination.getBodyRegion().front().push_back(fn->clone());
        }
    }
}

/// @brief Lowers a per-definition module the rest of the way and assembles it.
///
/// `convert-dsdl-to-llvm` leaves func, arith and scf standing; the upstream conversions finish
/// the job, and what comes out is translated to LLVM IR and handed to the target's own
/// assembler. No C is written and no compiler is invoked.
/// @param[in] module The per-definition module, already converted out of the DSDL dialect.
/// @param[in] triple The target to assemble for; the host's own when empty.
/// @param[out] object Receives the object file's bytes.
/// @return Success or a description of what failed.
llvm::Error assembleModule(mlir::ModuleOp module, const std::string& triple, std::string& object)
{
    mlir::PassManager pm(module.getContext());
    pm.addPass(mlir::createSCFToControlFlowPass());
    pm.addPass(mlir::createArithToLLVMConversionPass());
    pm.addPass(mlir::createConvertControlFlowToLLVMPass());
    pm.addPass(mlir::createConvertFuncToLLVMPass());
    pm.addPass(mlir::createReconcileUnrealizedCastsPass());
    if (mlir::failed(pm.run(module)))
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "LLVM lowering pipeline failed");
    }

    mlir::DialectRegistry registry;
    mlir::registerBuiltinDialectTranslation(registry);
    mlir::registerLLVMDialectTranslation(registry);
    module.getContext()->appendDialectRegistry(registry);

    llvm::LLVMContext llvmContext;
    auto              ir = mlir::translateModuleToLLVMIR(module, llvmContext);
    if (!ir)
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "translation to LLVM IR failed");
    }

    const llvm::Triple  resolved(triple.empty() ? llvm::sys::getDefaultTargetTriple() : triple);
    std::string         lookupError;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(resolved, lookupError);
    if (target == nullptr)
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "no backend for target '" + resolved.str() + "': " + lookupError);
    }
    std::unique_ptr<llvm::TargetMachine> machine(
        target->createTargetMachine(resolved, "generic", "", {}, llvm::Reloc::PIC_));
    if (!machine)
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "could not create a target machine for '" + resolved.str() + "'");
    }
    ir->setTargetTriple(resolved);
    ir->setDataLayout(machine->createDataLayout());

    // The plan is emitted as it is written: a primitive per field, each with its own stack slot
    // and its own loop. Codegen alone does not inline across those calls, so the module is run
    // through the ordinary optimisation pipeline first -- the same one a C compiler would apply
    // to the sources the other lane emits.
    llvm::LoopAnalysisManager     loopAnalyses;
    llvm::FunctionAnalysisManager functionAnalyses;
    llvm::CGSCCAnalysisManager    cgsccAnalyses;
    llvm::ModuleAnalysisManager   moduleAnalyses;
    llvm::PassBuilder             builder(machine.get());
    builder.registerModuleAnalyses(moduleAnalyses);
    builder.registerCGSCCAnalyses(cgsccAnalyses);
    builder.registerFunctionAnalyses(functionAnalyses);
    builder.registerLoopAnalyses(loopAnalyses);
    builder.crossRegisterProxies(loopAnalyses, functionAnalyses, cgsccAnalyses, moduleAnalyses);
    llvm::ModulePassManager optimize = builder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
    optimize.run(*ir, moduleAnalyses);

    llvm::SmallVector<char, 0> buffer;
    llvm::raw_svector_ostream  stream(buffer);
    llvm::legacy::PassManager  emit;
    if (machine->addPassesToEmitFile(emit, stream, nullptr, llvm::CodeGenFileType::ObjectFile))
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "the target cannot emit object files for '" + resolved.str() + "'");
    }
    emit.run(*ir);
    object.assign(buffer.begin(), buffer.end());
    return llvm::Error::success();
}

/// @brief What the C header named a member of one plan.
struct CBodyMember final
{
    std::string  cName;
    std::string  arrayKind;
    std::string  category;
    std::int64_t bitLength{0};
    std::string  compositeCTypeName;
    /// @brief The member is a `dsdl_runtime_view_t`, or an array of them.
    bool         heldAsView{false};
    std::int64_t arrayCapacity{0};
};

/// @brief What the C header named one plan and its members.
struct CBodyPlan final
{
    std::string                  cTypeName;
    llvm::StringMap<CBodyMember> members;
};

/// @brief Spells a plan body as C.
///
/// The names come from the `c_name` and `c_type_name` attributes `stampCNames` left on the
/// schema, so a body reaches the same identifiers the header declares.
class CSpelling final : public BodySpelling
{
public:
    CSpelling(mlir::ModuleOp module, mlir::dsdl::SchemaOp schema)
        : module_(module)
    {
        if (const auto nested = module->getAttrOfType<mlir::ArrayAttr>("llvmdsdl.c_nested_headers"))
        {
            for (const mlir::Attribute entry : nested)
            {
                const auto [cTypeName, headerPath] = mlir::cast<mlir::StringAttr>(entry).getValue().split('=');
                headers_[cTypeName]                = headerPath.str();
            }
        }
        // A body points at nested objects as well as its own, so every plan the module carries
        // contributes the tag its header declares.
        for (mlir::dsdl::SchemaOp other : module.getOps<mlir::dsdl::SchemaOp>())
        {
            if (other.getBody().empty())
            {
                tags_[planIdentity(other.getFullName(), other.getMajor(), other.getMinor(), {})] =
                    other.getCTypeName().value_or(llvm::StringRef{}).str();
                continue;
            }
            // The schema names the type; a section names its own, and only a service has one.
            tags_[planIdentity(other.getFullName(), other.getMajor(), other.getMinor(), {})] =
                other.getCTypeName().value_or(llvm::StringRef{}).str();
            for (mlir::dsdl::SerializationPlanOp plan :
                 other.getBody().front().getOps<mlir::dsdl::SerializationPlanOp>())
            {
                tags_[planIdentity(other, plan)] = plan.getCTypeName().str();
            }
        }
        if (schema.getBody().empty())
        {
            return;
        }
        for (mlir::dsdl::SerializationPlanOp plan : schema.getBody().front().getOps<mlir::dsdl::SerializationPlanOp>())
        {
            CBodyPlan entry;
            entry.cTypeName = plan.getCTypeName().str();
            if (!plan.getBody().empty())
            {
                for (mlir::dsdl::IOOp io : plan.getBody().front().getOps<mlir::dsdl::IOOp>())
                {
                    if (io.isPadding())
                    {
                        continue;
                    }
                    CBodyMember member;
                    member.cName              = io.getCName().value_or(llvm::StringRef{}).str();
                    member.arrayKind          = io.getArrayKind().str();
                    member.category           = io.getScalarCategory().str();
                    member.bitLength          = io.getBitLength();
                    member.compositeCTypeName = io.getCompositeCTypeName().value_or(llvm::StringRef{}).str();
                    member.heldAsView         = io.getHeldAsView();
                    member.arrayCapacity      = io.getArrayCapacity();
                    // A nested type's own schema is not cloned into this module for a source
                    // build, so the field is where its name comes from.
                    if (io.getCompositeFullName())
                    {
                        tags_[planIdentity(*io.getCompositeFullName(),
                                           io.getCompositeMajor().value_or(0),
                                           io.getCompositeMinor().value_or(0),
                                           {})] = member.compositeCTypeName;
                    }
                    entry.members[io.getName()] = std::move(member);
                }
            }
            if (plan.getIsUnion())
            {
                entry.members["_tag_"] = CBodyMember{"_tag_", "none", "unsigned", unionTagBits(plan), {}, false};
            }
            plans_[planIdentity(schema, plan)] = std::move(entry);
        }
    }

    // Functions.

    std::vector<std::string> openFunction(SourceWriter& w, mlir::func::FuncOp fn) const override
    {
        const auto direction = planBodyDirection(fn);
        if (!direction)
        {
            return openHelper(w, fn);
        }
        const std::string name = fn.getSymName().str();
        if ((*direction == "get") || (*direction == "set"))
        {
            // An accessor reads or writes the wire at the field's offset. A scalar one carries
            // the buffer and its size; a composite `get` answers the field's bytes and reports
            // their count through a third parameter, and a `set` takes the value as one.
            const std::vector<std::string> parameters = accessorParameters(fn, *direction == "get");
            std::string                    rendered;
            for (const auto& [argument, parameter] : llvm::zip(fn.getArguments(), parameters))
            {
                rendered += (rendered.empty() ? "" : ", ") + accessorTypeName(argument.getType()) + " " + parameter;
            }
            w.line(accessorTypeName(fn.getFunctionType().getResult(0)) + " " + name + "(" + rendered + ")");
            w.open("{");
            markUnused(w, fn, parameters);
            // The header declares the offsets signed. The plan counts in unsigned, so each is
            // taken once into the type the body operates in rather than converted at every use.
            std::vector<std::string> names;
            for (const auto& [argument, parameter] : llvm::zip(fn.getArguments(), parameters))
            {
                const std::string spelt = typeName(argument.getType());
                if (spelt == accessorTypeName(argument.getType()))
                {
                    names.push_back(parameter);
                    continue;
                }
                names.push_back(parameter + "_");
                std::string conversion = "const " + spelt;
                conversion += " " + names.back();
                conversion += " = (" + spelt;
                conversion += ") " + parameter;
                conversion += ";";
                w.line(conversion);
            }
            return names;
        }
        const std::string object = (*direction == "serialize") ? "obj" : "out_obj";
        if (*direction == "initialize")
        {
            w.line("int8_t " + name + "(" + typeName(fn.getArgument(0).getType()) + " " + object + ")");
            w.open("{");
            markUnused(w, fn, {object});
            return {object};
        }
        w.line("int8_t " + name + "(" + typeName(fn.getArgument(0).getType()) + " " + object + ", " +
               typeName(fn.getArgument(1).getType()) + " buffer, size_t* inout_buffer_size_bytes)");
        w.open("{");
        markUnused(w, fn, {object, "buffer", "inout_buffer_size_bytes"});
        return {object, "buffer", "inout_buffer_size_bytes"};
    }

    /// @brief The header that declares the type @p object points at.
    [[nodiscard]] std::string headerOf(const mlir::Value object) const
    {
        const auto found = headers_.find(entryPointTag(object));
        return (found == headers_.end()) ? std::string{} : found->second;
    }

    /// @brief The declaration of @p fn, so a body may call one defined after it.
    [[nodiscard]] std::string declarationOf(mlir::func::FuncOp fn) const
    {
        const auto               direction = planBodyDirection(fn);
        const std::string        name      = fn.getSymName().str();
        std::string              rendered;
        std::vector<std::string> parameters;
        if (!direction)
        {
            for (const mlir::BlockArgument argument : fn.getArguments())
            {
                parameters.push_back("p" + std::to_string(argument.getArgNumber()));
            }
        }
        else if ((*direction == "get") || (*direction == "set"))
        {
            parameters = accessorParameters(fn, *direction == "get");
        }
        else
        {
            parameters.emplace_back((*direction == "serialize") ? "obj" : "out_obj");
            if (*direction != "initialize")
            {
                parameters.emplace_back("buffer");
                parameters.emplace_back("inout_buffer_size_bytes");
            }
        }
        const bool accessor = direction && ((*direction == "get") || (*direction == "set"));
        for (const auto& [argument, parameter] : llvm::zip(fn.getArguments(), parameters))
        {
            rendered += (rendered.empty() ? "" : ", ") +
                        (accessor ? accessorTypeName(argument.getType()) : typeName(argument.getType())) + " " +
                        parameter;
        }
        const mlir::Type result = fn.getFunctionType().getResult(0);
        return (accessor ? accessorTypeName(result) : typeName(result)) + " " + name + "(" +
               (rendered.empty() ? "void" : rendered) + ");";
    }

    void closeFunction(SourceWriter& w, mlir::func::FuncOp /*fn*/) const override
    {
        w.close("}");
        w.blank();
    }

    [[nodiscard]] std::string functionName(const llvm::StringRef callee) const override
    {
        return callee.str();
    }

    [[nodiscard]] std::string valueName(const ValueRole       role,
                                        const llvm::StringRef member,
                                        const std::size_t     ordinal) const override
    {
        return snakeValueName(role, member, ordinal);
    }

    [[nodiscard]] llvm::ArrayRef<llvm::StringRef> reservedLocals() const override
    {
        // A body calls the runtime and a synthesised helper by bare name, and both carry a
        // prefix a role name cannot: a role name is a role word, or a member and a role word
        // joined. C's own keywords are equally out of reach for the same reason.
        return {};
    }

    // Statements.

    void declare(SourceWriter&         w,
                 const mlir::Type      type,
                 const llvm::StringRef name,
                 const llvm::StringRef expr) const override
    {
        const std::string spelt = typeName(type);
        if (spelt.back() == '*')
        {
            w.line(spelt + " const " + name.str() + " = " + expr.str() + ";");
        }
        else
        {
            w.line("const " + spelt + " " + name.str() + " = " + expr.str() + ";");
        }
    }

    void declareVariable(SourceWriter&         w,
                         const mlir::Type      type,
                         const llvm::StringRef name,
                         bool /*reassigned*/) const override
    {
        // Initialised where it is declared: a variable an arm assigns is read after the
        // statement, and the plan reaches only the arm that assigned it, which the compiler
        // cannot see.
        w.line(typeName(type) + " " + name.str() + " = " + zeroOf(type) + ";");
    }

    void assign(SourceWriter& w, const llvm::StringRef name, const llvm::StringRef expr) const override
    {
        w.line(name.str() + " = " + expr.str() + ";");
    }

    void discard(SourceWriter& w, const llvm::StringRef expr) const override
    {
        w.line("(void) " + expr.str() + ";");
    }

    void returnValue(SourceWriter& w, const llvm::StringRef expr) const override
    {
        w.line("return " + expr.str() + ";");
    }

    void returnWithSize(SourceWriter& /*w*/,
                        const llvm::StringRef /*error*/,
                        const llvm::StringRef /*used*/) const override
    {
        // A C body writes its size back through the pointer it is handed.
        llvm::report_fatal_error("C spelling: a body folded to answer its size");
    }

    [[nodiscard]] std::string bufferLength(mlir::dsdl::BufferLengthOp /*op*/,
                                           const ValueNames& /*names*/) const override
    {
        llvm::report_fatal_error("C spelling: a body folded to read its buffer's length");
    }

    void openIf(SourceWriter& w, const llvm::StringRef condition) const override
    {
        w.open("if (" + condition.str() + ") {");
    }

    void openElse(SourceWriter& w) const override
    {
        w.midway("} else {");
    }

    void openLoop(SourceWriter& w) const override
    {
        w.open("while (true) {");
    }

    void breakUnless(SourceWriter& w, const llvm::StringRef condition) const override
    {
        w.open("if (!(" + condition.str() + ")) {");
        w.line("break;");
        w.close("}");
    }

    void openFor(SourceWriter&         w,
                 const llvm::StringRef variable,
                 const llvm::StringRef lower,
                 const llvm::StringRef upper,
                 const llvm::StringRef step) const override
    {
        w.open("for (size_t " + variable.str() + " = " + lower.str() + "; " + variable.str() + " < " + upper.str() +
               "; " + variable.str() + " += " + step.str() + ") {");
    }

    void closeBlock(SourceWriter& w) const override
    {
        w.close("}");
    }

    // Values.

    [[nodiscard]] std::string constant(const mlir::TypedAttr value) const override
    {
        if (const auto integer = mlir::dyn_cast<mlir::IntegerAttr>(value))
        {
            const mlir::Type type = integer.getType();
            if (mlir::isa<mlir::IndexType>(type))
            {
                return std::to_string(integer.getValue().getZExtValue()) + "U";
            }
            const unsigned width = mlir::cast<mlir::IntegerType>(type).getWidth();
            if (width == 1)
            {
                return integer.getValue().isZero() ? "false" : "true";
            }
            if (width == 64)
            {
                // Spelt unsigned, as the arithmetic is: a negative value is its two's complement.
                return std::to_string(integer.getValue().getZExtValue()) + "ULL";
            }
            return std::to_string(integer.getValue().getSExtValue());
        }
        if (const auto floating = mlir::dyn_cast<mlir::FloatAttr>(value))
        {
            const bool  single = mlir::cast<mlir::FloatType>(floating.getType()).getWidth() <= 32;
            std::string text   = std::to_string(floating.getValueAsDouble());
            if (!text.contains('.'))
            {
                text += ".0";
            }
            return single ? text + "F" : text;
        }
        llvm::report_fatal_error("C spelling: a constant of an unexpected kind");
    }

    [[nodiscard]] std::string binary(const BinaryOperator  op,
                                     const llvm::StringRef lhs,
                                     const llvm::StringRef rhs,
                                     const mlir::Type      type) const override
    {
        if (isBool(type))
        {
            switch (op)
            {
            case BinaryOperator::And:
                return "(" + lhs.str() + " && " + rhs.str() + ")";
            case BinaryOperator::Or:
                return "(" + lhs.str() + " || " + rhs.str() + ")";
            case BinaryOperator::Xor:
                return "(" + lhs.str() + " != " + rhs.str() + ")";
            default:
                llvm::report_fatal_error("C spelling: an arithmetic operator on a bool");
            }
        }
        const bool signedForm =
            (op == BinaryOperator::DivS) || (op == BinaryOperator::RemS) || (op == BinaryOperator::ShiftRightS);
        const std::string a          = signedForm ? asSigned(lhs, type) : lhs.str();
        const std::string b          = signedForm ? asSigned(rhs, type) : rhs.str();
        const std::string expression = "(" + a + " " + operatorToken(op).str() + " " + b + ")";
        // A signed form answers in the signed type and a narrow one is promoted to int, so
        // either way the value is brought back to the type the plan gave it.
        return (signedForm || isNarrow(type)) ? ("(" + typeName(type) + ") " + expression) : expression;
    }

    [[nodiscard]] std::string logicalNot(const llvm::StringRef expr) const override
    {
        return "!(" + expr.str() + ")";
    }

    [[nodiscard]] std::string compare(const Comparison      comparison,
                                      const llvm::StringRef lhs,
                                      const llvm::StringRef rhs,
                                      const mlir::Type      type) const override
    {
        std::string a = lhs.str();
        std::string b = rhs.str();
        if (isSignedComparison(comparison) && !isSignedSpelt(type))
        {
            a = asSigned(lhs, type);
            b = asSigned(rhs, type);
        }
        return "(" + a + " " + comparisonToken(comparison).str() + " " + b + ")";
    }

    [[nodiscard]] std::string select(const llvm::StringRef condition,
                                     const llvm::StringRef ifTrue,
                                     const llvm::StringRef ifFalse,
                                     mlir::Type /*type*/) const override
    {
        return "(" + condition.str() + " ? " + ifTrue.str() + " : " + ifFalse.str() + ")";
    }

    [[nodiscard]] std::string convert(Conversion /*conversion*/,
                                      const llvm::StringRef value,
                                      mlir::Type /*from*/,
                                      const mlir::Type to) const override
    {
        // Every conversion the plan asks for is a width or signedness change between the
        // integer types a body holds, which C spells as the cast to the answer's type.
        return "(" + typeName(to) + ") " + value.str();
    }

    [[nodiscard]] std::string call(const llvm::StringRef callee, const llvm::ArrayRef<std::string> args) const override
    {
        std::string rendered;
        for (const std::string& argument : args)
        {
            rendered += (rendered.empty() ? "" : ", ") + argument;
        }
        return callee.str() + "(" + rendered + ")";
    }

    [[nodiscard]] bool spellsInline(mlir::Operation* op) const override
    {
        // An address of a member or an element is the expression that forms it. Declaring one
        // would put it in the block the plan formed it in, and C ends that block's scope before
        // the value is read again.
        return mlir::isa_and_nonnull<mlir::dsdl::MemberAddrOp, mlir::dsdl::ElementAddrOp>(op);
    }

    // The dialect.

    [[nodiscard]] std::string isNotNull(mlir::dsdl::IsNullOp op, const ValueNames& names) const override
    {
        return "(" + names(op.getPointer()) + " != NULL)";
    }

    [[nodiscard]] std::string isNull(mlir::dsdl::IsNullOp op, const ValueNames& names) const override
    {
        return "(" + names(op.getPointer()) + " == NULL)";
    }

    [[nodiscard]] std::string indexHolds(mlir::dsdl::IndexHoldsOp op, const ValueNames& names) const override
    {
        // The count survives the round trip through the index type as a signed quantity.
        const std::string value = names(op.getValue());
        return "((uint64_t) (ptrdiff_t) " + value + " == " + value + ")";
    }

    [[nodiscard]] std::string bufferOrEmpty(mlir::dsdl::BufferOrEmptyOp op, const ValueNames& names) const override
    {
        const std::string buffer = names(op.getBuffer());
        return "((" + buffer + " == NULL) ? (const uint8_t*) \"\" : " + buffer + ")";
    }

    [[nodiscard]] std::string bufferAt(mlir::dsdl::BufferAtOp op, const ValueNames& names) const override
    {
        return "&" + names(op.getBuffer()) + "[" + names(op.getByteOffset()) + "]";
    }

    [[nodiscard]] std::string loadScalar(mlir::dsdl::LoadScalarOp op, const ValueNames& names) const override
    {
        return "*" + names(op.getPointer());
    }

    void storeScalar(SourceWriter& w, mlir::dsdl::StoreScalarOp op, const ValueNames& names) const override
    {
        w.line("*" + names(op.getPointer()) + " = " + names(op.getValue()) + ";");
    }

    [[nodiscard]] std::string local(SourceWriter&         w,
                                    mlir::dsdl::LocalOp   op,
                                    const llvm::StringRef name,
                                    const ValueNames&     names) const override
    {
        const std::string storage =
            pointeeName(mlir::cast<mlir::dsdl::PtrType>(op.getAddress().getType()).getPointee());
        w.line(storage + " " + name.str() + " = (" + storage + ") " + names(op.getInit()) + ";");
        return "&" + name.str();
    }

    [[nodiscard]] std::string loadMember(mlir::dsdl::LoadMemberOp op, const ValueNames& names) const override
    {
        return memberPath(op.getObject(), op.getMember(), names);
    }

    void storeMember(SourceWriter& w, mlir::dsdl::StoreMemberOp op, const ValueNames& names) const override
    {
        w.line(memberPath(op.getObject(), op.getMember(), names) + " = " + names(op.getValue()) + ";");
    }

    [[nodiscard]] std::string loadElement(mlir::dsdl::LoadElementOp op, const ValueNames& names) const override
    {
        return elementPath(op.getObject(), op.getMember(), names(op.getIndex()), names);
    }

    void storeElement(SourceWriter& w, mlir::dsdl::StoreElementOp op, const ValueNames& names) const override
    {
        w.line(elementPath(op.getObject(), op.getMember(), names(op.getIndex()), names) + " = " + names(op.getValue()) +
               ";");
    }

    [[nodiscard]] std::string memberAddr(mlir::dsdl::MemberAddrOp op, const ValueNames& names) const override
    {
        return "&" + memberPath(op.getObject(), op.getMember(), names);
    }

    [[nodiscard]] std::string elementAddr(mlir::dsdl::ElementAddrOp op, const ValueNames& names) const override
    {
        return "&" + elementPath(op.getObject(), op.getMember(), names(op.getIndex()), names);
    }

    [[nodiscard]] std::string arrayLength(mlir::dsdl::ArrayLengthOp op, const ValueNames& names) const override
    {
        return "(uint64_t) " + memberPath(op.getObject(), op.getMember(), names) + ".count";
    }

    void setArrayLength(SourceWriter& w, mlir::dsdl::SetArrayLengthOp op, const ValueNames& names) const override
    {
        w.line(memberPath(op.getObject(), op.getMember(), names) + ".count = (size_t) " + names(op.getValue()) + ";");
    }

    [[nodiscard]] std::string unionTag(mlir::dsdl::UnionTagOp op, const ValueNames& names) const override
    {
        return "(uint64_t) " + names(op.getObject()) + "->_tag_";
    }

    void setUnionTag(SourceWriter& w, mlir::dsdl::SetUnionTagOp op, const ValueNames& names) const override
    {
        // The width the declaration gave the tag: a union of more than 256 options carries it in
        // more than a byte, and narrowing the write here would dispatch the wrong arm.
        const CBodyMember* const found = memberOf(op.getObject(), "_tag_");
        const std::string        storage =
            unsignedStorageType(static_cast<std::uint32_t>((found == nullptr) ? 8 : found->bitLength));
        w.line(names(op.getObject()) + "->_tag_ = (" + storage + ") " + names(op.getValue()) + ";");
    }

    [[nodiscard]] std::string writeBits(mlir::dsdl::WriteBitsOp op, const ValueNames& names) const override
    {
        const mlir::Type  valueType = op.getValue().getType();
        const std::string primitive =
            runtimePrimitive(true, valueType, static_cast<std::int64_t>(op.getWidth()), op.getIsSigned());
        const bool integral = !mlir::isa<mlir::FloatType>(valueType) && (op.getWidth() != 1);
        // A signed field's primitive takes a signed carrier, and the plan's i64 is spelt unsigned;
        // converting it implicitly is implementation-defined above INT64_MAX.
        const std::string value =
            (integral && op.getIsSigned()) ? ("(int64_t) " + names(op.getValue())) : names(op.getValue());
        std::string arguments = names(op.getBuffer()) + ", (size_t) " + names(op.getBufferSizeBytes()) + ", (size_t) " +
                                names(op.getBitOffset()) + ", " + value;
        if (integral)
        {
            arguments += ", (uint8_t) " + std::to_string(op.getWidth());
        }
        return primitive + "(" + arguments + ")";
    }

    [[nodiscard]] std::string readBits(mlir::dsdl::ReadBitsOp op, const ValueNames& names) const override
    {
        const mlir::Type  valueType = op.getValue().getType();
        const std::string primitive =
            runtimePrimitive(false, valueType, static_cast<std::int64_t>(op.getWidth()), op.getIsSigned());
        std::string arguments = names(op.getBuffer()) + ", (size_t) " + names(op.getBufferSizeBytes()) + ", (size_t) " +
                                names(op.getBitOffset());
        if (!mlir::isa<mlir::FloatType>(valueType) && (op.getWidth() != 1))
        {
            arguments += ", (uint8_t) " + std::to_string(op.getWidth());
        }
        return primitive + "(" + arguments + ")";
    }

    void bitWrite(SourceWriter& w, mlir::dsdl::BitWriteOp op, const ValueNames& names) const override
    {
        // A run of bits copied out of the object's own storage into the buffer.
        w.line("dsdl_runtime_copy_bits(" + names(op.getDestination()) + ", (size_t) " +
               names(op.getDestinationBitOffset()) + ", (size_t) " + names(op.getWidth()) + ", " +
               names(op.getSource()) + ", (size_t) " + names(op.getSourceBitOffset()) + ");");
    }

    void bitRead(SourceWriter& w, mlir::dsdl::BitReadOp op, const ValueNames& names) const override
    {
        w.line("dsdl_runtime_get_bits(" + names(op.getDestination()) + ", " + names(op.getBuffer()) + ", (size_t) " +
               names(op.getBufferSizeBytes()) + ", (size_t) " + names(op.getBitOffset()) + ", (size_t) " +
               names(op.getWidth()) + ");");
    }

    void writeBit(SourceWriter& /*w*/, mlir::dsdl::WriteBitOp /*op*/, const ValueNames& /*names*/) const override
    {
        // A C bool array is packed, so its runs reach C as the plan builds them.
        llvm::report_fatal_error("C spelling: a bool run expanded for a bool per element");
    }

    [[nodiscard]] std::string readBit(mlir::dsdl::ReadBitOp /*op*/, const ValueNames& /*names*/) const override
    {
        llvm::report_fatal_error("C spelling: a bool run expanded for a bool per element");
    }

    void imageRead(SourceWriter& w, mlir::dsdl::ImageReadOp op, const ValueNames& names) const override
    {
        w.line("dsdl_runtime_image_read(" + names(op.getObject()) + ", " + names(op.getBuffer()) + ", (size_t) " +
               names(op.getBufferSizeBytes()) + ", " + std::to_string(op.getBytes()) + "U);");
    }

    void imageWrite(SourceWriter& w, mlir::dsdl::ImageWriteOp op, const ValueNames& names) const override
    {
        w.line("dsdl_runtime_image_write(" + names(op.getBuffer()) + ", " + names(op.getObject()) + ", " +
               std::to_string(op.getBytes()) + "U);");
    }

    void declareCallSerdesSized(SourceWriter& /*w*/,
                                const llvm::StringRef /*error*/,
                                const llvm::StringRef /*consumed*/,
                                mlir::dsdl::CallSerdesSizedOp /*op*/,
                                const ValueNames& /*names*/) const override
    {
        // A C entry point takes its size by pointer, so a nested call reaches C as the plan builds it.
        llvm::report_fatal_error("C spelling: a nested call handed its size by value");
    }

    [[nodiscard]] std::string callSerdes(mlir::dsdl::CallSerdesOp op, const ValueNames& names) const override
    {
        // The nested type's own entry point, as its header publishes it.
        return entryPoint(op.getObject(), op.getDirection()) + "(" + names(op.getObject()) + ", " +
               names(op.getBuffer()) + ", " + names(op.getSize()) + ")";
    }

    void declareCallInitialize(SourceWriter&                w,
                               const llvm::StringRef        name,
                               mlir::dsdl::CallInitializeOp op,
                               const ValueNames&            names) const override
    {
        // C translates the initialise body as a function, so a nested one is the call its
        // header publishes, answering the same code the rest of the plan carries.
        const std::string invocation = entryPoint(op.getObject(), "initialize") + "(" + names(op.getObject()) + ")";
        if (name.empty())
        {
            discard(w, invocation);
            return;
        }
        declare(w, op.getError().getType(), name, invocation);
    }

    [[nodiscard]] std::string viewBytes(mlir::dsdl::LoadViewOp op, const ValueNames& names) const override
    {
        return viewPath(op, names) + ".bytes";
    }

    [[nodiscard]] std::string viewSize(mlir::dsdl::LoadViewOp op, const ValueNames& names) const override
    {
        return "(uint64_t) " + viewPath(op, names) + ".size_bytes";
    }

    void storeView(SourceWriter& w, mlir::dsdl::StoreViewOp op, const ValueNames& names) const override
    {
        const std::string path = op.getIndex()
                                     ? elementPath(op.getObject(), op.getMember(), names(op.getIndex()), names)
                                     : memberPath(op.getObject(), op.getMember(), names);
        w.line(path + ".bytes = " + names(op.getBytes()) + ";");
        w.line(path + ".size_bytes = (size_t) " + names(op.getSizeBytes()) + ";");
    }

    void clearView(SourceWriter& w, mlir::dsdl::ClearViewOp op, const ValueNames& names) const override
    {
        const std::string        path   = memberPath(op.getObject(), op.getMember(), names);
        const CBodyMember* const member = memberOf(op.getObject(), op.getMember());
        if ((member != nullptr) && (member->arrayKind != "none"))
        {
            w.line("dsdl_runtime_clear_views(" + elementBase(op.getObject(), op.getMember(), names) + ", " +
                   ((member->arrayKind == "fixed") ? std::to_string(member->arrayCapacity) + "U" : (path + ".count")) +
                   ");");
            return;
        }
        w.line("dsdl_runtime_clear_views(&" + path + ", 1U);");
    }

    void copyBytes(SourceWriter& w, mlir::dsdl::CopyBytesOp op, const ValueNames& names) const override
    {
        w.line("dsdl_runtime_copy_bytes(" + names(op.getDestination()) + ", " + names(op.getSource()) + ", (size_t) " +
               names(op.getSourceSizeBytes()) + ", " + std::to_string(op.getBytes()) + "U);");
    }

private:
    /// @brief An accessor's signature as the header declares it.
    ///
    /// The header renders the declaration from the field rather than from the plan, and spells
    /// the offsets and the value it carries signed. A definition that disagreed with it would
    /// not compile beside it.
    [[nodiscard]] std::string accessorTypeName(const mlir::Type type) const
    {
        const auto integer = mlir::dyn_cast<mlir::IntegerType>(type);
        return (integer && (integer.getWidth() == 64)) ? "int64_t" : typeName(type);
    }

    /// @brief What an accessor's parameters are called, by what the field needs of them.
    [[nodiscard]] static std::vector<std::string> accessorParameters(mlir::func::FuncOp fn, const bool reading)
    {
        // The buffer and its size, then the element's index where the field is a fixed array,
        // then what the accessor carries: the count a composite read reports through, or the
        // value a write takes.
        std::vector<std::string> parameters;
        const unsigned           count = fn.getNumArguments();
        for (unsigned index = 0; index < count; ++index)
        {
            if (index == 0)
            {
                parameters.emplace_back("buffer");
            }
            else if (index == 1)
            {
                parameters.emplace_back("buffer_size_bytes");
            }
            else if (mlir::isa<mlir::dsdl::PtrType>(fn.getArgument(index).getType()))
            {
                parameters.emplace_back("out_size");
            }
            else if (!reading && (index + 1 == count))
            {
                parameters.emplace_back("value");
            }
            else
            {
                parameters.emplace_back("index");
            }
        }
        return parameters;
    }

    /// @brief The member of the plan @p object points at, or nothing when it names none.
    [[nodiscard]] const CBodyMember* memberOf(const mlir::Value object, const llvm::StringRef member) const
    {
        const auto pointer = mlir::dyn_cast<mlir::dsdl::PtrType>(object.getType());
        if (!pointer)
        {
            return nullptr;
        }
        const auto identity = mlir::dyn_cast<mlir::dsdl::ObjectType>(pointer.getPointee());
        if (!identity)
        {
            return nullptr;
        }
        const auto plan = plans_.find(identity.getIdentity());
        if (plan == plans_.end())
        {
            return nullptr;
        }
        const auto found = plan->second.members.find(member);
        return (found == plan->second.members.end()) ? nullptr : &found->second;
    }

    /// @brief The member itself: what the header declared it as, reached through the object.
    [[nodiscard]] std::string memberPath(const mlir::Value     object,
                                         const llvm::StringRef member,
                                         const ValueNames&     names) const
    {
        const CBodyMember* const found = memberOf(object, member);
        return names(object) + "->" + ((found == nullptr) ? member.str() : found->cName);
    }

    /// @brief An array member's element storage: a fixed array is the member, a variable-length
    ///        one keeps its elements -- or, for bools, its packed bits -- beside a count.
    [[nodiscard]] std::string elementBase(const mlir::Value     object,
                                          const llvm::StringRef member,
                                          const ValueNames&     names) const
    {
        const CBodyMember* const found = memberOf(object, member);
        std::string              path  = memberPath(object, member, names);
        if ((found == nullptr) || (found->arrayKind == "fixed"))
        {
            return path;
        }
        return path + ((found->category == "bool") ? ".bitpacked" : ".elements");
    }

    [[nodiscard]] std::string elementPath(const mlir::Value     object,
                                          const llvm::StringRef member,
                                          const std::string&    index,
                                          const ValueNames&     names) const
    {
        return elementBase(object, member, names) + "[(size_t) " + index + "]";
    }

    /// @brief A view member, or one element of an array of them.
    [[nodiscard]] std::string viewPath(mlir::dsdl::LoadViewOp op, const ValueNames& names) const
    {
        return op.getIndex() ? elementPath(op.getObject(), op.getMember(), names(op.getIndex()), names)
                             : memberPath(op.getObject(), op.getMember(), names);
    }

    /// @brief The C type name of whatever @p object points at.
    [[nodiscard]] std::string entryPointTag(const mlir::Value object) const
    {
        const auto pointer = mlir::dyn_cast<mlir::dsdl::PtrType>(object.getType());
        const auto identity =
            pointer ? mlir::dyn_cast<mlir::dsdl::ObjectType>(pointer.getPointee()) : mlir::dsdl::ObjectType{};
        const auto found = identity ? tags_.find(identity.getIdentity()) : tags_.end();
        return (found == tags_.end()) ? std::string{} : found->second;
    }

    /// @brief The entry point a nested type's header publishes for @p direction.
    [[nodiscard]] std::string entryPoint(const mlir::Value object, const llvm::StringRef direction) const
    {
        return entryPointTag(object) + "__" + direction.str() + "_";
    }

    /// @brief The runtime primitive that carries a field of this width and value type.
    [[nodiscard]] static std::string runtimePrimitive(const bool         write,
                                                      const mlir::Type   valueType,
                                                      const std::int64_t width,
                                                      const bool         isSigned)
    {
        if (mlir::isa<mlir::FloatType>(valueType))
        {
            // Selected by the field's width, not the carrier's: a float16 field travels as a C
            // `float` and is written by set_f16.
            return std::string(write ? "dsdl_runtime_set_f" : "dsdl_runtime_get_f") + std::to_string(width);
        }
        if ((width == 1) && !isSigned)
        {
            return write ? "dsdl_runtime_set_bit" : "dsdl_runtime_get_bit";
        }
        if (write)
        {
            return isSigned ? "dsdl_runtime_set_ixx" : "dsdl_runtime_set_uxx";
        }
        // A read answers in a concrete width, so the primitive is the smallest standard integer
        // that holds the field rather than the field's own width.
        const unsigned holder = holderWidthFor(static_cast<unsigned>(width));
        return std::string(isSigned ? "dsdl_runtime_get_i" : "dsdl_runtime_get_u") + std::to_string(holder);
    }

    /// @brief The value a variable holds before an arm assigns it.
    [[nodiscard]] static std::string zeroOf(const mlir::Type type)
    {
        if (mlir::isa<mlir::dsdl::PtrType>(type))
        {
            return "NULL";
        }
        if (auto floating = mlir::dyn_cast<mlir::FloatType>(type))
        {
            return (floating.getWidth() <= 32) ? "0.0F" : "0.0";
        }
        if (mlir::isa<mlir::IndexType>(type))
        {
            return "0U";
        }
        const unsigned width = mlir::cast<mlir::IntegerType>(type).getWidth();
        if (width == 1)
        {
            return "false";
        }
        return (width == 64) ? "0ULL" : "0";
    }

    [[nodiscard]] static bool isBool(const mlir::Type type)
    {
        const auto integer = mlir::dyn_cast<mlir::IntegerType>(type);
        return integer && (integer.getWidth() == 1);
    }

    /// @brief Whether the type is already spelt as a signed C type.
    [[nodiscard]] static bool isSignedSpelt(const mlir::Type type)
    {
        const auto integer = mlir::dyn_cast<mlir::IntegerType>(type);
        return integer && (integer.getWidth() != 1) && (integer.getWidth() != 64);
    }

    /// @brief Whether C promotes the type to `int` before it operates on it.
    [[nodiscard]] static bool isNarrow(const mlir::Type type)
    {
        const auto integer = mlir::dyn_cast<mlir::IntegerType>(type);
        return integer && (integer.getWidth() > 1) && (integer.getWidth() < 64);
    }

    [[nodiscard]] static std::string asSigned(const llvm::StringRef value, const mlir::Type type)
    {
        return isSignedSpelt(type)
                   ? value.str()
                   : ("(int" + std::to_string(mlir::cast<mlir::IntegerType>(type).getWidth()) + "_t) " + value.str());
    }

    [[nodiscard]] static bool isSignedComparison(const Comparison comparison)
    {
        return (comparison == Comparison::LtS) || (comparison == Comparison::LeS) || (comparison == Comparison::GtS) ||
               (comparison == Comparison::GeS);
    }

    [[nodiscard]] static llvm::StringRef operatorToken(const BinaryOperator op)
    {
        switch (op)
        {
        case BinaryOperator::Add:
            return "+";
        case BinaryOperator::Sub:
            return "-";
        case BinaryOperator::Mul:
            return "*";
        case BinaryOperator::DivU:
        case BinaryOperator::DivS:
            return "/";
        case BinaryOperator::RemU:
        case BinaryOperator::RemS:
            return "%";
        case BinaryOperator::And:
            return "&";
        case BinaryOperator::Or:
            return "|";
        case BinaryOperator::Xor:
            return "^";
        case BinaryOperator::ShiftLeft:
            return "<<";
        case BinaryOperator::ShiftRightU:
        case BinaryOperator::ShiftRightS:
            return ">>";
        }
        return "+";
    }

    [[nodiscard]] static llvm::StringRef comparisonToken(const Comparison comparison)
    {
        switch (comparison)
        {
        case Comparison::Eq:
            return "==";
        case Comparison::Ne:
            return "!=";
        case Comparison::LtS:
        case Comparison::LtU:
            return "<";
        case Comparison::LeS:
        case Comparison::LeU:
            return "<=";
        case Comparison::GtS:
        case Comparison::GtU:
            return ">";
        case Comparison::GeS:
        case Comparison::GeU:
            return ">=";
        }
        return "==";
    }

    /// @brief Opens a helper: lowering built its signature, so the types are read off it.
    std::vector<std::string> openHelper(SourceWriter& w, mlir::func::FuncOp fn) const
    {
        std::vector<std::string> parameters;
        std::string              rendered;
        for (const mlir::BlockArgument argument : fn.getArguments())
        {
            parameters.push_back("p" + std::to_string(argument.getArgNumber()));
            rendered += (rendered.empty() ? "" : ", ") + typeName(argument.getType()) + " " + parameters.back();
        }
        w.line(typeName(fn.getFunctionType().getResult(0)) + " " + fn.getSymName().str() + "(" +
               (rendered.empty() ? "void" : rendered) + ")");
        w.open("{");
        markUnused(w, fn, parameters);
        return parameters;
    }

    /// @brief Marks a parameter the body never reads.
    ///
    /// A plan states what its helper is handed whether or not this one answers from it, and the
    /// signature is the plan's; a target that treats an unread parameter as a defect is told.
    static void markUnused(SourceWriter& w, mlir::func::FuncOp fn, const std::vector<std::string>& parameters)
    {
        for (const auto& [argument, parameter] : llvm::zip(fn.getArguments(), parameters))
        {
            if (!readsArgument(fn, argument.getArgNumber()))
            {
                w.line("(void) " + parameter + ";");
            }
        }
    }

    /// @brief The C spelling of a value type.
    [[nodiscard]] std::string typeName(const mlir::Type type) const
    {
        if (const auto pointer = mlir::dyn_cast<mlir::dsdl::PtrType>(type))
        {
            const std::string qualifier = pointer.getIsConst() ? "const " : "";
            return qualifier + pointeeName(pointer.getPointee()) + "*";
        }
        if (mlir::isa<mlir::IndexType>(type))
        {
            return "size_t";
        }
        if (auto floating = mlir::dyn_cast<mlir::FloatType>(type))
        {
            return (floating.getWidth() <= 32) ? "float" : "double";
        }
        const unsigned width = mlir::cast<mlir::IntegerType>(type).getWidth();
        if (width == 1)
        {
            return "bool";
        }
        // The plan's i64 is the wire arithmetic and the runtime's argument, both unsigned; a
        // signed comparison casts for the comparison alone.
        return (width == 64) ? "uint64_t" : ("int" + std::to_string(width) + "_t");
    }

    /// @brief What a pointer of the body points at.
    [[nodiscard]] std::string pointeeName(const mlir::Type pointee) const
    {
        if (mlir::isa<mlir::dsdl::ByteType>(pointee))
        {
            return "uint8_t";
        }
        if (mlir::isa<mlir::dsdl::SizeType>(pointee))
        {
            return "size_t";
        }
        if (const auto object = mlir::dyn_cast<mlir::dsdl::ObjectType>(pointee))
        {
            const auto found = tags_.find(object.getIdentity());
            return renderCTagSpelling(found == tags_.end() ? std::string{} : found->second);
        }
        return "void";
    }

    mlir::ModuleOp               module_;
    llvm::StringMap<CBodyPlan>   plans_;
    llvm::StringMap<std::string> tags_;
    llvm::StringMap<std::string> headers_;
};

}  // namespace

llvm::Error emit(const SemanticModule& semantic,
                 mlir::ModuleOp        module,
                 const Options&        options,
                 DiagnosticEngine&     diagnostics)
{
    if (options.outDir.empty())
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "output directory is required");
    }

    std::filesystem::path const outRoot(options.outDir);
    EmitterContext const        ctx(semantic,
                                    options.emitDeprecationAttributes,
                                    options.hostImageFolded,

                                    options.accessorsOnly,
                                    options.typeNameVersioning);
    const auto                  selectedTypeKeys = makeTypeKeySet(options.selectedTypeKeys);

    // Support artifacts are rendered from content compiled into this binary, so whether to write
    // them is independent of which definitions were selected -- except under `as-needed`, which
    // ties them to there being type code to support.
    bool anyTypeEmitted = false;
    for (const auto& def : semantic.definitions)
    {
        if (shouldEmitDefinition(def.info, selectedTypeKeys, options.supportGeneration))
        {
            anyTypeEmitted = true;
            break;
        }
    }
    const bool emitSupport = shouldEmitSupport(options.supportGeneration, anyTypeEmitted);

    std::unordered_map<std::string, mlir::Operation*> schemaByHeaderPath;
    llvm::StringMap<mlir::Operation*>                 schemaByKey;
    for (mlir::dsdl::SchemaOp op : module.getBodyRegion().front().getOps<mlir::dsdl::SchemaOp>())
    {
        const auto headerPath = op.getHeaderPath();
        if (!headerPath)
        {
            continue;
        }
        schemaByHeaderPath.emplace(headerPath->str(), op.getOperation());
        schemaByKey[op.getFullName().str() + "." + std::to_string(op.getMajor()) + "." +
                    std::to_string(op.getMinor())] = op.getOperation();
    }

    unsigned objectSizeBits     = 64U;
    bool     objectLittleEndian = false;
    if (options.artifact == Artifact::Object)
    {
        auto width = targetSizeBits(options.targetTriple);
        if (!width)
        {
            return width.takeError();
        }
        objectSizeBits = *width;
        objectLittleEndian =
            llvm::Triple(options.targetTriple.empty() ? llvm::sys::getDefaultTargetTriple() : options.targetTriple)
                .isLittleEndian();
    }

    for (const auto& def : semantic.definitions)
    {
        if (!shouldEmitDefinition(def.info, selectedTypeKeys, options.supportGeneration))
        {
            continue;
        }
        const std::vector<std::string> requiredTypeKeys{definitionTypeKey(def.info)};

        auto perDefModuleRef = mlir::OwningOpRef<mlir::ModuleOp>(mlir::ModuleOp::create(module.getLoc()));
        auto perDefModule    = *perDefModuleRef;
        // The contract the pipeline stamped on the module travels with each definition's share of it.
        perDefModule->setAttrs(module->getAttrDictionary());
        perDefModule->setAttr("llvmdsdl.headers_available", mlir::UnitAttr::get(perDefModule.getContext()));

        const std::string targetHeaderPath = EmitterContext::relativeHeaderPath(def);
        const auto        targetIt         = schemaByHeaderPath.find(targetHeaderPath);
        if (targetIt == schemaByHeaderPath.end())
        {
            diagnostics.error({"<mlir>", 1, 1},
                              "failed to locate schema op for " + def.info.fullName + " (" + targetHeaderPath + ")");
            return llvm::createStringError(llvm::inconvertibleErrorCode(), "schema selection failed");
        }
        mlir::Operation* const schemaClone = targetIt->second->clone();
        perDefModule.getBodyRegion().front().push_back(schemaClone);
        stampCNames(mlir::cast<mlir::dsdl::SchemaOp>(schemaClone), def, options.typeNameVersioning);
        // The nested types whose entry points this definition's bodies may call, each as the C name
        // the bodies call it by and the header that declares it, for the implementation file to
        // include where a body does call. A field held as a view is decoded by no call.
        {
            llvm::SmallVector<mlir::Attribute, 8> nestedHeaders;
            for (const auto& depRef : collectDefinitionCompositeDependencies(def, /*referencedOnly=*/true))
            {
                if (const auto* dep = ctx.find(depRef))
                {
                    nestedHeaders.push_back(
                        mlir::StringAttr::get(perDefModule.getContext(),
                                              ctx.cTypeName(*dep) + "=" + EmitterContext::relativeHeaderPath(*dep)));
                }
            }
            perDefModule->setAttr("llvmdsdl.c_nested_headers",
                                  mlir::ArrayAttr::get(perDefModule.getContext(), nestedHeaders));
        }
        if (options.artifact == Artifact::Object)
        {
            cloneReachableSchemas(schemaClone, perDefModule, schemaByKey);
            // The clones carry lowering's guesses; the module overload matches each to its own
            // definition, so a nested type's members are named the way its own object named them.
            (void) stampCNames(perDefModule, semantic, options.typeNameVersioning);
        }
        cloneFunctionsOf(mlir::cast<mlir::dsdl::SchemaOp>(schemaClone), module, perDefModule);

        mlir::PassManager pm(perDefModule.getContext());
        if (options.artifact == Artifact::Object)
        {
            pm.addPass(createConvertDSDLToLLVMPass(objectSizeBits, objectLittleEndian));
            pm.addPass(createEmitDSDLRuntimePass());
            if (mlir::failed(pm.run(perDefModule)))
            {
                diagnostics.error({"<mlir>", 1, 1}, "LLVM conversion failed");
                return llvm::createStringError(llvm::inconvertibleErrorCode(), "LLVM conversion failed");
            }
            std::string object;
            if (auto err = assembleModule(perDefModule, options.targetTriple, object))
            {
                return err;
            }
            std::filesystem::path objectDir = outRoot;
            for (const auto& ns : def.info.namespaceComponents)
            {
                objectDir /= ns;
            }
            if (auto err = writeGeneratedFile(objectDir / objectFileName(def.info),
                                              object,
                                              options.writePolicy,
                                              requiredTypeKeys))
            {
                return err;
            }
            continue;
        }
        mlir::dsdl::SchemaOp schema = schemaOf(perDefModule, def);
        if (!schema)
        {
            diagnostics.error({"<mlir>", 1, 1}, "no schema for " + def.info.fullName + " in the lowered module");
            return llvm::createStringError(llvm::inconvertibleErrorCode(), "no schema in the lowered module");
        }
        std::ostringstream emittedOut;
        SourceWriter       w = makeCWriter(emittedOut);
        {
            const CSpelling                       spelling(perDefModule, schema);
            PlanBodyLookups                       lookups(perDefModule);
            const std::vector<mlir::func::FuncOp> functions = schemaFunctions(perDefModule, schema.getSymName());
            // A nested type's entry point is declared by its own header, and only the types a
            // body calls are included: an unused include is lint the consumer has to answer for.
            std::set<std::string> nestedHeaders;
            for (const mlir::func::FuncOp fn : functions)
            {
                fn->walk([&](mlir::Operation* op) {
                    if (auto call = mlir::dyn_cast<mlir::dsdl::CallSerdesOp>(op))
                    {
                        nestedHeaders.insert(spelling.headerOf(call.getObject()));
                    }
                    else if (auto init = mlir::dyn_cast<mlir::dsdl::CallInitializeOp>(op))
                    {
                        nestedHeaders.insert(spelling.headerOf(init.getObject()));
                    }
                });
            }
            nestedHeaders.erase(std::string{});
            nestedHeaders.erase(schema.getHeaderPath().value_or(llvm::StringRef{}).str());
            w.line("#include <stdbool.h>");
            w.line("#include <stddef.h>");
            w.line("#include <stdint.h>");
            w.line("#include \"dsdl_runtime.h\"");
            for (const std::string& header : nestedHeaders)
            {
                w.line("#include \"" + header + "\"");
            }
            w.line("#include \"" + schema.getHeaderPath().value_or(llvm::StringRef{}).str() + "\"");
            w.blank();
            // Declared before any of them is defined: a body calls a helper, and a helper the
            // lowering built may be defined after the body that reaches it.
            for (const mlir::func::FuncOp fn : functions)
            {
                w.line(spelling.declarationOf(fn));
            }
            w.blank();
            for (const mlir::func::FuncOp fn : functions)
            {
                if (auto err = translateFunction(fn, spelling, w, lookups))
                {
                    diagnostics.error({"<mlir>", 1, 1}, llvm::toString(std::move(err)));
                    return llvm::createStringError(llvm::inconvertibleErrorCode(), "C body translation failed");
                }
            }
        }
        const std::string emitted = emittedOut.str();

        std::filesystem::path implDir = outRoot;
        for (const auto& ns : def.info.namespaceComponents)
        {
            implDir /= ns;
        }
        const std::string implPreamble =
            generatedCommentLine("C backend implementation") + "\n" + "/* Source: " + def.info.fullName + "." +
            std::to_string(def.info.majorVersion) + "." + std::to_string(def.info.minorVersion) + " */\n\n";
        std::string implContents;
        implContents.reserve(implPreamble.size() + emitted.size());
        implContents.append(implPreamble).append(emitted);
        if (auto err = writeGeneratedFile(implDir / implFileName(def.info),
                                          implContents,
                                          options.writePolicy,
                                          requiredTypeKeys))
        {
            return err;
        }
    }

    if (emitSupport)
    {
        auto runtimeHeader = loadRuntimeHeader();
        if (!runtimeHeader)
        {
            return runtimeHeader.takeError();
        }
        if (auto err = writeGeneratedFile(outRoot / "dsdl_runtime.h",
                                          generatedCommentLine("C runtime scaffold") + "\n\n" + *runtimeHeader,
                                          options.writePolicy))
        {
            return err;
        }
    }

    for (const auto& def : semantic.definitions)
    {
        if (!shouldEmitDefinition(def.info, selectedTypeKeys, options.supportGeneration))
        {
            continue;
        }
        const std::vector<std::string> requiredTypeKeys{definitionTypeKey(def.info)};

        std::filesystem::path dir = outRoot;
        for (const auto& ns : def.info.namespaceComponents)
        {
            dir /= ns;
        }
        if (auto err = writeGeneratedFile(dir / headerFileName(def.info),
                                          renderHeader(def, ctx, module),
                                          options.writePolicy,
                                          requiredTypeKeys))
        {
            return err;
        }
    }

    return llvm::Error::success();
}

namespace
{

/// @brief Makes every target the build carries available to look up.
///
/// Emitting for a target is then a matter of naming it, rather than of having a toolchain for it
/// installed. Asking about a target before this has run answers about no target at all.
void registerTargets()
{
    static std::once_flag once;
    std::call_once(once, [] {
        llvm::InitializeAllTargetInfos();
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmPrinters();
    });
}

}  // namespace

/// @brief What @p triple spells `size_t` at, in bits.
///
/// A variable-length array holds its count in one, so the struct a member is addressed within
/// depends on it. The per-definition module carries no data layout of its own, so this is asked
/// of the target rather than read off the module.
/// @param[in] triple The target, or empty for the host's own.
/// @return The width in bits, or an error naming the target when no backend knows it.
llvm::Expected<unsigned> targetSizeBits(const std::string& triple)
{
    registerTargets();
    const llvm::Triple  resolved(triple.empty() ? llvm::sys::getDefaultTargetTriple() : triple);
    std::string         lookupError;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(resolved, lookupError);
    if (target == nullptr)
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "no backend for target '" + resolved.str() + "': " + lookupError);
    }
    std::unique_ptr<llvm::TargetMachine> machine(
        target->createTargetMachine(resolved, "generic", "", {}, llvm::Reloc::PIC_));
    if (!machine)
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "could not create a target machine for '" + resolved.str() + "'");
    }
    return machine->createDataLayout().getPointerSizeInBits();
}

llvm::Error emitObject(const SemanticModule& semantic,
                       mlir::ModuleOp        module,
                       Options               options,
                       DiagnosticEngine&     diagnostics)
{
    registerTargets();
    options.artifact = Artifact::Object;
    return emit(semantic, module, options, diagnostics);
}

}  // namespace llvmdsdl::emitter::c
