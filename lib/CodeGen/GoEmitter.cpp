//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements Go backend code emission from lowered DSDL modules.
///
/// This file materializes Go type declarations and serdes helpers from backend-neutral lowering plans.
///
/// The line-building concatenations here carry NOLINT for
/// performance-inefficient-string-concatenation. Each one spells out a line of generated
/// source, and an append sequence would cost the reader the line itself.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/EmitCommon.h"
#include "llvmdsdl/CodeGen/SectionNaming.h"
#include "llvmdsdl/CodeGen/EmbeddedRuntimeSources.h"
#include "llvmdsdl/CodeGen/GoEmitter.h"

#include <llvm/ADT/StringRef.h>
#include <array>
#include <algorithm>
#include <cassert>
#include <cctype>  // IWYU pragma: keep -- libstdc++ reaches this transitively; libc++ needs it named.
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "llvmdsdl/CodeGen/ArrayWirePlan.h"
#include "llvmdsdl/CodeGen/CodegenDiagnosticText.h"
#include "llvmdsdl/CodeGen/ConstantLiteralRender.h"
#include "llvmdsdl/CodeGen/DefinitionDependencies.h"
#include "llvmdsdl/CodeGen/DefinitionIndex.h"
#include "llvmdsdl/CodeGen/EmitStep.h"
#include "llvmdsdl/CodeGen/EmitTrace.h"
#include "llvmdsdl/CodeGen/HelperSymbolResolver.h"
#include "llvmdsdl/CodeGen/LoweredRenderIR.h"
#include "llvmdsdl/CodeGen/LoweredFactsLookup.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/CodeGen/HelperBindingNaming.h"
#include "llvmdsdl/CodeGen/NativeEmitterTraversal.h"
#include "llvmdsdl/CodeGen/NativeFunctionSkeleton.h"
#include "llvmdsdl/CodeGen/StorageTypeTokens.h"
#include "llvmdsdl/CodeGen/TypeStorage.h"
#include "llvmdsdl/CodeGen/WireLayoutFacts.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"
#include "llvmdsdl/SerDes/HelperBodyPlan.h"
#include "llvmdsdl/CodeGen/SourceWriter.h"
#include "llvmdsdl/CodeGen/SerDesHelperDescriptors.h"
#include "llvmdsdl/CodeGen/SerDesStatementPlan.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/BitLengthSet.h"
#include "llvmdsdl/Semantics/Evaluator.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Version.h"
#include "mlir/IR/BuiltinOps.h"

namespace llvmdsdl
{
class DiagnosticEngine;

namespace
{

/// @brief Builds the collision-free exported field-name allocation for one section.
///
/// Two distinct DSDL field names (e.g. `fooBar` and `foo_bar`) both export to `FooBar`; without this
/// the struct would declare the same field twice (a compile error) and the (de)serializer would read
/// or write the wrong field. Padding fields carry no exported name and are excluded.
NamingScope makeExportedFieldIdents(const SemanticSection& section)
{
    std::vector<std::string> names;
    for (const auto& field : section.fields)
    {
        if (!field.isPadding)
        {
            names.push_back(field.name);
        }
    }
    // The generated methods a field must not collide with are claimed by the FieldName role's policy
    // (Go forbids a field and a method sharing a name), so the scope only has to keep the fields
    // apart from each other.
    return makeSectionFieldScope(CodegenNamingLanguage::Go, section);
}

std::string packagePathFromComponents(const std::vector<std::string>& components)
{
    std::string out;
    for (const auto& c : components)
    {
        if (!out.empty())
        {
            out += "/";
        }
        out += codegenProjectIdentifier(CodegenNamingLanguage::Go, IdentifierRole::NamespaceName, c);
    }
    return out;
}

std::string packageNameFromPath(const std::string& path)
{
    if (path.empty())
    {
        return "rootdsdl";
    }
    const auto split = path.find_last_of('/');
    const auto leaf  = split == std::string::npos ? path : path.substr(split + 1);
    auto       out   = codegenProjectIdentifier(CodegenNamingLanguage::Go, IdentifierRole::NamespaceName, leaf);
    if (out.empty())
    {
        out = "rootdsdl";
    }
    return out;
}

std::string unsignedStorageType(const std::uint32_t bitLength)
{
    return renderUnsignedStorageToken(StorageTokenLanguage::Go, bitLength);
}

std::string signedStorageType(const std::uint32_t bitLength)
{
    return renderSignedStorageToken(StorageTokenLanguage::Go, bitLength);
}

std::string goConstValue(const TypeExprAST& type, const Value& value)
{
    return renderConstantLiteral(ConstantLiteralLanguage::Go, value, makeConstantTypeInfo(type));
}

// gofmt is not configurable and indents with tabs; generated files that disagree churn in any
// consumer with format-on-save or a gofmt gate.
SourceWriter makeGoWriter(std::ostringstream& out)
{
    return SourceWriter{out, IndentPolicy::tabs()};
}

std::string generatedCommentLine(llvm::StringRef detail)
{
    return llvm::formatv("// Generated by llvmdsdl {0} ({1}).", llvmdsdl::kVersionString, detail).str();
}

/// @brief Emits a Go doc comment the way gofmt writes one.
///
/// gofmt does not leave a doc comment's body alone. Lines indented relative to the
/// prose around them are one of two things, and it rewrites both: a list, whose
/// markers it re-indents to fixed columns, or a code block, whose common indentation
/// it replaces with a single tab. Either way a blank comment line fences the
/// construct off from adjacent prose.
///
/// DSDL definitions indent freely -- the standard namespace uses bullet lists,
/// numbered lists and hanging descriptions -- so the emitter has to write the
/// canonical form or those types stay permanently unformatted.
///
/// @param[in,out] w Destination writer.
/// @param[in] doc Attached documentation, as written in the DSDL definition.
void emitAttachedDocGo(SourceWriter& w, const AttachedDoc& doc)
{
    const auto isBlank    = [](const std::string& t) { return t.find_first_not_of(" \t") == std::string::npos; };
    const auto isIndented = [&isBlank](const std::string& t) {
        return !isBlank(t) && (t.front() == ' ' || t.front() == '\t');
    };
    const auto stripped = [](const std::string& t) {
        const auto at = t.find_first_not_of(" \t");
        return at == std::string::npos ? std::string{} : t.substr(at);
    };

    /// A list item opens with a bullet or a number, then a space.
    const auto listMarker = [&stripped](const std::string& t) -> std::string {
        const auto body = stripped(t);
        if (body.size() >= 2 && (body[0] == '-' || body[0] == '*' || body[0] == '+') && body[1] == ' ')
        {
            return "-";
        }
        std::size_t digits = 0;
        while (digits < body.size() && (std::isdigit(static_cast<unsigned char>(body[digits])) != 0))
        {
            ++digits;
        }
        if (digits > 0 && digits + 1 < body.size() && (body[digits] == '.' || body[digits] == ')') &&
            body[digits + 1] == ' ')
        {
            return body.substr(0, digits) + ".";
        }
        return {};
    };

    const auto& lines = doc.lines;

    /// gofmt separates a construct from the prose on either side of it.
    const auto fenceBefore = [&](const std::size_t i) {
        if (i > 0 && !isBlank(lines[i - 1].text))
        {
            w.line("//");
        }
    };
    const auto fenceAfter = [&](const std::size_t end) {
        if (end < lines.size() && !isBlank(lines[end].text))
        {
            w.line("//");
        }
    };

    /// The extent of an indented run. A blank line inside one does not end it, so long
    /// as the indentation resumes afterwards.
    const auto runEnd = [&](std::size_t i) {
        std::size_t end  = i;
        std::size_t last = i;
        while (end < lines.size() && (isIndented(lines[end].text) || isBlank(lines[end].text)))
        {
            if (isIndented(lines[end].text))
            {
                last = end;
            }
            ++end;
        }
        return last + 1;
    };

    for (std::size_t i = 0; i < lines.size();)
    {
        if (!isIndented(lines[i].text))
        {
            if (isBlank(lines[i].text))
            {
                // gofmt collapses a run of blank comment lines to one separator.
                w.line("//");
                while (i < lines.size() && isBlank(lines[i].text))
                {
                    ++i;
                }
                continue;
            }
            w.line("// " + lines[i].text);
            ++i;
            continue;
        }

        const std::size_t end = runEnd(i);

        // Whether a run is a list or a code block is settled by its first line. A block
        // that happens to contain numbered lines stays a block: gofmt reproduces it
        // verbatim rather than reading a list out of the middle of it.
        if (!listMarker(lines[i].text).empty())
        {
            for (std::size_t k = i; k < end; ++k)
            {
                if (isBlank(lines[k].text))
                {
                    w.line("//");
                    continue;
                }
                const auto body   = stripped(lines[k].text);
                const auto marker = listMarker(lines[k].text);
                if (marker.empty())
                {
                    // Continuations align under the item text, which sits five columns in.
                    w.line("//     " + body);
                }
                else if (marker == "-")
                {
                    w.line("//   - " + body.substr(2));
                }
                else
                {
                    w.line("//  " + marker + " " + body.substr(marker.size() + 1));
                }
            }
            fenceAfter(end);
            i = end;
            continue;
        }

        std::string common;
        bool        first = true;
        for (std::size_t k = i; k < end; ++k)
        {
            if (isBlank(lines[k].text))
            {
                continue;
            }
            const auto indent = lines[k].text.substr(0, lines[k].text.find_first_not_of(" \t"));
            if (first)
            {
                common = indent;
                first  = false;
            }
            else
            {
                // The block is re-indented by whatever all of its lines share, so a line
                // deeper than its neighbours stays deeper.
                std::size_t shared = 0;
                while (shared < common.size() && shared < indent.size() && common[shared] == indent[shared])
                {
                    ++shared;
                }
                common.resize(shared);
            }
        }

        fenceBefore(i);
        for (std::size_t k = i; k < end; ++k)
        {
            w.line(isBlank(lines[k].text) ? std::string{"//"} : "//\t" + lines[k].text.substr(common.size()));
        }
        fenceAfter(end);
        i = end;
    }
}

/// @brief Go spelling of the helper body shapes (see HelperBodyPlan.h).
///
/// Guards rather than conditional expressions throughout, which is how the rest of
/// the generated Go reads. The float identity is the one single-line form: a
/// `func(value float32) float32 { return value }` on its own line, matching what a
/// reader would write.
class GoHelperBodySpelling final : public HelperBodySpelling
{
public:
    explicit GoHelperBodySpelling(SourceWriter& w)
        : w_(w)
    {
    }

