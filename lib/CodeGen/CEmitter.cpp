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
#include "llvmdsdl/CodeGen/CEmitter.h"
#include "llvmdsdl/CodeGen/EmbeddedRuntimeSources.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <mlir/Conversion/ArithToEmitC/ArithToEmitCPass.h>
#include <mlir/Conversion/FuncToEmitC/FuncToEmitCPass.h>
#include <mlir/Conversion/SCFToEmitC/SCFToEmitC.h>
#include <mlir/IR/Attributes.h>
#include <mlir/IR/Block.h>
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
#include <cstddef>
#include <cstdint>

#include "llvmdsdl/CodeGen/TypeStorage.h"
#include "llvmdsdl/CodeGen/CHeaderRender.h"
#include "llvmdsdl/CodeGen/CodegenDiagnosticText.h"
#include "llvmdsdl/CodeGen/ConstantLiteralRender.h"
#include "llvmdsdl/CodeGen/DefinitionDependencies.h"
#include "llvmdsdl/CodeGen/DefinitionIndex.h"
#include "llvmdsdl/CodeGen/LoweredFactsLookup.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/CodeGen/SourceWriter.h"
#include "llvmdsdl/CodeGen/StorageTypeTokens.h"
#include "llvmdsdl/CodeGen/WireLayoutFacts.h"
#include "llvmdsdl/Transforms/Passes.h"
#include "mlir/Conversion/Passes.h"  // IWYU pragma: keep
#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVMPass.h"
#include "mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Target/Cpp/CppEmitter.h"
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
#include "mlir/Transforms/Passes.h"
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
#include "mlir/IR/BuiltinAttributes.h"

namespace llvmdsdl
{
namespace
{

std::string headerFileName(const DiscoveredDefinition& info)
{
    return renderDefinitionFileStem(CodegenNamingLanguage::C, info.shortName, info.majorVersion, info.minorVersion) +
           ".h";
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
    return renderIncludeGuard(CodegenNamingLanguage::C,
                              "LLVMDSDL_",
                              info.fullName,
                              info.majorVersion,
                              info.minorVersion,
                              "_H");
}

std::string valueToCExpr(const TypeExprAST& type, const Value& value)
{
    return renderConstantLiteral(ConstantLiteralLanguage::C, value, makeConstantTypeInfo(type));
}

std::string unsignedStorageType(const std::uint32_t bitLength)
{
    return renderUnsignedStorageToken(StorageTokenLanguage::C, bitLength);
}

std::string signedStorageType(const std::uint32_t bitLength)
{
    return renderSignedStorageToken(StorageTokenLanguage::C, bitLength);
}

class EmitterContext final
{
public:
    EmitterContext(const SemanticModule&    semantic,
                   const bool               emitDeprecationAttributes,
                   const TypeNameVersioning typeNameVersioning)
        : index_(semantic)
        , emitDeprecationAttributes_(emitDeprecationAttributes)
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
/// Both delimiters have to go, not just the closing one. `*/` obviously ends the comment early and
/// spills the rest of the sentence into code position. `/*` is subtler: the comment still ends where
/// it should, so the file compiles -- but it warns under `-Wcomment`, which is an error under
/// `-Werror`, and generated code that cannot be compiled with warnings as errors is generated code
/// somebody has to work around. Definitions do write both: the standard namespace and the showroom
/// both carry doc comments that quote C and JSDoc syntax.
///
/// A space is inserted rather than a backslash or an entity, because the text ends up in front of a
/// human reading a header and `* /` reads as what it is.
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
            return ctx.cTypeName(*type.compositeType);
        }
        return "uint8_t";
    }
    return "uint8_t";
}

