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
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/CEmitter.h"
#include "llvmdsdl/CodeGen/EmbeddedRuntimeSources.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/ilist_iterator.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/LogicalResult.h>
#include <mlir/IR/Attributes.h>
#include <mlir/IR/Block.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/OperationSupport.h>
#include <mlir/IR/OwningOpRef.h>
#include <mlir/IR/Region.h>
#include <mlir/Support/LLVM.h>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <system_error>
#include <variant>

#include "llvmdsdl/CodeGen/TypeStorage.h"
#include "llvmdsdl/CodeGen/CHeaderRender.h"
#include "llvmdsdl/CodeGen/CodegenDiagnosticText.h"
#include "llvmdsdl/CodeGen/ConstantLiteralRender.h"
#include "llvmdsdl/CodeGen/DefinitionDependencies.h"
#include "llvmdsdl/CodeGen/DefinitionIndex.h"
#include "llvmdsdl/CodeGen/LoweredFactsLookup.h"
#include "llvmdsdl/CodeGen/NamingPolicy.h"
#include "llvmdsdl/CodeGen/StorageTypeTokens.h"
#include "llvmdsdl/CodeGen/WireLayoutFacts.h"
#include "llvmdsdl/Transforms/Passes.h"
#include "mlir/Conversion/Passes.h"  // IWYU pragma: keep
#include "mlir/Pass/PassManager.h"
#include "mlir/Target/Cpp/CppEmitter.h"
#include "mlir/Transforms/Passes.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Evaluator.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Support/Rational.h"
#include "llvmdsdl/Version.h"
#include "mlir/IR/BuiltinAttributes.h"