    void spellIdentity(const HelperBody& body) override
    {
        oneLiner(body, "return value");
    }

    void spellMask(const HelperBody& body) override
    {
        oneLiner(body, "return value & " + mask(body.bits));
    }

    void spellSaturateUnsigned(const HelperBody& body) override
    {
        open(body);
        w_.open("if value > " + mask(body.bits) + " {");
        w_.line("return " + mask(body.bits));
        w_.close("}");
        w_.line("return value");
        w_.close("}");
    }

    void spellSaturateSigned(const HelperBody& body) override
    {
        open(body);
        w_.open("if value < " + std::to_string(body.minValue) + " {");
        w_.line("return " + std::to_string(body.minValue));
        w_.close("}");
        w_.open("if value > " + std::to_string(body.maxValue) + " {");
        w_.line("return " + std::to_string(body.maxValue));
        w_.close("}");
        w_.line("return value");
        w_.close("}");
    }

    void spellSignExtend(const HelperBody& body) override
    {
        const auto bitMask = mask(body.bits);
        const auto signBit = "uint64(" + std::to_string(std::uint64_t{1} << (body.bits - 1U)) + ")";
        open(body);
        w_.line("raw := uint64(value) & " + bitMask);
        w_.open("if (raw & " + signBit + ") != 0 {");
        w_.line("return int64(raw | (^" + bitMask + "))");
        w_.close("}");
        w_.line("return int64(raw)");
        w_.close("}");
    }

    void spellStatusGuard(const HelperBody& body) override
    {
        open(body);
        switch (body.guard)
        {
        case HelperGuardKind::CapacityTooSmall:
            w_.open("if " + std::to_string(body.requiredBits) + " > capacityBits {");
            w_.line("return -dsdlruntime.DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL");
            break;
        case HelperGuardKind::ArrayLengthOutOfRange:
            w_.open("if (value < 0) || (value > " + std::to_string(body.capacity) + ") {");
            w_.line("return -dsdlruntime.DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH");
            break;
        case HelperGuardKind::DelimiterOutOfRange:
            w_.open("if (payloadBytes < 0) || (payloadBytes > remainingBytes) {");
            w_.line("return -dsdlruntime.DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_DELIMITER_HEADER");
            break;
        }
        w_.close("}");
        w_.line("return dsdlruntime.DSDL_RUNTIME_SUCCESS");
        w_.close("}");
    }

    void spellTagMembership(const HelperBody& body) override
    {
        open(body);
        if (body.allowedTags.empty())
        {
            w_.line("return -dsdlruntime.DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG");
            w_.close("}");
            return;
        }
        std::string condition;
        for (const auto tag : body.allowedTags)
        {
            if (!condition.empty())
            {
                condition += " || ";
            }
            condition += "(tagValue == " + std::to_string(tag) + ")";
        }
        w_.open("if " + condition + " {");
        w_.line("return dsdlruntime.DSDL_RUNTIME_SUCCESS");
        w_.close("}");
        w_.line("return -dsdlruntime.DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG");
        w_.close("}");
    }

private:
    static std::string floatType(const HelperSignature signature)
    {
        return signature == HelperSignature::Float64 ? "float64" : "float32";
    }

    static std::string mask(const std::uint32_t bits)
    {
        return renderMaskLiteral(HelperSpellingLanguage::Go, bits);
    }

    /// @brief The closure's parameter list and return type, e.g. "(value uint64) uint64".
    static std::string signature(const HelperBody& body)
    {
        switch (body.signature)
        {
        case HelperSignature::UnsignedToUnsigned:
            return "(value uint64) uint64";
        case HelperSignature::SignedToSigned:
            return "(value int64) int64";
        case HelperSignature::ValueToStatus:
            return "(" + statusParamName(body) + " int64) int8";
        case HelperSignature::PairToStatus:
            return "(payloadBytes int64, remainingBytes int64) int8";
        case HelperSignature::Float32:
        case HelperSignature::Float64:
            return "(value " + floatType(body.signature) + ") " + floatType(body.signature);
        }
        return "(value uint64) uint64";
    }

    /// @brief Names the parameter after what the helper asks about.
    static std::string statusParamName(const HelperBody& body)
    {
        if (body.kind == HelperBodyKind::TagMembership)
        {
            return "tagValue";
        }
        return body.guard == HelperGuardKind::CapacityTooSmall ? "capacityBits" : "value";
    }

    /// @brief Emits a whole-body-in-one-statement helper on a single line.
    ///
    /// Go reads a trivial closure better inline than spread over three lines, and
    /// every helper whose body is one `return` gets the same treatment.
    void oneLiner(const HelperBody& body, const std::string& statement)
    {
        w_.line(body.symbol + " := func" + signature(body) + " { " + statement + " }");
    }

    /// @brief Emits the closure's opening line and descends into its body.
    void open(const HelperBody& body)
    {
        w_.open(body.symbol + " := func" + signature(body) + " {");
    }

    SourceWriter& w_;
};

/// @brief One member of a generated Go struct, with whatever documents it.
struct GoStructMember final
{
    /// @brief Field name, already projected.
    std::string name;

    /// @brief Field type, already rendered.
    std::string type;

