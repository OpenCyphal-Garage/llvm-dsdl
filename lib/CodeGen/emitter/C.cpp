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
#include "llvmdsdl/Support/PlanSymbol.h"
#include <map>
#include <optional>
#include <mlir/IR/SymbolTable.h>
#include <llvm/ADT/SmallVector.h>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <variant>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <llvm/ADT/STLExtras.h>
#include <llvm/Support/ErrorHandling.h>
#include <mlir/IR/BuiltinAttributeInterfaces.h>
#include <mlir/IR/BuiltinTypes.h>
#include <utility>
#include "llvmdsdl/IR/DSDLTypes.h"
#include "llvmdsdl/CodeGen/BodyTranslator.h"
#include "llvmdsdl/CodeGen/SourceWriter.h"
#include "llvmdsdl/CodeGen/TypeStorage.h"
#include "llvmdsdl/Transforms/PlanSteps.h"
#include "llvmdsdl/CodeGen/emitter/CHeaderRender.h"
#include "llvmdsdl/CodeGen/emitter/CIncludes.h"
#include "llvmdsdl/CodeGen/ImportSet.h"
#include "llvmdsdl/CodeGen/ConstantLiteralRender.h"
#include "llvmdsdl/CodeGen/DefinitionDependencies.h"
#include "llvmdsdl/CodeGen/DefinitionIndex.h"
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
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Evaluator.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Version.h"
#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/GeneratedFact.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/SurfacePlan.h"
#include "llvmdsdl/Transforms/SurfaceTree.h"
#include "mlir/IR/BuiltinAttributes.h"

namespace llvmdsdl::emitter::c
{
namespace
{

/// @brief The symbol of the function the pipeline lowers @p function of @p def's @p section to.
/// @param[in] member The member an accessor reaches; empty for any other function.
std::string loweredSymbol(const SemanticDefinition& def,
                          const llvm::StringRef     section,
                          const PlanFunction        function,
                          const llvm::StringRef     member = {})
{
    PlanSymbol symbol =
        planFunction(def.info.fullName, def.info.majorVersion, def.info.minorVersion, section, function);
    symbol.member = member.str();
    return renderPlanSymbol(symbol);
}

/// @brief The names C's output declares and the files it writes, as the surface declares them.
class CSurface final
{
public:
    explicit CSurface(const SurfaceTree& tree)
        : tree_(tree)
    {
    }

    [[nodiscard]] const SurfaceTree& tree() const
    {
        return tree_;
    }

    /// @brief The file scope the definition keyed @p key is declared in.
    [[nodiscard]] std::size_t file(const llvm::StringRef key) const
    {
        return tree_.definitionScope(key);
    }

    /// @brief The header the definition keyed @p key is declared in, from the output directory.
    [[nodiscard]] const std::string& header(const llvm::StringRef key) const
    {
        return tree_.scope(file(key)).path;
    }

    /// @brief The file beside that header whose name ends in @p extension instead.
    [[nodiscard]] std::string besideHeader(const llvm::StringRef key, const llvm::StringRef extension) const
    {
        const std::string& path = header(key);
        return path.substr(0, path.size() - languageTraits(Language::C).composition.fileExtension.size()) +
               extension.str();
    }

    /// @brief The name of @p section's type.
    [[nodiscard]] const std::string& typeName(const llvm::StringRef key, const llvm::StringRef section) const
    {
        return tree_.scope(tree_.typeScope(key, section)).name;
    }

    /// @brief @p section's structure tag, as C spells a use of it.
    [[nodiscard]] std::string tag(const llvm::StringRef key, const llvm::StringRef section) const
    {
        return renderCTagSpelling(declared(key, SurfaceDeclKind::Tag, section));
    }

    /// @brief The name of the declaration of @p kind in the definition's file for @p member of
    ///        @p section, stating @p fact.
    [[nodiscard]] const std::string& declared(const llvm::StringRef              key,
                                              const SurfaceDeclKind              kind,
                                              const llvm::StringRef              section,
                                              const llvm::StringRef              member = {},
                                              const std::optional<GeneratedFact> fact   = std::nullopt) const
    {
        return tree_.nameOf(file(key), kind, SurfaceEntity{key.str(), section.str(), member.str(), {}}, fact);
    }

    /// @brief The name of the data member of @p section's type that @p member is, or that states
    ///        @p fact; empty where the type declares none, as in an accessors-only run.
    [[nodiscard]] std::string member(const llvm::StringRef              key,
                                     const llvm::StringRef              section,
                                     const llvm::StringRef              member,
                                     const std::optional<GeneratedFact> fact = std::nullopt) const
    {
        const SurfaceDecl* const decl = tree_.find(tree_.typeScope(key, section),
                                                   SurfaceDeclKind::Field,
                                                   SurfaceEntity{key.str(), section.str(), member.str(), {}},
                                                   fact);
        return (decl != nullptr) ? decl->name : std::string{};
    }

    /// @brief The name the lowered function @p symbol is linked under.
    [[nodiscard]] const std::string& linkName(const llvm::StringRef symbol) const
    {
        const std::optional<PlanSymbol> plan = parsePlanSymbol(symbol);
        if (!plan)
        {
            llvm::report_fatal_error(llvm::Twine("C: a function the plan grammar does not name: ") + symbol);
        }
        return tree_.nameOf(symbol, loweredFunctionKind(plan->function));
    }

    /// @brief The name of the function that wraps the lowered function @p symbol.
    [[nodiscard]] const std::string& wrapper(const llvm::StringRef symbol) const
    {
        return tree_.nameOf(symbol, SurfaceDeclKind::Wrapper);
    }

private:
    const SurfaceTree& tree_;
};

/// @brief Renames every lowered function in @p module, and every reference to one, to its C link name.
///
/// An object is linked against the header's declarations, which name the functions as C spells
/// them; a nested body the object calls is declared under the same name its own object defines.
void renameToCLinkNames(mlir::ModuleOp module, const CSurface& names)
{
    std::map<std::string, std::string> renames;
    const auto                         consider = [&](const llvm::StringRef name) {
        if (parsePlanSymbol(name))
        {
            renames.emplace(name.str(), names.linkName(name));
        }
    };
    for (mlir::func::FuncOp fn : module.getOps<mlir::func::FuncOp>())
    {
        consider(fn.getSymName());
    }
    if (const auto uses = mlir::SymbolTable::getSymbolUses(&module.getBodyRegion()))
    {
        for (const mlir::SymbolTable::SymbolUse& use : *uses)
        {
            consider(use.getSymbolRef().getRootReference().getValue());
        }
    }
    mlir::MLIRContext* const context = module.getContext();
    for (const auto& [from, to] : renames)
    {
        const auto fromName = mlir::StringAttr::get(context, from);
        const auto toName   = mlir::StringAttr::get(context, to);
        (void) mlir::SymbolTable::replaceAllSymbolUses(fromName, toName, module);
        if (auto fn = module.lookupSymbol<mlir::func::FuncOp>(fromName))
        {
            mlir::SymbolTable::setSymbolName(fn, toName);
        }
    }
}

std::string valueToCExpr(const TypeExprAST& type, const Value& value)
{
    return renderConstantLiteral(Language::C, value, makeConstantTypeInfo(type));
}

std::string unsignedStorageType(const std::uint32_t bitLength, const CFileNames& file)
{
    return file.standard(renderUnsignedStorageToken(Language::C, bitLength));
}

std::string signedStorageType(const std::uint32_t bitLength, const CFileNames& file)
{
    return file.standard(renderSignedStorageToken(Language::C, bitLength));
}

class EmitterContext final
{
public:
    EmitterContext(const SemanticModule& semantic,
                   const CSurface&       names,
                   const bool            emitDeprecationAttributes,
                   const bool            hostImageFolded,
                   const bool            accessorsOnly)
        : index_(semantic)
        , names_(names)
        , emitDeprecationAttributes_(emitDeprecationAttributes)
        , hostImageFolded_(hostImageFolded)
        , accessorsOnly_(accessorsOnly)
    {
    }