namespace llvmdsdl
{
namespace
{

std::string sanitizeMacroToken(std::string token)
{
    for (char& c : token)
    {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
        {
            c = '_';
        }
        else
        {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    if (!token.empty() && std::isdigit(static_cast<unsigned char>(token.front())))
    {
        token.insert(token.begin(), '_');
    }
    return token;
}

std::string headerFileName(const DiscoveredDefinition& info)
{
    return llvm::formatv("{0}_{1}_{2}.h", info.shortName, info.majorVersion, info.minorVersion).str();
}

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

std::string sectionSuffix(const std::string& sectionName)
{
    if (sectionName == "request")
    {
        return "__request";
    }
    if (sectionName == "response")
    {
        return "__response";
    }
    return "";
}

std::string sectionIRFunctionStem(const SemanticDefinition& def, const std::string& sectionName)
{
    return mangleSymbol(def.info.fullName, def.info.majorVersion, def.info.minorVersion) + sectionSuffix(sectionName);
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

std::string cTypeNameFromInfo(const DiscoveredDefinition& info)
{
    std::string out;
    for (std::size_t i = 0; i < info.namespaceComponents.size(); ++i)
    {
        if (i > 0)
        {
            out += "__";
        }
        out += codegenSanitizeIdentifier(CodegenNamingLanguage::C, info.namespaceComponents[i]);
    }
    if (!out.empty())
    {
        out += "__";
    }
    out += codegenSanitizeIdentifier(CodegenNamingLanguage::C, info.shortName);
    return out;
}

std::string headerGuard(const DiscoveredDefinition& info)
{
    std::string g = "LLVMDSDL_" + info.fullName + "_" + std::to_string(info.majorVersion) + "_" +
                    std::to_string(info.minorVersion) + "_H";
    for (char& c : g)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)))
        {
            c = '_';
        }
        else
        {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    return g;
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
    EmitterContext(const SemanticModule& semantic, const bool emitDeprecationAttributes)
        : index_(semantic)
        , emitDeprecationAttributes_(emitDeprecationAttributes)
    {
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
        return cTypeNameFromInfo(def.info);
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
        return cTypeNameFromInfo(tmp);
    }

    std::string relativeHeaderPath(const SemanticDefinition& def) const
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
    DefinitionIndex index_;
    bool            emitDeprecationAttributes_{false};
};

void emitLine(std::ostringstream& out, const int indent, const std::string& line)
{
    out << std::string(static_cast<std::size_t>(indent) * 2U, ' ') << line << '\n';
}

std::string generatedCommentLine(llvm::StringRef detail)
{
    return llvm::formatv("/* Generated by llvmdsdl {0} ({1}). */", llvmdsdl::kVersionString, detail).str();
}

std::string sanitizeCCommentText(std::string text)
{
    std::size_t pos = 0U;
    while ((pos = text.find("*/", pos)) != std::string::npos)
    {
        text.replace(pos, 2U, "* /");
        pos += 3U;
    }
    return text;
}

void emitAttachedDocC(std::ostringstream& out, const int indent, const AttachedDoc& doc)
{
    for (const auto& line : doc.lines)
    {
        emitLine(out, indent, "/* " + sanitizeCCommentText(line.text) + " */");
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

void emitArrayMacros(std::ostringstream& out, const std::string& typeName, const SemanticSection& section)
{
    for (const auto& field : section.fields)
    {
        if (field.isPadding || field.resolvedType.arrayKind == ArrayKind::None)
        {
            continue;
        }
        const auto fieldName = sanitizeMacroToken(field.name);
        emitLine(out,
                 0,
                 "#define " + typeName + "_" + fieldName + "_ARRAY_CAPACITY_ " +
                     std::to_string(field.resolvedType.arrayCapacity) + "U");
        emitLine(out,
                 0,
                 "#define " + typeName + "_" + fieldName + "_ARRAY_IS_VARIABLE_LENGTH_ " +
                     (isVariableArray(field.resolvedType.arrayKind) ? "true" : "false"));
    }
    if (!section.fields.empty())
    {
        out << "\n";
    }
}

void emitSectionTypedef(std::ostringstream&    out,
                        const std::string&     typeName,
                        const SemanticSection& section,
                        const EmitterContext&  ctx,
                        const bool             deprecatedAttribute)
{
    emitLine(out, 0, "typedef struct " + typeName + " {");

    std::size_t emitted = 0;
    for (const auto& field : section.fields)
    {
        if (field.isPadding)
        {
            continue;
        }

        const auto cMember  = codegenSanitizeIdentifier(CodegenNamingLanguage::C, field.name);
        const auto baseType = cTypeFromFieldType(field.resolvedType, ctx);

        if (field.resolvedType.arrayKind == ArrayKind::None)
        {
            emitAttachedDocC(out, 1, field.doc);
            emitLine(out, 1, baseType + " " + cMember + ";");
            ++emitted;
            continue;
        }

        if (field.resolvedType.arrayKind == ArrayKind::Fixed)
        {
            emitAttachedDocC(out, 1, field.doc);
            if (field.resolvedType.scalarCategory == SemanticScalarCategory::Bool)
            {
                emitLine(out,
                         1,
                         "uint8_t " + cMember + "[(" + std::to_string(field.resolvedType.arrayCapacity) +
                             "U + 7U) / 8U];");
            }
            else
            {
                emitLine(out,
                         1,
                         baseType + " " + cMember + "[" + std::to_string(field.resolvedType.arrayCapacity) + "U];");
            }
            ++emitted;
            continue;
        }

        emitAttachedDocC(out, 1, field.doc);
        emitLine(out, 1, "struct {");
        if (field.resolvedType.scalarCategory == SemanticScalarCategory::Bool)
        {
            emitLine(out,
                     2,
                     "uint8_t bitpacked[(" + std::to_string(field.resolvedType.arrayCapacity) + "U + 7U) / 8U];");
        }
        else
        {
            emitLine(out, 2, baseType + " elements[" + std::to_string(field.resolvedType.arrayCapacity) + "U];");
        }
        emitLine(out, 2, "size_t count;");
        emitLine(out, 1, "} " + cMember + ";");
        ++emitted;
    }

    if (section.isUnion)
    {
        // Tag storage must match the wire tag width (uint8 for <=256 options, uint16 for
        // 257..65536, etc.); a hardcoded uint8_t truncates a wide tag and mis-dispatches.
        // sectionFacts isn't threaded here; resolveUnionTagBits falls back to the
        // analyzer-set per-field width, which is authoritative.
        emitLine(out, 1, unsignedStorageType(resolveUnionTagBits(section, nullptr)) + " _tag_;");
        ++emitted;
    }

    if (emitted == 0)
    {
        emitLine(out, 1, "uint8_t _dummy_;");
    }

    if (deprecatedAttribute)
    {
        emitLine(out, 0, "} __attribute__((deprecated)) " + typeName + ";");
    }
    else
    {
        emitLine(out, 0, "} " + typeName + ";");
    }
    out << "\n";

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
        emitLine(out, 0, "#define " + typeName + "_UNION_OPTION_COUNT_ " + std::to_string(optionCount) + "U");
        out << "\n";
    }
}

void emitSectionConstants(std::ostringstream& out, const std::string& typeName, const SemanticSection& section)
{
    for (const auto& c : section.constants)
    {
        emitAttachedDocC(out, 0, c.doc);
        emitLine(out,
                 0,
                 "#define " + typeName + "_" + sanitizeMacroToken(c.name) + " (" + valueToCExpr(c.type, c.value) + ")");
    }
    if (!section.constants.empty())
    {
        out << "\n";
    }
}

void emitSectionMetadata(std::ostringstream&    out,
                         const std::string&     typeName,
                         const std::string&     fullName,
                         std::uint32_t          majorVersion,
                         std::uint32_t          minorVersion,
                         const SemanticSection& section,
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
        emitLine(out, 0, line);
    }
    const bool zohAliasEligible = sectionFacts != nullptr && sectionFacts->zohAliasEligible;
    const std::string zohAliasReason = zohAliasEligible
                                           ? std::string("eligible")
                                           : ((sectionFacts != nullptr && !sectionFacts->zohAliasReason.empty())
                                                  ? sectionFacts->zohAliasReason
                                                  : std::string("not-proven"));
    emitLine(out, 0, "#define " + typeName + "_ZOH_ALIAS_ELIGIBLE_ " + std::string(zohAliasEligible ? "true" : "false"));
    emitLine(out, 0, "#define " + typeName + "_ZOH_ALIAS_REASON_ \"" + zohAliasReason + "\"");
    emitLine(out, 0, "#define " + typeName + "_IS_DEPRECATED_ " + std::string(section.deprecated ? "true" : "false"));
    out << "\n";
}

void emitSection(std::ostringstream&       out,
                 const EmitterContext&     ctx,
                 const SemanticDefinition& def,
                 const std::string&        typeName,
                 const std::string&        fullName,
                 const std::string&        sectionName,
                 const SemanticSection&    section,
                 const AttachedDoc&        typeDoc,
                 const LoweredSectionFacts* const sectionFacts)
{
    emitSectionMetadata(out, typeName, fullName, def.info.majorVersion, def.info.minorVersion, section, sectionFacts);
    emitSectionConstants(out, typeName, section);
    emitArrayMacros(out, typeName, section);
    emitAttachedDocC(out, 0, docWithDeprecationNotice(typeDoc, section.deprecated, def.info.fullName, def.info.majorVersion, def.info.minorVersion));
    emitSectionTypedef(out, typeName, section, ctx, section.deprecated && ctx.emitDeprecationAttributes());

    const auto irStem = sectionIRFunctionStem(def, sectionName);
    emitLine(out,
             0,
             "int8_t " + irStem + "__serialize_ir_(const " + typeName +
                 "* const obj, uint8_t* buffer, size_t* const "
                 "inout_buffer_size_bytes);");
    emitLine(out,
             0,
             "int8_t " + irStem + "__deserialize_ir_(" + typeName +
                 "* const out_obj, const uint8_t* buffer, size_t* const "
                 "inout_buffer_size_bytes);");
    out << "\n";

    emitLine(out,
             0,
             "static inline int8_t " + typeName + "__serialize_(const " + typeName +
                 "* const obj, uint8_t* const buffer, size_t* const "
                 "inout_buffer_size_bytes)");
    emitLine(out, 0, "{");
    emitLine(out, 1, "return " + irStem + "__serialize_ir_(obj, buffer, inout_buffer_size_bytes);");
    emitLine(out, 0, "}");
    out << "\n";

    emitLine(out,
             0,
             "static inline int8_t " + typeName + "__deserialize_(" + typeName +
                 "* const out_obj, const uint8_t* buffer, size_t* const "
                 "inout_buffer_size_bytes)");
    emitLine(out, 0, "{");
    emitLine(out, 1, "return " + irStem + "__deserialize_ir_(out_obj, buffer, inout_buffer_size_bytes);");
    emitLine(out, 0, "}");
    out << "\n";

    emitLine(out,
             0,
             "static inline int8_t " + typeName +
                 "__try_deserialize_view_(const uint8_t* const buffer, size_t* const inout_buffer_size_bytes, "
                 "const uint8_t** const out_view_bytes)");
    emitLine(out, 0, "{");
    emitLine(out, 1, "if ((buffer == NULL) || (inout_buffer_size_bytes == NULL) || (out_view_bytes == NULL)) {");
    emitLine(out, 2, "return -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
    emitLine(out, 1, "}");
    emitLine(out, 1, "*out_view_bytes = NULL;");
    emitLine(out, 1, "const size_t _required = " + typeName + "_SERIALIZATION_BUFFER_SIZE_BYTES_;");
    emitLine(out, 1, "if (*inout_buffer_size_bytes < _required) {");
    emitLine(out, 2, "*inout_buffer_size_bytes = _required;");
    emitLine(out, 2, "return -DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL;");
    emitLine(out, 1, "}");
    emitLine(out, 1, "#if defined(LLVMDSDL_TARGET_ENDIANNESS_BIG)");
    emitLine(out, 2, "(void)buffer;");
    emitLine(out, 2, "(void)_required;");
    emitLine(out, 2, "*inout_buffer_size_bytes = 0U;");
    emitLine(out, 2, "return -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
    emitLine(out, 1, "#elif " + typeName + "_ZOH_ALIAS_ELIGIBLE_");
    emitLine(out, 2, "*out_view_bytes = buffer;");
    emitLine(out, 2, "*inout_buffer_size_bytes = _required;");
    emitLine(out, 2, "return DSDL_RUNTIME_SUCCESS;");
    emitLine(out, 1, "#else");
    emitLine(out, 2, "*inout_buffer_size_bytes = 0U;");
    emitLine(out, 2, "return -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
    emitLine(out, 1, "#endif");
    emitLine(out, 0, "}");
    out << "\n";

    emitLine(out,
             0,
             "static inline int8_t " + typeName +
                 "__try_serialize_view_(const uint8_t* const view_bytes, const size_t view_size_bytes, "
                 "uint8_t* const buffer, size_t* const inout_buffer_size_bytes)");
    emitLine(out, 0, "{");
    emitLine(out, 1, "if ((view_bytes == NULL) || (buffer == NULL) || (inout_buffer_size_bytes == NULL)) {");
    emitLine(out, 2, "return -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
    emitLine(out, 1, "}");
    emitLine(out, 1, "const size_t _required = " + typeName + "_SERIALIZATION_BUFFER_SIZE_BYTES_;");
    emitLine(out, 1, "if (view_size_bytes != _required) {");
    emitLine(out, 2, "return -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
    emitLine(out, 1, "}");
    emitLine(out, 1, "if (*inout_buffer_size_bytes < _required) {");
    emitLine(out, 2, "*inout_buffer_size_bytes = _required;");
    emitLine(out, 2, "return -DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL;");
    emitLine(out, 1, "}");
    emitLine(out, 1, "#if defined(LLVMDSDL_TARGET_ENDIANNESS_BIG)");
    emitLine(out, 2, "(void)buffer;");
    emitLine(out, 2, "(void)view_bytes;");
    emitLine(out, 2, "*inout_buffer_size_bytes = 0U;");
    emitLine(out, 2, "return -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
    emitLine(out, 1, "#elif " + typeName + "_ZOH_ALIAS_ELIGIBLE_");
    emitLine(out, 2, "(void)memcpy(buffer, view_bytes, _required);");
    emitLine(out, 2, "*inout_buffer_size_bytes = _required;");
    emitLine(out, 2, "return DSDL_RUNTIME_SUCCESS;");
    emitLine(out, 1, "#else");
    emitLine(out, 2, "*inout_buffer_size_bytes = 0U;");
    emitLine(out, 2, "return -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
    emitLine(out, 1, "#endif");
    emitLine(out, 0, "}");
    out << "\n";
}

llvm::Expected<std::string> loadRuntimeHeader()
{
    if (const auto data = embedded_runtime::find("dsdl_runtime.h"))
    {
        return std::string(*data);
    }
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "embedded runtime source missing: dsdl_runtime.h");
}

std::string renderHeader(const SemanticDefinition& def, const EmitterContext& ctx, const LoweredFactsMap& loweredFacts)
{
    std::ostringstream out;
    const auto         guard        = headerGuard(def.info);
    const auto         baseTypeName = ctx.cTypeName(def);

    out << generatedCommentLine("C backend") << "\n";
    out << "/* Source: " << def.info.fullName << "." << def.info.majorVersion << "." << def.info.minorVersion
        << " */\n\n";
    out << "#ifndef " << guard << "\n";
    out << "#define " << guard << "\n\n";
    out << "#include <stddef.h>\n";
    out << "#include <stdint.h>\n";
    out << "#include <stdbool.h>\n";
    out << "#include <string.h>\n";
    out << "#include \"dsdl_runtime.h\"\n";

    for (const auto& depRef : collectDefinitionCompositeDependencies(def))
    {
        if (const auto* dep = ctx.find(depRef))
        {
            out << "#include \"" << ctx.relativeHeaderPath(*dep) << "\"\n";
        }
    }
    out << "\n";

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
        const auto requestType  = baseTypeName + "__Request";
        const auto responseType = baseTypeName + "__Response";

        for (const auto& line : renderCServiceAliasIdentityMacros(baseTypeName,
                                                                  def.info.fullName,
                                                                  def.info.majorVersion,
                                                                  def.info.minorVersion))
        {
            emitLine(out, 0, line);
        }
        out << "\n";

        emitSection(out,
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
            emitSection(out,
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
            emitLine(out, 0, line);
        }
        out << "\n";

        for (const auto& line : renderCServiceAliasWrapperLines(baseTypeName, requestType))
        {
            emitLine(out, 0, line);
        }
    }
    else
    {
        emitSection(out,
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

    std::filesystem::path outRoot(options.outDir);
    EmitterContext        ctx(semantic, options.emitDeprecationAttributes);
    const auto            selectedTypeKeys = makeTypeKeySet(options.selectedTypeKeys);

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
        perDefModule->setAttr("llvmdsdl.headers_available", mlir::UnitAttr::get(perDefModule.getContext()));
        perDefModule->setAttr("llvmdsdl.require_typed_lowering", mlir::UnitAttr::get(perDefModule.getContext()));

        const std::string targetHeaderPath = ctx.relativeHeaderPath(def);
        const auto        targetIt         = schemaByHeaderPath.find(targetHeaderPath);
        if (targetIt == schemaByHeaderPath.end())
        {
            diagnostics.error({"<mlir>", 1, 1},
                              "failed to locate schema op for " + def.info.fullName + " (" + targetHeaderPath + ")");
            return llvm::createStringError(llvm::inconvertibleErrorCode(), "schema selection failed");
        }
        perDefModule.getBodyRegion().front().push_back(targetIt->second->clone());

        mlir::PassManager pm(perDefModule.getContext());
        pm.addPass(createLowerDSDLExecPass());
        if (options.optimizeLoweredSerDes)
        {
            addOptimizeLoweredSerDesPipeline(pm);
        }
        pm.addPass(createConvertDSDLToEmitCPass());
        pm.addPass(mlir::createCanonicalizerPass());
        pm.addPass(mlir::createCSEPass());
        pm.addPass(mlir::createSCFToEmitC());
        pm.addPass(mlir::createConvertArithToEmitC());
        pm.addPass(mlir::createConvertFuncToEmitC());

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
        if (auto err = writeGeneratedFile(implDir / implFileName(def.info),
                                          implPreamble + implGuardOpen + emitted + implGuardClose,
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

}  // namespace llvmdsdl