    /// @brief Doc comment attached to the field, if any.
    AttachedDoc doc;
};

/// @brief Emits struct members with their types aligned the way gofmt aligns them.
///
/// gofmt pads each name so a run of members shares one type column, and a run ends
/// wherever a comment interrupts it. Emitting a member at a time cannot do that: the
/// width belongs to the run, so the run has to be measured before any of it is
/// written.
///
/// @param[in,out] w Destination writer, positioned inside the struct body.
/// @param[in] members Members in declaration order.
void emitAlignedStructMembers(SourceWriter& w, const std::vector<GoStructMember>& members)
{
    for (std::size_t i = 0; i < members.size();)
    {
        // A documented member begins a run; the run continues while members are undocumented.
        std::size_t end   = i + 1;
        std::size_t width = members[i].name.size();
        while (end < members.size() && members[end].doc.lines.empty())
        {
            width = std::max(width, members[end].name.size());
            ++end;
        }
        for (std::size_t k = i; k < end; ++k)
        {
            emitAttachedDocGo(w, members[k].doc);
            w.line(members[k].name + std::string(width - members[k].name.size() + 1U, ' ') + members[k].type);
        }
        i = end;
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

    /// @brief Attaches an emit-order trace sink (for the emit-order verifier). Null (default) disables tracing at zero
    /// cost.
    void setTraceSink(EmitTraceSink* const sink)
    {
        traceSink_ = sink;
    }

    /// @brief Records one abstract emit op into the attached sink (no-op when unattached).
    template <typename PayloadT = std::int64_t>
    void trace(const EmitTraceOp op, const PayloadT payload = -1) const
    {
        emitTrace(traceSink_, op, static_cast<std::int64_t>(payload));
    }

    /// @brief Marks the start of one (type, direction) trace segment (no-op when unattached).
    void traceSection(const std::string& canonicalName, const EmitTraceDirection direction) const
    {
        emitTraceSection(traceSink_, canonicalName, direction);
    }

    const SemanticDefinition* find(const SemanticTypeRef& ref) const
    {
        return index_.find(ref);
    }

    static std::string packagePath(const DiscoveredDefinition& info)
    {
        return packagePathFromComponents(info.namespaceComponents);
    }

    std::string packagePath(const SemanticTypeRef& ref) const
    {
        if (const auto* def = find(ref))
        {
            return packagePath(def->info);
        }
        return packagePathFromComponents(ref.namespaceComponents);
    }

    std::string goTypeName(const DiscoveredDefinition& info) const
    {
        return renderDefinitionTypeName(CodegenNamingLanguage::Go,
                                        info.namespaceComponents,
                                        info.shortName,
                                        info.majorVersion,
                                        info.minorVersion,
                                        typeNameVersioning_);
    }

    std::string goTypeName(const SemanticTypeRef& ref) const
    {
        if (const auto* def = find(ref))
        {
            return goTypeName(def->info);
        }
        DiscoveredDefinition tmp;
        tmp.shortName    = ref.shortName;
        tmp.majorVersion = ref.majorVersion;
        tmp.minorVersion = ref.minorVersion;
        return goTypeName(tmp);
    }

    static std::string goFileName(const DiscoveredDefinition& info)
    {
        return renderDefinitionFileStem(CodegenNamingLanguage::Go,
                                        info.shortName,
                                        info.majorVersion,
                                        info.minorVersion) +
               ".go";
    }

private:
    DefinitionIndex    index_;
    TypeNameVersioning typeNameVersioning_{TypeNameVersioning::Unversioned};
    EmitTraceSink*     traceSink_ = nullptr;
};

std::map<std::string, std::string> computeImportAliases(const SemanticDefinition& def, const EmitterContext& ctx)
{
    const auto deps = collectDefinitionCompositeDependencies(def);

    const std::string                  currentPath = llvmdsdl::EmitterContext::packagePath(def.info);
    std::map<std::string, std::string> out;
    std::set<std::string>              usedAliases;

    for (const auto& depRef : deps)
    {
        SemanticTypeRef ref = depRef;
        if (const auto* resolved = ctx.find(depRef))
        {
            ref.namespaceComponents = resolved->info.namespaceComponents;
            ref.shortName           = resolved->info.shortName;
        }

        const auto depPath = ctx.packagePath(ref);
        if (depPath.empty() || depPath == currentPath)
        {
            continue;
        }
        auto alias = "pkg_" + codegenProjectIdentifier(CodegenNamingLanguage::Go,
                                                       IdentifierRole::NamespaceName,
                                                       llvm::join(ref.namespaceComponents, "_"));
        if (alias == "pkg_")
        {
            alias = "pkg_dep";
        }
        std::size_t suffix    = 1;
        const auto  baseAlias = alias;
        while (usedAliases.contains(alias))
        {
            alias = baseAlias + "_" + std::to_string(suffix++);
        }
        usedAliases.insert(alias);
        out.emplace(depPath, alias);
    }

    return out;
}

std::string goBaseFieldType(const SemanticFieldType&                  type,
                            const EmitterContext&                     ctx,
                            const std::string&                        currentPackagePath,
                            const std::map<std::string, std::string>& importAliases)
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
        return type.bitLength == 64 ? "float64" : "float32";
    case SemanticScalarCategory::Void:
        return "uint8";
    case SemanticScalarCategory::Composite:
        if (type.compositeType)
        {
            const auto depPath = ctx.packagePath(*type.compositeType);
            auto       depType = ctx.goTypeName(*type.compositeType);
            if (depPath.empty() || depPath == currentPackagePath)
            {
                return depType;
            }
            const auto it = importAliases.find(depPath);
            if (it != importAliases.end())
            {
                return it->second + "." + depType;
            }
            return depType;
        }
        return "uint8";
    }
    return "uint8";
}

std::string goFieldType(const SemanticFieldType&                  type,
                        const EmitterContext&                     ctx,
                        const std::string&                        currentPackagePath,
                        const std::map<std::string, std::string>& importAliases)
{
    auto base = goBaseFieldType(type, ctx, currentPackagePath, importAliases);
    if (type.arrayKind == ArrayKind::None)
    {
        return base;
    }
    if (type.arrayKind == ArrayKind::Fixed)
    {
        return "[" + std::to_string(type.arrayCapacity) + "]" + base;
    }
    return "[]" + base;
}

class FunctionBodyEmitter final
{
public:
    FunctionBodyEmitter(const EmitterContext&                     ctx,
                        std::string                               currentPackagePath,
                        const std::map<std::string, std::string>& importAliases)
        : ctx_(ctx)
        , currentPackagePath_(std::move(currentPackagePath))
        , importAliases_(importAliases)
    {
    }

    /// @brief Records one abstract emit op via the shared EmitterContext sink (no-op when unattached).
    template <typename PayloadT = std::int64_t>
    void trace(const EmitTraceOp op, const PayloadT payload = -1) const
    {
        ctx_.trace(op, payload);
    }

    void emitSerializeFunction(SourceWriter&              w,
                               const std::string&         typeName,
                               const SemanticSection&     section,
                               const LoweredSectionFacts* sectionFacts)
    {
        fieldIdents_.emplace(makeExportedFieldIdents(section));
        w.open("func (obj *" + typeName + ") Serialize(buffer []byte) (int8, int) {");
        w.open("if obj == nil {");
        w.line("return -dsdlruntime.DSDL_RUNTIME_ERROR_INVALID_ARGUMENT, 0");
        w.close("}");
        w.line("offsetBits := 0");
        const auto emitted = emitNativeFunctionSkeleton(
            section,
            sectionFacts,
            HelperBindingDirection::Serialize,
            NativeFunctionSkeletonCallbacks{[&w](const SectionHelperBindingPlan& helperBindings) {
                                                emitSerializeMlirHelperBindings(w, helperBindings);
                                            },
                                            [&w](const std::string& missingHelperRequirement) {
                                                w.line("// missing lowered helper contract: " +
                                                       missingHelperRequirement);
                                                w.line("return -dsdlruntime.DSDL_RUNTIME_ERROR_INVALID_ARGUMENT, 0");
                                            },
                                            [&w](const SectionHelperBindingPlan& helperBindings) {
                                                const auto capacityHelper =
                                                    helperBindingName(helperBindings.capacityCheck->symbol);
                                                w.open("if rc := " + capacityHelper +
                                                       "(int64(len(buffer) * 8)); rc != "
                                                       "dsdlruntime.DSDL_RUNTIME_SUCCESS {");
                                                w.line("return rc, 0");
                                                w.close("}");
                                            },
                                            [this, &w, &section, sectionFacts](const LoweredBodyRenderIR& renderIR) {
                                                NativeEmitterTraversalCallbacks callbacks;
                                                callbacks.onUnionDispatch =
                                                    [this, &w, &section, sectionFacts, &renderIR](
                                                        const std::vector<PlannedFieldStep>& unionBranches) {
                                                        emitSerializeUnion(w,
                                                                           section,
                                                                           unionBranches,
                                                                           sectionFacts,
                                                                           renderIR.helperBindings);
                                                    };
                                                callbacks.onFieldAlignment = [this,
                                                                              &w](const std::int64_t alignmentBits) {
                                                    emitAlignSerialize(w, alignmentBits);
                                                };
                                                callbacks.onField = [this, &w](const PlannedFieldStep& fieldStep) {
                                                    const auto* const field = fieldStep.field;
                                                    emitSerializeAny(w,
                                                                     field->resolvedType,
                                                                     "obj." + exportedFieldIdent(field->name),
                                                                     fieldStep.arrayLengthPrefixBits,
                                                                     fieldStep.fieldFacts);
                                                };
                                                callbacks.onPaddingAlignment = [this,
                                                                                &w](const std::int64_t alignmentBits) {
                                                    emitAlignSerialize(w, alignmentBits);
                                                };
                                                callbacks.onPadding = [this, &w](const PlannedFieldStep& fieldStep) {
                                                    const auto* const field = fieldStep.field;
                                                    emitSerializePadding(w, field->resolvedType);
                                                };
                                                return callbacks;
                                            },
                                            [this, &w]() {
                                                emitAlignSerialize(w, 8);
                                                w.line("return dsdlruntime.DSDL_RUNTIME_SUCCESS, offsetBits / 8");
                                            }});
        if (!emitted)
        {
            w.close("}");
            return;
        }
        w.close("}");
    }

