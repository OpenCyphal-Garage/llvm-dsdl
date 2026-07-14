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
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/GoEmitter.h"

#include <llvm/ADT/StringRef.h>
#include <cassert>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <system_error>
#include <utility>
#include <variant>

#include "llvmdsdl/CodeGen/ArrayWirePlan.h"
#include "llvmdsdl/CodeGen/CodegenDiagnosticText.h"
#include "llvmdsdl/CodeGen/ConstantLiteralRender.h"
#include "llvmdsdl/CodeGen/DefinitionDependencies.h"
#include "llvmdsdl/CodeGen/DefinitionIndex.h"
#include "llvmdsdl/CodeGen/EmitStep.h"
#include "llvmdsdl/CodeGen/EmitTrace.h"
#include "llvmdsdl/CodeGen/HelperBindingRender.h"
#include "llvmdsdl/CodeGen/HelperSymbolResolver.h"
#include "llvmdsdl/CodeGen/LoweredRenderIR.h"
#include "llvmdsdl/CodeGen/LoweredFactsLookup.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/CodeGen/NamingPolicy.h"
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
#include "llvmdsdl/CodeGen/SerDesHelperDescriptors.h"
#include "llvmdsdl/CodeGen/SerDesStatementPlan.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/BitLengthSet.h"
#include "llvmdsdl/Semantics/Evaluator.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/Rational.h"
#include "llvmdsdl/Version.h"
#include "mlir/IR/BuiltinOps.h"

namespace llvmdsdl
{
class DiagnosticEngine;

namespace
{

std::string toExportedIdent(const llvm::StringRef in)
{
    auto out = codegenToPascalCaseIdentifier(CodegenNamingLanguage::Go, in);
    if (!std::isupper(static_cast<unsigned char>(out.front())))
    {
        out.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(out.front())));
    }
    return out;
}