void emitArrayMacros(SourceWriter& w, const std::string& typeName, const SemanticSection& section)
{
    const NamingScope constScope = makeSectionConstantScope(CodegenNamingLanguage::C, section);
    for (const auto& field : section.fields)
    {
        if (field.isPadding || field.resolvedType.arrayKind == ArrayKind::None)
        {
            continue;
        }
        const auto named = [&](const ArrayMetadataKind kind) {
            return constScope.get(IdentifierRole::MacroName,
                                  arrayMetadataName(CodegenNamingLanguage::C, field.name, kind));
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

void emitSectionTypedef(SourceWriter&          w,
                        const std::string&     typeName,
                        const SemanticSection& section,
                        const EmitterContext&  ctx,
                        const bool             deprecatedAttribute)
{
    // One scope for the whole section: the keyword and claimed-name escapes make the projection
    // many-to-one, so two distinct DSDL fields can otherwise land on one member. The serializer
    // reads the same scope through the `c_name` attributes stamped in `emitCImplementations`.
    const NamingScope fieldScope = makeSectionFieldScope(CodegenNamingLanguage::C, section);
    w.open("typedef struct " + typeName + " {");

    std::size_t emitted = 0;
    for (const auto& field : section.fields)
    {
        if (field.isPadding)
        {
            continue;
        }

        const auto cMember  = fieldScope.get(IdentifierRole::FieldName, field.name);
        const auto baseType = cTypeFromFieldType(field.resolvedType, ctx);

        if (field.resolvedType.arrayKind == ArrayKind::None)
        {
            emitAttachedDocC(w, field.doc);
            // NOLINTNEXTLINE(performance-inefficient-string-concatenation)
            w.line(baseType + " " + cMember + ";");
            ++emitted;
            continue;
        }

        if (field.resolvedType.arrayKind == ArrayKind::Fixed)
        {
            emitAttachedDocC(w, field.doc);
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

        emitAttachedDocC(w, field.doc);
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
        // sectionFacts isn't threaded here; resolveUnionTagBits falls back to the
        // analyzer-set per-field width, which is authoritative.
        w.line(unsignedStorageType(resolveUnionTagBits(section, nullptr)) + " _tag_;");
        ++emitted;
    }

    if (emitted == 0)
    {
        w.line("uint8_t _dummy_;");
    }

    if (deprecatedAttribute)
    {
        // After the typedef name, not between the closing brace and the name. The two positions are
        // not equivalent: GCC reads the earlier one as deprecating the anonymous struct *type* and
        // warns once, at the definition -- which the generated file then suppresses with its own
        // `#pragma GCC diagnostic ignored`, so user code naming the typedef is told nothing at all.
        // Placed after the name it deprecates the typedef, and the diagnostic lands where it is
        // useful: on the code that uses it. Clang warns either way, so this only shows up on GCC.
        w.close("} " + typeName + " __attribute__((deprecated));");
    }
    else
    {
        w.close("} " + typeName + ";");
    }
    w.blank();

    if (section.isUnion)
    {
        std::size_t optionCount = 0;
        for (const auto& f : section.fields)
        {
            if (!f.isPadding)
            {
                ++optionCount;
            }
        }
        w.line("#define " + typeName + "_UNION_OPTION_COUNT_ " + std::to_string(optionCount) + "U");
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
    NamingScope const constScope = makeSectionConstantScope(CodegenNamingLanguage::C, section);
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

void emitSectionMetadata(SourceWriter&                    w,
                         const std::string&               typeName,
                         const std::string&               fullName,
                         std::uint32_t                    majorVersion,
                         std::uint32_t                    minorVersion,
                         const SemanticSection&           section,
                         const LoweredSectionFacts* const sectionFacts)
{
    CHeaderTypeMetadata metadata;
    metadata.typeName                     = typeName;
    metadata.fullName                     = fullName;
    metadata.majorVersion                 = majorVersion;
    metadata.minorVersion                 = minorVersion;
    metadata.extentBytes                  = static_cast<std::uint64_t>(section.extentBits.value_or(0) / 8);
    metadata.serializationBufferSizeBytes = static_cast<std::uint64_t>((section.serializationBufferSizeBits + 7) / 8);
    for (const auto& line : renderCTypeMetadataMacros(metadata))
    {
        w.line(line);
    }
    const bool  zohAliasEligible = sectionFacts != nullptr && sectionFacts->zohAliasEligible;
    std::string zohAliasReason   = "not-proven";
    if (zohAliasEligible)
    {
        zohAliasReason = "eligible";
    }
    else if (sectionFacts != nullptr && !sectionFacts->zohAliasReason.empty())
    {
        zohAliasReason = sectionFacts->zohAliasReason;
    }
    w.line("#define " + typeName + "_ZOH_ALIAS_ELIGIBLE_ " + std::string(zohAliasEligible ? "true" : "false"));
    w.line("#define " + typeName + "_ZOH_ALIAS_REASON_ \"" + zohAliasReason + "\"");
    w.line("#define " + typeName + "_IS_DEPRECATED_ " + std::string(section.deprecated ? "true" : "false"));
    w.blank();
}

void emitSection(SourceWriter&                    w,
                 const EmitterContext&            ctx,
                 const SemanticDefinition&        def,
                 const std::string&               typeName,
                 const std::string&               fullName,
                 const std::string&               sectionName,
                 const SemanticSection&           section,
                 const AttachedDoc&               typeDoc,
                 const LoweredSectionFacts* const sectionFacts)
{
    emitSectionMetadata(w, typeName, fullName, def.info.majorVersion, def.info.minorVersion, section, sectionFacts);
    emitSectionConstants(w, typeName, section);
    emitArrayMacros(w, typeName, section);
    emitAttachedDocC(w,
                     docWithDeprecationNotice(typeDoc,
                                              section.deprecated,
                                              def.info.fullName,
                                              def.info.majorVersion,
                                              def.info.minorVersion));
    emitSectionTypedef(w, typeName, section, ctx, section.deprecated && ctx.emitDeprecationAttributes());

    const auto irStem = sectionIRFunctionStem(def, sectionName);
    w.line("int8_t " + irStem + "__serialize_ir_(const " + typeName +
           "* const obj, uint8_t* buffer, size_t* const "
           "inout_buffer_size_bytes);");
    w.line("int8_t " + irStem + "__deserialize_ir_(" + typeName +
           "* const out_obj, const uint8_t* buffer, size_t* const "
           "inout_buffer_size_bytes);");
    w.blank();

    w.line("static inline int8_t " + typeName + "__serialize_(const " + typeName +
           "* const obj, uint8_t* const buffer, size_t* const "
           "inout_buffer_size_bytes)");
    w.open("{");
    w.line("return " + irStem + "__serialize_ir_(obj, buffer, inout_buffer_size_bytes);");
    w.close("}");
    w.blank();

    w.line("static inline int8_t " + typeName + "__deserialize_(" + typeName +
           "* const out_obj, const uint8_t* buffer, size_t* const "
           "inout_buffer_size_bytes)");
    w.open("{");
    w.line("return " + irStem + "__deserialize_ir_(out_obj, buffer, inout_buffer_size_bytes);");
    w.close("}");
    w.blank();

    w.line("static inline int8_t " + typeName +
           "__try_deserialize_view_(const uint8_t* const buffer, size_t* const inout_buffer_size_bytes, "
           "const uint8_t** const out_view_bytes)");
    w.open("{");
    w.open("if ((buffer == NULL) || (inout_buffer_size_bytes == NULL) || (out_view_bytes == NULL)) {");
    w.line("return -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
    w.close("}");
    w.line("*out_view_bytes = NULL;");
    w.line("const size_t _required = " + typeName + "_SERIALIZATION_BUFFER_SIZE_BYTES_;");
    w.open("if (*inout_buffer_size_bytes < _required) {");
    w.line("*inout_buffer_size_bytes = _required;");
    w.line("return -DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL;");
    w.close("}");
    w.open("#if defined(LLVMDSDL_TARGET_ENDIANNESS_BIG)");
    w.line("(void)buffer;");
    w.line("(void)_required;");
    w.line("*inout_buffer_size_bytes = 0U;");
    w.line("return -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
    w.midway("#elif " + typeName + "_ZOH_ALIAS_ELIGIBLE_");
    w.line("*out_view_bytes = buffer;");
    w.line("*inout_buffer_size_bytes = _required;");
    w.line("return DSDL_RUNTIME_SUCCESS;");
    w.midway("#else");
    w.line("*inout_buffer_size_bytes = 0U;");
    w.line("return -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
    w.close("#endif");
    w.close("}");
    w.blank();

    w.line("static inline int8_t " + typeName +
           "__try_serialize_view_(const uint8_t* const view_bytes, const size_t view_size_bytes, "
           "uint8_t* const buffer, size_t* const inout_buffer_size_bytes)");
    w.open("{");
    w.open("if ((view_bytes == NULL) || (buffer == NULL) || (inout_buffer_size_bytes == NULL)) {");
    w.line("return -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
    w.close("}");
    w.line("const size_t _required = " + typeName + "_SERIALIZATION_BUFFER_SIZE_BYTES_;");
    w.open("if (view_size_bytes != _required) {");
    w.line("return -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
    w.close("}");
    w.open("if (*inout_buffer_size_bytes < _required) {");
    w.line("*inout_buffer_size_bytes = _required;");
    w.line("return -DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL;");
    w.close("}");
    w.open("#if defined(LLVMDSDL_TARGET_ENDIANNESS_BIG)");
    w.line("(void)buffer;");
    w.line("(void)view_bytes;");
    w.line("*inout_buffer_size_bytes = 0U;");
    w.line("return -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
    w.midway("#elif " + typeName + "_ZOH_ALIAS_ELIGIBLE_");
    w.line("(void)memcpy(buffer, view_bytes, _required);");
    w.line("*inout_buffer_size_bytes = _required;");
    w.line("return DSDL_RUNTIME_SUCCESS;");
    w.midway("#else");
    w.line("*inout_buffer_size_bytes = 0U;");
    w.line("return -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
    w.close("#endif");
    w.close("}");
    w.blank();
}

llvm::Expected<std::string> loadRuntimeHeader()
{
    if (const auto data = embedded_runtime::find("dsdl_runtime.h"))
    {
        return std::string(*data);
    }
    return llvm::createStringError(llvm::inconvertibleErrorCode(), "embedded runtime source missing: dsdl_runtime.h");
}

std::string renderHeader(const SemanticDefinition& def, const EmitterContext& ctx, const LoweredFactsMap& loweredFacts)
{
    std::ostringstream out;
    SourceWriter       w            = makeCWriter(out);
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
        const auto [anyVersion, thisVersion] = renderVersionSentinelMacros(CodegenNamingLanguage::C,
                                                                           def.info.fullName,
                                                                           def.info.majorVersion,
                                                                           def.info.minorVersion);
        out << "#if defined(" << anyVersion << ") && !defined(" << thisVersion << ")\n";
        out << "#  error \"" << def.info.fullName
            << ": two versions of one type in one translation unit, but generated type names are "
               "unversioned. Regenerate with --versioned-type-names to use both.\"\n";
        out << "#endif\n";
        out << "#define " << anyVersion << "\n";
        out << "#define " << thisVersion << "\n\n";
    }

    out << "#include <stddef.h>\n";
    out << "#include <stdint.h>\n";
    out << "#include <stdbool.h>\n";
    out << "#include <string.h>\n";
    out << "#include \"dsdl_runtime.h\"\n";

    for (const auto& depRef : collectDefinitionCompositeDependencies(def))
    {
        if (const auto* dep = ctx.find(depRef))
        {
            out << "#include \"" << llvmdsdl::EmitterContext::relativeHeaderPath(*dep) << "\"\n";
        }
    }
    w.blank();

    // Generated code must never warn about itself. A deprecated typedef is referenced by this very
    // header -- in its own declaration, in its serializer signatures, and, when a deprecated type is
    // used as a field, in the struct body of an unrelated type (uavcan.file.Path.1.0 is deprecated and
    // embedded by five other definitions). Suppressing across the whole body covers all three. The
    // region ends before the include guard closes, so a user naming the type still gets the warning.
    if (ctx.emitDeprecationAttributes())
    {
        out << "#pragma GCC diagnostic push\n";
        out << "#pragma GCC diagnostic ignored \"-Wdeprecated-declarations\"\n\n";
    }

    if (def.isService)
    {
        const auto requestType  = baseTypeName + renderSectionTypeSuffix(CodegenNamingLanguage::C, "request");
        const auto responseType = baseTypeName + renderSectionTypeSuffix(CodegenNamingLanguage::C, "response");

        for (const auto& line : renderCServiceAliasIdentityMacros(baseTypeName,
                                                                  def.info.fullName,
                                                                  def.info.majorVersion,
                                                                  def.info.minorVersion))
        {
            w.line(line);
        }
        w.blank();

        emitSection(w,
                    ctx,
                    def,
                    requestType,
                    def.info.fullName + ".Request",
                    "request",
                    def.request,
                    def.doc,
                    lookupLoweredSectionFacts(loweredFacts, def, "request"));
        if (def.response)
        {
            emitSection(w,
                        ctx,
                        def,
                        responseType,
                        def.info.fullName + ".Response",
                        "response",
                        *def.response,
                        def.doc,
                        lookupLoweredSectionFacts(loweredFacts, def, "response"));
        }
        for (const auto& line : renderCServiceAliasBridgeLines(baseTypeName, requestType))
        {
            w.line(line);
        }
        w.blank();

        for (const auto& line : renderCServiceAliasWrapperLines(baseTypeName, requestType))
        {
            w.line(line);
        }
    }
    else
    {
        emitSection(w,
                    ctx,
                    def,
                    baseTypeName,
                    def.info.fullName,
                    "",
                    def.request,
                    def.doc,
                    lookupLoweredSectionFacts(loweredFacts, def, ""));
    }

    if (ctx.emitDeprecationAttributes())
    {
        out << "#pragma GCC diagnostic pop\n\n";
    }

    out << "#endif /* " << guard << " */\n";
    return out.str();
}

/// @brief Clones the schemas @p target reaches, for their layout alone.
///
/// A C translation unit needs only the nested type's name, which its header supplies. An object
/// addresses members by position, so it needs the nested type's layout, and that lives in the
/// nested type's own schema. They are marked so that the body builder passes over them: the
/// serialisation of a nested type belongs to the nested type's object, and a second copy here
/// would be a duplicate symbol and a second thing to keep right.
void cloneReachableSchemas(mlir::Operation*                         target,
                           mlir::ModuleOp                           destination,
                           const llvm::StringMap<mlir::Operation*>& byKey)
{
    llvm::SmallVector<mlir::Operation*, 8> pending{target};
    llvm::StringSet<>                      seen;
    while (!pending.empty())
    {
        mlir::Operation* const at = pending.pop_back_val();
        at->walk([&](mlir::Operation* const op) {
            if (op->getName().getStringRef() != "dsdl.io")
            {
                return;
            }
            const auto name  = op->getAttrOfType<mlir::StringAttr>("composite_full_name");
            const auto major = op->getAttrOfType<mlir::IntegerAttr>("composite_major");
            const auto minor = op->getAttrOfType<mlir::IntegerAttr>("composite_minor");
            if (!name || !major || !minor)
            {
                return;
            }
            const std::string key =
                name.getValue().str() + "." + std::to_string(major.getInt()) + "." + std::to_string(minor.getInt());
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
            clone->setAttr("llvmdsdl.layout_only", mlir::UnitAttr::get(clone->getContext()));
            destination.getBodyRegion().front().push_back(clone);
            pending.push_back(clone);
        });
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

}  // namespace

llvm::Error emitC(const SemanticModule& semantic,
                  mlir::ModuleOp        module,
                  const CEmitOptions&   options,
                  DiagnosticEngine&     diagnostics)
{
    if (options.outDir.empty())
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "output directory is required");
    }

    std::filesystem::path const outRoot(options.outDir);
    EmitterContext const        ctx(semantic, options.emitDeprecationAttributes, options.typeNameVersioning);
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

    const auto mlirCoverageDiagnostic = codegen_diagnostic_text::mlirSchemaCoverageValidationFailedForEmission("C");

    LoweredFactsMap loweredFacts;
    if (!collectLoweredFactsFromMlir(semantic, module, diagnostics, "C", &loweredFacts, options.optimizeLoweredSerDes))
    {
        diagnostics.error({"<mlir>", 1, 1}, mlirCoverageDiagnostic);
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "%s", mlirCoverageDiagnostic.c_str());
    }

    std::unordered_map<std::string, mlir::Operation*> schemaByHeaderPath;
    llvm::StringMap<mlir::Operation*>                 schemaByKey;
    for (mlir::Operation& op : module.getBodyRegion().front())
    {
        if (op.getName().getStringRef() != "dsdl.schema")
        {
            continue;
        }
        const auto headerPathAttr = op.getAttrOfType<mlir::StringAttr>("header_path");
        if (!headerPathAttr)
        {
            continue;
        }
        schemaByHeaderPath.emplace(headerPathAttr.str(), &op);
        const auto fullName = op.getAttrOfType<mlir::StringAttr>("full_name");
        const auto major    = op.getAttrOfType<mlir::IntegerAttr>("major");
        const auto minor    = op.getAttrOfType<mlir::IntegerAttr>("minor");
        if (fullName && major && minor)
        {
            schemaByKey[fullName.getValue().str() + "." + std::to_string(major.getInt()) + "." +
                        std::to_string(minor.getInt())] = &op;
        }
    }

    unsigned objectSizeBits = 64U;
    if (options.artifact == CEmitArtifact::Object)
    {
        auto width = targetSizeBits(options.targetTriple);
        if (!width)
        {
            return width.takeError();
        }
        objectSizeBits = *width;
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
        perDefModule->setAttr("llvmdsdl.names_final", mlir::UnitAttr::get(perDefModule.getContext()));
        perDefModule->setAttr("llvmdsdl.headers_available", mlir::UnitAttr::get(perDefModule.getContext()));

        const std::string targetHeaderPath = llvmdsdl::EmitterContext::relativeHeaderPath(def);
        const auto        targetIt         = schemaByHeaderPath.find(targetHeaderPath);
        if (targetIt == schemaByHeaderPath.end())
        {
            diagnostics.error({"<mlir>", 1, 1},
                              "failed to locate schema op for " + def.info.fullName + " (" + targetHeaderPath + ")");
            return llvm::createStringError(llvm::inconvertibleErrorCode(), "schema selection failed");
        }
        mlir::Operation* const schemaClone = targetIt->second->clone();
        perDefModule.getBodyRegion().front().push_back(schemaClone);
        stampCNames(*schemaClone, def, options.typeNameVersioning);
        if (options.artifact == CEmitArtifact::Object)
        {
            cloneReachableSchemas(schemaClone, perDefModule, schemaByKey);
            // The clones carry lowering's guesses; the module overload matches each to its own
            // definition, so a nested type's members are named the way its own object named them.
            (void) stampCNames(perDefModule, semantic, options.typeNameVersioning);
        }

        mlir::PassManager pm(perDefModule.getContext());
        pm.addPass(createLowerDSDLExecPass());
        if (options.optimizeLoweredSerDes)
        {
            addOptimizeLoweredSerDesPipeline(pm);
        }
        pm.addPass(createBuildDSDLPlanBodiesPass());
        if (options.artifact == CEmitArtifact::Object)
        {
            pm.addPass(createConvertDSDLToLLVMPass(objectSizeBits));
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
        pm.addPass(createConvertDSDLToEmitCPass());
        pm.addPass(mlir::createCanonicalizerPass());
        pm.addPass(mlir::createCSEPass());
        pm.addPass(mlir::createSCFToEmitC());
        pm.addPass(mlir::createConvertArithToEmitC());
        pm.addPass(mlir::createConvertFuncToEmitC());
        // A counted loop's bound arrives as an index and leaves as a size_t; the two
        // conversions meet in the middle and this drops the cast between them, which
        // translateToCpp has no spelling for.
        pm.addPass(mlir::createReconcileUnrealizedCastsPass());

        if (mlir::failed(pm.run(perDefModule)))
        {
            diagnostics.error({"<mlir>", 1, 1}, "EmitC lowering pipeline failed");
            return llvm::createStringError(llvm::inconvertibleErrorCode(), "EmitC lowering pipeline failed");
        }

        std::string              emitted;
        llvm::raw_string_ostream emittedStream(emitted);
        if (mlir::failed(mlir::emitc::translateToCpp(perDefModule, emittedStream, options.declareVariablesAtTop)))
        {
            diagnostics.error({"<mlir>", 1, 1}, "EmitC translation failed");
            return llvm::createStringError(llvm::inconvertibleErrorCode(), "EmitC translation failed");
        }

        std::filesystem::path implDir = outRoot;
        for (const auto& ns : def.info.namespaceComponents)
        {
            implDir /= ns;
        }
        const std::string implPreamble =
            generatedCommentLine("C backend implementation") + "\n" + "/* Source: " + def.info.fullName + "." +
            std::to_string(def.info.majorVersion) + "." + std::to_string(def.info.minorVersion) + " */\n\n";
        // The header suppresses deprecation diagnostics across its own body, and this translation unit
        // needs the same treatment for the same reason: it names the deprecated typedef in every
        // serializer signature it defines. The region opens before the includes so that a deprecated
        // type pulled in as a field is covered too, and closes at end of file, which is where this
        // translation unit stops being generated code.
        const std::string implGuardOpen =
            options.emitDeprecationAttributes
                ? std::string("#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "
                              "\"-Wdeprecated-declarations\"\n\n")
                : std::string();
        const std::string implGuardClose =
            options.emitDeprecationAttributes ? std::string("\n#pragma GCC diagnostic pop\n") : std::string();
        std::string implContents;
        implContents.reserve(implPreamble.size() + implGuardOpen.size() + emitted.size() + implGuardClose.size());
        implContents.append(implPreamble).append(implGuardOpen).append(emitted).append(implGuardClose);
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
                                          renderHeader(def, ctx, loweredFacts),
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
/// depends on it. The per-definition module carries no data layout of its own, which is why this
/// is asked of the target rather than read off the module.
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
                       CEmitOptions          options,
                       DiagnosticEngine&     diagnostics)
{
    registerTargets();
    options.artifact = CEmitArtifact::Object;
    return emitC(semantic, module, options, diagnostics);
}

}  // namespace llvmdsdl