    void emitDeserializeFunction(SourceWriter&              w,
                                 const std::string&         typeName,
                                 const SemanticSection&     section,
                                 const LoweredSectionFacts* sectionFacts)
    {
        fieldIdents_.emplace(makeExportedFieldIdents(section));
        w.open("func (obj *" + typeName + ") Deserialize(buffer []byte) (int8, int) {");
        w.open("if obj == nil {");
        w.line("return -dsdlruntime.DSDL_RUNTIME_ERROR_INVALID_ARGUMENT, 0");
        w.close("}");
        w.line("capacityBytes := len(buffer)");
        w.line("capacityBits := capacityBytes * 8");
        w.line("offsetBits := 0");
        const auto emitted = emitNativeFunctionSkeleton(
            section,
            sectionFacts,
            HelperBindingDirection::Deserialize,
            NativeFunctionSkeletonCallbacks{[&w](const SectionHelperBindingPlan& helperBindings) {
                                                emitDeserializeMlirHelperBindings(w, helperBindings);
                                            },
                                            [&w](const std::string& missingHelperRequirement) {
                                                w.line("// missing lowered helper contract: " +
                                                       missingHelperRequirement);
                                                w.line("return -dsdlruntime.DSDL_RUNTIME_ERROR_INVALID_ARGUMENT, 0");
                                            },
                                            nullptr,
                                            [this, &w, &section, sectionFacts](const LoweredBodyRenderIR& renderIR) {
                                                NativeEmitterTraversalCallbacks callbacks;
                                                callbacks.onUnionDispatch =
                                                    [this, &w, &section, sectionFacts, &renderIR](
                                                        const std::vector<PlannedFieldStep>& unionBranches) {
                                                        emitDeserializeUnion(w,
                                                                             section,
                                                                             unionBranches,
                                                                             sectionFacts,
                                                                             renderIR.helperBindings);
                                                    };
                                                callbacks.onFieldAlignment = [this,
                                                                              &w](const std::int64_t alignmentBits) {
                                                    emitAlignDeserialize(w, alignmentBits);
                                                };
                                                callbacks.onField = [this, &w](const PlannedFieldStep& fieldStep) {
                                                    const auto* const field = fieldStep.field;
                                                    emitDeserializeAny(w,
                                                                       field->resolvedType,
                                                                       "obj." + exportedFieldIdent(field->name),
                                                                       fieldStep.arrayLengthPrefixBits,
                                                                       fieldStep.fieldFacts);
                                                };
                                                callbacks.onPaddingAlignment = [this,
                                                                                &w](const std::int64_t alignmentBits) {
                                                    emitAlignDeserialize(w, alignmentBits);
                                                };
                                                callbacks.onPadding = [this, &w](const PlannedFieldStep& fieldStep) {
                                                    const auto* const field = fieldStep.field;
                                                    emitDeserializePadding(w, field->resolvedType);
                                                };
                                                return callbacks;
                                            },
                                            [this, &w]() {
                                                emitAlignDeserialize(w, 8);
                                                w.line("consumedBits := dsdlruntime.ChooseMin(offsetBits, "
                                                       "capacityBits)");
                                                w.line("return dsdlruntime.DSDL_RUNTIME_SUCCESS, consumedBits / 8");
                                            }});
        if (!emitted)
        {
            w.close("}");
            return;
        }
        w.close("}");
    }

    /// @brief Collision-free exported field name for the section currently being emitted.
    std::string exportedFieldIdent(const llvm::StringRef name) const
    {
        return fieldIdents_ ? fieldIdents_->get(IdentifierRole::FieldName, name)
                            : codegenProjectIdentifier(CodegenNamingLanguage::Go, IdentifierRole::FieldName, name);
    }

private:
    const EmitterContext&                     ctx_;
    std::string                               currentPackagePath_;
    const std::map<std::string, std::string>& importAliases_;

    /// @brief Per-section exported field-name allocation; set at the start of each function body.
    std::optional<NamingScope> fieldIdents_;
    std::size_t                id_{0};

    std::string nextName(const std::string& prefix)
    {
        return "_" + prefix + std::to_string(id_++) + "_";
    }

    static std::string helperBindingName(const std::string& helperSymbol)
    {
        return renderHelperBindingIdentifier(CodegenNamingLanguage::Go, helperSymbol);
    }

    static void emitMlirHelperBindings(SourceWriter&                   w,
                                       const SectionHelperBindingPlan& plan,
                                       const HelperDirection           direction,
                                       const bool                      emitCapacityCheck)
    {
        GoHelperBodySpelling spelling(w);
        for (const auto& body : buildSectionHelperBodies(
                 plan,
                 direction,
                 [](const std::string& symbol) { return helperBindingName(symbol); },
                 emitCapacityCheck))
        {
            renderHelperBody(body, spelling);
        }
    }

    static void emitSerializeMlirHelperBindings(SourceWriter& w, const SectionHelperBindingPlan& plan)
    {
        emitMlirHelperBindings(w, plan, HelperDirection::Serialize, /*emitCapacityCheck=*/true);
    }

    static void emitDeserializeMlirHelperBindings(SourceWriter& w, const SectionHelperBindingPlan& plan)
    {
        emitMlirHelperBindings(w, plan, HelperDirection::Deserialize, /*emitCapacityCheck=*/false);
    }

    void emitAlignSerialize(SourceWriter& w, std::int64_t alignmentBits)
    {
        if (alignmentBits <= 1)
        {
            return;
        }
        trace(EmitTraceOp::Align, alignmentBits);
        const auto err = nextName("err");
        w.open("for (offsetBits % " + std::to_string(alignmentBits) + ") != 0 {");
        w.line(err + " := dsdlruntime.SetBit(buffer, offsetBits, false)");
        w.open("if " + err + " < 0 {");
        w.line("return " + err + ", 0");
        w.close("}");
        w.line("offsetBits++");
        w.close("}");
    }

    void emitAlignDeserialize(SourceWriter& w, std::int64_t alignmentBits) const
    {
        if (alignmentBits <= 1)
        {
            return;
        }
        trace(EmitTraceOp::Align, alignmentBits);
        w.line("offsetBits = (offsetBits + " + std::to_string(alignmentBits - 1) + ") & ^" +
               std::to_string(alignmentBits - 1));
    }

    void emitSerializePadding(SourceWriter& w, const SemanticFieldType& type)
    {
        if (type.bitLength == 0)
        {
            return;
        }
        trace(EmitTraceOp::Pad, type.bitLength);
        const auto err = nextName("err");
        w.line(err + " := dsdlruntime.SetUxx(buffer, offsetBits, 0, " + std::to_string(type.bitLength) + ")");
        w.open("if " + err + " < 0 {");
        w.line("return " + err + ", 0");
        w.close("}");
        trace(EmitTraceOp::Advance, type.bitLength);
        w.line("offsetBits += " + std::to_string(type.bitLength));
    }

    void emitDeserializePadding(SourceWriter& w, const SemanticFieldType& type) const
    {
        if (type.bitLength == 0)
        {
            return;
        }
        trace(EmitTraceOp::Pad, type.bitLength);
        trace(EmitTraceOp::Advance, type.bitLength);
        w.line("offsetBits += " + std::to_string(type.bitLength));
    }

    /// @brief Go spelling of the shared union-prologue steps (see EmitStep.h).
    class UnionSpelling final : public UnionSectionSpelling
    {
    public:
        UnionSpelling(FunctionBodyEmitter&            owner,
                      SourceWriter&                   w,
                      const std::int64_t              tagBits,
                      const SectionHelperBindingPlan& helperBindings)
            : owner_(owner)
            , w_(w)
            , tagBits_(tagBits)
            , helperBindings_(helperBindings)
        {
        }

        void spellSerializeValidateTag() override
        {
            const auto validateHelper =
                llvmdsdl::FunctionBodyEmitter::helperBindingName(helperBindings_.unionTagValidate->symbol);
            owner_.trace(EmitTraceOp::ValidateTag);
            w_.open("if rc := " + validateHelper + "(int64(obj.Tag)); rc != dsdlruntime.DSDL_RUNTIME_SUCCESS {");
            w_.line("return rc, 0");
            w_.close("}");
        }

        void spellSerializeWriteMaskedTag() override
        {
            const auto tagExpr =
                llvmdsdl::FunctionBodyEmitter::helperBindingName(helperBindings_.unionTagMask->symbol) +
                "(uint64(obj.Tag))";
            const auto tagErr = owner_.nextName("err");
            owner_.trace(EmitTraceOp::MaskTag);
            owner_.trace(EmitTraceOp::WriteTag, tagBits_);
            w_.line(tagErr + " := dsdlruntime.SetUxx(buffer, offsetBits, " + tagExpr + ", " + std::to_string(tagBits_) +
                    ")");
            w_.open("if " + tagErr + " < 0 {");
            w_.line("return " + tagErr + ", 0");
            w_.close("}");
        }

        void spellDeserializeReadMaskStoreTag() override
        {
            const auto rawTag = owner_.nextName("tag");
            owner_.trace(EmitTraceOp::ReadTag, tagBits_);
            w_.line(rawTag + " := dsdlruntime.GetU64(buffer, offsetBits, " + std::to_string(tagBits_) + ")");
            const auto tagExpr =
                llvmdsdl::FunctionBodyEmitter::helperBindingName(helperBindings_.unionTagMask->symbol) + "(" + rawTag +
                ")";
            owner_.trace(EmitTraceOp::MaskTag);
            owner_.trace(EmitTraceOp::StoreTag);
            w_.line("obj.Tag = " + unsignedStorageType(static_cast<std::uint32_t>(tagBits_)) + "(" + tagExpr + ")");
        }