    /// @brief The names the output declares.
    const CSurface& names() const
    {
        return names_;
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

private:
    DefinitionIndex index_;
    const CSurface& names_;
    bool            emitDeprecationAttributes_{false};
    bool            hostImageFolded_{false};
    bool            accessorsOnly_{false};
};

/// @brief The key of the definition @p def, which its schema and the surface name it by.
std::string keyOf(const SemanticDefinition& def)
{
    return renderDefinitionKey(definitionRef(def.info));
}

/// @brief The key of the definition @p ref names.
std::string keyOf(const SemanticTypeRef& ref)
{
    return renderDefinitionKey(definitionRef(ref));
}

/// @brief One section's names, as the surface declares them.
class CSection final
{
public:
    CSection(const CSurface& names, std::string key, std::string section)
        : names_(names)
        , key_(std::move(key))
        , section_(std::move(section))
    {
    }

    /// @brief The name of the section's type.
    [[nodiscard]] const std::string& typeName() const
    {
        return names_.typeName(key_, section_);
    }

    /// @brief The section's structure tag, as C spells a use of it.
    [[nodiscard]] std::string tag() const
    {
        return names_.tag(key_, section_);
    }

    /// @brief The name of the data member that the DSDL field @p member is; empty where the type
    ///        declares none.
    [[nodiscard]] std::string field(const llvm::StringRef member) const
    {
        return names_.member(key_, section_, member);
    }

    /// @brief The name of the data member the generator adds to state @p fact; empty where it adds
    ///        none.
    [[nodiscard]] std::string dataMember(const GeneratedFact fact) const
    {
        return names_.member(key_, section_, {}, fact);
    }

    /// @brief The name of the macro holding the tag value of the union option @p member.
    [[nodiscard]] const std::string& option(const llvm::StringRef member) const
    {
        return names_.declared(key_, SurfaceDeclKind::Option, section_, member);
    }

    /// @brief The name of the macro the DSDL constant @p member is, or that states @p fact of it.
    [[nodiscard]] const std::string& constant(const llvm::StringRef              member,
                                              const std::optional<GeneratedFact> fact = std::nullopt) const
    {
        return names_.declared(key_, SurfaceDeclKind::Constant, section_, member, fact);
    }

    /// @brief The name of the macro stating @p fact of the section.
    [[nodiscard]] const std::string& generated(const GeneratedFact fact) const
    {
        return names_.declared(key_, SurfaceDeclKind::Constant, section_, {}, fact);
    }

    /// @brief The name of the function stating @p fact of the union option @p member.
    [[nodiscard]] const std::string& optionFunction(const llvm::StringRef member, const GeneratedFact fact) const
    {
        return names_.declared(key_, SurfaceDeclKind::Method, section_, member, fact);
    }

    [[nodiscard]] const std::string& key() const
    {
        return key_;
    }