/// @brief Builds the collision-free exported field-name allocation for one section.
///
/// Two distinct DSDL field names (e.g. `fooBar` and `foo_bar`) both export to `FooBar`; without this
/// the struct would declare the same field twice (a compile error) and the (de)serializer would read
/// or write the wrong field. Padding fields carry no exported name and are excluded.
CodegenIdentifierAllocator makeExportedFieldIdents(const SemanticSection& section)
{
    std::vector<std::string> names;
    for (const auto& field : section.fields)
    {
        if (!field.isPadding)
        {
            names.push_back(field.name);
        }
    }
    return CodegenIdentifierAllocator(names, [](llvm::StringRef name) { return toExportedIdent(name); });
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
        out += codegenToSnakeCaseIdentifier(CodegenNamingLanguage::Go, c);
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
    auto       out   = codegenSanitizeIdentifier(CodegenNamingLanguage::Go, leaf);
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

void emitLine(std::ostringstream& out, const int indent, const std::string& line)
{
    out << std::string(static_cast<std::size_t>(indent) * 2U, ' ') << line << '\n';
}

std::string generatedCommentLine(llvm::StringRef detail)
{
    return llvm::formatv("// Generated by llvmdsdl {0} ({1}).", llvmdsdl::kVersionString, detail).str();
}

void emitAttachedDocGo(std::ostringstream& out, const int indent, const AttachedDoc& doc)
{
    for (const auto& line : doc.lines)
    {
        emitLine(out, indent, "// " + line.text);
    }
}

class EmitterContext final
{
public:
    explicit EmitterContext(const SemanticModule& semantic)
        : index_(semantic)
    {
    }

    /// @brief Attaches an emit-order trace sink (for the emit-order verifier). Null (default) disables tracing at zero cost.
    void setTraceSink(EmitTraceSink* const sink) { traceSink_ = sink; }

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

    std::string packagePath(const DiscoveredDefinition& info) const
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
        return codegenToPascalCaseIdentifier(CodegenNamingLanguage::Go, info.shortName) + "_" +
               std::to_string(info.majorVersion) + "_" + std::to_string(info.minorVersion);
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

    std::string goFileName(const DiscoveredDefinition& info) const
    {
        return codegenToSnakeCaseIdentifier(CodegenNamingLanguage::Go, info.shortName) + "_" +
               std::to_string(info.majorVersion) + "_" + std::to_string(info.minorVersion) + ".go";
    }

private:
    DefinitionIndex index_;
    EmitTraceSink*  traceSink_ = nullptr;
};

std::map<std::string, std::string> computeImportAliases(const SemanticDefinition& def, const EmitterContext& ctx)
{
    const auto deps = collectDefinitionCompositeDependencies(def);

    const std::string                  currentPath = ctx.packagePath(def.info);
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
        auto alias =
            "pkg_" + codegenSanitizeIdentifier(CodegenNamingLanguage::Go, llvm::join(ref.namespaceComponents, "_"));
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
            const auto depType = ctx.goTypeName(*type.compositeType);
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
    const auto base = goBaseFieldType(type, ctx, currentPackagePath, importAliases);
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

    void emitSerializeFunction(std::ostringstream&        out,
                               const std::string&         typeName,
                               const SemanticSection&     section,
                               const LoweredSectionFacts* sectionFacts)
    {
        fieldIdents_.emplace(makeExportedFieldIdents(section));
        emitLine(out, 0, "func (obj *" + typeName + ") Serialize(buffer []byte) (int8, int) {");
        emitLine(out, 1, "if obj == nil {");
        emitLine(out, 2, "return -dsdlruntime.DSDL_RUNTIME_ERROR_INVALID_ARGUMENT, 0");
        emitLine(out, 1, "}");
        emitLine(out, 1, "offsetBits := 0");
        const auto emitted = emitNativeFunctionSkeleton(
            section,
            sectionFacts,
            HelperBindingDirection::Serialize,
            NativeFunctionSkeletonCallbacks{[this, &out](const SectionHelperBindingPlan& helperBindings) {
                                                emitSerializeMlirHelperBindings(out, helperBindings, 1);
                                            },
                                            [&out](const std::string& missingHelperRequirement) {
                                                emitLine(out,
                                                         1,
                                                         "// missing lowered helper contract: " +
                                                             missingHelperRequirement);
                                                emitLine(out,
                                                         1,
                                                         "return -dsdlruntime.DSDL_RUNTIME_ERROR_INVALID_ARGUMENT, 0");
                                            },
                                            [this, &out](const SectionHelperBindingPlan& helperBindings) {
                                                const auto capacityHelper =
                                                    helperBindingName(helperBindings.capacityCheck->symbol);
                                                emitLine(out,
                                                         1,
                                                         "if rc := " + capacityHelper +
                                                             "(int64(len(buffer) * 8)); rc != "
                                                             "dsdlruntime.DSDL_RUNTIME_SUCCESS {");
                                                emitLine(out, 2, "return rc, 0");
                                                emitLine(out, 1, "}");
                                            },
                                            [this, &out, &section, sectionFacts](const LoweredBodyRenderIR& renderIR) {
                                                NativeEmitterTraversalCallbacks callbacks;
                                                callbacks.onUnionDispatch =
                                                    [this, &out, &section, sectionFacts, &renderIR](
                                                        const std::vector<PlannedFieldStep>& unionBranches) {
                                                        emitSerializeUnion(out,
                                                                           section,
                                                                           unionBranches,
                                                                           1,
                                                                           sectionFacts,
                                                                           renderIR.helperBindings);
                                                    };
                                                callbacks.onFieldAlignment = [this,
                                                                              &out](const std::int64_t alignmentBits) {
                                                    emitAlignSerialize(out, alignmentBits, 1);
                                                };
                                                callbacks.onField = [this, &out](const PlannedFieldStep& fieldStep) {
                                                    const auto* const field = fieldStep.field;
                                                    emitSerializeAny(out,
                                                                     field->resolvedType,
                                                                     "obj." + exportedFieldIdent(field->name),
                                                                     1,
                                                                     fieldStep.arrayLengthPrefixBits,
                                                                     fieldStep.fieldFacts);
                                                };
                                                callbacks.onPaddingAlignment =
                                                    [this, &out](const std::int64_t alignmentBits) {
                                                        emitAlignSerialize(out, alignmentBits, 1);
                                                    };
                                                callbacks.onPadding = [this, &out](const PlannedFieldStep& fieldStep) {
                                                    const auto* const field = fieldStep.field;
                                                    emitSerializePadding(out, field->resolvedType, 1);
                                                };
                                                return callbacks;
                                            },
                                            [this, &out]() {
                                                emitAlignSerialize(out, 8, 1);
                                                emitLine(out,
                                                         1,
                                                         "return dsdlruntime.DSDL_RUNTIME_SUCCESS, offsetBits / 8");
                                            }});
        if (!emitted)
        {
            emitLine(out, 0, "}");
            return;
        }
        emitLine(out, 0, "}");
    }

    void emitDeserializeFunction(std::ostringstream&        out,
                                 const std::string&         typeName,
                                 const SemanticSection&     section,
                                 const LoweredSectionFacts* sectionFacts)
    {
        fieldIdents_.emplace(makeExportedFieldIdents(section));
        emitLine(out, 0, "func (obj *" + typeName + ") Deserialize(buffer []byte) (int8, int) {");
        emitLine(out, 1, "if obj == nil {");
        emitLine(out, 2, "return -dsdlruntime.DSDL_RUNTIME_ERROR_INVALID_ARGUMENT, 0");
        emitLine(out, 1, "}");
        emitLine(out, 1, "capacityBytes := len(buffer)");
        emitLine(out, 1, "capacityBits := capacityBytes * 8");
        emitLine(out, 1, "offsetBits := 0");
        const auto emitted = emitNativeFunctionSkeleton(
            section,
            sectionFacts,
            HelperBindingDirection::Deserialize,
            NativeFunctionSkeletonCallbacks{[this, &out](const SectionHelperBindingPlan& helperBindings) {
                                                emitDeserializeMlirHelperBindings(out, helperBindings, 1);
                                            },
                                            [&out](const std::string& missingHelperRequirement) {
                                                emitLine(out,
                                                         1,
                                                         "// missing lowered helper contract: " +
                                                             missingHelperRequirement);
                                                emitLine(out,
                                                         1,
                                                         "return -dsdlruntime.DSDL_RUNTIME_ERROR_INVALID_ARGUMENT, 0");
                                            },
                                            nullptr,
                                            [this, &out, &section, sectionFacts](const LoweredBodyRenderIR& renderIR) {
                                                NativeEmitterTraversalCallbacks callbacks;
                                                callbacks.onUnionDispatch =
                                                    [this, &out, &section, sectionFacts, &renderIR](
                                                        const std::vector<PlannedFieldStep>& unionBranches) {
                                                        emitDeserializeUnion(out,
                                                                             section,
                                                                             unionBranches,
                                                                             1,
                                                                             sectionFacts,
                                                                             renderIR.helperBindings);
                                                    };
                                                callbacks.onFieldAlignment = [this,
                                                                              &out](const std::int64_t alignmentBits) {
                                                    emitAlignDeserialize(out, alignmentBits, 1);
                                                };
                                                callbacks.onField = [this, &out](const PlannedFieldStep& fieldStep) {
                                                    const auto* const field = fieldStep.field;
                                                    emitDeserializeAny(out,
                                                                       field->resolvedType,
                                                                       "obj." + exportedFieldIdent(field->name),
                                                                       1,
                                                                       fieldStep.arrayLengthPrefixBits,
                                                                       fieldStep.fieldFacts);
                                                };
                                                callbacks.onPaddingAlignment =
                                                    [this, &out](const std::int64_t alignmentBits) {
                                                        emitAlignDeserialize(out, alignmentBits, 1);
                                                    };
                                                callbacks.onPadding = [this, &out](const PlannedFieldStep& fieldStep) {
                                                    const auto* const field = fieldStep.field;
                                                    emitDeserializePadding(out, field->resolvedType, 1);
                                                };
                                                return callbacks;
                                            },
                                            [this, &out]() {
                                                emitAlignDeserialize(out, 8, 1);
                                                emitLine(out,
                                                         1,
                                                         "consumedBits := dsdlruntime.ChooseMin(offsetBits, "
                                                         "capacityBits)");
                                                emitLine(out,
                                                         1,
                                                         "return dsdlruntime.DSDL_RUNTIME_SUCCESS, consumedBits / 8");
                                            }});
        if (!emitted)
        {
            emitLine(out, 0, "}");
            return;
        }
        emitLine(out, 0, "}");
    }

    /// @brief Collision-free exported field name for the section currently being emitted.
    std::string exportedFieldIdent(const llvm::StringRef name) const
    {
        return fieldIdents_ ? fieldIdents_->get(name) : toExportedIdent(name);
    }

private:
    const EmitterContext&                     ctx_;
    std::string                               currentPackagePath_;
    const std::map<std::string, std::string>& importAliases_;

    /// @brief Per-section exported field-name allocation; set at the start of each function body.
    std::optional<CodegenIdentifierAllocator> fieldIdents_;
    std::size_t                               id_{0};

    std::string nextName(const std::string& prefix)
    {
        return "_" + prefix + std::to_string(id_++) + "_";
    }

    std::string helperBindingName(const std::string& helperSymbol) const
    {
        return renderHelperBindingIdentifier(CodegenNamingLanguage::Go, helperSymbol);
    }

    void emitSerializeMlirHelperBindings(std::ostringstream&             out,
                                         const SectionHelperBindingPlan& plan,
                                         const int                       indent)
    {
        for (const auto& line : renderSectionHelperBindings(
                 plan,
                 HelperBindingRenderLanguage::Go,
                 ScalarBindingRenderDirection::Serialize,
                 [this](const std::string& symbol) { return helperBindingName(symbol); },
                 /*emitCapacityCheck=*/true))
        {
            emitLine(out, indent, line);
        }
    }

    void emitDeserializeMlirHelperBindings(std::ostringstream&             out,
                                           const SectionHelperBindingPlan& plan,
                                           const int                       indent)
    {
        for (const auto& line : renderSectionHelperBindings(
                 plan,
                 HelperBindingRenderLanguage::Go,
                 ScalarBindingRenderDirection::Deserialize,
                 [this](const std::string& symbol) { return helperBindingName(symbol); },
                 /*emitCapacityCheck=*/false))
        {
            emitLine(out, indent, line);
        }
    }

    void emitAlignSerialize(std::ostringstream& out, std::int64_t alignmentBits, int indent)
    {
        if (alignmentBits <= 1)
        {
            return;
        }
        trace(EmitTraceOp::Align, alignmentBits);
        const auto err = nextName("err");
        emitLine(out, indent, "for (offsetBits % " + std::to_string(alignmentBits) + ") != 0 {");
        emitLine(out, indent + 1, err + " := dsdlruntime.SetBit(buffer, offsetBits, false)");
        emitLine(out, indent + 1, "if " + err + " < 0 {");
        emitLine(out, indent + 2, "return " + err + ", 0");
        emitLine(out, indent + 1, "}");
        emitLine(out, indent + 1, "offsetBits++");
        emitLine(out, indent, "}");
    }

    void emitAlignDeserialize(std::ostringstream& out, std::int64_t alignmentBits, int indent)
    {
        if (alignmentBits <= 1)
        {
            return;
        }
        trace(EmitTraceOp::Align, alignmentBits);
        emitLine(out,
                 indent,
                 "offsetBits = (offsetBits + " + std::to_string(alignmentBits - 1) + ") & ^" +
                     std::to_string(alignmentBits - 1));
    }

    void emitSerializePadding(std::ostringstream& out, const SemanticFieldType& type, int indent)
    {
        if (type.bitLength == 0)
        {
            return;
        }
        trace(EmitTraceOp::Pad, type.bitLength);
        const auto err = nextName("err");
        emitLine(out,
                 indent,
                 err + " := dsdlruntime.SetUxx(buffer, offsetBits, 0, " + std::to_string(type.bitLength) + ")");
        emitLine(out, indent, "if " + err + " < 0 {");
        emitLine(out, indent + 1, "return " + err + ", 0");
        emitLine(out, indent, "}");
        trace(EmitTraceOp::Advance, type.bitLength);
        emitLine(out, indent, "offsetBits += " + std::to_string(type.bitLength));
    }

    void emitDeserializePadding(std::ostringstream& out, const SemanticFieldType& type, int indent)
    {
        if (type.bitLength == 0)
        {
            return;
        }
        trace(EmitTraceOp::Pad, type.bitLength);
        trace(EmitTraceOp::Advance, type.bitLength);
        emitLine(out, indent, "offsetBits += " + std::to_string(type.bitLength));
    }

    /// @brief Go spelling of the shared union-prologue steps (see EmitStep.h).
    class UnionSpelling final : public UnionSectionSpelling
    {
    public:
        UnionSpelling(FunctionBodyEmitter&            owner,
                      std::ostringstream&             out,
                      const int                       indent,
                      const std::int64_t              tagBits,
                      const SectionHelperBindingPlan& helperBindings)
            : owner_(owner)
            , out_(out)
            , indent_(indent)
            , tagBits_(tagBits)
            , helperBindings_(helperBindings)
        {
        }

        void spellSerializeValidateTag() override
        {
            const auto validateHelper = owner_.helperBindingName(helperBindings_.unionTagValidate->symbol);
            owner_.trace(EmitTraceOp::ValidateTag);
            emitLine(out_,
                     indent_,
                     "if rc := " + validateHelper + "(int64(obj.Tag)); rc != dsdlruntime.DSDL_RUNTIME_SUCCESS {");
            emitLine(out_, indent_ + 1, "return rc, 0");
            emitLine(out_, indent_, "}");
        }

        void spellSerializeWriteMaskedTag() override
        {
            const auto tagExpr =
                owner_.helperBindingName(helperBindings_.unionTagMask->symbol) + "(uint64(obj.Tag))";
            const auto tagErr = owner_.nextName("err");
            owner_.trace(EmitTraceOp::MaskTag);
            owner_.trace(EmitTraceOp::WriteTag, tagBits_);
            emitLine(out_,
                     indent_,
                     tagErr + " := dsdlruntime.SetUxx(buffer, offsetBits, " + tagExpr + ", " +
                         std::to_string(tagBits_) + ")");
            emitLine(out_, indent_, "if " + tagErr + " < 0 {");
            emitLine(out_, indent_ + 1, "return " + tagErr + ", 0");
            emitLine(out_, indent_, "}");
        }

        void spellDeserializeReadMaskStoreTag() override
        {
            const auto rawTag = owner_.nextName("tag");
            owner_.trace(EmitTraceOp::ReadTag, tagBits_);
            emitLine(out_,
                     indent_,
                     rawTag + " := dsdlruntime.GetU64(buffer, offsetBits, " + std::to_string(tagBits_) + ")");
            const auto tagExpr = owner_.helperBindingName(helperBindings_.unionTagMask->symbol) + "(" + rawTag + ")";
            owner_.trace(EmitTraceOp::MaskTag);
            owner_.trace(EmitTraceOp::StoreTag);
            emitLine(out_,
                     indent_,
                     "obj.Tag = " + unsignedStorageType(static_cast<std::uint32_t>(tagBits_)) + "(" + tagExpr + ")");
        }

        void spellDeserializeValidateTag() override
        {
            const auto validateHelper = owner_.helperBindingName(helperBindings_.unionTagValidate->symbol);
            owner_.trace(EmitTraceOp::ValidateTag);
            emitLine(out_,
                     indent_,
                     "if rc := " + validateHelper + "(int64(obj.Tag)); rc != dsdlruntime.DSDL_RUNTIME_SUCCESS {");
            emitLine(out_, indent_ + 1, "return rc, 0");
            emitLine(out_, indent_, "}");
        }

        void spellAdvanceTag() override
        {
            owner_.trace(EmitTraceOp::Advance, tagBits_);
            emitLine(out_, indent_, "offsetBits += " + std::to_string(tagBits_));
        }

        void spellBeginDispatch() override
        {
            owner_.trace(EmitTraceOp::Switch);
            emitLine(out_, indent_, "switch obj.Tag {");
        }

        void spellBeginCase(const std::int64_t optionIndex, const bool /*firstCase*/) override
        {
            owner_.trace(EmitTraceOp::Case, optionIndex);
            emitLine(out_, indent_, "case " + std::to_string(optionIndex) + ":");
        }

        void spellEndCase() override
        {
            // Go cases are keyword-delimited; nothing to close.
        }

        void spellBadTagDefault() override
        {
            owner_.trace(EmitTraceOp::DefaultBadTag);
            emitLine(out_, indent_, "default:");
            emitLine(out_,
                     indent_ + 1,
                     "return -dsdlruntime.DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG, "
                     "0");
        }

        void spellEndDispatch() override { emitLine(out_, indent_, "}"); }

    private:
        FunctionBodyEmitter&            owner_;
        std::ostringstream&             out_;
        const int                       indent_;
        const std::int64_t              tagBits_;
        const SectionHelperBindingPlan& helperBindings_;
    };

    void emitSerializeUnion(std::ostringstream&                  out,
                            const SemanticSection&               section,
                            const std::vector<PlannedFieldStep>& unionBranches,
                            int                                  indent,
                            const LoweredSectionFacts*           sectionFacts,
                            const SectionHelperBindingPlan&      helperBindings)
    {
        const auto    tagBits = resolveUnionTagBits(section, sectionFacts);
        UnionSpelling spelling(*this, out, indent, static_cast<std::int64_t>(tagBits), helperBindings);
        std::vector<UnionCaseRender> cases;
        cases.reserve(unionBranches.size());
        for (const auto& step : unionBranches)
        {
            const auto& field = *step.field;
            cases.push_back(UnionCaseRender{field.unionOptionIndex, [this, &out, indent, &step, &field]() {
                                                emitAlignSerialize(out, field.resolvedType.alignmentBits, indent + 1);
                                                emitSerializeAny(out,
                                                                 field.resolvedType,
                                                                 "obj." + exportedFieldIdent(field.name),
                                                                 indent + 1,
                                                                 step.arrayLengthPrefixBits,
                                                                 step.fieldFacts);
                                            }});
        }
        renderUnionSection(EmitTraceDirection::Serialize, cases, spelling);
    }

    void emitDeserializeUnion(std::ostringstream&                  out,
                              const SemanticSection&               section,
                              const std::vector<PlannedFieldStep>& unionBranches,
                              int                                  indent,
                              const LoweredSectionFacts*           sectionFacts,
                              const SectionHelperBindingPlan&      helperBindings)
    {
        const auto    tagBits = resolveUnionTagBits(section, sectionFacts);
        UnionSpelling spelling(*this, out, indent, static_cast<std::int64_t>(tagBits), helperBindings);
        std::vector<UnionCaseRender> cases;
        cases.reserve(unionBranches.size());
        for (const auto& step : unionBranches)
        {
            const auto& field = *step.field;
            cases.push_back(UnionCaseRender{field.unionOptionIndex, [this, &out, indent, &step, &field]() {
                                                emitAlignDeserialize(out, field.resolvedType.alignmentBits, indent + 1);
                                                emitDeserializeAny(out,
                                                                   field.resolvedType,
                                                                   "obj." + exportedFieldIdent(field.name),
                                                                   indent + 1,
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
        FieldSpelling(FunctionBodyEmitter&         owner,
                      std::ostringstream&          out,
                      const int                    indent,
                      const HelperBindingDirection direction)
            : owner_(owner)
            , out_(out)
            , indent_(indent)
            , direction_(direction)
        {
        }

        void spellPad(const FieldEmitStep& step) override
        {
            if (direction_ == HelperBindingDirection::Serialize)
            {
                owner_.emitSerializePadding(out_, step.type, indent_);
            }
            else
            {
                owner_.emitDeserializePadding(out_, step.type, indent_);
            }
        }

        void spellScalarSerialize(const FieldEmitStep& step, const std::string& expr) override
        {
            switch (step.kind)
            {
            case FieldStepKind::ScalarBool: {
                const auto err = owner_.nextName("err");
                owner_.trace(EmitTraceOp::WriteScalarBool, 1);
                emitLine(out_, indent_, err + " := dsdlruntime.SetBit(buffer, offsetBits, " + expr + ")");
                emitLine(out_, indent_, "if " + err + " < 0 {");
                emitLine(out_, indent_ + 1, "return " + err + ", 0");
                emitLine(out_, indent_, "}");
                owner_.trace(EmitTraceOp::Advance, 1);
                emitLine(out_, indent_, "offsetBits += 1");
                break;
            }
            case FieldStepKind::ScalarUint: {
                std::string valueExpr = "uint64(" + expr + ")";
                assert(!step.scalarHelperSymbol.empty());
                const auto helper = owner_.helperBindingName(step.scalarHelperSymbol);
                valueExpr         = helper + "(" + valueExpr + ")";
                const auto err    = owner_.nextName("err");
                owner_.trace(EmitTraceOp::WriteScalarUint, step.bits);
                emitLine(out_,
                         indent_,
                         err + " := dsdlruntime.SetUxx(buffer, offsetBits, " + valueExpr + ", " +
                             std::to_string(step.bits) + ")");
                emitLine(out_, indent_, "if " + err + " < 0 {");
                emitLine(out_, indent_ + 1, "return " + err + ", 0");
                emitLine(out_, indent_, "}");
                owner_.trace(EmitTraceOp::Advance, step.bits);
                emitLine(out_, indent_, "offsetBits += " + std::to_string(step.bits));
                break;
            }
            case FieldStepKind::ScalarSint: {
                std::string valueExpr = "int64(" + expr + ")";
                assert(!step.scalarHelperSymbol.empty());
                const auto helper = owner_.helperBindingName(step.scalarHelperSymbol);
                valueExpr         = helper + "(" + valueExpr + ")";

                const auto err = owner_.nextName("err");
                owner_.trace(EmitTraceOp::WriteScalarSint, step.bits);
                emitLine(out_,
                         indent_,
                         err + " := dsdlruntime.SetIxx(buffer, offsetBits, " + valueExpr + ", " +
                             std::to_string(step.bits) + ")");
                emitLine(out_, indent_, "if " + err + " < 0 {");
                emitLine(out_, indent_ + 1, "return " + err + ", 0");
                emitLine(out_, indent_, "}");
                owner_.trace(EmitTraceOp::Advance, step.bits);
                emitLine(out_, indent_, "offsetBits += " + std::to_string(step.bits));
                break;
            }
            case FieldStepKind::ScalarFloat: {
                const auto  floatType = std::string(step.bits == 64 ? "float64" : "float32");
                std::string valueExpr = floatType + "(" + expr + ")";
                assert(!step.scalarHelperSymbol.empty());
                const auto helper = owner_.helperBindingName(step.scalarHelperSymbol);
                valueExpr         = helper + "(" + valueExpr + ")";
                const auto err    = owner_.nextName("err");
                owner_.trace(EmitTraceOp::WriteScalarFloat, step.bits);
                if (step.bits == 16)
                {
                    emitLine(out_, indent_, err + " := dsdlruntime.SetF16(buffer, offsetBits, " + valueExpr + ")");
                }
                else if (step.bits == 32)
                {
                    emitLine(out_, indent_, err + " := dsdlruntime.SetF32(buffer, offsetBits, " + valueExpr + ")");
                }
                else
                {
                    emitLine(out_, indent_, err + " := dsdlruntime.SetF64(buffer, offsetBits, " + valueExpr + ")");
                }
                emitLine(out_, indent_, "if " + err + " < 0 {");
                emitLine(out_, indent_ + 1, "return " + err + ", 0");
                emitLine(out_, indent_, "}");
                owner_.trace(EmitTraceOp::Advance, step.bits);
                emitLine(out_, indent_, "offsetBits += " + std::to_string(step.bits));
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
                emitLine(out_, indent_, expr + " = dsdlruntime.GetBit(buffer, offsetBits)");
                owner_.trace(EmitTraceOp::Advance, 1);
                emitLine(out_, indent_, "offsetBits += 1");
                break;
            case FieldStepKind::ScalarUint: {
                assert(!step.scalarHelperSymbol.empty());
                const auto helper = owner_.helperBindingName(step.scalarHelperSymbol);
                const auto raw    = owner_.nextName("raw");
                owner_.trace(EmitTraceOp::ReadScalarUint, step.bits);
                emitLine(out_,
                         indent_,
                         raw + " := uint64(dsdlruntime.GetU64(buffer, offsetBits, " + std::to_string(step.bits) +
                             "))");
                emitLine(out_,
                         indent_,
                         expr + " = " + unsignedStorageType(static_cast<std::uint32_t>(step.bits)) + "(" + helper +
                             "(" + raw + "))");
                owner_.trace(EmitTraceOp::Advance, step.bits);
                emitLine(out_, indent_, "offsetBits += " + std::to_string(step.bits));
                break;
            }
            case FieldStepKind::ScalarSint: {
                assert(!step.scalarHelperSymbol.empty());
                const auto helper = owner_.helperBindingName(step.scalarHelperSymbol);
                const auto raw    = owner_.nextName("raw");
                owner_.trace(EmitTraceOp::ReadScalarSint, step.bits);
                emitLine(out_,
                         indent_,
                         raw + " := int64(dsdlruntime.GetU64(buffer, offsetBits, " + std::to_string(step.bits) +
                             "))");
                emitLine(out_,
                         indent_,
                         expr + " = " + signedStorageType(static_cast<std::uint32_t>(step.bits)) + "(" + helper +
                             "(" + raw + "))");
                owner_.trace(EmitTraceOp::Advance, step.bits);
                emitLine(out_, indent_, "offsetBits += " + std::to_string(step.bits));
                break;
            }
            case FieldStepKind::ScalarFloat: {
                assert(!step.scalarHelperSymbol.empty());
                const auto helper = owner_.helperBindingName(step.scalarHelperSymbol);
                owner_.trace(EmitTraceOp::ReadScalarFloat, step.bits);
                if (step.bits == 16)
                {
                    emitLine(out_,
                             indent_,
                             expr + " = float32(" + helper + "(float32(dsdlruntime.GetF16(buffer, offsetBits))))");
                }
                else if (step.bits == 32)
                {
                    emitLine(out_,
                             indent_,
                             expr + " = float32(" + helper + "(float32(dsdlruntime.GetF32(buffer, offsetBits))))");
                }
                else
                {
                    emitLine(out_, indent_, expr + " = " + helper + "(dsdlruntime.GetF64(buffer, offsetBits))");
                }
                owner_.trace(EmitTraceOp::Advance, step.bits);
                emitLine(out_, indent_, "offsetBits += " + std::to_string(step.bits));
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
            const auto validateHelper = owner_.helperBindingName(step.arrayHelpers->validateSymbol);
            const auto validateRc     = owner_.nextName("lenRc");
            owner_.trace(EmitTraceOp::LenValidate, step.prefixBits);
            emitLine(out_, indent_, validateRc + " := " + validateHelper + "(int64(len(" + expr + ")))");
            emitLine(out_, indent_, "if " + validateRc + " < 0 {");
            emitLine(out_, indent_ + 1, "return " + validateRc + ", 0");
            emitLine(out_, indent_, "}");

            std::string prefixExpr = "uint64(len(" + expr + "))";
            assert(!step.arrayHelpers->prefixSymbol.empty());
            const auto serPrefixHelper = owner_.helperBindingName(step.arrayHelpers->prefixSymbol);
            prefixExpr                 = serPrefixHelper + "(" + prefixExpr + ")";
            const auto err             = owner_.nextName("err");
            owner_.trace(EmitTraceOp::LenWrite, step.prefixBits);
            emitLine(out_,
                     indent_,
                     err + " := dsdlruntime.SetUxx(buffer, offsetBits, " + prefixExpr + ", " +
                         std::to_string(step.prefixBits) + ")");
            emitLine(out_, indent_, "if " + err + " < 0 {");
            emitLine(out_, indent_ + 1, "return " + err + ", 0");
            emitLine(out_, indent_, "}");
            owner_.trace(EmitTraceOp::Advance, step.prefixBits);
            emitLine(out_, indent_, "offsetBits += " + std::to_string(step.prefixBits));
        }

        std::string spellVariableArrayLenDeserialize(const FieldEmitStep& step, const std::string& expr) override
        {
            const auto count    = owner_.nextName("count");
            const auto rawCount = owner_.nextName("countRaw");
            owner_.trace(EmitTraceOp::LenRead, step.prefixBits);
            emitLine(out_,
                     indent_,
                     rawCount + " := dsdlruntime.GetU64(buffer, offsetBits, " + std::to_string(step.prefixBits) +
                         ")");
            owner_.trace(EmitTraceOp::Advance, step.prefixBits);
            emitLine(out_, indent_, "offsetBits += " + std::to_string(step.prefixBits));
            std::string countExpr = "int(" + rawCount + ")";
            assert(step.arrayHelpers.has_value());
            assert(!step.arrayHelpers->prefixSymbol.empty());
            const auto deserPrefixHelper = owner_.helperBindingName(step.arrayHelpers->prefixSymbol);
            countExpr                    = "int(" + deserPrefixHelper + "(" + rawCount + "))";
            emitLine(out_, indent_, count + " := " + countExpr);
            assert(!step.arrayHelpers->validateSymbol.empty());
            const auto validateHelper = owner_.helperBindingName(step.arrayHelpers->validateSymbol);
            const auto validateRc     = owner_.nextName("lenRc");
            owner_.trace(EmitTraceOp::LenValidate, step.prefixBits);
            emitLine(out_, indent_, validateRc + " := " + validateHelper + "(int64(" + count + "))");
            emitLine(out_, indent_, "if " + validateRc + " < 0 {");
            emitLine(out_, indent_ + 1, "return " + validateRc + ", 0");
            emitLine(out_, indent_, "}");
            const auto itemType = goBaseFieldType(step.children.front().type,
                                                  owner_.ctx_,
                                                  owner_.currentPackagePath_,
                                                  owner_.importAliases_);
            emitLine(out_, indent_, expr + " = make([]" + itemType + ", " + count + ")");
            return count;
        }

        std::string spellFixedArrayCountDeserialize(const FieldEmitStep& step, const std::string& expr) override
        {
            (void) expr;
            const auto count = owner_.nextName("count");
            emitLine(out_, indent_, count + " := " + std::to_string(step.capacity));
            return count;
        }

        std::string spellBeginElemLoopSerialize(const FieldEmitStep& step, const std::string& expr) override
        {
            const auto index = owner_.nextName("index");
            const auto count = step.kind == FieldStepKind::VariableArray ? "len(" + expr + ")"
                                                                         : std::to_string(step.capacity);
            owner_.trace(EmitTraceOp::ElemLoop);
            emitLine(out_, indent_, "for " + index + " := 0; " + index + " < " + count + "; " + index + "++ {");
            ++indent_;
            return expr + "[" + index + "]";
        }

        std::string spellBeginElemLoopDeserialize(const FieldEmitStep& step,
                                                  const std::string&   expr,
                                                  const std::string&   countExpr) override
        {
            (void) step;
            const auto index = owner_.nextName("index");
            owner_.trace(EmitTraceOp::ElemLoop);
            emitLine(out_,
                     indent_,
                     "for " + index + " := 0; " + index + " < " + countExpr + "; " + index + "++ {");
            ++indent_;
            return expr + "[" + index + "]";
        }

        void spellEndElemLoopSerialize(const FieldEmitStep& step, const std::string& expr) override
        {
            (void) step;
            (void) expr;
            --indent_;
            emitLine(out_, indent_, "}");
        }

        void spellEndElemLoopDeserialize(const FieldEmitStep& step, const std::string& expr) override
        {
            (void) step;
            (void) expr;
            --indent_;
            emitLine(out_, indent_, "}");
        }

        void spellCompositeSerialize(const FieldEmitStep& step, const std::string& expr) override
        {
            owner_.trace(step.type.compositeSealed ? EmitTraceOp::CompositeInline
                                                   : EmitTraceOp::CompositeDelimHeader);
            const auto sizeVar  = owner_.nextName("sizeBytes");
            const auto maxBytes = (step.type.bitLengthSet.max() + 7) / 8;
            if (!step.type.compositeSealed)
            {
                emitLine(out_, indent_, "offsetBits += 32");
            }
            emitLine(out_, indent_, sizeVar + " := " + std::to_string(maxBytes));
            if (!step.type.compositeSealed)
            {
                const auto remainingVar = owner_.nextName("remaining");
                emitLine(out_,
                         indent_,
                         remainingVar + " := len(buffer) - dsdlruntime.ChooseMin(offsetBits/8, "
                                        "len(buffer))");
                assert(!step.delimiterValidateSymbol.empty());
                const auto helper     = owner_.helperBindingName(step.delimiterValidateSymbol);
                const auto validateRc = owner_.nextName("rc");
                emitLine(out_,
                         indent_,
                         validateRc + " := " + helper + "(int64(" + sizeVar + "), int64(" + remainingVar + "))");
                emitLine(out_, indent_, "if " + validateRc + " < 0 {");
                emitLine(out_, indent_ + 1, "return " + validateRc + ", 0");
                emitLine(out_, indent_, "}");
            }
            const auto startVar = owner_.nextName("start");
            const auto endVar   = owner_.nextName("end");
            emitLine(out_, indent_, startVar + " := dsdlruntime.ChooseMin(offsetBits/8, len(buffer))");
            emitLine(out_,
                     indent_,
                     endVar + " := dsdlruntime.ChooseMin(" + startVar + "+" + sizeVar + ", len(buffer))");
            const auto rcVar       = owner_.nextName("rc");
            const auto consumedVar = owner_.nextName("consumed");
            emitLine(out_,
                     indent_,
                     rcVar + ", " + consumedVar + " := " + expr + ".Serialize(buffer[" + startVar + ":" + endVar +
                         "])");
            emitLine(out_, indent_, "if " + rcVar + " < 0 {");
            emitLine(out_, indent_ + 1, "return " + rcVar + ", 0");
            emitLine(out_, indent_, "}");
            emitLine(out_, indent_, sizeVar + " = " + consumedVar);
            if (!step.type.compositeSealed)
            {
                const auto hdrErr = owner_.nextName("err");
                emitLine(out_,
                         indent_,
                         hdrErr + " := dsdlruntime.SetUxx(buffer, offsetBits-32, uint64(" + sizeVar + "), 32)");
                emitLine(out_, indent_, "if " + hdrErr + " < 0 {");
                emitLine(out_, indent_ + 1, "return " + hdrErr + ", 0");
                emitLine(out_, indent_, "}");
            }
            emitLine(out_, indent_, "offsetBits += " + sizeVar + " * 8");
        }

        void spellCompositeDeserialize(const FieldEmitStep& step, const std::string& expr) override
        {
            owner_.trace(step.type.compositeSealed ? EmitTraceOp::CompositeInline
                                                   : EmitTraceOp::CompositeDelimHeader);
            if (!step.type.compositeSealed)
            {
                const auto sizeVar = owner_.nextName("sizeBytes");
                emitLine(out_, indent_, sizeVar + " := int(dsdlruntime.GetU32(buffer, offsetBits, 32))");
                emitLine(out_, indent_, "offsetBits += 32");
                const auto remainingVar = owner_.nextName("remaining");
                emitLine(out_,
                         indent_,
                         remainingVar + " := capacityBytes - dsdlruntime.ChooseMin(offsetBits/8, "
                                        "capacityBytes)");
                assert(!step.delimiterValidateSymbol.empty());
                const auto helper     = owner_.helperBindingName(step.delimiterValidateSymbol);
                const auto validateRc = owner_.nextName("rc");
                emitLine(out_,
                         indent_,
                         validateRc + " := " + helper + "(int64(" + sizeVar + "), int64(" + remainingVar + "))");
                emitLine(out_, indent_, "if " + validateRc + " < 0 {");
                emitLine(out_, indent_ + 1, "return " + validateRc + ", 0");
                emitLine(out_, indent_, "}");
                const auto startVar = owner_.nextName("start");
                const auto endVar   = owner_.nextName("end");
                emitLine(out_, indent_, startVar + " := dsdlruntime.ChooseMin(offsetBits/8, len(buffer))");
                emitLine(out_,
                         indent_,
                         endVar + " := dsdlruntime.ChooseMin(" + startVar + "+" + sizeVar + ", len(buffer))");
                const auto rcVar       = owner_.nextName("rc");
                const auto consumedVar = owner_.nextName("consumed");
                emitLine(out_,
                         indent_,
                         rcVar + ", " + consumedVar + " := " + expr + ".Deserialize(buffer[" + startVar + ":" +
                             endVar + "])");
                emitLine(out_, indent_, "_ = " + consumedVar);
                emitLine(out_, indent_, "if " + rcVar + " < 0 {");
                emitLine(out_, indent_ + 1, "return " + rcVar + ", 0");
                emitLine(out_, indent_, "}");
                emitLine(out_, indent_, "offsetBits += " + sizeVar + " * 8");
                return;
            }

            const auto startVar = owner_.nextName("start");
            emitLine(out_, indent_, startVar + " := dsdlruntime.ChooseMin(offsetBits/8, len(buffer))");
            const auto rcVar       = owner_.nextName("rc");
            const auto consumedVar = owner_.nextName("consumed");
            emitLine(out_,
                     indent_,
                     rcVar + ", " + consumedVar + " := " + expr + ".Deserialize(buffer[" + startVar +
                         ":len(buffer)])");
            emitLine(out_, indent_, "if " + rcVar + " < 0 {");
            emitLine(out_, indent_ + 1, "return " + rcVar + ", 0");
            emitLine(out_, indent_, "}");
            emitLine(out_, indent_, "offsetBits += " + consumedVar + " * 8");
        }

    private:
        FunctionBodyEmitter&         owner_;
        std::ostringstream&          out_;
        int                          indent_;
        const HelperBindingDirection direction_;
    };

    void emitSerializeAny(std::ostringstream&          out,
                          const SemanticFieldType&     type,
                          const std::string&           expr,
                          int                          indent,
                          std::optional<std::uint32_t> arrayLengthPrefixBitsOverride = std::nullopt,
                          const LoweredFieldFacts*     fieldFacts                    = nullptr)
    {
        const auto steps =
            buildFieldEmitSteps(type, fieldFacts, arrayLengthPrefixBitsOverride, HelperBindingDirection::Serialize);
        FieldSpelling spelling(*this, out, indent, HelperBindingDirection::Serialize);
        renderFieldSteps(steps, expr, HelperBindingDirection::Serialize, spelling);
    }

    void emitDeserializeAny(std::ostringstream&          out,
                            const SemanticFieldType&     type,
                            const std::string&           expr,
                            int                          indent,
                            std::optional<std::uint32_t> arrayLengthPrefixBitsOverride = std::nullopt,
                            const LoweredFieldFacts*     fieldFacts                    = nullptr)
    {
        const auto steps =
            buildFieldEmitSteps(type, fieldFacts, arrayLengthPrefixBitsOverride, HelperBindingDirection::Deserialize);
        FieldSpelling spelling(*this, out, indent, HelperBindingDirection::Deserialize);
        renderFieldSteps(steps, expr, HelperBindingDirection::Deserialize, spelling);
    }
};

void emitSectionType(std::ostringstream&                       out,
                     const EmitterContext&                     ctx,
                     const std::string&                        typeName,
                     const std::string&                        fullName,
                     std::uint32_t                             majorVersion,
                     std::uint32_t                             minorVersion,
                     const SemanticSection&                    section,
                     const AttachedDoc&                        typeDoc,
                     const std::string&                        currentPackagePath,
                     const std::map<std::string, std::string>& importAliases,
                     const LoweredSectionFacts*                sectionFacts)
{
    const auto typeConstPrefix = codegenToUpperSnakeCaseIdentifier(CodegenNamingLanguage::Go, typeName);
    emitLine(out, 0, "const " + typeConstPrefix + "_FULL_NAME = \"" + fullName + "\"");
    emitLine(out,
             0,
             "const " + typeConstPrefix + "_FULL_NAME_AND_VERSION = \"" + fullName + "." +
                 std::to_string(majorVersion) + "." + std::to_string(minorVersion) + "\"");
    emitLine(out,
             0,
             "const " + typeConstPrefix + "_EXTENT_BYTES = " + std::to_string(section.extentBits.value_or(0) / 8));
    emitLine(out,
             0,
             "const " + typeConstPrefix +
                 "_SERIALIZATION_BUFFER_SIZE_BYTES = " + std::to_string((section.serializationBufferSizeBits + 7) / 8));
    const bool zohAliasEligible = sectionFacts != nullptr && sectionFacts->zohAliasEligible;
    const std::string zohAliasReason =
        (sectionFacts != nullptr && !sectionFacts->zohAliasReason.empty()) ? sectionFacts->zohAliasReason : "not-proven";
    emitLine(out,
             0,
             "const " + typeConstPrefix + "_ZOH_ALIAS_ELIGIBLE = " + std::string(zohAliasEligible ? "true" : "false"));
    emitLine(out, 0, "const " + typeConstPrefix + "_ZOH_ALIAS_REASON = \"" + zohAliasReason + "\"");

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
        emitLine(out, 0, "const " + typeConstPrefix + "_UNION_OPTION_COUNT = " + std::to_string(optionCount));
    }

    for (const auto& c : section.constants)
    {
        emitAttachedDocGo(out, 0, c.doc);
        emitLine(out,
                 0,
                 "const " + typeConstPrefix + "_" +
                     codegenToUpperSnakeCaseIdentifier(CodegenNamingLanguage::Go, c.name) + " = " +
                     goConstValue(c.type, c.value));
    }
    out << "\n";

    emitAttachedDocGo(out, 0, typeDoc);
    emitLine(out, 0, "type " + typeName + " struct {");
    const CodegenIdentifierAllocator fieldIdents = makeExportedFieldIdents(section);
    for (const auto& field : section.fields)
    {
        if (field.isPadding)
        {
            continue;
        }
        emitAttachedDocGo(out, 1, field.doc);
        emitLine(out,
                 1,
                 fieldIdents.get(field.name) + " " +
                     goFieldType(field.resolvedType, ctx, currentPackagePath, importAliases));
    }
    if (section.isUnion)
    {
        // Tag storage must match the wire tag width (uint8 for <=256 options, uint16 for
        // 257..65536, etc.); a hardcoded uint8 truncates a wide tag and mis-dispatches.
        emitLine(out, 1, "Tag " + unsignedStorageType(resolveUnionTagBits(section, sectionFacts)));
    }
    if (section.fields.empty())
    {
        emitLine(out, 1, "_ uint8");
    }
    emitLine(out, 0, "}");
    out << "\n";

    const auto canonicalSectionName =
        fullName + "." + std::to_string(majorVersion) + "." + std::to_string(minorVersion);
    FunctionBodyEmitter body(ctx, currentPackagePath, importAliases);
    ctx.traceSection(canonicalSectionName, EmitTraceDirection::Serialize);
    body.emitSerializeFunction(out, typeName, section, sectionFacts);
    out << "\n";
    ctx.traceSection(canonicalSectionName, EmitTraceDirection::Deserialize);
    body.emitDeserializeFunction(out, typeName, section, sectionFacts);
}

std::string renderDefinitionFile(const SemanticDefinition& def,
                                 const EmitterContext&     ctx,
                                 const std::string&        moduleName,
                                 const LoweredFactsMap&    loweredFacts)
{
    const auto currentPackagePath = ctx.packagePath(def.info);
    const auto packageName        = packageNameFromPath(currentPackagePath);
    const auto imports            = computeImportAliases(def, ctx);

    std::ostringstream out;
    emitLine(out, 0, generatedCommentLine("Go backend"));
    emitLine(out,
             0,
             "// Source: " + def.info.fullName + "." + std::to_string(def.info.majorVersion) + "." +
                 std::to_string(def.info.minorVersion));
    out << "\n";
    emitLine(out, 0, "package " + packageName);
    out << "\n";

    emitLine(out, 0, "import (");
    emitLine(out, 1, "dsdlruntime \"" + moduleName + "/dsdlruntime\"");
    for (const auto& [path, alias] : imports)
    {
        emitLine(out, 1, alias + " \"" + moduleName + "/" + path + "\"");
    }
    emitLine(out, 0, ")");
    out << "\n";

    const auto baseType = ctx.goTypeName(def.info);
    if (!def.isService)
    {
        emitSectionType(out,
                        ctx,
                        baseType,
                        def.info.fullName,
                        def.info.majorVersion,
                        def.info.minorVersion,
                        def.request,
                        def.doc,
                        currentPackagePath,
                        imports,
                        lookupLoweredSectionFacts(loweredFacts, def, ""));
        return out.str();
    }

    const auto reqType  = baseType + "_Request";
    const auto respType = baseType + "_Response";
    emitSectionType(out,
                    ctx,
                    reqType,
                    def.info.fullName + ".Request",
                    def.info.majorVersion,
                    def.info.minorVersion,
                    def.request,
                    def.doc,
                    currentPackagePath,
                    imports,
                    lookupLoweredSectionFacts(loweredFacts, def, "request"));
    out << "\n";
    if (def.response)
    {
        emitSectionType(out,
                        ctx,
                        respType,
                        def.info.fullName + ".Response",
                        def.info.majorVersion,
                        def.info.minorVersion,
                        *def.response,
                        def.doc,
                        currentPackagePath,
                        imports,
                        lookupLoweredSectionFacts(loweredFacts, def, "response"));
        out << "\n";
    }
    emitLine(out, 0, "type " + baseType + " = " + reqType);
    const auto baseConstPrefix = codegenToUpperSnakeCaseIdentifier(CodegenNamingLanguage::Go, baseType);
    const auto reqConstPrefix  = codegenToUpperSnakeCaseIdentifier(CodegenNamingLanguage::Go, reqType);
    emitLine(out, 0, "const " + baseConstPrefix + "_ZOH_ALIAS_ELIGIBLE = " + reqConstPrefix + "_ZOH_ALIAS_ELIGIBLE");
    emitLine(out, 0, "const " + baseConstPrefix + "_ZOH_ALIAS_REASON = " + reqConstPrefix + "_ZOH_ALIAS_REASON");
    return out.str();
}

llvm::Expected<std::string> loadGoRuntime()
{
    const std::filesystem::path runtimePath =
        std::filesystem::path(LLVMDSDL_SOURCE_DIR) / "runtime" / "go" / "dsdl_runtime.go";
    std::ifstream in(runtimePath.string());
    if (!in)
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "failed to read Go runtime");
    }
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
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
    const auto mlirCoverageDiagnostic = codegen_diagnostic_text::mlirSchemaCoverageValidationFailedForEmission("Go");
    LoweredFactsMap loweredFacts;
    if (!collectLoweredFactsFromMlir(semantic, module, diagnostics, "Go", &loweredFacts, options.optimizeLoweredSerDes))
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "%s", mlirCoverageDiagnostic.c_str());
    }

    std::filesystem::path outRoot(options.outDir);
    const auto            selectedTypeKeys = makeTypeKeySet(options.selectedTypeKeys);

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

    EmitterContext ctx(semantic);
    ctx.setTraceSink(traceSink);

    for (const auto& def : semantic.definitions)
    {
        if (!shouldEmitDefinition(def.info, selectedTypeKeys))
        {
            continue;
        }
        const std::vector<std::string> requiredTypeKeys{definitionTypeKey(def.info)};

        const auto            dirRel = ctx.packagePath(def.info);
        std::filesystem::path dir    = outRoot;
        if (!dirRel.empty())
        {
            dir /= dirRel;
        }
        if (auto err = writeGeneratedFile(dir / ctx.goFileName(def.info),
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