        void spellDeserializeValidateTag() override
        {
            const auto validateHelper =
                llvmdsdl::FunctionBodyEmitter::helperBindingName(helperBindings_.unionTagValidate->symbol);
            owner_.trace(EmitTraceOp::ValidateTag);
            w_.open("if rc := " + validateHelper + "(int64(obj.Tag)); rc != dsdlruntime.DSDL_RUNTIME_SUCCESS {");
            w_.line("return rc, 0");
            w_.close("}");
        }

        void spellAdvanceTag() override
        {
            owner_.trace(EmitTraceOp::Advance, tagBits_);
            w_.line("offsetBits += " + std::to_string(tagBits_));
        }

        void spellBeginDispatch() override
        {
            owner_.trace(EmitTraceOp::Switch);
            // gofmt aligns `case` with its `switch`, so the dispatch line opens no level of its own.
            w_.line("switch obj.Tag {");
        }

        void spellBeginCase(const std::int64_t optionIndex, const bool /*firstCase*/) override
        {
            owner_.trace(EmitTraceOp::Case, optionIndex);
            w_.open("case " + std::to_string(optionIndex) + ":");
        }

        void spellEndCase() override
        {
            w_.dedent();
        }

        void spellBadTagDefault() override
        {
            owner_.trace(EmitTraceOp::DefaultBadTag);
            w_.open("default:");
            w_.line("return -dsdlruntime.DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG, "
                    "0");
            w_.dedent();
        }

        void spellEndDispatch() override
        {
            w_.line("}");
        }

    private:
        FunctionBodyEmitter&            owner_;
        SourceWriter&                   w_;
        const std::int64_t              tagBits_;
        const SectionHelperBindingPlan& helperBindings_;
    };

    void emitSerializeUnion(SourceWriter&                        w,
                            const SemanticSection&               section,
                            const std::vector<PlannedFieldStep>& unionBranches,
                            const LoweredSectionFacts*           sectionFacts,
                            const SectionHelperBindingPlan&      helperBindings)
    {
        const auto                   tagBits = resolveUnionTagBits(section, sectionFacts);
        UnionSpelling                spelling(*this, w, static_cast<std::int64_t>(tagBits), helperBindings);
        std::vector<UnionCaseRender> cases;
        cases.reserve(unionBranches.size());
        for (const auto& step : unionBranches)
        {
            const auto& field = *step.field;
            cases.push_back(UnionCaseRender{field.unionOptionIndex, [this, &w, &step, &field]() {
                                                emitAlignSerialize(w, field.resolvedType.alignmentBits);
                                                emitSerializeAny(w,
                                                                 field.resolvedType,
                                                                 "obj." + exportedFieldIdent(field.name),
                                                                 step.arrayLengthPrefixBits,
                                                                 step.fieldFacts);
                                            }});
        }
        renderUnionSection(EmitTraceDirection::Serialize, cases, spelling);
    }

    void emitDeserializeUnion(SourceWriter&                        w,
                              const SemanticSection&               section,
                              const std::vector<PlannedFieldStep>& unionBranches,
                              const LoweredSectionFacts*           sectionFacts,
                              const SectionHelperBindingPlan&      helperBindings)
    {
        const auto                   tagBits = resolveUnionTagBits(section, sectionFacts);
        UnionSpelling                spelling(*this, w, static_cast<std::int64_t>(tagBits), helperBindings);
        std::vector<UnionCaseRender> cases;
        cases.reserve(unionBranches.size());
        for (const auto& step : unionBranches)
        {
            const auto& field = *step.field;
            cases.push_back(UnionCaseRender{field.unionOptionIndex, [this, &w, &step, &field]() {
                                                emitAlignDeserialize(w, field.resolvedType.alignmentBits);
                                                emitDeserializeAny(w,
                                                                   field.resolvedType,
                                                                   "obj." + exportedFieldIdent(field.name),
                                                                   step.arrayLengthPrefixBits,
                                                                   step.fieldFacts);
                                            }});
        }
        renderUnionSection(EmitTraceDirection::Deserialize, cases, spelling);
    }

    /// @brief Go spelling of the shared recursive field-body steps (see EmitStep.h).
    ///
    /// Leaf statement idioms only; all cross-group and recursive ordering comes
    /// from renderFieldSteps. Fixed arrays are compile-time sized `[N]T`, so the
    /// D2 length guard is type-system-subsumed (spellFixedArrayLenCheck is a
    /// documented no-op).
    class FieldSpelling final : public FieldStepSpelling
    {
    public:
        FieldSpelling(FunctionBodyEmitter& owner, SourceWriter& w, const HelperBindingDirection direction)
            : owner_(owner)
            , w_(w)
            , direction_(direction)
        {
        }

        void spellPad(const FieldEmitStep& step) override
        {
            if (direction_ == HelperBindingDirection::Serialize)
            {
                owner_.emitSerializePadding(w_, step.type);
            }
            else
            {
                owner_.emitDeserializePadding(w_, step.type);
            }
        }

        void spellScalarSerialize(const FieldEmitStep& step, const std::string& expr) override
        {
            switch (step.kind)
            {
            case FieldStepKind::ScalarBool: {
                const auto err = owner_.nextName("err");
                owner_.trace(EmitTraceOp::WriteScalarBool, 1);
                w_.line(err + " := dsdlruntime.SetBit(buffer, offsetBits, " + expr + ")");
                w_.open("if " + err + " < 0 {");
                w_.line("return " + err + ", 0");
                w_.close("}");
                owner_.trace(EmitTraceOp::Advance, 1);
                w_.line("offsetBits += 1");
                break;
            }
            case FieldStepKind::ScalarUint: {
                std::string valueExpr = "uint64(" + expr + ")";
                assert(!step.scalarHelperSymbol.empty());
                const auto helper = llvmdsdl::FunctionBodyEmitter::helperBindingName(step.scalarHelperSymbol);
                valueExpr         = helper + "(" + valueExpr + ")";
                const auto err    = owner_.nextName("err");
                owner_.trace(EmitTraceOp::WriteScalarUint, step.bits);
                w_.line(err + " := dsdlruntime.SetUxx(buffer, offsetBits, " + valueExpr + ", " +
                        std::to_string(step.bits) + ")");
                w_.open("if " + err + " < 0 {");
                w_.line("return " + err + ", 0");
                w_.close("}");
                owner_.trace(EmitTraceOp::Advance, step.bits);
                w_.line("offsetBits += " + std::to_string(step.bits));
                break;
            }
            case FieldStepKind::ScalarSint: {
                std::string valueExpr = "int64(" + expr + ")";
                assert(!step.scalarHelperSymbol.empty());
                const auto helper = llvmdsdl::FunctionBodyEmitter::helperBindingName(step.scalarHelperSymbol);
                valueExpr         = helper + "(" + valueExpr + ")";

                const auto err = owner_.nextName("err");
                owner_.trace(EmitTraceOp::WriteScalarSint, step.bits);
                w_.line(err + " := dsdlruntime.SetIxx(buffer, offsetBits, " + valueExpr + ", " +
                        std::to_string(step.bits) + ")");
                w_.open("if " + err + " < 0 {");
                w_.line("return " + err + ", 0");
                w_.close("}");
                owner_.trace(EmitTraceOp::Advance, step.bits);
                w_.line("offsetBits += " + std::to_string(step.bits));
                break;
            }
            case FieldStepKind::ScalarFloat: {
                const auto  floatType = std::string(step.bits == 64 ? "float64" : "float32");
                std::string valueExpr = floatType + "(" + expr + ")";
                assert(!step.scalarHelperSymbol.empty());
                const auto helper = llvmdsdl::FunctionBodyEmitter::helperBindingName(step.scalarHelperSymbol);
                valueExpr         = helper + "(" + valueExpr + ")";
                const auto err    = owner_.nextName("err");
                owner_.trace(EmitTraceOp::WriteScalarFloat, step.bits);
                if (step.bits == 16)
                {
                    w_.line(err + " := dsdlruntime.SetF16(buffer, offsetBits, " + valueExpr + ")");
                }
                else if (step.bits == 32)
                {
                    w_.line(err + " := dsdlruntime.SetF32(buffer, offsetBits, " + valueExpr + ")");
                }
                else
                {
                    w_.line(err + " := dsdlruntime.SetF64(buffer, offsetBits, " + valueExpr + ")");
                }
                w_.open("if " + err + " < 0 {");
                w_.line("return " + err + ", 0");
                w_.close("}");
                owner_.trace(EmitTraceOp::Advance, step.bits);
                w_.line("offsetBits += " + std::to_string(step.bits));
                break;
            }
            default:
                assert(false && "not a scalar step");
                break;
            }
        }

