//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Canonical profile-agnostic C++ ABI staging for object emission.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/CppObjectAbiEmitter.h"

#include "llvmdsdl/CodeGen/DefinitionDependencies.h"
#include "llvmdsdl/CodeGen/DefinitionIndex.h"
#include "llvmdsdl/CodeGen/SectionNaming.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/CodeGen/StorageTypeTokens.h"
#include "llvmdsdl/CodeGen/WireLayoutFacts.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Version.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace llvmdsdl
{
namespace
{

struct SectionPlan final
{
    std::string            sectionName;
    std::string            cppTypeName;
    std::string            cTypeName;
    std::string            shimTypeName;
    const SemanticSection* section{nullptr};
};

std::string cppNamespacePath(const std::vector<std::string>& components)
{
    std::string out;
    for (std::size_t i = 0; i < components.size(); ++i)
    {
        if (i > 0)
        {
            out += "::";
        }
        out += codegenProjectIdentifier(CodegenNamingLanguage::Cpp, IdentifierRole::NamespaceName, components[i]);
    }
    return out;
}

std::string headerFileName(const DiscoveredDefinition& info)
{
    return renderDefinitionFileStem(CodegenNamingLanguage::C, info.shortName, info.majorVersion, info.minorVersion) +
           ".h";
}

std::string cppHeaderFileName(const DiscoveredDefinition& info)
{
    return renderDefinitionFileStem(CodegenNamingLanguage::Cpp, info.shortName, info.majorVersion, info.minorVersion) +
           "_abi.hpp";
}

std::string cppSourceFileName(const DiscoveredDefinition& info)
{
    return renderDefinitionFileStem(CodegenNamingLanguage::Cpp, info.shortName, info.majorVersion, info.minorVersion) +
           "_abi.cpp";
}

std::string shimHeaderFileName(const DiscoveredDefinition& info)
{
    return renderDefinitionFileStem(CodegenNamingLanguage::C, info.shortName, info.majorVersion, info.minorVersion) +
           "_c_shim.h";
}

std::string shimSourceFileName(const DiscoveredDefinition& info)
{
    return renderDefinitionFileStem(CodegenNamingLanguage::C, info.shortName, info.majorVersion, info.minorVersion) +
           "_c_shim.cpp";
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

std::string cppTypeNameFromInfo(const DiscoveredDefinition& info, const TypeNameVersioning versioning)
{
    // `false` reproduces what this emitter did before it shared the rule: it never versioned its
    // type name, and never had the version count to decide with. It is the C++ policy that decides
    // now, so the two lanes move together the next time that policy changes.
    return renderDefinitionTypeName(CodegenNamingLanguage::Cpp,
                                    info.namespaceComponents,
                                    info.shortName,
                                    info.majorVersion,
                                    info.minorVersion,
                                    versioning);
}

std::string shimTypeNameFromInfo(const DiscoveredDefinition& info, const TypeNameVersioning versioning)
{
    // The C type name under a prefix that keeps the shim's struct distinct from the C backend's own.
    return "llvmdsdl_cppabi__" + cTypeNameFromInfo(info, versioning);
}

std::string shimSymbolStem(const SemanticDefinition& def, const std::string& sectionName)
{
    return "llvmdsdl_cppabi_" +
           renderDefinitionSymbolBase(def.info.fullName, def.info.majorVersion, def.info.minorVersion) +
           renderSectionSymbolSuffix(sectionName);
}

std::string canonicalQualifiedPrefix(const DiscoveredDefinition& info)
{
    const auto nsPath = cppNamespacePath(info.namespaceComponents);
    if (nsPath.empty())
    {
        return "::abi::";
    }
    return "::" + nsPath + "::abi::";
}

std::filesystem::path namespacePath(const std::vector<std::string>& namespaceComponents)
{
    std::filesystem::path out;
    for (const auto& ns : namespaceComponents)
    {
        out /= ns;
    }
    return out;
}

std::filesystem::path canonicalHeaderPath(const DiscoveredDefinition& info)
{
    return std::filesystem::path("abi") / namespacePath(info.namespaceComponents) / cppHeaderFileName(info);
}

std::filesystem::path canonicalSourcePath(const DiscoveredDefinition& info)
{
    return std::filesystem::path("abi") / namespacePath(info.namespaceComponents) / cppSourceFileName(info);
}

std::filesystem::path shimHeaderPath(const DiscoveredDefinition& info)
{
    return std::filesystem::path("c_shim") / namespacePath(info.namespaceComponents) / shimHeaderFileName(info);
}

std::filesystem::path shimSourcePath(const DiscoveredDefinition& info)
{
    return std::filesystem::path("c_shim") / namespacePath(info.namespaceComponents) / shimSourceFileName(info);
}

std::filesystem::path adapterHeaderPath(llvm::StringRef profile, const DiscoveredDefinition& info)
{
    return std::filesystem::path(profile.str()) / namespacePath(info.namespaceComponents) /
           (renderDefinitionFileStem(CodegenNamingLanguage::Cpp, info.shortName, info.majorVersion, info.minorVersion) +
            ".hpp");
}

std::filesystem::path cHeaderPath(const DiscoveredDefinition& info)
{
    return std::filesystem::path("c") / namespacePath(info.namespaceComponents) / headerFileName(info);
}

std::string bytesForBitsExpr(const std::string& bitsExpr)
{
    return "((" + bitsExpr + ") + 7U) / 8U";
}

std::string sizeLiteral(const std::int64_t v)
{
    const std::int64_t nonNegative = std::max<std::int64_t>(v, 0);
    return std::to_string(static_cast<std::uint64_t>(nonNegative)) + "U";
}

void emitLine(std::ostringstream& out, const std::size_t indent, const std::string& line)
{
    for (std::size_t i = 0; i < indent; ++i)
    {
        out << "  ";
    }
    out << line << '\n';
}

void emitNamespaceOpen(std::ostringstream& out, const std::vector<std::string>& components)
{
    for (const auto& component : components)
    {
        emitLine(out,
                 0,
                 "namespace " +
                     codegenProjectIdentifier(CodegenNamingLanguage::Cpp, IdentifierRole::NamespaceName, component) +
                     " {");
    }
}

void emitNamespaceClose(std::ostringstream& out, const std::vector<std::string>& components)
{
    for (auto it = components.rbegin(); it != components.rend(); ++it)
    {
        emitLine(out,
                 0,
                 "} // namespace " +
                     codegenProjectIdentifier(CodegenNamingLanguage::Cpp, IdentifierRole::NamespaceName, *it));
    }
}

class EmitterContext final
{
public:
    EmitterContext(const SemanticModule& semantic, const TypeNameVersioning typeNameVersioning)
        : index_(semantic)
        , typeNameVersioning_(typeNameVersioning)
    {
    }

    /// @brief Whether generated type names carry the definition's version.
    TypeNameVersioning typeNameVersioning() const
    {
        return typeNameVersioning_;
    }

    const SemanticDefinition* find(const SemanticTypeRef& ref) const
    {
        return index_.find(ref);
    }

    std::string abiQualifiedTypeName(const SemanticTypeRef& ref) const
    {
        std::ostringstream out;
        out << "::";
        for (const auto& ns : ref.namespaceComponents)
        {
            out << codegenProjectIdentifier(CodegenNamingLanguage::Cpp, IdentifierRole::NamespaceName, ns) << "::";
        }
        // Through the same producer the struct is declared with, rather than re-deriving the short
        // name here: the two agree today only because neither versions the name, and the whole point
        // of a shared producer is that they keep agreeing when that changes.
        DiscoveredDefinition tmp;
        tmp.shortName           = ref.shortName;
        tmp.namespaceComponents = ref.namespaceComponents;
        tmp.majorVersion        = ref.majorVersion;
        tmp.minorVersion        = ref.minorVersion;
        out << "abi::" << cppTypeNameFromInfo(tmp, typeNameVersioning_);
        return out.str();
    }

    std::string shimQualifiedTypeName(const SemanticTypeRef& ref) const
    {
        if (const auto* def = find(ref))
        {
            return shimTypeNameFromInfo(def->info, typeNameVersioning_);
        }

        DiscoveredDefinition tmp;
        tmp.shortName           = ref.shortName;
        tmp.namespaceComponents = ref.namespaceComponents;
        return shimTypeNameFromInfo(tmp, typeNameVersioning_);
    }

private:
    DefinitionIndex index_;
    TypeNameVersioning typeNameVersioning_{TypeNameVersioning::Unversioned};
};

std::string cppScalarType(const SemanticFieldType& type, const EmitterContext& ctx)
{
    switch (type.scalarCategory)
    {
    case SemanticScalarCategory::Bool:
        return "bool";
    case SemanticScalarCategory::Byte:
    case SemanticScalarCategory::Utf8:
    case SemanticScalarCategory::UnsignedInt:
        return renderUnsignedStorageToken(StorageTokenLanguage::Cpp, type.bitLength);
    case SemanticScalarCategory::SignedInt:
        return renderSignedStorageToken(StorageTokenLanguage::Cpp, type.bitLength);
    case SemanticScalarCategory::Float:
        return (type.bitLength == 64U) ? "double" : "float";
    case SemanticScalarCategory::Void:
        return "std::uint8_t";
    case SemanticScalarCategory::Composite:
        if (type.compositeType)
        {
            return ctx.abiQualifiedTypeName(*type.compositeType);
        }
        return "std::uint8_t";
    }
    return "std::uint8_t";
}

std::string cScalarType(const SemanticFieldType& type)
{
    switch (type.scalarCategory)
    {
    case SemanticScalarCategory::Bool:
        return "bool";
    case SemanticScalarCategory::Byte:
    case SemanticScalarCategory::Utf8:
    case SemanticScalarCategory::UnsignedInt:
        return renderUnsignedStorageToken(StorageTokenLanguage::C, type.bitLength);
    case SemanticScalarCategory::SignedInt:
        return renderSignedStorageToken(StorageTokenLanguage::C, type.bitLength);
    case SemanticScalarCategory::Float:
        return (type.bitLength == 64U) ? "double" : "float";
    case SemanticScalarCategory::Void:
        return "uint8_t";
    case SemanticScalarCategory::Composite:
        return "void";
    }
    return "uint8_t";
}

std::string shimScalarType(const SemanticFieldType& type, const EmitterContext& ctx)
{
    if (type.scalarCategory == SemanticScalarCategory::Composite && type.compositeType)
    {
        return ctx.shimQualifiedTypeName(*type.compositeType);
    }
    return cScalarType(type);
}

std::vector<SectionPlan> sectionPlansForDefinition(const SemanticDefinition& def,
                                                   const TypeNameVersioning  versioning)
{
    std::vector<SectionPlan> out;
    if (def.isService)
    {
        const std::string cppBase  = cppTypeNameFromInfo(def.info, versioning);
        const std::string cBase    = cTypeNameFromInfo(def.info, versioning);
        const std::string shimBase = shimTypeNameFromInfo(def.info, versioning);

        out.push_back(
            SectionPlan{"request", cppBase + "_Request", cBase + "__Request", shimBase + "__Request", &def.request});
        if (def.response)
        {
            out.push_back(SectionPlan{"response",
                                      cppBase + "_Response",
                                      cBase + "__Response",
                                      shimBase + "__Response",
                                      &(*def.response)});
        }
    }
    else
    {
        out.push_back(SectionPlan{"",
                                  cppTypeNameFromInfo(def.info, versioning),
                                  cTypeNameFromInfo(def.info, versioning),
                                  shimTypeNameFromInfo(def.info, versioning),
                                  &def.request});
    }
    return out;
}

const LoweredSectionFacts* lookupSectionFacts(const LoweredFactsMap&    loweredFacts,
                                              const SemanticDefinition& def,
                                              const std::string&        sectionName)
{
    const auto key   = loweredTypeKey(def.info.fullName, def.info.majorVersion, def.info.minorVersion);
    const auto defIt = loweredFacts.find(key);
    if (defIt == loweredFacts.end())
    {
        return nullptr;
    }
    const auto sectionIt = defIt->second.find(sectionName);
    if (sectionIt == defIt->second.end())
    {
        return nullptr;
    }
    return &sectionIt->second;
}

void emitCanonicalStruct(std::ostringstream&        out,
                         const SectionPlan&         plan,
                         const SemanticSection&     section,
                         const EmitterContext&      ctx,
                         const LoweredSectionFacts* sectionFacts)
{
    const NamingScope cppScope = makeSectionFieldScope(CodegenNamingLanguage::Cpp, section);
    emitLine(out, 0, "struct " + plan.cppTypeName + ";");
    emitLine(out,
             0,
             "std::int8_t " + plan.cppTypeName + "_serialize_(const " + plan.cppTypeName +
                 "* obj, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes);");
    emitLine(out,
             0,
             "std::int8_t " + plan.cppTypeName + "_deserialize_(" + plan.cppTypeName +
                 "* out_obj, const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes);");
    emitLine(out,
             0,
             "std::int8_t " + plan.cppTypeName +
                 "_try_deserialize_view_(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, "
                 "const std::uint8_t** out_view_bytes);");
    emitLine(out,
             0,
             "std::int8_t " + plan.cppTypeName +
                 "_try_serialize_view_(const std::uint8_t* view_bytes, std::size_t view_size_bytes, "
                 "std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes);");
    out << '\n';

    emitLine(out, 0, "struct " + plan.cppTypeName + " {");

    std::size_t emittedFields = 0;
    for (const auto& field : section.fields)
    {
        if (field.isPadding)
        {
            continue;
        }

        const std::string member   = cppScope.get(IdentifierRole::FieldName, field.name);
        const std::string elemType = cppScalarType(field.resolvedType, ctx);
        const std::string cap      = sizeLiteral(field.resolvedType.arrayCapacity);

        if (field.resolvedType.arrayKind == ArrayKind::None)
        {
            emitLine(out, 1, elemType + " " + member + "{};");
        }
        else if (field.resolvedType.arrayKind == ArrayKind::Fixed)
        {
            emitLine(out, 1, "std::array<" + elemType + ", " + cap + "> " + member + "{};");
        }
        else
        {
            emitLine(out, 1, "::llvmdsdl::cppabi::BoundedSequence<" + elemType + ", " + cap + "> " + member + "{};");
        }
        ++emittedFields;
    }

    if (section.isUnion)
    {
        // Tag storage must match the wire tag width (uint8 for <=256 options, uint16 for
        // 257..65536, etc.); a hardcoded uint8 truncates a wide tag and mis-dispatches.
        emitLine(out,
                 1,
                 renderUnsignedStorageToken(StorageTokenLanguage::Cpp, resolveUnionTagBits(section, sectionFacts)) +
                     " _tag_{0U};");
        ++emittedFields;
    }
    if (emittedFields == 0)
    {
        emitLine(out, 1, "std::uint8_t _dummy_{0U};");
    }

    const bool        zohEligible = sectionFacts != nullptr && sectionFacts->zohAliasEligible;
    const std::string zohReason   = (sectionFacts != nullptr && !sectionFacts->zohAliasReason.empty())
                                        ? sectionFacts->zohAliasReason
                                        : std::string("not-proven");

    emitLine(out,
             1,
             "static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = " +
                 sizeLiteral((section.serializationBufferSizeBits + 7) / 8) + ";");
    emitLine(out,
             1,
             "static constexpr std::size_t EXTENT_BYTES = " +
                 sizeLiteral(section.extentBits.has_value() ? (section.extentBits.value() / 8) : 0) + ";");
    emitLine(out, 1, std::string("static constexpr bool ZOH_ALIAS_ELIGIBLE = ") + (zohEligible ? "true;" : "false;"));
    emitLine(out, 1, "static constexpr const char* ZOH_ALIAS_REASON = \"" + zohReason + "\";");

    emitLine(out, 1, "void to_c(" + plan.cTypeName + "* out) const;");
    emitLine(out, 1, "static void from_c(" + plan.cppTypeName + "* out, const " + plan.cTypeName + "* in);");
    out << '\n';

    emitLine(out, 1, "std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {");
    emitLine(out, 2, "return " + plan.cppTypeName + "_serialize_(this, buffer, inout_buffer_size_bytes);");
    emitLine(out, 1, "}");
    emitLine(out, 1, "std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {");
    emitLine(out, 2, "return " + plan.cppTypeName + "_deserialize_(this, buffer, inout_buffer_size_bytes);");
    emitLine(out, 1, "}");
    emitLine(out,
             1,
             "static std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* "
             "inout_buffer_size_bytes, "
             "const std::uint8_t** out_view_bytes) {");
    emitLine(out,
             2,
             "return " + plan.cppTypeName + "_try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);");
    emitLine(out, 1, "}");
    emitLine(out,
             1,
             "static std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, "
             "std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {");
    emitLine(out,
             2,
             "return " + plan.cppTypeName +
                 "_try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);");
    emitLine(out, 1, "}");

    emitLine(out, 0, "};");
    out << '\n';
}

void emitCanonicalConversionBodyToC(std::ostringstream& out, const SectionPlan& plan, const SemanticSection& section)
{
    // The canonical struct and the C struct it converts to are named by two different
    // emitters, so both scopes are rebuilt here from the same section rather than a member
    // name being invented locally: what this writes has to be what each of them declared.
    const NamingScope cppScope = makeSectionFieldScope(CodegenNamingLanguage::Cpp, section);
    const NamingScope cScope   = makeSectionFieldScope(CodegenNamingLanguage::C, section);
    emitLine(out, 0, "void " + plan.cppTypeName + "::to_c(" + plan.cTypeName + "* const out) const");
    emitLine(out, 0, "{");
    emitLine(out, 1, "if (out == nullptr) {");
    emitLine(out, 2, "return;");
    emitLine(out, 1, "}");
    emitLine(out, 1, "*out = " + plan.cTypeName + "{};");

    for (const auto& field : section.fields)
    {
        if (field.isPadding)
        {
            continue;
        }
        const std::string cppMember = cppScope.get(IdentifierRole::FieldName, field.name);
        const std::string cMember   = cScope.get(IdentifierRole::FieldName, field.name);
        const std::string cap       = sizeLiteral(field.resolvedType.arrayCapacity);

        if (field.resolvedType.arrayKind == ArrayKind::None)
        {
            if (field.resolvedType.scalarCategory == SemanticScalarCategory::Composite)
            {
                emitLine(out, 1, cppMember + ".to_c(&out->" + cMember + ");");
            }
            else
            {
                emitLine(out,
                         1,
                         "out->" + cMember + " = static_cast<std::remove_reference_t<decltype(out->" + cMember +
                             ")>>(" + cppMember + ");");
            }
            continue;
        }

        if (field.resolvedType.arrayKind == ArrayKind::Fixed)
        {
            if (field.resolvedType.scalarCategory == SemanticScalarCategory::Bool)
            {
                emitLine(out, 1, "for (std::size_t i = 0U; i < " + bytesForBitsExpr(cap) + "; ++i) {");
                emitLine(out, 2, "out->" + cMember + "[i] = 0U;");
                emitLine(out, 1, "}");
                emitLine(out, 1, "for (std::size_t i = 0U; i < " + cap + "; ++i) {");
                emitLine(out, 2, "if (" + cppMember + "[i]) {");
                emitLine(out,
                         3,
                         "out->" + cMember + "[i / 8U] = static_cast<std::uint8_t>(out->" + cMember +
                             "[i / 8U] | (1U << (i % 8U)));");
                emitLine(out, 2, "}");
                emitLine(out, 1, "}");
            }
            else if (field.resolvedType.scalarCategory == SemanticScalarCategory::Composite)
            {
                emitLine(out, 1, "for (std::size_t i = 0U; i < " + cap + "; ++i) {");
                emitLine(out, 2, cppMember + "[i].to_c(&out->" + cMember + "[i]);");
                emitLine(out, 1, "}");
            }
            else
            {
                emitLine(out, 1, "for (std::size_t i = 0U; i < " + cap + "; ++i) {");
                emitLine(out,
                         2,
                         "out->" + cMember + "[i] = static_cast<std::remove_reference_t<decltype(out->" + cMember +
                             "[i])>>(" + cppMember + "[i]);");
                emitLine(out, 1, "}");
            }
            continue;
        }

        emitLine(out, 1, "out->" + cMember + ".count = " + cppMember + ".count;");
        emitLine(out,
                 1,
                 "const std::size_t " + cppMember + "_copy_count = std::min<std::size_t>(" + cppMember + ".count, " +
                     cap + ");");
        if (field.resolvedType.scalarCategory == SemanticScalarCategory::Bool)
        {
            emitLine(out, 1, "for (std::size_t i = 0U; i < " + bytesForBitsExpr(cap) + "; ++i) {");
            emitLine(out, 2, "out->" + cMember + ".bitpacked[i] = 0U;");
            emitLine(out, 1, "}");
            emitLine(out, 1, "for (std::size_t i = 0U; i < " + cppMember + "_copy_count; ++i) {");
            emitLine(out, 2, "if (" + cppMember + ".elements[i]) {");
            emitLine(out,
                     3,
                     "out->" + cMember + ".bitpacked[i / 8U] = static_cast<std::uint8_t>(out->" + cMember +
                         ".bitpacked[i / 8U] | (1U << (i % 8U)));");
            emitLine(out, 2, "}");
            emitLine(out, 1, "}");
        }
        else if (field.resolvedType.scalarCategory == SemanticScalarCategory::Composite)
        {
            emitLine(out, 1, "for (std::size_t i = 0U; i < " + cppMember + "_copy_count; ++i) {");
            emitLine(out, 2, cppMember + ".elements[i].to_c(&out->" + cMember + ".elements[i]);");
            emitLine(out, 1, "}");
        }
        else
        {
            emitLine(out, 1, "for (std::size_t i = 0U; i < " + cppMember + "_copy_count; ++i) {");
            emitLine(out,
                     2,
                     "out->" + cMember + ".elements[i] = static_cast<std::remove_reference_t<decltype(out->" + cMember +
                         ".elements[i])>>(" + cppMember + ".elements[i]);");
            emitLine(out, 1, "}");
        }
    }

    if (section.isUnion)
    {
        emitLine(out, 1, "out->_tag_ = _tag_;");
    }

    emitLine(out, 0, "}");
    out << '\n';
}

void emitCanonicalConversionBodyFromC(std::ostringstream& out, const SectionPlan& plan, const SemanticSection& section)
{
    // The canonical struct and the C struct it converts to are named by two different
    // emitters, so both scopes are rebuilt here from the same section rather than a member
    // name being invented locally: what this writes has to be what each of them declared.
    const NamingScope cppScope = makeSectionFieldScope(CodegenNamingLanguage::Cpp, section);
    const NamingScope cScope   = makeSectionFieldScope(CodegenNamingLanguage::C, section);
    emitLine(out,
             0,
             "void " + plan.cppTypeName + "::from_c(" + plan.cppTypeName + "* const out, const " + plan.cTypeName +
                 "* const in)");
    emitLine(out, 0, "{");
    emitLine(out, 1, "if ((out == nullptr) || (in == nullptr)) {");
    emitLine(out, 2, "return;");
    emitLine(out, 1, "}");

    for (const auto& field : section.fields)
    {
        if (field.isPadding)
        {
            continue;
        }
        const std::string cppMember = cppScope.get(IdentifierRole::FieldName, field.name);
        const std::string cMember   = cScope.get(IdentifierRole::FieldName, field.name);
        const std::string cap       = sizeLiteral(field.resolvedType.arrayCapacity);

        if (field.resolvedType.arrayKind == ArrayKind::None)
        {
            if (field.resolvedType.scalarCategory == SemanticScalarCategory::Composite)
            {
                emitLine(out,
                         1,
                         "decltype(out->" + cppMember + ")::from_c(&out->" + cppMember + ", &in->" + cMember + ");");
            }
            else
            {
                emitLine(out,
                         1,
                         "out->" + cppMember + " = static_cast<std::remove_reference_t<decltype(out->" + cppMember +
                             ")>>(in->" + cMember + ");");
            }
            continue;
        }

        if (field.resolvedType.arrayKind == ArrayKind::Fixed)
        {
            if (field.resolvedType.scalarCategory == SemanticScalarCategory::Bool)
            {
                emitLine(out, 1, "for (std::size_t i = 0U; i < " + cap + "; ++i) {");
                emitLine(out,
                         2,
                         "out->" + cppMember + "[i] = ((in->" + cMember +
                             "[i / 8U] & static_cast<std::uint8_t>(1U << (i % 8U))) != 0U);");
                emitLine(out, 1, "}");
            }
            else if (field.resolvedType.scalarCategory == SemanticScalarCategory::Composite)
            {
                emitLine(out, 1, "for (std::size_t i = 0U; i < " + cap + "; ++i) {");
                emitLine(out,
                         2,
                         "decltype(out->" + cppMember + "[i])::from_c(&out->" + cppMember + "[i], &in->" + cMember +
                             "[i]);");
                emitLine(out, 1, "}");
            }
            else
            {
                emitLine(out, 1, "for (std::size_t i = 0U; i < " + cap + "; ++i) {");
                emitLine(out,
                         2,
                         "out->" + cppMember + "[i] = static_cast<std::remove_reference_t<decltype(out->" + cppMember +
                             "[i])>>(in->" + cMember + "[i]);");
                emitLine(out, 1, "}");
            }
            continue;
        }

        emitLine(out, 1, "out->" + cppMember + ".count = in->" + cMember + ".count;");
        emitLine(out,
                 1,
                 "const std::size_t " + cppMember + "_copy_count = std::min<std::size_t>(in->" + cMember + ".count, " +
                     cap + ");");

        if (field.resolvedType.scalarCategory == SemanticScalarCategory::Bool)
        {
            emitLine(out, 1, "for (std::size_t i = 0U; i < " + cap + "; ++i) {");
            emitLine(out, 2, "out->" + cppMember + ".elements[i] = false;");
            emitLine(out, 1, "}");
            emitLine(out, 1, "for (std::size_t i = 0U; i < " + cppMember + "_copy_count; ++i) {");
            emitLine(out,
                     2,
                     "out->" + cppMember + ".elements[i] = ((in->" + cMember +
                         ".bitpacked[i / 8U] & static_cast<std::uint8_t>(1U << (i % 8U))) != 0U);");
            emitLine(out, 1, "}");
        }
        else if (field.resolvedType.scalarCategory == SemanticScalarCategory::Composite)
        {
            emitLine(out, 1, "for (std::size_t i = 0U; i < " + cppMember + "_copy_count; ++i) {");
            emitLine(out,
                     2,
                     "decltype(out->" + cppMember + ".elements[i])::from_c(&out->" + cppMember + ".elements[i], &in->" +
                         cMember + ".elements[i]);");
            emitLine(out, 1, "}");
        }
        else
        {
            emitLine(out, 1, "for (std::size_t i = 0U; i < " + cppMember + "_copy_count; ++i) {");
            emitLine(out,
                     2,
                     "out->" + cppMember + ".elements[i] = static_cast<std::remove_reference_t<decltype(out->" +
                         cppMember + ".elements[i])>>(in->" + cMember + ".elements[i]);");
            emitLine(out, 1, "}");
        }
    }

    if (section.isUnion)
    {
        emitLine(out, 1, "out->_tag_ = in->_tag_;");
    }

    emitLine(out, 0, "}");
    out << '\n';
}

void emitCanonicalWireFns(std::ostringstream& out, const SectionPlan& plan)
{
    emitLine(out,
             0,
             "std::int8_t " + plan.cppTypeName + "_serialize_(const " + plan.cppTypeName +
                 "* const obj, std::uint8_t* const buffer, std::size_t* const inout_buffer_size_bytes)");
    emitLine(out, 0, "{");
    emitLine(out, 1, "if (obj == nullptr) {");
    emitLine(out, 2, "return -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
    emitLine(out, 1, "}");
    emitLine(out, 1, plan.cTypeName + " c_obj{};");
    emitLine(out, 1, "obj->to_c(&c_obj);");
    emitLine(out, 1, "return " + plan.cTypeName + "__serialize_(&c_obj, buffer, inout_buffer_size_bytes);");
    emitLine(out, 0, "}");
    out << '\n';

    emitLine(out,
             0,
             "std::int8_t " + plan.cppTypeName + "_deserialize_(" + plan.cppTypeName +
                 "* const out_obj, const std::uint8_t* const buffer, std::size_t* const inout_buffer_size_bytes)");
    emitLine(out, 0, "{");
    emitLine(out, 1, "if (out_obj == nullptr) {");
    emitLine(out, 2, "return -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
    emitLine(out, 1, "}");
    emitLine(out, 1, plan.cTypeName + " c_obj{};");
    emitLine(out,
             1,
             "const std::int8_t rc = " + plan.cTypeName + "__deserialize_(&c_obj, buffer, inout_buffer_size_bytes);");
    emitLine(out, 1, "if (rc >= 0) {");
    emitLine(out, 2, plan.cppTypeName + "::from_c(out_obj, &c_obj);");
    emitLine(out, 1, "}");
    emitLine(out, 1, "return rc;");
    emitLine(out, 0, "}");
    out << '\n';

    emitLine(out,
             0,
             "std::int8_t " + plan.cppTypeName +
                 "_try_deserialize_view_(const std::uint8_t* const buffer, std::size_t* const "
                 "inout_buffer_size_bytes, "
                 "const std::uint8_t** const out_view_bytes)");
    emitLine(out, 0, "{");
    emitLine(out,
             1,
             "return " + plan.cTypeName + "__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);");
    emitLine(out, 0, "}");
    out << '\n';

    emitLine(out,
             0,
             "std::int8_t " + plan.cppTypeName +
                 "_try_serialize_view_(const std::uint8_t* const view_bytes, const std::size_t view_size_bytes, "
                 "std::uint8_t* const buffer, std::size_t* const inout_buffer_size_bytes)");
    emitLine(out, 0, "{");
    emitLine(out,
             1,
             "return " + plan.cTypeName +
                 "__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);");
    emitLine(out, 0, "}");
    out << '\n';
}

std::string renderCanonicalHeader(const SemanticDefinition& def,
                                  const LoweredFactsMap&    loweredFacts,
                                  const EmitterContext&     ctx)
{
    std::ostringstream out;
    const std::string  guard = renderIncludeGuard(CodegenNamingLanguage::Cpp,
                                                  "LLVMDSDL_CPPABI_",
                                                  def.info.fullName,
                                                  def.info.majorVersion,
                                                  def.info.minorVersion,
                                                  "_HPP");

    out << "/* Generated by llvmdsdl " << kVersionString << " (obj-cpp canonical ABI) */\n";
    out << "/* Source: " << def.info.fullName << "." << def.info.majorVersion << "." << def.info.minorVersion
        << " */\n\n";
    out << "#ifndef " << guard << "\n";
    out << "#define " << guard << "\n\n";
    out << "#include <array>\n";
    out << "#include <cstddef>\n";
    out << "#include <cstdint>\n";
    out << "#include \"dsdl_cppabi_runtime.hpp\"\n";
    out << "#ifdef __cplusplus\n";
    out << "extern \"C\" {\n";
    out << "#endif\n";
    out << "#include \"" << cHeaderPath(def.info).generic_string() << "\"\n";
    out << "#ifdef __cplusplus\n";
    out << "}  // extern \"C\"\n";
    out << "#endif\n";

    for (const auto& depRef : collectDefinitionCompositeDependencies(def))
    {
        if (const auto* dep = ctx.find(depRef))
        {
            if (dep->info.fullName == def.info.fullName && dep->info.majorVersion == def.info.majorVersion &&
                dep->info.minorVersion == def.info.minorVersion)
            {
                continue;
            }
            out << "#include \"" << canonicalHeaderPath(dep->info).generic_string() << "\"\n";
        }
    }
    out << "\n";

    emitNamespaceOpen(out, def.info.namespaceComponents);
    emitLine(out, 0, "namespace abi {");
    out << '\n';

    const auto plans = sectionPlansForDefinition(def, ctx.typeNameVersioning());
    for (const auto& plan : plans)
    {
        emitCanonicalStruct(out, plan, *plan.section, ctx, lookupSectionFacts(loweredFacts, def, plan.sectionName));
    }

    if (def.isService)
    {
        const auto baseName = cppTypeNameFromInfo(def.info, ctx.typeNameVersioning());
        emitLine(out, 0, "using " + baseName + " = " + baseName + "_Request;");
        emitLine(out,
                 0,
                 "inline std::int8_t " + baseName + "_serialize_(const " + baseName +
                     "* const obj, std::uint8_t* const buffer, "
                     "std::size_t* const inout_buffer_size_bytes)");
        emitLine(out, 0, "{");
        emitLine(out,
                 1,
                 "return " + baseName + "_Request_serialize_(reinterpret_cast<const " + baseName +
                     "_Request*>(obj), buffer, inout_buffer_size_bytes);");
        emitLine(out, 0, "}");
        emitLine(out,
                 0,
                 "inline std::int8_t " + baseName + "_deserialize_(" + baseName +
                     "* const out_obj, const std::uint8_t* const buffer, "
                     "std::size_t* const inout_buffer_size_bytes)");
        emitLine(out, 0, "{");
        emitLine(out,
                 1,
                 "return " + baseName + "_Request_deserialize_(reinterpret_cast<" + baseName +
                     "_Request*>(out_obj), buffer, inout_buffer_size_bytes);");
        emitLine(out, 0, "}");
        emitLine(out,
                 0,
                 "inline std::int8_t " + baseName +
                     "_try_deserialize_view_(const std::uint8_t* const buffer, std::size_t* const "
                     "inout_buffer_size_bytes, const std::uint8_t** const out_view_bytes)");
        emitLine(out, 0, "{");
        emitLine(out,
                 1,
                 "return " + baseName +
                     "_Request_try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);");
        emitLine(out, 0, "}");
        emitLine(out,
                 0,
                 "inline std::int8_t " + baseName +
                     "_try_serialize_view_(const std::uint8_t* const view_bytes, const std::size_t view_size_bytes, "
                     "std::uint8_t* const buffer, std::size_t* const inout_buffer_size_bytes)");
        emitLine(out, 0, "{");
        emitLine(out,
                 1,
                 "return " + baseName +
                     "_Request_try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);");
        emitLine(out, 0, "}");
    }

    emitLine(out, 0, "} // namespace abi");
    emitNamespaceClose(out, def.info.namespaceComponents);

    out << "\n#endif /* " << guard << " */\n";
    return out.str();
}

std::string renderCanonicalSource(const SemanticDefinition& def, const TypeNameVersioning versioning)
{
    std::ostringstream out;

    out << "/* Generated by llvmdsdl " << kVersionString << " (obj-cpp canonical ABI) */\n";
    out << "/* Source: " << def.info.fullName << "." << def.info.majorVersion << "." << def.info.minorVersion
        << " */\n\n";
    out << "#include \"" << canonicalHeaderPath(def.info).generic_string() << "\"\n";
    out << "#include <algorithm>\n\n";
    out << "#include <type_traits>\n\n";

    emitNamespaceOpen(out, def.info.namespaceComponents);
    emitLine(out, 0, "namespace abi {");
    out << '\n';

    const auto plans = sectionPlansForDefinition(def, versioning);
    for (const auto& plan : plans)
    {
        emitCanonicalConversionBodyToC(out, plan, *plan.section);
        emitCanonicalConversionBodyFromC(out, plan, *plan.section);
        emitCanonicalWireFns(out, plan);
    }

    emitLine(out, 0, "} // namespace abi");
    emitNamespaceClose(out, def.info.namespaceComponents);

    return out.str();
}

void emitShimStruct(std::ostringstream&    out,
                    const SectionPlan&     plan,
                    const SemanticSection& section,
                    const EmitterContext&  ctx)
{
    const NamingScope cScope = makeSectionFieldScope(CodegenNamingLanguage::C, section);
    emitLine(out, 0, "typedef struct " + plan.shimTypeName + " {");

    std::size_t emittedFields = 0;
    for (const auto& field : section.fields)
    {
        if (field.isPadding)
        {
            continue;
        }
        const std::string member   = cScope.get(IdentifierRole::FieldName, field.name);
        const std::string elemType = shimScalarType(field.resolvedType, ctx);
        const std::string cap      = sizeLiteral(field.resolvedType.arrayCapacity);

        if (field.resolvedType.arrayKind == ArrayKind::None)
        {
            emitLine(out, 1, elemType + " " + member + ";");
        }
        else if (field.resolvedType.arrayKind == ArrayKind::Fixed)
        {
            emitLine(out, 1, elemType + " " + member + "[" + cap + "];");
        }
        else
        {
            emitLine(out, 1, "struct {");
            emitLine(out, 2, elemType + " elements[" + cap + "];");
            emitLine(out, 2, "size_t count;");
            emitLine(out, 1, "} " + member + ";");
        }
        ++emittedFields;
    }

    if (section.isUnion)
    {
        // Tag storage must match the wire tag width; a hardcoded uint8_t truncates a wide
        // tag (>256-option unions get a 16-bit tag). sectionFacts isn't threaded to the
        // shim struct; resolveUnionTagBits falls back to the authoritative per-field width.
        emitLine(out,
                 1,
                 renderUnsignedStorageToken(StorageTokenLanguage::C, resolveUnionTagBits(section, nullptr)) +
                     " _tag_;");
        ++emittedFields;
    }
    if (emittedFields == 0)
    {
        emitLine(out, 1, "uint8_t _dummy_;");
    }

    emitLine(out, 0, "} " + plan.shimTypeName + ";");
    out << '\n';
}

void emitShimPrototypes(std::ostringstream& out, const SemanticDefinition& def, const SectionPlan& plan)
{
    const auto symbolStem = shimSymbolStem(def, plan.sectionName);
    emitLine(out,
             0,
             "int8_t " + symbolStem + "__serialize_(const " + plan.shimTypeName +
                 "* obj, uint8_t* buffer, size_t* inout_buffer_size_bytes);");
    emitLine(out,
             0,
             "int8_t " + symbolStem + "__deserialize_(" + plan.shimTypeName +
                 "* out_obj, const uint8_t* buffer, size_t* inout_buffer_size_bytes);");
    emitLine(out,
             0,
             "int8_t " + symbolStem +
                 "__try_deserialize_view_(const uint8_t* buffer, size_t* inout_buffer_size_bytes, "
                 "const uint8_t** out_view_bytes);");
    emitLine(out,
             0,
             "int8_t " + symbolStem +
                 "__try_serialize_view_(const uint8_t* view_bytes, size_t view_size_bytes, "
                 "uint8_t* buffer, size_t* inout_buffer_size_bytes);");
    out << '\n';
}

std::string renderShimHeader(const SemanticDefinition& def, const EmitterContext& ctx)
{
    std::ostringstream out;

    const std::string guard = renderIncludeGuard(CodegenNamingLanguage::Cpp,
                                                 "LLVMDSDL_CPPABI_C_SHIM_",
                                                 def.info.fullName,
                                                 def.info.majorVersion,
                                                 def.info.minorVersion,
                                                 "_H");

    out << "/* Generated by llvmdsdl " << kVersionString << " (obj-cpp C shim) */\n";
    out << "/* Source: " << def.info.fullName << "." << def.info.majorVersion << "." << def.info.minorVersion
        << " */\n\n";
    out << "#ifndef " << guard << "\n";
    out << "#define " << guard << "\n\n";
    out << "#include <stdbool.h>\n";
    out << "#include <stddef.h>\n";
    out << "#include <stdint.h>\n\n";

    for (const auto& depRef : collectDefinitionCompositeDependencies(def))
    {
        if (const auto* dep = ctx.find(depRef))
        {
            if (dep->info.fullName == def.info.fullName && dep->info.majorVersion == def.info.majorVersion &&
                dep->info.minorVersion == def.info.minorVersion)
            {
                continue;
            }
            out << "#include \"" << shimHeaderPath(dep->info).generic_string() << "\"\n";
        }
    }

    out << "\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n";

    const auto plans = sectionPlansForDefinition(def, ctx.typeNameVersioning());
    for (const auto& plan : plans)
    {
        emitShimStruct(out, plan, *plan.section, ctx);
    }
    if (def.isService)
    {
        const auto base = shimTypeNameFromInfo(def.info, ctx.typeNameVersioning());
        emitLine(out, 0, "typedef " + base + "__Request " + base + ";");
        out << '\n';
    }

    for (const auto& plan : plans)
    {
        emitShimPrototypes(out, def, plan);
    }

    if (def.isService)
    {
        const auto base        = shimTypeNameFromInfo(def.info, ctx.typeNameVersioning());
        const auto requestStem = shimSymbolStem(def, "request");
        const auto baseStem    = shimSymbolStem(def, "");
        emitLine(out,
                 0,
                 "int8_t " + baseStem + "__serialize_(const " + base +
                     "* obj, uint8_t* buffer, size_t* inout_buffer_size_bytes);");
        emitLine(out,
                 0,
                 "int8_t " + baseStem + "__deserialize_(" + base +
                     "* out_obj, const uint8_t* buffer, size_t* inout_buffer_size_bytes);");
        emitLine(out,
                 0,
                 "int8_t " + baseStem +
                     "__try_deserialize_view_(const uint8_t* buffer, size_t* inout_buffer_size_bytes, "
                     "const uint8_t** out_view_bytes);");
        emitLine(out,
                 0,
                 "int8_t " + baseStem +
                     "__try_serialize_view_(const uint8_t* view_bytes, size_t view_size_bytes, "
                     "uint8_t* buffer, size_t* inout_buffer_size_bytes);");
        out << '\n';
        (void) requestStem;
    }

    out << "#ifdef __cplusplus\n}  // extern \"C\"\n#endif\n\n";
    out << "#endif /* " << guard << " */\n";
    return out.str();
}

void emitShimDefinition(std::ostringstream&       out,
                        const SemanticDefinition& def,
                        const SectionPlan&        plan,
                        const std::string&        canonicalPrefix)
{
    const std::string canonicalType = canonicalPrefix + plan.cppTypeName;
    const std::string symbolStem    = shimSymbolStem(def, plan.sectionName);

    emitLine(out,
             0,
             "static_assert(sizeof(" + canonicalType + ") == sizeof(" + plan.shimTypeName +
                 "), "
                 "\"shim/canonical size mismatch\");");
    emitLine(out,
             0,
             "static_assert(alignof(" + canonicalType + ") == alignof(" + plan.shimTypeName +
                 "), "
                 "\"shim/canonical alignment mismatch\");");
    emitLine(out,
             0,
             "static_assert(std::is_trivially_copyable<" + canonicalType +
                 ">::value, "
                 "\"canonical ABI type must be trivially copyable\");");
    emitLine(out,
             0,
             "static_assert(std::is_trivially_copyable<" + plan.shimTypeName +
                 ">::value, "
                 "\"shim type must be trivially copyable\");");
    out << '\n';

    emitLine(out,
             0,
             "int8_t " + symbolStem + "__serialize_(const " + plan.shimTypeName +
                 "* const obj, uint8_t* const buffer, size_t* const inout_buffer_size_bytes)");
    emitLine(out, 0, "{");
    emitLine(out, 1, "if (obj == nullptr) {");
    emitLine(out, 2, "return -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
    emitLine(out, 1, "}");
    emitLine(out, 1, canonicalType + " abi_obj{};");
    emitLine(out, 1, "std::memcpy(&abi_obj, obj, sizeof(abi_obj));");
    emitLine(out, 1, "return " + canonicalType + "_serialize_(&abi_obj, buffer, inout_buffer_size_bytes);");
    emitLine(out, 0, "}");
    out << '\n';

    emitLine(out,
             0,
             "int8_t " + symbolStem + "__deserialize_(" + plan.shimTypeName +
                 "* const out_obj, const uint8_t* const buffer, size_t* const inout_buffer_size_bytes)");
    emitLine(out, 0, "{");
    emitLine(out, 1, "if (out_obj == nullptr) {");
    emitLine(out, 2, "return -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;");
    emitLine(out, 1, "}");
    emitLine(out, 1, canonicalType + " abi_obj{};");
    emitLine(out,
             1,
             "const int8_t rc = " + canonicalType + "_deserialize_(&abi_obj, buffer, inout_buffer_size_bytes);");
    emitLine(out, 1, "if (rc >= 0) {");
    emitLine(out, 2, "std::memcpy(out_obj, &abi_obj, sizeof(abi_obj));");
    emitLine(out, 1, "}");
    emitLine(out, 1, "return rc;");
    emitLine(out, 0, "}");
    out << '\n';

    emitLine(out,
             0,
             "int8_t " + symbolStem +
                 "__try_deserialize_view_(const uint8_t* const buffer, size_t* const inout_buffer_size_bytes, "
                 "const uint8_t** const out_view_bytes)");
    emitLine(out, 0, "{");
    emitLine(out,
             1,
             "return " + canonicalType + "_try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);");
    emitLine(out, 0, "}");
    out << '\n';

    emitLine(out,
             0,
             "int8_t " + symbolStem +
                 "__try_serialize_view_(const uint8_t* const view_bytes, const size_t view_size_bytes, "
                 "uint8_t* const buffer, size_t* const inout_buffer_size_bytes)");
    emitLine(out, 0, "{");
    emitLine(out,
             1,
             "return " + canonicalType +
                 "_try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);");
    emitLine(out, 0, "}");
    out << '\n';
}

std::string renderShimSource(const SemanticDefinition& def, const TypeNameVersioning versioning)
{
    std::ostringstream out;
    const std::string  canonicalPrefix = canonicalQualifiedPrefix(def.info);

    out << "/* Generated by llvmdsdl " << kVersionString << " (obj-cpp C shim) */\n";
    out << "/* Source: " << def.info.fullName << "." << def.info.majorVersion << "." << def.info.minorVersion
        << " */\n\n";
    out << "#include \"" << canonicalHeaderPath(def.info).generic_string() << "\"\n";
    out << "#include \"" << shimHeaderPath(def.info).generic_string() << "\"\n";
    out << "#include <cstring>\n";
    out << "#include <type_traits>\n\n";
    out << "extern \"C\" {\n\n";

    const auto plans = sectionPlansForDefinition(def, versioning);
    for (const auto& plan : plans)
    {
        emitShimDefinition(out, def, plan, canonicalPrefix);
    }

    if (def.isService)
    {
        const auto shimBase = shimTypeNameFromInfo(def.info, versioning);
        const auto baseStem = shimSymbolStem(def, "");
        const auto reqStem  = shimSymbolStem(def, "request");

        emitLine(out,
                 0,
                 "int8_t " + baseStem + "__serialize_(const " + shimBase +
                     "* const obj, uint8_t* const buffer, size_t* const inout_buffer_size_bytes)");
        emitLine(out, 0, "{");
        emitLine(out,
                 1,
                 "return " + reqStem + "__serialize_(reinterpret_cast<const " + shimBase +
                     "__Request*>(obj), "
                     "buffer, inout_buffer_size_bytes);");
        emitLine(out, 0, "}");
        out << '\n';

        emitLine(out,
                 0,
                 "int8_t " + baseStem + "__deserialize_(" + shimBase +
                     "* const out_obj, const uint8_t* const buffer, size_t* const inout_buffer_size_bytes)");
        emitLine(out, 0, "{");
        emitLine(out,
                 1,
                 "return " + reqStem + "__deserialize_(reinterpret_cast<" + shimBase +
                     "__Request*>(out_obj), "
                     "buffer, inout_buffer_size_bytes);");
        emitLine(out, 0, "}");
        out << '\n';

        emitLine(out,
                 0,
                 "int8_t " + baseStem +
                     "__try_deserialize_view_(const uint8_t* const buffer, size_t* const inout_buffer_size_bytes, "
                     "const uint8_t** const out_view_bytes)");
        emitLine(out, 0, "{");
        emitLine(out,
                 1,
                 "return " + reqStem + "__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);");
        emitLine(out, 0, "}");
        out << '\n';

        emitLine(out,
                 0,
                 "int8_t " + baseStem +
                     "__try_serialize_view_(const uint8_t* const view_bytes, const size_t view_size_bytes, "
                     "uint8_t* const buffer, size_t* const inout_buffer_size_bytes)");
        emitLine(out, 0, "{");
        emitLine(out,
                 1,
                 "return " + reqStem +
                     "__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);");
        emitLine(out, 0, "}");
        out << '\n';
    }

    out << "} // extern \"C\"\n";
    return out.str();
}

std::string renderAdapterHeader(const SemanticDefinition& def,
                                llvm::StringRef           profile,
                                const TypeNameVersioning  versioning)
{
    std::ostringstream out;

    const std::string guard = renderIncludeGuard(CodegenNamingLanguage::Cpp,
                                                 "LLVMDSDL_CPPABI_ADAPTER_" + profile.str() + "_",
                                                 def.info.fullName,
                                                 def.info.majorVersion,
                                                 def.info.minorVersion,
                                                 "_HPP");

    out << "/* Generated by llvmdsdl " << kVersionString << " (obj-cpp " << profile.str() << " adapter) */\n";
    out << "/* Source: " << def.info.fullName << "." << def.info.majorVersion << "." << def.info.minorVersion
        << " */\n\n";
    out << "#ifndef " << guard << "\n";
    out << "#define " << guard << "\n\n";
    out << "#include \"" << canonicalHeaderPath(def.info).generic_string() << "\"\n\n";

    emitNamespaceOpen(out, def.info.namespaceComponents);

    const auto        baseName        = cppTypeNameFromInfo(def.info, versioning);
    const auto        cppNs           = cppNamespacePath(def.info.namespaceComponents);
    const std::string canonicalPrefix = cppNs.empty() ? "::abi::" : ("::" + cppNs + "::abi::");
    if (def.isService)
    {
        emitLine(out, 0, "using " + baseName + "_Request = " + canonicalPrefix + baseName + "_Request;");
        if (def.response)
        {
            emitLine(out, 0, "using " + baseName + "_Response = " + canonicalPrefix + baseName + "_Response;");
        }
        emitLine(out, 0, "using " + baseName + " = " + baseName + "_Request;");
    }
    else
    {
        emitLine(out, 0, "using " + baseName + " = " + canonicalPrefix + baseName + ";");
    }

    emitNamespaceClose(out, def.info.namespaceComponents);
    out << "\n#endif /* " << guard << " */\n";
    return out.str();
}

llvm::Error emitRuntimeHeader(const CppObjectAbiEmitOptions& options)
{
    const std::filesystem::path runtimePath = options.stageRoot / "dsdl_cppabi_runtime.hpp";
    std::ostringstream          out;

    out << "/* Generated by llvmdsdl " << kVersionString << " (obj-cpp canonical runtime) */\n\n";
    out << "#ifndef LLVMDSDL_CPPABI_RUNTIME_HPP\n";
    out << "#define LLVMDSDL_CPPABI_RUNTIME_HPP\n\n";
    out << "#include <array>\n";
    out << "#include <cstddef>\n\n";
    out << "namespace llvmdsdl {\n";
    out << "namespace cppabi {\n\n";
    out << "template <typename T, std::size_t Capacity>\n";
    out << "struct BoundedSequence final\n";
    out << "{\n";
    out << "  std::array<T, Capacity> elements{};\n";
    out << "  std::size_t count{0U};\n";
    out << "};\n\n";
    out << "}  // namespace cppabi\n";
    out << "}  // namespace llvmdsdl\n\n";
    out << "#endif /* LLVMDSDL_CPPABI_RUNTIME_HPP */\n";

    return writeGeneratedFile(runtimePath, out.str(), options.writePolicy);
}

llvm::Error emitCRuntimeForwardHeader(const CppObjectAbiEmitOptions& options)
{
    const std::filesystem::path runtimePath = options.stageRoot / "dsdl_runtime.h";
    std::ostringstream          out;

    out << "/* Generated by llvmdsdl " << kVersionString << " (obj-cpp C runtime forwarder) */\n\n";
    out << "#ifndef LLVMDSDL_CPPABI_C_RUNTIME_FORWARD_H\n";
    out << "#define LLVMDSDL_CPPABI_C_RUNTIME_FORWARD_H\n\n";
    out << "#include \"c/dsdl_runtime.h\"\n\n";
    out << "#endif /* LLVMDSDL_CPPABI_C_RUNTIME_FORWARD_H */\n";

    return writeGeneratedFile(runtimePath, out.str(), options.writePolicy);
}

}  // namespace

llvm::Error emitCppObjectAbiStage(const SemanticModule&                     semantic,
                                  const LoweredFactsMap&                    loweredFacts,
                                  const CppObjectAbiEmitOptions&            options,
                                  std::vector<std::filesystem::path>* const outCppSources)
{
    if (outCppSources == nullptr)
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "null outCppSources sink");
    }

    const auto     selectedTypeKeys = makeTypeKeySet(options.selectedTypeKeys);
    EmitterContext ctx(semantic, options.typeNameVersioning);

    if (auto err = emitRuntimeHeader(options))
    {
        return err;
    }
    if (auto err = emitCRuntimeForwardHeader(options))
    {
        return err;
    }

    outCppSources->clear();

    for (const auto& def : semantic.definitions)
    {
        if (!shouldEmitDefinition(def.info, selectedTypeKeys))
        {
            continue;
        }

        const std::vector<std::string> requiredTypeKeys{definitionTypeKey(def.info)};

        const auto canonicalHeader = options.stageRoot / canonicalHeaderPath(def.info);
        const auto canonicalSource = options.stageRoot / canonicalSourcePath(def.info);
        const auto shimHeader      = options.stageRoot / shimHeaderPath(def.info);
        const auto shimSource      = options.stageRoot / shimSourcePath(def.info);

        if (auto err = writeGeneratedFile(canonicalHeader,
                                          renderCanonicalHeader(def, loweredFacts, ctx),
                                          options.writePolicy,
                                          requiredTypeKeys))
        {
            return err;
        }
        if (auto err =
                writeGeneratedFile(canonicalSource, renderCanonicalSource(def, options.typeNameVersioning), options.writePolicy, requiredTypeKeys))
        {
            return err;
        }
        if (auto err =
                writeGeneratedFile(shimHeader, renderShimHeader(def, ctx), options.writePolicy, requiredTypeKeys))
        {
            return err;
        }
        if (auto err = writeGeneratedFile(shimSource, renderShimSource(def, options.typeNameVersioning), options.writePolicy, requiredTypeKeys))
        {
            return err;
        }

        outCppSources->push_back(canonicalSource);
        outCppSources->push_back(shimSource);

        for (const llvm::StringRef profile :
             {llvm::StringRef("std"), llvm::StringRef("pmr"), llvm::StringRef("autosar")})
        {
            const auto adapter = options.stageRoot / adapterHeaderPath(profile, def.info);
            if (auto err = writeGeneratedFile(adapter,
                                              renderAdapterHeader(def, profile, options.typeNameVersioning),
                                              options.writePolicy,
                                              requiredTypeKeys))
            {
                return err;
            }
        }
    }

    return llvm::Error::success();
}

}  // namespace llvmdsdl