    [[nodiscard]] const std::string& section() const
    {
        return section_;
    }

private:
    const CSurface& names_;
    std::string     key_;
    std::string     section_;
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

std::string cTypeFromFieldType(const SemanticFieldType& type, const EmitterContext& ctx, const CFileNames& file)
{
    switch (type.scalarCategory)
    {
    case SemanticScalarCategory::Bool:
        return file.standard("bool");
    case SemanticScalarCategory::Byte:
    case SemanticScalarCategory::Utf8:
    case SemanticScalarCategory::UnsignedInt:
        return unsignedStorageType(type.bitLength, file);
    case SemanticScalarCategory::SignedInt:
        return signedStorageType(type.bitLength, file);
    case SemanticScalarCategory::Float:
        if (type.bitLength == 64U)
        {
            return "double";
        }
        return "float";
    case SemanticScalarCategory::Void:
        return file.standard("uint8_t");
    case SemanticScalarCategory::Composite:
        if (type.compositeType)
        {
            const auto*       nested = ctx.find(*type.compositeType);
            const std::string key    = (nested != nullptr) ? keyOf(*nested) : keyOf(*type.compositeType);
            return file.declaredIn(ctx.names().header(key), ctx.names().tag(key, {}));
        }
        return file.standard("uint8_t");
    }
    return file.standard("uint8_t");
}

/// @brief Declares the tag value that selects each of a union's options.
void emitUnionOptionTagMacros(SourceWriter& w, const CSection& names, const SectionMetadata& metadata)
{
    if (!metadata.isUnion)
    {
        return;
    }
    for (const auto& option : metadata.unionOptions)
    {
        w.line("#define " + names.option(option.name) + " " + std::to_string(option.tag) + "U");
    }
    w.blank();
}

void emitArrayMacros(SourceWriter& w, const CSection& names, const SemanticSection& section, const CFileNames& file)
{
    for (const auto& field : section.fields)
    {
        if (field.isPadding || field.resolvedType.arrayKind == ArrayKind::None)
        {
            continue;
        }
        w.line("#define " + names.constant(field.name, GeneratedFact::ArrayCapacity) + " " +
               std::to_string(field.resolvedType.arrayCapacity) + "U");
        w.line("#define " + names.constant(field.name, GeneratedFact::ArrayIsVariableLength) + " " +
               (isVariableArray(field.resolvedType.arrayKind) ? file.standard("true") : file.standard("false")));
    }
    if (!section.fields.empty())
    {
        w.blank();
    }
}

void emitSectionTypedef(SourceWriter&                         w,
                        const CSection&                       names,
                        const SemanticSection&                section,
                        const SectionMetadata&                metadata,
                        const EmitterContext&                 ctx,
                        const bool                            deprecatedAttribute,
                        const mlir::dsdl::SerializationPlanOp plan,
                        const CFileNames&                     file)
{
    const std::string& typeName = names.typeName();
    w.open("typedef " + names.tag() + " {");

    for (const auto& field : section.fields)
    {
        if (field.isPadding)
        {
            continue;
        }

        const std::string cMember = names.field(field.name);
        const bool viewMember     = std::ranges::find(metadata.viewMembers, field.name) != metadata.viewMembers.end();
        const auto baseType       = viewMember ? std::string{file.runtime("dsdl_runtime_view_t")}
                                               : cTypeFromFieldType(field.resolvedType, ctx, file);

        emitAttachedDocC(w, field.doc);
        if (viewMember)
        {
            // NOLINTNEXTLINE(performance-inefficient-string-concatenation)
            w.line("/* Held as a view: the bytes of the " +
                   ctx.names().typeName(keyOf(*field.resolvedType.compositeType), {}) +
                   " this field carries, and their count" +
                   ((field.resolvedType.arrayKind == ArrayKind::None) ? "" : ", per element") + ". */");
        }

        if (field.resolvedType.arrayKind == ArrayKind::None)
        {
            // NOLINTNEXTLINE(performance-inefficient-string-concatenation)
            w.line(baseType + " " + cMember + ";");
            continue;
        }

        if (field.resolvedType.arrayKind == ArrayKind::Fixed)
        {
            if (field.resolvedType.scalarCategory == SemanticScalarCategory::Bool)
            {
                w.line(file.standard("uint8_t") + " " + cMember + "[(" +
                       std::to_string(field.resolvedType.arrayCapacity) + "U + 7U) / 8U];");
            }
            else
            {
                // NOLINTBEGIN(performance-inefficient-string-concatenation)
                w.line(baseType + " " + cMember + "[" + std::to_string(field.resolvedType.arrayCapacity) + "U];");
                // NOLINTEND(performance-inefficient-string-concatenation)
            }
            continue;
        }

        w.open("struct {");
        if (field.resolvedType.scalarCategory == SemanticScalarCategory::Bool)
        {
            w.line(file.standard("uint8_t") + " bitpacked[(" + std::to_string(field.resolvedType.arrayCapacity) +
                   "U + 7U) / 8U];");
        }
        else
        {
            w.line(baseType + " elements[" + std::to_string(field.resolvedType.arrayCapacity) + "U];");
        }
        w.line(file.standard("size_t") + " count;");
        w.close("} " + cMember + ";");
    }

    if (const std::string tag = names.dataMember(GeneratedFact::UnionTag); !tag.empty())
    {
        // Tag storage must match the wire tag width (uint8 for <=256 options, uint16 for
        // 257..65536, etc.); a hardcoded uint8_t truncates a wide tag and mis-dispatches.
        w.line(unsignedStorageType(unionTagBits(plan), file) + " " + tag + ";");
    }

    if (const std::string placeholder = names.dataMember(GeneratedFact::Placeholder); !placeholder.empty())
    {
        w.line(file.standard("uint8_t") + " " + placeholder + ";");
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
        const std::string tag = names.tag();
        // NOLINTBEGIN(performance-inefficient-string-concatenation)
        w.line(file.runtime("DSDL_RUNTIME_STATIC_ASSERT") + "(sizeof(" + tag +
               ") == " + std::to_string(metadata.serializationBufferSizeBytes) + "U, \"" + typeName +
               ": the structure is not the byte image its serialisation assumes\");");
        for (const auto& member : metadata.hostImageMembers)
        {
            const std::string cMember = names.field(member.fieldName);
            w.line(file.runtime("DSDL_RUNTIME_STATIC_ASSERT") + "(" + file.standard("offsetof") + "(" + tag + ", " +
                   cMember + ") == " + std::to_string(member.offsetBytes) + "U, \"" + typeName + "." + cMember +
                   ": not at the offset its serialisation assumes\");");
        }
        // NOLINTEND(performance-inefficient-string-concatenation)
        w.blank();
    }

    if (metadata.isUnion)
    {
        w.line("#define " + names.generated(GeneratedFact::UnionOptionCount) + " " +
               std::to_string(metadata.unionOptions.size()) + "U");
        w.blank();
    }
}

void emitSectionConstants(SourceWriter&          w,
                          const CSection&        names,
                          const SemanticSection& section,
                          const CFileNames&      file)
{
    for (const auto& c : section.constants)
    {
        emitAttachedDocC(w, c.doc);
        const std::string literal = valueToCExpr(c.type, c.value);
        w.line("#define " + names.constant(c.name) + " (" +
               (std::holds_alternative<bool>(c.value.data) ? file.standard(literal) : literal) + ")");
    }
    if (!section.constants.empty())
    {
        w.blank();
    }
}

void emitSectionMetadata(SourceWriter&          w,
                         const CSection&        names,
                         const SectionMetadata& metadata,
                         const CFileNames&      file)
{
    for (const auto& line :
         renderTypeMetadataMacros([&](const GeneratedFact fact) { return names.generated(fact); }, metadata, file))
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
                             const CSection&        names,
                             const SectionMetadata& metadata,
                             const CFileNames&      file)
{
    if (!metadata.isUnion)
    {
        return;
    }
    const std::string objectType = names.tag();
    const std::string unionTag   = names.dataMember(GeneratedFact::UnionTag);
    for (const auto& option : metadata.unionOptions)
    {
        const std::string& tag = names.option(option.name);

        // NOLINTBEGIN(performance-inefficient-string-concatenation)
        w.line("static inline " + file.standard("bool") + " " +
               names.optionFunction(option.name, GeneratedFact::OptionTest) + "(const " + objectType + "* const obj)");
        // NOLINTEND(performance-inefficient-string-concatenation)
        w.open("{");
        // NOLINTNEXTLINE(performance-inefficient-string-concatenation)
        w.line("return (obj != " + file.standard("NULL") + ") && (obj->" + unionTag + " == " + tag + ");");
        w.close("}");
        w.blank();

        // NOLINTBEGIN(performance-inefficient-string-concatenation)
        w.line("static inline void " + names.optionFunction(option.name, GeneratedFact::OptionSelect) + "(" +
               objectType + "* const obj)");
        // NOLINTEND(performance-inefficient-string-concatenation)
        w.open("{");
        w.open("if (obj != " + file.standard("NULL") + ") {");
        // NOLINTNEXTLINE(performance-inefficient-string-concatenation)
        w.line("obj->" + unionTag + " = " + tag + ";");
        w.close("}");
        w.close("}");
        w.blank();
    }
}

void emitSection(SourceWriter&              w,
                 const EmitterContext&      ctx,
                 const CFileNames&          file,
                 const SemanticDefinition&  def,
                 const CSection&            names,
                 const SemanticSection&     section,
                 const AttachedDoc&         typeDoc,
                 const mlir::dsdl::SchemaOp schema)
{
    const std::string&                    typeName    = names.typeName();
    const std::string&                    sectionName = names.section();
    const SectionMetadata                 metadata    = sectionMetadata(def.info, section, schema, sectionName);
    const mlir::dsdl::SerializationPlanOp plan        = sectionPlan(schema, sectionName);
    emitSectionMetadata(w, names, metadata, file);
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
    emitSectionConstants(w, names, section, file);
    // The object type and its serialisation, which an accessors-only run leaves out.
    if (!ctx.accessorsOnly())
    {
        const std::string serialize          = loweredSymbol(def, sectionName, PlanFunction::Serialize);
        const std::string deserialize        = loweredSymbol(def, sectionName, PlanFunction::Deserialize);
        const std::string initialize         = loweredSymbol(def, sectionName, PlanFunction::Initialize);
        const std::string loweredSerialize   = ctx.names().linkName(serialize);
        const std::string loweredDeserialize = ctx.names().linkName(deserialize);
        const std::string loweredInitialize  = ctx.names().linkName(initialize);
        emitArrayMacros(w, names, section, file);
        emitUnionOptionTagMacros(w, names, metadata);
        emitAttachedDocC(w,
                         docWithDeprecationNotice(typeDoc,
                                                  section.deprecated,
                                                  def.info.fullName,
                                                  def.info.majorVersion,
                                                  def.info.minorVersion));
        emitSectionTypedef(w,
                           names,
                           section,
                           metadata,
                           ctx,
                           section.deprecated && ctx.emitDeprecationAttributes(),
                           plan,
                           file);

        const auto objectType = names.tag();
        w.line(file.standard("int8_t") + " " + loweredSerialize + "(const " + objectType + "* obj, " +
               file.standard("uint8_t") + "* buffer, " + file.standard("size_t") +
               "* "
               "inout_buffer_size_bytes);");
        w.line(file.standard("int8_t") + " " + loweredDeserialize + "(" + objectType + "* out_obj, const " +
               file.standard("uint8_t") + "* buffer, " + file.standard("size_t") +
               "* "
               "inout_buffer_size_bytes);");
        w.line(file.standard("int8_t") + " " + loweredInitialize + "(" + objectType + "* out_obj);");
        w.blank();

        w.line("static inline " + file.standard("int8_t") + " " + ctx.names().wrapper(serialize) + "(const " +
               objectType + "* const obj, " + file.standard("uint8_t") + "* const buffer, " + file.standard("size_t") +
               "* const "
               "inout_buffer_size_bytes)");
        w.open("{");
        w.line("return " + loweredSerialize + "(obj, buffer, inout_buffer_size_bytes);");
        w.close("}");
        w.blank();

        w.line("static inline " + file.standard("int8_t") + " " + ctx.names().wrapper(deserialize) + "(" + objectType +
               "* const out_obj, const " + file.standard("uint8_t") + "* buffer, " + file.standard("size_t") +
               "* const "
               "inout_buffer_size_bytes)");
        w.open("{");
        w.line("return " + loweredDeserialize + "(out_obj, buffer, inout_buffer_size_bytes);");
        w.close("}");
        w.blank();

        w.line("static inline " + file.standard("int8_t") + " " + ctx.names().wrapper(initialize) + "(" + objectType +
               "* const out_obj)");
        w.open("{");
        w.line("return " + loweredInitialize + "(out_obj);");
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
                              (schemaModule.lookupSymbol<mlir::func::FuncOp>(
                                   planAccessorSymbol(schema, sectionName, true, kPlanUnionTagMember)) != nullptr);
    if ((metadata.wireFlat.holds && !section.isUnion) || unionFlat)
    {
        struct Subject final
        {
            std::string       name;
            SemanticFieldType type;
        };
        std::vector<Subject> subjects;
        if (unionFlat)
        {
            SemanticFieldType tag;
            tag.scalarCategory = SemanticScalarCategory::UnsignedInt;
            tag.bitLength      = metadata.unionTagBits;
            subjects.push_back(Subject{kPlanUnionTagMember.str(), tag});
        }
        for (const auto& field : section.fields)
        {
            if (!field.isPadding)
            {
                subjects.push_back(Subject{field.name, field.resolvedType});
            }
        }
        for (const auto& [name, type] : subjects)
        {
            const std::string getter = loweredSymbol(def, sectionName, PlanFunction::Get, name);
            const std::string setter = loweredSymbol(def, sectionName, PlanFunction::Set, name);
            const ArrayKind   kind   = type.arrayKind;
            if ((kind != ArrayKind::None) && (kind != ArrayKind::Fixed))
            {
                continue;
            }
            const bool        indexed   = kind == ArrayKind::Fixed;
            const std::string irIndex   = indexed ? ", " + file.standard("int64_t") + " index" : "";
            const std::string cIndex    = indexed ? ", const " + file.standard("size_t") + " index" : "";
            const std::string passIndex = indexed ? ", (" + file.standard("int64_t") + ") index" : "";
            const std::string size      = "(" + file.standard("int64_t") + ") buffer_size_bytes";
            if (type.scalarCategory == SemanticScalarCategory::Composite)
            {
                // A nested composite's getter answers the buffer from the field's offset and, through
                // the pointer, what remains of this one, for the nested type's own accessors.
                // NOLINTBEGIN(performance-inefficient-string-concatenation)
                const std::string& irGet = ctx.names().linkName(getter);
                w.line("const " + file.standard("uint8_t") + "* " + irGet + "(const " + file.standard("uint8_t") +
                       "* buffer, " + file.standard("int64_t") + " buffer_size_bytes" + irIndex + ", " +
                       file.standard("size_t") + "* out_size);");
                w.blank();
                w.line("static inline const " + file.standard("uint8_t") + "* " + ctx.names().wrapper(getter) +
                       "(const " + file.standard("uint8_t") + "* const buffer, const " + file.standard("size_t") +
                       " buffer_size_bytes" + cIndex + ", " + file.standard("size_t") + "* const out_size)");
                w.open("{");
                w.line(file.standard("size_t") + "               sub_size = 0;");
                w.line("const " + file.standard("uint8_t") + "* const sub      = " + irGet + "(buffer, " + size +
                       passIndex + ", &sub_size);");
                w.line("if (out_size != " + file.standard("NULL") + ")");
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
            std::string irType  = file.standard("int64_t");
            if (isFloat)
            {
                irType = (type.bitLength <= 32) ? "float" : "double";
            }
            const std::string cType = cTypeFromFieldType(type, ctx, file);
            // NOLINTBEGIN(performance-inefficient-string-concatenation)
            const std::string& irGet = ctx.names().linkName(getter);
            const std::string& irSet = ctx.names().linkName(setter);
            w.line(irType + " " + irGet + "(const " + file.standard("uint8_t") + "* buffer, " +
                   file.standard("int64_t") + " buffer_size_bytes" + irIndex + ");");
            w.line(file.standard("int8_t") + " " + irSet + "(" + file.standard("uint8_t") + "* buffer, " +
                   file.standard("int64_t") + " buffer_size_bytes" + irIndex + ", " + irType + " value);");
            w.blank();
            w.line("static inline " + cType + " " + ctx.names().wrapper(getter) + "(const " + file.standard("uint8_t") +
                   "* const buffer, const " + file.standard("size_t") + " buffer_size_bytes" + cIndex + ")");
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
            w.line("static inline " + file.standard("int8_t") + " " + ctx.names().wrapper(setter) + "(" +
                   file.standard("uint8_t") + "* const buffer, const " + file.standard("size_t") +
                   " buffer_size_bytes" + cIndex + ", const " + cType + " value)");
            w.open("{");
            w.line("return " + irSet + "(buffer, (" + file.standard("int64_t") + ") buffer_size_bytes" + passIndex +
                   ", (" + irType + ") " + (isBool ? std::string("(value ? 1 : 0)") : std::string("value")) + ");");
            w.close("}");
            w.blank();
            // NOLINTEND(performance-inefficient-string-concatenation)
        }
    }

    if (!ctx.accessorsOnly())
    {
        emitUnionOptionWrappers(w, names, metadata, file);
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
    const CSurface&            names  = ctx.names();
    const std::string          key    = keyOf(def);
    std::ostringstream         out;
    // The declarations first, naming what they take from other headers as they write it; the
    // includes are written after, from what was named.
    ImportSet          includes;
    const CFileNames   file(includes, names.header(key));
    std::ostringstream body;
    SourceWriter       w     = makeCWriter(body);
    const auto         guard = [&](const GeneratedFact fact) {
        return names.tree().find(names.file(key), SurfaceDeclKind::Guard, SurfaceEntity{key, {}, {}, {}}, fact);
    };
    const std::string& includeGuard = guard(GeneratedFact::IncludeGuard)->name;

    out << generatedCommentLine("C backend") << "\n";
    out << "/* Source: " << def.info.fullName << "." << def.info.majorVersion << "." << def.info.minorVersion
        << " */\n\n";
    out << "#ifndef " << includeGuard << "\n";
    out << "#define " << includeGuard << "\n\n";

    // Under the unversioned scheme this type's name carries no version, so two versions of it are
    // one identifier. Generating both is fine -- they are separate headers -- but including both is
    // not, and saying so here beats a cascade of redefinitions from inside generated code. One
    // sentinel is for the type, spelt from its unversioned name, and one is for this version.
    const SurfaceDecl* const anyVersion  = guard(GeneratedFact::SelectedType);
    const SurfaceDecl* const thisVersion = guard(GeneratedFact::SelectedVersion);
    if ((anyVersion != nullptr) && (thisVersion != nullptr))
    {
        out << "#if defined(" << anyVersion->name << ") && !defined(" << thisVersion->name << ")\n";
        out << "#  error \"" << def.info.fullName
            << ": two versions of one type in one translation unit, but generated type names are "
               "unversioned. Regenerate with --versioned-type-names to use both.\"\n";
        out << "#endif\n";
        out << "#define " << anyVersion->name << "\n";
        out << "#define " << thisVersion->name << "\n\n";
    }

    if (def.isService)
    {
        const CSection request(names, key, "request");
        const auto     service = [&](const GeneratedFact fact) {
            return names.declared(key, SurfaceDeclKind::Constant, {}, {}, fact);
        };
        for (const auto& line : renderServiceAliasIdentityMacros(service,
                                                                 def.info.fullName,
                                                                 def.info.majorVersion,
                                                                 def.info.minorVersion,
                                                                 def.info.fixedPortId,
                                                                 file))
        {
            w.line(line);
        }
        w.blank();

        emitSection(w, ctx, file, def, request, def.request, def.doc, schema);
        if (def.response)
        {
            emitSection(w, ctx, file, def, CSection(names, key, "response"), *def.response, def.doc, schema);
        }
        for (const auto& line : renderServiceAliasBridgeLines(
                 names.declared(key, SurfaceDeclKind::Alias, {}),
                 request.tag(),
                 service,
                 [&](const GeneratedFact fact) { return request.generated(fact); },
                 def.request.deprecated && ctx.emitDeprecationAttributes()))
        {
            w.line(line);
        }
        w.blank();

        // The wrappers call the request's serialisation, which an accessors-only run does not emit.
        if (!ctx.accessorsOnly())
        {
            const auto fact = [](const EntryPoint entryPoint) {
                switch (entryPoint)
                {
                case EntryPoint::Serialize:
                    return GeneratedFact::Serialize;
                case EntryPoint::Deserialize:
                    return GeneratedFact::Deserialize;
                case EntryPoint::Initialize:
                    break;
                }
                return GeneratedFact::Initialize;
            };
            const auto function = [](const EntryPoint entryPoint) {
                switch (entryPoint)
                {
                case EntryPoint::Serialize:
                    return PlanFunction::Serialize;
                case EntryPoint::Deserialize:
                    return PlanFunction::Deserialize;
                case EntryPoint::Initialize:
                    break;
                }
                return PlanFunction::Initialize;
            };
            for (const auto& line : renderServiceAliasWrapperLines(
                     [&](const EntryPoint entryPoint) {
                         return names.declared(key, SurfaceDeclKind::Wrapper, {}, {}, fact(entryPoint));
                     },
                     [&](const EntryPoint entryPoint) {
                         return names.wrapper(loweredSymbol(def, "request", function(entryPoint)));
                     },
                     request.tag(),
                     file))
            {
                w.line(line);
            }
        }
    }
    else
    {
        emitSection(w, ctx, file, def, CSection(names, key, {}), def.request, def.doc, schema);
    }

    const std::string declarations = body.str();
    out << renderIncludeLines(includes);
    out << "\n" << declarations;
    out << "#endif /* " << includeGuard << " */\n";
    return out.str();
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

/// @brief Marks each integer parameter and result narrower than 32 bits with the extension the C
///        ABI gives the type the header declares for it.
///
/// A caller compiled from the header takes an `int8_t` or a `bool` its callee answers as already
/// extended to 32 bits, and hands its own narrow arguments extended the same way. An object that
/// answers `-2` in the low byte alone reads as 254. The header spells a one-bit integer `bool`,
/// which the ABI zero-extends, and a wider one `intN_t`, which it sign-extends.
void markNarrowIntegerExtensions(mlir::ModuleOp module)
{
    const auto extension = [](const mlir::Type type) -> std::optional<llvm::StringRef> {
        const auto integer = mlir::dyn_cast<mlir::IntegerType>(type);
        if (!integer || (integer.getWidth() >= 32))
        {
            return std::nullopt;
        }
        return (integer.getWidth() == 1) ? mlir::LLVM::LLVMDialect::getZExtAttrName()
                                         : mlir::LLVM::LLVMDialect::getSExtAttrName();
    };
    const mlir::UnitAttr marked = mlir::UnitAttr::get(module.getContext());
    for (mlir::func::FuncOp fn : module.getBodyRegion().front().getOps<mlir::func::FuncOp>())
    {
        for (const auto& [index, type] : llvm::enumerate(fn.getArgumentTypes()))
        {
            if (const auto name = extension(type))
            {
                fn.setArgAttr(static_cast<unsigned>(index), *name, marked);
            }
        }
        for (const auto& [index, type] : llvm::enumerate(fn.getResultTypes()))
        {
            if (const auto name = extension(type))
            {
                fn.setResultAttr(static_cast<unsigned>(index), *name, marked);
            }
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
    markNarrowIntegerExtensions(module);
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
    /// @brief The member is a `dsdl_runtime_view_t`, or an array of them.
    bool         heldAsView{false};
    std::int64_t arrayCapacity{0};
};

/// @brief What the C header named one plan and its members.
struct CBodyPlan final
{
    llvm::StringMap<CBodyMember> members;
};

/// @brief Spells a plan body as C.
///
/// A type is named from its identity, and a member through the section scope the header declares
/// it in, so a body reaches the identifiers the header declares.
class CSpelling final : public BodySpelling
{
    struct NestedType;

public:
    /// @param[in] module The module the bodies are in.
    /// @param[in] schema The schema the bodies belong to.
    /// @param[in] def The definition @p schema was lowered from.
    /// @param[in] versioning Whether type names carry the version.
    /// @param[in] headers The header declaring each nested type a body may call, by the plan
    ///            identity of its object.
    /// @param[in] file How the implementation file names what it takes from other headers.
    CSpelling(mlir::dsdl::SchemaOp         schema,
              const CSurface&              names,
              llvm::StringMap<std::string> headers,
              const CFileNames&            file)
        : names_(names)
        , headers_(std::move(headers))
        , file_(file)
    {
        // A body points at nested objects as well as its own, so every type the surface declares is
        // found by the identity of its plan.
        for (const SurfaceScope& scope : names.tree().plan().scopes)
        {
            if ((scope.kind != SurfaceScopeKind::Type) || !scope.of)
            {
                continue;
            }
            if (const std::optional<SchemaSymbol> symbol = parseSchemaSymbol(scope.of->schema))
            {
                types_[planIdentity(symbol->fullName, symbol->major, symbol->minor, scope.of->section)] =
                    NestedType{.key = scope.of->schema, .section = scope.of->section, .schema = *symbol};
            }
        }
        if (schema.getBody().empty())
        {
            return;
        }
        const std::string key = schema.getSymName().str();
        for (mlir::dsdl::SerializationPlanOp plan : schema.getBody().front().getOps<mlir::dsdl::SerializationPlanOp>())
        {
            const llvm::StringRef section = plan.getSection().value_or(llvm::StringRef{});
            CBodyPlan             entry;
            if (!plan.getBody().empty())
            {
                for (mlir::dsdl::IOOp io : plan.getBody().front().getOps<mlir::dsdl::IOOp>())
                {
                    if (io.isPadding())
                    {
                        continue;
                    }
                    CBodyMember member;
                    member.cName                = names.member(key, section, io.getName());
                    member.arrayKind            = io.getArrayKind().str();
                    member.category             = io.getScalarCategory().str();
                    member.bitLength            = io.getBitLength();
                    member.heldAsView           = io.getHeldAsView();
                    member.arrayCapacity        = io.getArrayCapacity();
                    entry.members[io.getName()] = std::move(member);
                }
            }
            if (plan.getIsUnion())
            {
                entry.members[kPlanUnionTagMember] =
                    CBodyMember{names.member(key, section, {}, GeneratedFact::UnionTag),
                                "none",
                                "unsigned",
                                unionTagBits(plan),
                                false};
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
        const std::string& name = names_.linkName(fn.getSymName());
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
            w.line(file_.standard("int8_t") + " " + name + "(" + typeName(fn.getArgument(0).getType()) + " " + object +
                   ")");
            w.open("{");
            markUnused(w, fn, {object});
            return {object};
        }
        w.line(file_.standard("int8_t") + " " + name + "(" + typeName(fn.getArgument(0).getType()) + " " + object +
               ", " + typeName(fn.getArgument(1).getType()) + " buffer, " + file_.standard("size_t") +
               "* inout_buffer_size_bytes)");
        w.open("{");
        markUnused(w, fn, {object, "buffer", "inout_buffer_size_bytes"});
        return {object, "buffer", "inout_buffer_size_bytes"};
    }

    /// @brief The declaration of the helper @p fn, so a body may call it before it is defined.
    [[nodiscard]] std::string helperDeclaration(mlir::func::FuncOp fn) const
    {
        std::string parameters;
        for (const mlir::BlockArgument argument : fn.getArguments())
        {
            parameters += (parameters.empty() ? "" : ", ") + typeName(argument.getType()) + " p" +
                          std::to_string(argument.getArgNumber());
        }
        return typeName(fn.getFunctionType().getResult(0)) + " " + names_.linkName(fn.getSymName()) + "(" +
               (parameters.empty() ? "void" : parameters) + ");";
    }

    void closeFunction(SourceWriter& w, mlir::func::FuncOp /*fn*/) const override
    {
        w.close("}");
        w.blank();
    }

    [[nodiscard]] std::string functionName(const llvm::StringRef callee) const override
    {
        return names_.linkName(callee);
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
        w.open("while (" + file_.standard("true") + ") {");
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
        w.open("for (" + file_.standard("size_t") + " " + variable.str() + " = " + lower.str() + "; " + variable.str() +
               " < " + upper.str() + "; " + variable.str() + " += " + step.str() + ") {");
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
                return integer.getValue().isZero() ? file_.standard("false") : file_.standard("true");
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
        return "(" + names(op.getPointer()) + " != " + file_.standard("NULL") + ")";
    }

    [[nodiscard]] std::string isNull(mlir::dsdl::IsNullOp op, const ValueNames& names) const override
    {
        return "(" + names(op.getPointer()) + " == " + file_.standard("NULL") + ")";
    }

    [[nodiscard]] std::string indexHolds(mlir::dsdl::IndexHoldsOp op, const ValueNames& names) const override
    {
        // The count survives the round trip through the index type as a signed quantity.
        const std::string value = names(op.getValue());
        return "((" + file_.standard("uint64_t") + ") (" + file_.standard("ptrdiff_t") + ") " + value + " == " + value +
               ")";
    }

    [[nodiscard]] std::string bufferOrEmpty(mlir::dsdl::BufferOrEmptyOp op, const ValueNames& names) const override
    {
        const std::string buffer = names(op.getBuffer());
        return "((" + buffer + " == " + file_.standard("NULL") + ") ? (const " + file_.standard("uint8_t") +
               "*) \"\" : " + buffer + ")";
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
        return "(" + file_.standard("uint64_t") + ") " + memberPath(op.getObject(), op.getMember(), names) + ".count";
    }

    void setArrayLength(SourceWriter& w, mlir::dsdl::SetArrayLengthOp op, const ValueNames& names) const override
    {
        w.line(memberPath(op.getObject(), op.getMember(), names) + ".count = (" + file_.standard("size_t") + ") " +
               names(op.getValue()) + ";");
    }

    [[nodiscard]] std::string unionTag(mlir::dsdl::UnionTagOp op, const ValueNames& names) const override
    {
        return "(" + file_.standard("uint64_t") + ") " + names(op.getObject()) + "->" +
               memberOf(op.getObject(), kPlanUnionTagMember)->cName;
    }

    void setUnionTag(SourceWriter& w, mlir::dsdl::SetUnionTagOp op, const ValueNames& names) const override
    {
        // The width the declaration gave the tag: a union of more than 256 options carries it in
        // more than a byte, and narrowing the write here would dispatch the wrong arm.
        const CBodyMember* const found = memberOf(op.getObject(), "_tag_");
        const std::string        storage =
            unsignedStorageType(static_cast<std::uint32_t>((found == nullptr) ? 8 : found->bitLength), file_);
        w.line(names(op.getObject()) + "->" + found->cName + " = (" + storage + ") " + names(op.getValue()) + ";");
    }

    [[nodiscard]] std::string writeBits(mlir::dsdl::WriteBitsOp op, const ValueNames& names) const override
    {
        const mlir::Type  valueType = op.getValue().getType();
        const std::string primitive =
            runtimePrimitive(true, valueType, static_cast<std::int64_t>(op.getWidth()), op.getIsSigned());
        const bool integral = !mlir::isa<mlir::FloatType>(valueType) && (op.getWidth() != 1);
        // A signed field's primitive takes a signed carrier, and the plan's i64 is spelt unsigned;
        // converting it implicitly is implementation-defined above INT64_MAX.
        const std::string value     = (integral && op.getIsSigned())
                                          ? ("(" + file_.standard("int64_t") + ") " + names(op.getValue()))
                                          : names(op.getValue());
        std::string       arguments = names(op.getBuffer()) + ", (" + file_.standard("size_t") + ") " +
                                      names(op.getBufferSizeBytes()) + ", (" + file_.standard("size_t") + ") " +
                                      names(op.getBitOffset()) + ", " + value;
        if (integral)
        {
            arguments += ", (" + file_.standard("uint8_t") + ") " + std::to_string(op.getWidth());
        }
        return primitive + "(" + arguments + ")";
    }

    [[nodiscard]] std::string readBits(mlir::dsdl::ReadBitsOp op, const ValueNames& names) const override
    {
        const mlir::Type  valueType = op.getValue().getType();
        const std::string primitive =
            runtimePrimitive(false, valueType, static_cast<std::int64_t>(op.getWidth()), op.getIsSigned());
        std::string arguments = names(op.getBuffer()) + ", (" + file_.standard("size_t") + ") " +
                                names(op.getBufferSizeBytes()) + ", (" + file_.standard("size_t") + ") " +
                                names(op.getBitOffset());
        if (!mlir::isa<mlir::FloatType>(valueType) && (op.getWidth() != 1))
        {
            arguments += ", (" + file_.standard("uint8_t") + ") " + std::to_string(op.getWidth());
        }
        return primitive + "(" + arguments + ")";
    }

    void bitWrite(SourceWriter& w, mlir::dsdl::BitWriteOp op, const ValueNames& names) const override
    {
        // A run of bits copied out of the object's own storage into the buffer.
        w.line(file_.runtime("dsdl_runtime_copy_bits") + "(" + names(op.getDestination()) + ", (" +
               file_.standard("size_t") + ") " + names(op.getDestinationBitOffset()) + ", (" +
               file_.standard("size_t") + ") " + names(op.getWidth()) + ", " + names(op.getSource()) + ", (" +
               file_.standard("size_t") + ") " + names(op.getSourceBitOffset()) + ");");
    }

    void bitRead(SourceWriter& w, mlir::dsdl::BitReadOp op, const ValueNames& names) const override
    {
        w.line(file_.runtime("dsdl_runtime_get_bits") + "(" + names(op.getDestination()) + ", " +
               names(op.getBuffer()) + ", (" + file_.standard("size_t") + ") " + names(op.getBufferSizeBytes()) +
               ", (" + file_.standard("size_t") + ") " + names(op.getBitOffset()) + ", (" + file_.standard("size_t") +
               ") " + names(op.getWidth()) + ");");
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
        w.line(file_.runtime("dsdl_runtime_image_read") + "(" + names(op.getObject()) + ", " + names(op.getBuffer()) +
               ", (" + file_.standard("size_t") + ") " + names(op.getBufferSizeBytes()) + ", " +
               std::to_string(op.getBytes()) + "U);");
    }

    void imageWrite(SourceWriter& w, mlir::dsdl::ImageWriteOp op, const ValueNames& names) const override
    {
        w.line(file_.runtime("dsdl_runtime_image_write") + "(" + names(op.getBuffer()) + ", " + names(op.getObject()) +
               ", " + std::to_string(op.getBytes()) + "U);");
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
        return file_.declaredIn(headerOf(op.getObject()), entryPoint(op.getObject(), op.getDirection())) + "(" +
               names(op.getObject()) + ", " + names(op.getBuffer()) + ", " + names(op.getSize()) + ")";
    }

    void declareCallInitialize(SourceWriter&                w,
                               const llvm::StringRef        name,
                               mlir::dsdl::CallInitializeOp op,
                               const ValueNames&            names) const override
    {
        // C translates the initialise body as a function, so a nested one is the call its
        // header publishes, answering the same code the rest of the plan carries.
        const std::string invocation =
            file_.declaredIn(headerOf(op.getObject()), entryPoint(op.getObject(), "initialize")) + "(" +
            names(op.getObject()) + ")";
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
        return "(" + file_.standard("uint64_t") + ") " + viewPath(op, names) + ".size_bytes";
    }

    void storeView(SourceWriter& w, mlir::dsdl::StoreViewOp op, const ValueNames& names) const override
    {
        const std::string path = op.getIndex()
                                     ? elementPath(op.getObject(), op.getMember(), names(op.getIndex()), names)
                                     : memberPath(op.getObject(), op.getMember(), names);
        w.line(path + ".bytes = " + names(op.getBytes()) + ";");
        w.line(path + ".size_bytes = (" + file_.standard("size_t") + ") " + names(op.getSizeBytes()) + ";");
    }

    void clearView(SourceWriter& w, mlir::dsdl::ClearViewOp op, const ValueNames& names) const override
    {
        const std::string        path   = memberPath(op.getObject(), op.getMember(), names);
        const CBodyMember* const member = memberOf(op.getObject(), op.getMember());
        if ((member != nullptr) && (member->arrayKind != "none"))
        {
            w.line(file_.runtime("dsdl_runtime_clear_views") + "(" +
                   elementBase(op.getObject(), op.getMember(), names) + ", " +
                   ((member->arrayKind == "fixed") ? std::to_string(member->arrayCapacity) + "U" : (path + ".count")) +
                   ");");
            return;
        }
        w.line(file_.runtime("dsdl_runtime_clear_views") + "(&" + path + ", 1U);");
    }

    void copyBytes(SourceWriter& w, mlir::dsdl::CopyBytesOp op, const ValueNames& names) const override
    {
        w.line(file_.runtime("dsdl_runtime_copy_bytes") + "(" + names(op.getDestination()) + ", " +
               names(op.getSource()) + ", (" + file_.standard("size_t") + ") " + names(op.getSourceSizeBytes()) + ", " +
               std::to_string(op.getBytes()) + "U);");
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
        return (integer && (integer.getWidth() == 64)) ? file_.standard("int64_t") : typeName(type);
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
        return path + ((found->category == file_.standard("bool")) ? ".bitpacked" : ".elements");
    }

    [[nodiscard]] std::string elementPath(const mlir::Value     object,
                                          const llvm::StringRef member,
                                          const std::string&    index,
                                          const ValueNames&     names) const
    {
        return elementBase(object, member, names) + "[(" + file_.standard("size_t") + ") " + index + "]";
    }

    /// @brief A view member, or one element of an array of them.
    [[nodiscard]] std::string viewPath(mlir::dsdl::LoadViewOp op, const ValueNames& names) const
    {
        return op.getIndex() ? elementPath(op.getObject(), op.getMember(), names(op.getIndex()), names)
                             : memberPath(op.getObject(), op.getMember(), names);
    }

    /// @brief The type whatever @p object points at is.
    [[nodiscard]] const NestedType& typeOf(const mlir::Value object) const
    {
        const auto pointer = mlir::dyn_cast<mlir::dsdl::PtrType>(object.getType());
        const auto identity =
            pointer ? mlir::dyn_cast<mlir::dsdl::ObjectType>(pointer.getPointee()) : mlir::dsdl::ObjectType{};
        const auto found = identity ? types_.find(identity.getIdentity()) : types_.end();
        if (found == types_.end())
        {
            llvm::report_fatal_error("C spelling: an object of a type the surface does not declare");
        }
        return found->second;
    }

    /// @brief The entry point a nested type's header publishes for @p direction.
    [[nodiscard]] std::string entryPoint(const mlir::Value object, const llvm::StringRef direction) const
    {
        const NestedType& type     = typeOf(object);
        PlanFunction      function = PlanFunction::Initialize;
        if (direction == "serialize")
        {
            function = PlanFunction::Serialize;
        }
        else if (direction == "deserialize")
        {
            function = PlanFunction::Deserialize;
        }
        return names_.wrapper(renderPlanSymbol(
            planFunction(type.schema.fullName, type.schema.major, type.schema.minor, type.section, function)));
    }

    /// @brief The runtime primitive that carries a field of this width and value type.
    [[nodiscard]] std::string runtimePrimitive(const bool         write,
                                               const mlir::Type   valueType,
                                               const std::int64_t width,
                                               const bool         isSigned) const
    {
        if (mlir::isa<mlir::FloatType>(valueType))
        {
            // Selected by the field's width, not the carrier's: a float16 field travels as a C
            // `float` and is written by set_f16.
            return file_.runtime(std::string(write ? "dsdl_runtime_set_f" : "dsdl_runtime_get_f") +
                                 std::to_string(width));
        }
        if ((width == 1) && !isSigned)
        {
            return write ? file_.runtime("dsdl_runtime_set_bit") : file_.runtime("dsdl_runtime_get_bit");
        }
        if (write)
        {
            return isSigned ? file_.runtime("dsdl_runtime_set_ixx") : file_.runtime("dsdl_runtime_set_uxx");
        }
        // A read answers in a concrete width, so the primitive is the smallest standard integer
        // that holds the field rather than the field's own width.
        const unsigned holder = holderWidthFor(static_cast<unsigned>(width));
        return file_.runtime(std::string(isSigned ? "dsdl_runtime_get_i" : "dsdl_runtime_get_u") +
                             std::to_string(holder));
    }

    /// @brief The value a variable holds before an arm assigns it.
    [[nodiscard]] std::string zeroOf(const mlir::Type type) const
    {
        if (mlir::isa<mlir::dsdl::PtrType>(type))
        {
            return file_.standard("NULL");
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
            return file_.standard("false");
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
        w.line(typeName(fn.getFunctionType().getResult(0)) + " " + names_.linkName(fn.getSymName()) + "(" +
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
            return file_.standard("size_t");
        }
        if (auto floating = mlir::dyn_cast<mlir::FloatType>(type))
        {
            return (floating.getWidth() <= 32) ? "float" : "double";
        }
        const unsigned width = mlir::cast<mlir::IntegerType>(type).getWidth();
        if (width == 1)
        {
            return file_.standard("bool");
        }
        // The plan's i64 is the wire arithmetic and the runtime's argument, both unsigned; a
        // signed comparison casts for the comparison alone.
        return file_.standard((width == 64) ? std::string(file_.standard("uint64_t"))
                                            : "int" + std::to_string(width) + "_t");
    }

    /// @brief What a pointer of the body points at.
    [[nodiscard]] std::string pointeeName(const mlir::Type pointee) const
    {
        if (mlir::isa<mlir::dsdl::ByteType>(pointee))
        {
            return file_.standard("uint8_t");
        }
        if (mlir::isa<mlir::dsdl::SizeType>(pointee))
        {
            return file_.standard("size_t");
        }
        if (const auto object = mlir::dyn_cast<mlir::dsdl::ObjectType>(pointee))
        {
            const auto found  = types_.find(object.getIdentity());
            const auto header = headers_.find(object.getIdentity());
            if (found == types_.end())
            {
                llvm::report_fatal_error("C spelling: an object of a type the surface does not declare");
            }
            return file_.declaredIn((header == headers_.end()) ? llvm::StringRef{} : llvm::StringRef(header->second),
                                    names_.tag(found->second.key, found->second.section));
        }
        return "void";
    }

    /// @brief The header that declares the type @p object points at.
    [[nodiscard]] std::string headerOf(const mlir::Value object) const
    {
        const auto pointer = mlir::dyn_cast<mlir::dsdl::PtrType>(object.getType());
        const auto identity =
            pointer ? mlir::dyn_cast<mlir::dsdl::ObjectType>(pointer.getPointee()) : mlir::dsdl::ObjectType{};
        const auto found = identity ? headers_.find(identity.getIdentity()) : headers_.end();
        return (found == headers_.end()) ? std::string{} : found->second;
    }

    /// @brief A type a body names, by the definition it is a section of.
    struct NestedType final
    {
        std::string  key;
        std::string  section;
        SchemaSymbol schema;
    };

    const CSurface&              names_;
    llvm::StringMap<CBodyPlan>   plans_;
    llvm::StringMap<NestedType>  types_;
    llvm::StringMap<std::string> headers_;
    /// @brief How the implementation file names what it takes from other headers.
    const CFileNames& file_;
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

    auto tree = SurfaceTree::read(module, languageTraits(Language::C));
    if (!tree)
    {
        return tree.takeError();
    }
    const CSurface              names(*tree);
    std::filesystem::path const outRoot(options.outDir);
    EmitterContext const        ctx(semantic,
                                    names,
                                    options.emitDeprecationAttributes,
                                    options.hostImageFolded,
                                    options.accessorsOnly);
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

    llvm::StringMap<mlir::Operation*> schemaByKey;
    for (mlir::dsdl::SchemaOp op : module.getBodyRegion().front().getOps<mlir::dsdl::SchemaOp>())
    {
        schemaByKey[planIdentity(op.getFullName(), op.getMajor(), op.getMinor(), {})] = op.getOperation();
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

        const auto target =
            schemaByKey.find(planIdentity(def.info.fullName, def.info.majorVersion, def.info.minorVersion, {}));
        if (target == schemaByKey.end())
        {
            diagnostics.error({"<mlir>", 1, 1}, "failed to locate schema op for " + definitionTypeKey(def.info));
            return llvm::createStringError(llvm::inconvertibleErrorCode(), "schema selection failed");
        }
        mlir::Operation* const schemaClone = target->second->clone();
        perDefModule.getBodyRegion().front().push_back(schemaClone);
        if (options.artifact == Artifact::Object)
        {
            // A C translation unit needs only a nested type's name, which its header supplies. An
            // object addresses members by position, so it needs the nested type's layout, which
            // lives in the nested type's own schema. Their functions stay behind: the serialisation
            // of a nested type belongs to the nested type's object.
            for (const mlir::dsdl::SchemaOp reached :
                 schemasReachedBy(mlir::cast<mlir::dsdl::SchemaOp>(target->second)))
            {
                perDefModule.getBodyRegion().front().push_back(reached->clone());
            }
        }
        cloneFunctionsOf(mlir::cast<mlir::dsdl::SchemaOp>(schemaClone), module, perDefModule);

        mlir::PassManager pm(perDefModule.getContext());
        if (options.artifact == Artifact::Object)
        {
            renameToCLinkNames(perDefModule, names);
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
            if (auto err = writeGeneratedFile(outRoot / names.besideHeader(keyOf(def), ".o"),
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
        // The declarations and bodies first, naming what they take from other headers as they write
        // it; the includes are written after, from what was named. The file's own header, which
        // declares every function the file defines, is included ahead of them.
        const std::string& ownHeader = names.header(keyOf(def));
        ImportSet          includes;
        std::ostringstream declarationsOut;
        std::ostringstream bodiesOut;
        SourceWriter       declarations = makeCWriter(declarationsOut);
        SourceWriter       bodies       = makeCWriter(bodiesOut);
        {
            // The nested types whose entry points this definition's bodies may call, each by the
            // header that declares it. A field held as a view is decoded by no call.
            llvm::StringMap<std::string> dependencyHeaders;
            for (const auto& depRef : collectDefinitionCompositeDependencies(def, /*referencedOnly=*/true))
            {
                if (const auto* dep = ctx.find(depRef))
                {
                    dependencyHeaders
                        [planIdentity(dep->info.fullName, dep->info.majorVersion, dep->info.minorVersion, {})] =
                            names.header(keyOf(*dep));
                }
            }
            const CFileNames                      file(includes, ownHeader);
            const CSpelling                       spelling(schema, names, std::move(dependencyHeaders), file);
            PlanBodyLookups                       lookups(perDefModule);
            const std::vector<mlir::func::FuncOp> functions = schemaFunctions(perDefModule, schema.getSymName());
            for (const mlir::func::FuncOp fn : functions)
            {
                if (auto err = translateFunction(fn, spelling, bodies, lookups))
                {
                    diagnostics.error({"<mlir>", 1, 1}, llvm::toString(std::move(err)));
                    return llvm::createStringError(llvm::inconvertibleErrorCode(), "C body translation failed");
                }
            }
            // A helper is declared before any function is defined, since the lowering may define
            // it after the body that calls it. The file's own header declares the bodies.
            bool declared = false;
            for (const mlir::func::FuncOp fn : functions)
            {
                if (!planBodyDirection(fn))
                {
                    declarations.line(spelling.helperDeclaration(fn));
                    declared = true;
                }
            }
            if (declared)
            {
                declarations.blank();
            }
        }
        const std::string others  = renderIncludeLines(includes);
        const std::string emitted = "#include \"" + ownHeader + "\"\n\n" + (others.empty() ? "" : others + "\n") +
                                    declarationsOut.str() + bodiesOut.str();

        const std::string implPreamble =
            generatedCommentLine("C backend implementation") + "\n" + "/* Source: " + def.info.fullName + "." +
            std::to_string(def.info.majorVersion) + "." + std::to_string(def.info.minorVersion) + " */\n\n";
        std::string implContents;
        implContents.reserve(implPreamble.size() + emitted.size());
        implContents.append(implPreamble).append(emitted);
        if (auto err = writeGeneratedFile(outRoot / names.besideHeader(keyOf(def), ".c"),
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

        if (auto err = writeGeneratedFile(outRoot / names.header(keyOf(def)),
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