        void spellScalarDeserialize(const FieldEmitStep& step, const std::string& expr) override
        {
            switch (step.kind)
            {
            case FieldStepKind::ScalarBool:
                owner_.trace(EmitTraceOp::ReadScalarBool, 1);
                w_.line(expr + " = dsdlruntime.GetBit(buffer, offsetBits)");
                owner_.trace(EmitTraceOp::Advance, 1);
                w_.line("offsetBits += 1");
                break;
            case FieldStepKind::ScalarUint: {
                assert(!step.scalarHelperSymbol.empty());
                const auto helper = llvmdsdl::FunctionBodyEmitter::helperBindingName(step.scalarHelperSymbol);
                const auto raw    = owner_.nextName("raw");
                owner_.trace(EmitTraceOp::ReadScalarUint, step.bits);
                w_.line(raw + " := uint64(dsdlruntime.GetU64(buffer, offsetBits, " + std::to_string(step.bits) + "))");
                w_.line(expr + " = " + unsignedStorageType(static_cast<std::uint32_t>(step.bits)) + "(" + helper + "(" +
                        raw + "))");
                owner_.trace(EmitTraceOp::Advance, step.bits);
                w_.line("offsetBits += " + std::to_string(step.bits));
                break;
            }
            case FieldStepKind::ScalarSint: {
                assert(!step.scalarHelperSymbol.empty());
                const auto helper = llvmdsdl::FunctionBodyEmitter::helperBindingName(step.scalarHelperSymbol);
                const auto raw    = owner_.nextName("raw");
                owner_.trace(EmitTraceOp::ReadScalarSint, step.bits);
                w_.line(raw + " := int64(dsdlruntime.GetU64(buffer, offsetBits, " + std::to_string(step.bits) + "))");
                w_.line(expr + " = " + signedStorageType(static_cast<std::uint32_t>(step.bits)) + "(" + helper + "(" +
                        raw + "))");
                owner_.trace(EmitTraceOp::Advance, step.bits);
                w_.line("offsetBits += " + std::to_string(step.bits));
                break;
            }
            case FieldStepKind::ScalarFloat: {
                assert(!step.scalarHelperSymbol.empty());
                const auto helper = llvmdsdl::FunctionBodyEmitter::helperBindingName(step.scalarHelperSymbol);
                owner_.trace(EmitTraceOp::ReadScalarFloat, step.bits);
                if (step.bits == 16)
                {
                    w_.line(expr + " = float32(" + helper + "(float32(dsdlruntime.GetF16(buffer, offsetBits))))");
                }
                else if (step.bits == 32)
                {
                    w_.line(expr + " = float32(" + helper + "(float32(dsdlruntime.GetF32(buffer, offsetBits))))");
                }
                else
                {
                    w_.line(expr + " = " + helper + "(dsdlruntime.GetF64(buffer, offsetBits))");
                }
                owner_.trace(EmitTraceOp::Advance, step.bits);
                w_.line("offsetBits += " + std::to_string(step.bits));
                break;
            }
            default:
                assert(false && "not a scalar step");
                break;
            }
        }

        void spellFixedArrayLenCheck(const FieldEmitStep& step, const std::string& expr) override
        {
            // D2: Go fixed arrays are compile-time sized `[N]T`; the exact-length
            // guard is subsumed by the type system, so there is no emit site.
            (void) step;
            (void) expr;
        }

        void spellVariableArrayLenSerialize(const FieldEmitStep& step, const std::string& expr) override
        {
            assert(step.arrayHelpers.has_value());
            assert(!step.arrayHelpers->validateSymbol.empty());
            const auto validateHelper =
                llvmdsdl::FunctionBodyEmitter::helperBindingName(step.arrayHelpers->validateSymbol);
            const auto validateRc = owner_.nextName("lenRc");
            owner_.trace(EmitTraceOp::LenValidate, step.prefixBits);
            w_.line(validateRc + " := " + validateHelper + "(int64(len(" + expr + ")))");
            w_.open("if " + validateRc + " < 0 {");
            w_.line("return " + validateRc + ", 0");
            w_.close("}");

            std::string prefixExpr = "uint64(len(" + expr + "))";
            assert(!step.arrayHelpers->prefixSymbol.empty());
            const auto serPrefixHelper =
                llvmdsdl::FunctionBodyEmitter::helperBindingName(step.arrayHelpers->prefixSymbol);
            prefixExpr     = serPrefixHelper + "(" + prefixExpr + ")";
            const auto err = owner_.nextName("err");
            owner_.trace(EmitTraceOp::LenWrite, step.prefixBits);
            w_.line(err + " := dsdlruntime.SetUxx(buffer, offsetBits, " + prefixExpr + ", " +
                    std::to_string(step.prefixBits) + ")");
            w_.open("if " + err + " < 0 {");
            w_.line("return " + err + ", 0");
            w_.close("}");
            owner_.trace(EmitTraceOp::Advance, step.prefixBits);
            w_.line("offsetBits += " + std::to_string(step.prefixBits));
        }

        std::string spellVariableArrayLenDeserialize(const FieldEmitStep& step, const std::string& expr) override
        {
            const auto count    = owner_.nextName("count");
            const auto rawCount = owner_.nextName("countRaw");
            owner_.trace(EmitTraceOp::LenRead, step.prefixBits);
            w_.line(rawCount + " := dsdlruntime.GetU64(buffer, offsetBits, " + std::to_string(step.prefixBits) + ")");
            owner_.trace(EmitTraceOp::Advance, step.prefixBits);
            w_.line("offsetBits += " + std::to_string(step.prefixBits));
            std::string countExpr = "int(" + rawCount + ")";
            assert(step.arrayHelpers.has_value());
            assert(!step.arrayHelpers->prefixSymbol.empty());
            const auto deserPrefixHelper =
                llvmdsdl::FunctionBodyEmitter::helperBindingName(step.arrayHelpers->prefixSymbol);
            countExpr = "int(" + deserPrefixHelper + "(" + rawCount + "))";
            w_.line(count + " := " + countExpr);
            assert(!step.arrayHelpers->validateSymbol.empty());
            const auto validateHelper =
                llvmdsdl::FunctionBodyEmitter::helperBindingName(step.arrayHelpers->validateSymbol);
            const auto validateRc = owner_.nextName("lenRc");
            owner_.trace(EmitTraceOp::LenValidate, step.prefixBits);
            w_.line(validateRc + " := " + validateHelper + "(int64(" + count + "))");
            w_.open("if " + validateRc + " < 0 {");
            w_.line("return " + validateRc + ", 0");
            w_.close("}");
            const auto itemType = goBaseFieldType(step.children.front().type,
                                                  owner_.ctx_,
                                                  owner_.currentPackagePath_,
                                                  owner_.importAliases_);
            w_.line(expr + " = make([]" + itemType + ", " + count + ")");
            return count;
        }

        std::string spellFixedArrayCountDeserialize(const FieldEmitStep& step, const std::string& expr) override
        {
            (void) expr;
            const auto count = owner_.nextName("count");
            w_.line(count + " := " + std::to_string(step.capacity));
            return count;
        }

        std::string spellBeginElemLoopSerialize(const FieldEmitStep& step, const std::string& expr) override
        {
            const auto index = owner_.nextName("index");
            const auto count =
                step.kind == FieldStepKind::VariableArray ? "len(" + expr + ")" : std::to_string(step.capacity);
            owner_.trace(EmitTraceOp::ElemLoop);
            w_.open("for " + index + " := 0; " + index + " < " + count + "; " + index + "++ {");
            return expr + "[" + index + "]";
        }

        std::string spellBeginElemLoopDeserialize(const FieldEmitStep& step,
                                                  const std::string&   expr,
                                                  const std::string&   countExpr) override
        {
            (void) step;
            const auto index = owner_.nextName("index");
            owner_.trace(EmitTraceOp::ElemLoop);
            w_.open("for " + index + " := 0; " + index + " < " + countExpr + "; " + index + "++ {");
            return expr + "[" + index + "]";
        }

        void spellEndElemLoopSerialize(const FieldEmitStep& step, const std::string& expr) override
        {
            (void) step;
            (void) expr;
            w_.close("}");
        }

        void spellEndElemLoopDeserialize(const FieldEmitStep& step, const std::string& expr) override
        {
            (void) step;
            (void) expr;
            w_.close("}");
        }

        void spellCompositeSerialize(const FieldEmitStep& step, const std::string& expr) override
        {
            owner_.trace(step.type.compositeSealed ? EmitTraceOp::CompositeInline : EmitTraceOp::CompositeDelimHeader);
            const auto sizeVar  = owner_.nextName("sizeBytes");
            const auto maxBytes = (step.type.bitLengthSet.max() + 7) / 8;
            if (!step.type.compositeSealed)
            {
                w_.line("offsetBits += 32");
            }
            w_.line(sizeVar + " := " + std::to_string(maxBytes));
            if (!step.type.compositeSealed)
            {
                const auto remainingVar = owner_.nextName("remaining");
                w_.line(remainingVar + " := len(buffer) - dsdlruntime.ChooseMin(offsetBits/8, "
                                       "len(buffer))");
                assert(!step.delimiterValidateSymbol.empty());
                const auto helper     = llvmdsdl::FunctionBodyEmitter::helperBindingName(step.delimiterValidateSymbol);
                const auto validateRc = owner_.nextName("rc");
                w_.line(validateRc + " := " + helper + "(int64(" + sizeVar + "), int64(" + remainingVar + "))");
                w_.open("if " + validateRc + " < 0 {");
                w_.line("return " + validateRc + ", 0");
                w_.close("}");
            }
            const auto startVar = owner_.nextName("start");
            const auto endVar   = owner_.nextName("end");
            w_.line(startVar + " := dsdlruntime.ChooseMin(offsetBits/8, len(buffer))");
            w_.line(endVar + " := dsdlruntime.ChooseMin(" + startVar + "+" + sizeVar + ", len(buffer))");
            const auto rcVar       = owner_.nextName("rc");
            const auto consumedVar = owner_.nextName("consumed");
            w_.line(rcVar + ", " + consumedVar + " := " + expr + ".Serialize(buffer[" + startVar + ":" + endVar + "])");
            w_.open("if " + rcVar + " < 0 {");
            w_.line("return " + rcVar + ", 0");
            w_.close("}");
            w_.line(sizeVar + " = " + consumedVar);
            if (!step.type.compositeSealed)
            {
                const auto hdrErr = owner_.nextName("err");
                w_.line(hdrErr + " := dsdlruntime.SetUxx(buffer, offsetBits-32, uint64(" + sizeVar + "), 32)");
                w_.open("if " + hdrErr + " < 0 {");
                w_.line("return " + hdrErr + ", 0");
                w_.close("}");
            }
            w_.line("offsetBits += " + sizeVar + " * 8");
        }

        void spellCompositeDeserialize(const FieldEmitStep& step, const std::string& expr) override
        {
            owner_.trace(step.type.compositeSealed ? EmitTraceOp::CompositeInline : EmitTraceOp::CompositeDelimHeader);
            if (!step.type.compositeSealed)
            {
                const auto sizeVar = owner_.nextName("sizeBytes");
                w_.line(sizeVar + " := int(dsdlruntime.GetU32(buffer, offsetBits, 32))");
                w_.line("offsetBits += 32");
                const auto remainingVar = owner_.nextName("remaining");
                w_.line(remainingVar + " := capacityBytes - dsdlruntime.ChooseMin(offsetBits/8, "
                                       "capacityBytes)");
                assert(!step.delimiterValidateSymbol.empty());
                const auto helper     = llvmdsdl::FunctionBodyEmitter::helperBindingName(step.delimiterValidateSymbol);
                const auto validateRc = owner_.nextName("rc");
                w_.line(validateRc + " := " + helper + "(int64(" + sizeVar + "), int64(" + remainingVar + "))");
                w_.open("if " + validateRc + " < 0 {");
                w_.line("return " + validateRc + ", 0");
                w_.close("}");
                const auto startVar = owner_.nextName("start");
                const auto endVar   = owner_.nextName("end");
                w_.line(startVar + " := dsdlruntime.ChooseMin(offsetBits/8, len(buffer))");
                w_.line(endVar + " := dsdlruntime.ChooseMin(" + startVar + "+" + sizeVar + ", len(buffer))");
                const auto rcVar       = owner_.nextName("rc");
                const auto consumedVar = owner_.nextName("consumed");
                w_.line(rcVar + ", " + consumedVar + " := " + expr + ".Deserialize(buffer[" + startVar + ":" + endVar +
                        "])");
                w_.line("_ = " + consumedVar);
                w_.open("if " + rcVar + " < 0 {");
                w_.line("return " + rcVar + ", 0");
                w_.close("}");
                w_.line("offsetBits += " + sizeVar + " * 8");
                return;
            }

            const auto startVar = owner_.nextName("start");
            w_.line(startVar + " := dsdlruntime.ChooseMin(offsetBits/8, len(buffer))");
            const auto rcVar       = owner_.nextName("rc");
            const auto consumedVar = owner_.nextName("consumed");
            w_.line(rcVar + ", " + consumedVar + " := " + expr + ".Deserialize(buffer[" + startVar + ":len(buffer)])");
            w_.open("if " + rcVar + " < 0 {");
            w_.line("return " + rcVar + ", 0");
            w_.close("}");
            w_.line("offsetBits += " + consumedVar + " * 8");
        }

    private:
        FunctionBodyEmitter&         owner_;
        SourceWriter&                w_;
        const HelperBindingDirection direction_;
    };

    void emitSerializeAny(SourceWriter&                w,
                          const SemanticFieldType&     type,
                          const std::string&           expr,
                          std::optional<std::uint32_t> arrayLengthPrefixBitsOverride = std::nullopt,
                          const LoweredFieldFacts*     fieldFacts                    = nullptr)
    {
        const auto steps =
            buildFieldEmitSteps(type, fieldFacts, arrayLengthPrefixBitsOverride, HelperBindingDirection::Serialize);
        FieldSpelling spelling(*this, w, HelperBindingDirection::Serialize);
        renderFieldSteps(steps, expr, HelperBindingDirection::Serialize, spelling);
    }

    void emitDeserializeAny(SourceWriter&                w,
                            const SemanticFieldType&     type,
                            const std::string&           expr,
                            std::optional<std::uint32_t> arrayLengthPrefixBitsOverride = std::nullopt,
                            const LoweredFieldFacts*     fieldFacts                    = nullptr)
    {
        const auto steps =
            buildFieldEmitSteps(type, fieldFacts, arrayLengthPrefixBitsOverride, HelperBindingDirection::Deserialize);
        FieldSpelling spelling(*this, w, HelperBindingDirection::Deserialize);
        renderFieldSteps(steps, expr, HelperBindingDirection::Deserialize, spelling);
    }
};

void emitSectionType(SourceWriter&                             w,
                     const EmitterContext&                     ctx,
                     const std::string&                        typeName,
                     const std::string&                        fullName,
                     std::uint32_t                             majorVersion,
                     std::uint32_t                             minorVersion,
                     const SemanticSection&                    section,
                     const AttachedDoc&                        typeDoc,
                     const std::string&                        definitionFullName,
                     const std::string&                        currentPackagePath,
                     const std::map<std::string, std::string>& importAliases,
                     const LoweredSectionFacts*                sectionFacts)
{
    const auto typeConstPrefix =
        codegenProjectIdentifier(CodegenNamingLanguage::Go, IdentifierRole::ConstantName, typeName);
    w.line("const " + typeConstPrefix + "_FULL_NAME = \"" + fullName + "\"");
    w.line("const " + typeConstPrefix + "_IS_DEPRECATED = " + std::string(section.deprecated ? "true" : "false"));
    w.line("const " + typeConstPrefix + "_FULL_NAME_AND_VERSION = \"" + fullName + "." + std::to_string(majorVersion) +
           "." + std::to_string(minorVersion) + "\"");
    w.line("const " + typeConstPrefix + "_EXTENT_BYTES = " + std::to_string(section.extentBits.value_or(0) / 8));
    w.line("const " + typeConstPrefix +
           "_SERIALIZATION_BUFFER_SIZE_BYTES = " + std::to_string((section.serializationBufferSizeBits + 7) / 8));
    const bool        zohAliasEligible = sectionFacts != nullptr && sectionFacts->zohAliasEligible;
    const std::string zohAliasReason   = (sectionFacts != nullptr && !sectionFacts->zohAliasReason.empty())
                                             ? sectionFacts->zohAliasReason
                                             : "not-proven";
    w.line("const " + typeConstPrefix + "_ZOH_ALIAS_ELIGIBLE = " + std::string(zohAliasEligible ? "true" : "false"));
    w.line("const " + typeConstPrefix + "_ZOH_ALIAS_REASON = \"" + zohAliasReason + "\"");

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
        w.line("const " + typeConstPrefix + "_UNION_OPTION_COUNT = " + std::to_string(optionCount));
    }

    std::vector<std::string> constNames;
    constNames.reserve(section.constants.size());
    for (const auto& c : section.constants)
    {
        constNames.push_back(c.name);
    }
    NamingScope const constScope = makeSectionConstantScope(CodegenNamingLanguage::Go, section);
    for (const auto& c : section.constants)
    {
        // gofmt separates a documented declaration from whatever precedes it, so a doc
        // comment landing directly under another constant needs the blank line first.
        if (!c.doc.lines.empty())
        {
            w.blank();
        }
        emitAttachedDocGo(w, c.doc);
        w.line("const " + typeConstPrefix + "_" + constScope.get(IdentifierRole::ConstantName, c.name) + " = " +
               goConstValue(c.type, c.value));
    }
    w.blank();

    emitAttachedDocGo(w,
                      docWithDeprecationNotice(typeDoc,
                                               section.deprecated,
                                               definitionFullName,
                                               majorVersion,
                                               minorVersion));
    w.open("type " + typeName + " struct {");
    const NamingScope fieldIdents = makeExportedFieldIdents(section);

    // gofmt aligns a struct's types into a column, and a doc comment starts a fresh
    // one: the members are collected first so each run's width is known before any of
    // it is written.
    std::vector<GoStructMember> members;
    for (const auto& field : section.fields)
    {
        if (field.isPadding)
        {
            continue;
        }
        members.push_back(GoStructMember{fieldIdents.get(IdentifierRole::FieldName, field.name),
                                         goFieldType(field.resolvedType, ctx, currentPackagePath, importAliases),
                                         field.doc});
    }
    if (section.isUnion)
    {
        // Tag storage must match the wire tag width (uint8 for <=256 options, uint16 for
        // 257..65536, etc.); a hardcoded uint8 truncates a wide tag and mis-dispatches.
        members.push_back(GoStructMember{"Tag", unsignedStorageType(resolveUnionTagBits(section, sectionFacts)), {}});
    }
    if (section.fields.empty())
    {
        members.push_back(GoStructMember{"_", "uint8", {}});
    }
    emitAlignedStructMembers(w, members);
    w.close("}");
    w.blank();

    const auto canonicalSectionName =
        fullName + "." + std::to_string(majorVersion) + "." + std::to_string(minorVersion);
    FunctionBodyEmitter body(ctx, currentPackagePath, importAliases);
    ctx.traceSection(canonicalSectionName, EmitTraceDirection::Serialize);
    body.emitSerializeFunction(w, typeName, section, sectionFacts);
    w.blank();
    ctx.traceSection(canonicalSectionName, EmitTraceDirection::Deserialize);
    body.emitDeserializeFunction(w, typeName, section, sectionFacts);
}

std::string renderDefinitionFile(const SemanticDefinition& def,
                                 const EmitterContext&     ctx,
                                 const std::string&        moduleName,
                                 const LoweredFactsMap&    loweredFacts)
{
    const auto currentPackagePath = llvmdsdl::EmitterContext::packagePath(def.info);
    const auto packageName        = packageNameFromPath(currentPackagePath);
    const auto imports            = computeImportAliases(def, ctx);

    std::ostringstream out;
    SourceWriter       w = makeGoWriter(out);
    w.line(generatedCommentLine("Go backend"));
    w.line("// Source: " + def.info.fullName + "." + std::to_string(def.info.majorVersion) + "." +
           std::to_string(def.info.minorVersion));
    w.blank();
    w.line("package " + packageName);
    w.blank();

    w.open("import (");
    w.line("dsdlruntime \"" + moduleName + "/dsdlruntime\"");
    for (const auto& [path, alias] : imports)
    {
        // NOLINTNEXTLINE(performance-inefficient-string-concatenation)
        w.line(alias + " \"" + moduleName + "/" + path + "\"");
    }
    w.close(")");
    w.blank();

    const auto baseType = ctx.goTypeName(def.info);
    if (!def.isService)
    {
        emitSectionType(w,
                        ctx,
                        baseType,
                        def.info.fullName,
                        def.info.majorVersion,
                        def.info.minorVersion,
                        def.request,
                        def.doc,
                        def.info.fullName,
                        currentPackagePath,
                        imports,
                        lookupLoweredSectionFacts(loweredFacts, def, ""));
        return out.str();
    }

    const auto reqType  = baseType + renderSectionTypeSuffix(CodegenNamingLanguage::Go, "request");
    const auto respType = baseType + renderSectionTypeSuffix(CodegenNamingLanguage::Go, "response");
    emitSectionType(w,
                    ctx,
                    reqType,
                    def.info.fullName + ".Request",
                    def.info.majorVersion,
                    def.info.minorVersion,
                    def.request,
                    def.doc,
                    def.info.fullName,
                    currentPackagePath,
                    imports,
                    lookupLoweredSectionFacts(loweredFacts, def, "request"));
    w.blank();
    if (def.response)
    {
        emitSectionType(w,
                        ctx,
                        respType,
                        def.info.fullName + ".Response",
                        def.info.majorVersion,
                        def.info.minorVersion,
                        *def.response,
                        def.doc,
                        def.info.fullName,
                        currentPackagePath,
                        imports,
                        lookupLoweredSectionFacts(loweredFacts, def, "response"));
        w.blank();
    }
    w.line("type " + baseType + " = " + reqType);
    // gofmt separates top-level declarations of different kinds, so the alias and the
    // constants that follow it do not sit together.
    w.blank();
    const auto baseConstPrefix =
        codegenProjectIdentifier(CodegenNamingLanguage::Go, IdentifierRole::ConstantName, baseType);
    const auto reqConstPrefix =
        codegenProjectIdentifier(CodegenNamingLanguage::Go, IdentifierRole::ConstantName, reqType);
    w.line("const " + baseConstPrefix + "_ZOH_ALIAS_ELIGIBLE = " + reqConstPrefix + "_ZOH_ALIAS_ELIGIBLE");
    w.line("const " + baseConstPrefix + "_ZOH_ALIAS_REASON = " + reqConstPrefix + "_ZOH_ALIAS_REASON");
    return out.str();
}

llvm::Expected<std::string> loadGoRuntime()
{
    if (const auto data = embedded_runtime::find("go/dsdl_runtime.go"))
    {
        return std::string(*data);
    }
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "embedded runtime source missing: go/dsdl_runtime.go");
}

std::string renderGoMod(const GoEmitOptions& options)
{
    std::ostringstream out;
    out << generatedCommentLine("Go backend module metadata") << "\n";
    out << "module " << options.moduleName << "\n\n";
    out << "go 1.22\n";
    return out.str();
}

}  // namespace

llvm::Error emitGo(const SemanticModule& semantic,
                   mlir::ModuleOp        module,
                   const GoEmitOptions&  options,
                   DiagnosticEngine&     diagnostics,
                   EmitTraceSink*        traceSink)
{
    if (options.outDir.empty())
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "output directory is required");
    }

    // Go is the one backend that cannot express unversioned names where it matters. A DSDL namespace
    // becomes one package and every version of a type lands in it, so two versions give one struct
    // name and one set of metadata constants -- the package does not compile, and no include-time
    // guard can help because Go compiles the package as a whole. Refuse rather than write a tree
    // that cannot build.
    if (options.typeNameVersioning == TypeNameVersioning::Unversioned)
    {
        std::map<std::string, std::vector<std::string>> versionsByFullName;
        for (const auto& def : semantic.definitions)
        {
            versionsByFullName[def.info.fullName].push_back(std::to_string(def.info.majorVersion) + "." +
                                                            std::to_string(def.info.minorVersion));
        }
        bool refused = false;
        for (const auto& [fullName, versions] : versionsByFullName)
        {
            if (versions.size() > 1U)
            {
                std::string list;
                for (const auto& v : versions)
                {
                    list += (list.empty() ? "" : ", ") + v;
                }
                std::string clash;
                clash.append("'")
                    .append(fullName)
                    .append("' has ")
                    .append(std::to_string(versions.size()))
                    .append(" versions (")
                    .append(list)
                    .append(") in one namespace, which Go compiles as one package; unversioned "
                            "type names would declare it more than once. Pass "
                            "--versioned-type-names, or select one version.");
                diagnostics.error({"<go>", 1, 1}, clash);
                refused = true;
            }
        }
        if (refused)
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "Go cannot emit unversioned type names for a namespace holding "
                                           "more than one version of a type");
        }
    }
    const auto mlirCoverageDiagnostic = codegen_diagnostic_text::mlirSchemaCoverageValidationFailedForEmission("Go");
    LoweredFactsMap loweredFacts;
    if (!collectLoweredFactsFromMlir(semantic, module, diagnostics, "Go", &loweredFacts, options.optimizeLoweredSerDes))
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "%s", mlirCoverageDiagnostic.c_str());
    }

    std::filesystem::path const outRoot(options.outDir);
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

    if (emitSupport)
    {
        if (options.emitGoMod)
        {
            if (auto err = writeGeneratedFile(outRoot / "go.mod", renderGoMod(options), options.writePolicy))
            {
                return err;
            }
        }

        auto runtime = loadGoRuntime();
        if (!runtime)
        {
            return runtime.takeError();
        }
        if (auto err = writeGeneratedFile(outRoot / "dsdlruntime" / "dsdl_runtime.go",
                                          generatedCommentLine("Go runtime scaffold") + "\n\n" + *runtime,
                                          options.writePolicy))
        {
            return err;
        }
    }

    EmitterContext ctx(semantic, options.typeNameVersioning);
    ctx.setTraceSink(traceSink);

    for (const auto& def : semantic.definitions)
    {
        if (!shouldEmitDefinition(def.info, selectedTypeKeys, options.supportGeneration))
        {
            continue;
        }
        const std::vector<std::string> requiredTypeKeys{definitionTypeKey(def.info)};

        const auto            dirRel = llvmdsdl::EmitterContext::packagePath(def.info);
        std::filesystem::path dir    = outRoot;
        if (!dirRel.empty())
        {
            dir /= dirRel;
        }
        if (auto err = writeGeneratedFile(dir / llvmdsdl::EmitterContext::goFileName(def.info),
                                          renderDefinitionFile(def, ctx, options.moduleName, loweredFacts),
                                          options.writePolicy,
                                          requiredTypeKeys))
        {
            return err;
        }
    }

    return llvm::Error::success();
}

}  // namespace llvmdsdl
