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
/// This file materialises Go type declarations and serdes helpers from backend-neutral lowering plans.
///
/// The line-building concatenations here carry NOLINT for
/// performance-inefficient-string-concatenation. Each one spells out a line of generated
/// source, and an append sequence would cost the reader the line itself.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/BodyTranslator.h"
#include "llvmdsdl/CodeGen/EmitCommon.h"
#include "llvmdsdl/CodeGen/SectionNaming.h"
#include "llvmdsdl/CodeGen/EmbeddedRuntimeSources.h"
#include "llvmdsdl/CodeGen/emitter/Go.h"

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

#include "llvmdsdl/CodeGen/ConstantLiteralRender.h"
#include "llvmdsdl/CodeGen/DefinitionDependencies.h"
#include "llvmdsdl/CodeGen/DefinitionIndex.h"
#include "llvmdsdl/CodeGen/SchemaLookup.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/CodeGen/HelperBindingNaming.h"
#include "llvmdsdl/CodeGen/StorageTypeTokens.h"
#include "llvmdsdl/CodeGen/TypeStorage.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include "llvmdsdl/CodeGen/SourceWriter.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Evaluator.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Version.h"
#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/IR/DSDLTypes.h"
#include "llvmdsdl/Transforms/PlanSteps.h"
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/Support/ErrorHandling.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/IR/Attributes.h>
#include <mlir/IR/BuiltinAttributeInterfaces.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinTypeInterfaces.h>
#include <mlir/IR/BuiltinTypes.h>
#include <mlir/IR/Types.h>
#include <mlir/IR/Value.h>
#include <mlir/Support/LLVM.h>
#include <iomanip>
#include "mlir/IR/BuiltinOps.h"

namespace llvmdsdl::emitter::go
{

namespace
{

/// @brief Builds the collision-free exported field-name allocation for one section.
///
/// Two distinct DSDL field names (e.g. `fooBar` and `foo_bar`) both export to `FooBar`; without this
/// the struct would declare the same field twice (a compile error) and the (de)serialiser would read
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
};

std::map<std::string, std::string> computeImportAliases(const SemanticDefinition& def, const EmitterContext& ctx)
{
    const auto deps = collectDefinitionCompositeDependencies(def);

    const std::string                  currentPath = EmitterContext::packagePath(def.info);
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

/// @brief The Go spelling of the plan-body vocabulary, for one schema.
///
/// Members are named from the same scope the struct declaration names them in, built from the
/// schema's own fields. A nested type is never named: a nested call is a method call on the
/// member, and a variable-length array is resized by the runtime, which takes the element type
/// from the slice. The plan's `i64` is `uint64`, `i8` is `int8`, an index is `int`, and the size a
/// plan is handed by pointer is a local `int`. A buffer is a slice, and a pointer into it is a
/// sub-slice clamped to the buffer's end.
///
/// The generation lane holds the output to gofmt, so every operation is its own statement and no
/// operator sits inside a call argument or an index: those are the places gofmt respaces.
class GoSpelling final : public BodySpelling
{
public:
    explicit GoSpelling(mlir::dsdl::SchemaOp schema)
    {
        if (schema.getBody().empty())
        {
            return;
        }
        for (mlir::dsdl::SerializationPlanOp plan : schema.getBody().front().getOps<mlir::dsdl::SerializationPlanOp>())
        {
            Plan entry;
            entry.unionTagBits = plan.getUnionTagBits().value_or(0);
            NamingScope                   scope(CodegenNamingLanguage::Go);
            std::vector<mlir::dsdl::IOOp> fields;
            if (!plan.getBody().empty())
            {
                for (mlir::dsdl::IOOp io : plan.getBody().front().getOps<mlir::dsdl::IOOp>())
                {
                    if (!io.isPadding())
                    {
                        (void) scope.declare(IdentifierRole::FieldName, io.getName());
                        fields.push_back(io);
                    }
                }
            }
            for (mlir::dsdl::IOOp io : fields)
            {
                entry.members[io.getName()] = Member{scope.get(IdentifierRole::FieldName, io.getName()), io};
            }
            plans_[planIdentity(schema, plan)] = std::move(entry);
        }
    }

    // Functions.

    std::vector<std::string> openFunction(SourceWriter& w, mlir::func::FuncOp fn) const override
    {
        const auto direction = planBodyDirection(fn);
        inBody_              = direction.has_value();
        if (!direction)
        {
            std::vector<std::string> parameters;
            std::string              list;
            for (const auto [index, argument] : llvm::enumerate(fn.getArguments()))
            {
                parameters.push_back("p" + std::to_string(index));
                list += (list.empty() ? "" : ", ") + parameters.back() + " " + typeName(argument.getType());
            }
            w.open("func " + functionName(fn.getSymName()) + "(" + list + ") " + typeName(fn.getResultTypes().front()) +
                   " {");
            return parameters;
        }
        const Plan& plan = planOf(fn.getArgument(0));
        w.open("func (obj *" + plan.typeName + ") " + (*direction == "serialize" ? "Serialize" : "Deserialize") +
               "(buffer []byte) (int8, int) {");
        // The size a plan is handed by pointer, read at entry and written back at the end.
        w.line("inoutBufferSizeBytes := len(buffer)");
        return {"obj", "buffer", "inoutBufferSizeBytes"};
    }

    void closeFunction(SourceWriter& w, mlir::func::FuncOp /*fn*/) const override
    {
        w.close("}");
    }

    [[nodiscard]] std::string functionName(const llvm::StringRef callee) const override
    {
        return renderHelperBindingIdentifier(CodegenNamingLanguage::Go, callee);
    }

    /// @brief Names the Go type each plan's bodies are methods of.
    void setTypeName(const llvm::StringRef identity, const std::string& typeName)
    {
        const auto found = plans_.find(identity);
        if (found != plans_.end())
        {
            found->second.typeName = typeName;
        }
    }

    // Statements.

    void declare(SourceWriter& w,
                 const mlir::Type /*type*/,
                 const llvm::StringRef name,
                 const llvm::StringRef expr) const override
    {
        w.line(name.str() + " := " + expr.str());
    }

    void declareVariable(SourceWriter&         w,
                         const mlir::Type      type,
                         const llvm::StringRef name,
                         const bool /*reassigned*/) const override
    {
        w.line("var " + name.str() + " " + typeName(type));
    }

    void assign(SourceWriter& w, const llvm::StringRef name, const llvm::StringRef expr) const override
    {
        w.line(name.str() + " = " + expr.str());
    }

    void discard(SourceWriter& w, const llvm::StringRef expr) const override
    {
        w.line("_ = " + expr.str());
    }

    void returnValue(SourceWriter& w, const llvm::StringRef expr) const override
    {
        // A body answers the runtime's error code; its Go signature answers the code and the
        // size, which is the size used on success and nothing on failure.
        if (inBody_)
        {
            w.open("if " + expr.str() + " == int8(0) {");
            w.line("return int8(0), inoutBufferSizeBytes");
            w.close("}");
            w.line("return " + expr.str() + ", 0");
            return;
        }
        w.line("return " + expr.str());
    }

    void openIf(SourceWriter& w, const llvm::StringRef condition) const override
    {
        w.open("if " + condition.str() + " {");
    }

    void openElse(SourceWriter& w) const override
    {
        w.midway("} else {");
    }

    void openLoop(SourceWriter& w) const override
    {
        w.open("for {");
    }

    void breakUnless(SourceWriter& w, const llvm::StringRef condition) const override
    {
        w.open("if !" + condition.str() + " {");
        w.line("break");
        w.close("}");
    }

    void openFor(SourceWriter&         w,
                 const llvm::StringRef variable,
                 const llvm::StringRef lower,
                 const llvm::StringRef upper,
                 const llvm::StringRef step) const override
    {
        w.open("for " + variable.str() + " := " + lower.str() + "; " + variable.str() + " < " + upper.str() + "; " +
               variable.str() + " += " + step.str() + " {");
    }

    void closeBlock(SourceWriter& w) const override
    {
        w.close("}");
    }

    void declareSelect(SourceWriter&         w,
                       const mlir::Type      type,
                       const llvm::StringRef name,
                       const llvm::StringRef condition,
                       const llvm::StringRef ifTrue,
                       const llvm::StringRef ifFalse) const override
    {
        w.line("var " + name.str() + " " + typeName(type));
        w.open("if " + condition.str() + " {");
        w.line(name.str() + " = " + ifTrue.str());
        w.midway("} else {");
        w.line(name.str() + " = " + ifFalse.str());
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
                return "int(" + std::to_string(integer.getValue().getZExtValue()) + ")";
            }
            const unsigned width = mlir::cast<mlir::IntegerType>(type).getWidth();
            if (width == 1)
            {
                return integer.getValue().isZero() ? "false" : "true";
            }
            // A negative value of an unsigned-spelt type is its two's complement, which the
            // type holds; the signed forms read it back as the value it stands for.
            const std::string digits = isSignedSpelt(type) ? std::to_string(integer.getValue().getSExtValue())
                                                           : std::to_string(integer.getValue().getZExtValue());
            return typeName(type) + "(" + digits + ")";
        }
        if (const auto real = mlir::dyn_cast<mlir::FloatAttr>(value))
        {
            const bool         single = real.getType().isF32();
            std::ostringstream stream;
            stream << std::setprecision(single ? 9 : 17) << real.getValueAsDouble();
            std::string text = stream.str();
            if (text.find_first_of(".eEn") == std::string::npos)
            {
                text += ".0";
            }
            return (single ? "float32(" : "float64(") + text + ")";
        }
        llvm::report_fatal_error("Go spelling: a constant of an unexpected kind");
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
                return lhs.str() + " && " + rhs.str();
            case BinaryOperator::Or:
                return lhs.str() + " || " + rhs.str();
            case BinaryOperator::Xor:
                return lhs.str() + " != " + rhs.str();
            default:
                llvm::report_fatal_error("Go spelling: an arithmetic operator on a bool");
            }
        }
        // Go's fixed-width arithmetic wraps, which is the plan's arithmetic.
        const bool signedForm =
            op == BinaryOperator::DivS || op == BinaryOperator::RemS || op == BinaryOperator::ShiftRightS;
        const bool unsignedForm =
            op == BinaryOperator::DivU || op == BinaryOperator::RemU || op == BinaryOperator::ShiftRightU;
        const bool  recast = (signedForm && !isSignedSpelt(type)) || (unsignedForm && isSignedSpelt(type));
        std::string a      = lhs.str();
        std::string b      = rhs.str();
        if (recast)
        {
            a = signedForm ? asSigned(lhs, type) : asUnsigned(lhs, type);
            b = signedForm ? asSigned(rhs, type) : asUnsigned(rhs, type);
        }
        const std::string expression = a + " " + operatorToken(op) + " " + b;
        return recast ? typeName(type) + "(" + expression + ")" : expression;
    }

    [[nodiscard]] std::string compare(const Comparison      comparison,
                                      const llvm::StringRef lhs,
                                      const llvm::StringRef rhs,
                                      const mlir::Type      type) const override
    {
        std::string a = lhs.str();
        std::string b = rhs.str();
        switch (comparison)
        {
        case Comparison::Eq:
            return a + " == " + b;
        case Comparison::Ne:
            return a + " != " + b;
        case Comparison::LtS:
        case Comparison::LeS:
        case Comparison::GtS:
        case Comparison::GeS:
            if (!isSignedSpelt(type))
            {
                a = asSigned(lhs, type);
                b = asSigned(rhs, type);
            }
            break;
        case Comparison::LtU:
        case Comparison::LeU:
        case Comparison::GtU:
        case Comparison::GeU:
            if (isSignedSpelt(type))
            {
                a = asUnsigned(lhs, type);
                b = asUnsigned(rhs, type);
            }
            break;
        }
        return a + " " + comparisonToken(comparison) + " " + b;
    }

    [[nodiscard]] std::string select(const llvm::StringRef /*condition*/,
                                     const llvm::StringRef /*ifTrue*/,
                                     const llvm::StringRef /*ifFalse*/,
                                     const mlir::Type /*type*/) const override
    {
        llvm::report_fatal_error("Go spelling: a select is a statement");
    }

    [[nodiscard]] std::string convert(const Conversion      conversion,
                                      const llvm::StringRef value,
                                      const mlir::Type      from,
                                      const mlir::Type      to) const override
    {
        if (isBool(from))
        {
            // Go converts no bool to a number; the runtime does.
            return typeName(to) + "(dsdlruntime.BoolToUint64(" + value.str() + "))";
        }
        if (conversion == Conversion::SignExtend && !isSignedSpelt(from))
        {
            return typeName(to) + "(" + signedStorageType(widthOf(to)) + "(" + asSigned(value, from) + "))";
        }
        return typeName(to) + "(" + value.str() + ")";
    }

    [[nodiscard]] std::string call(const llvm::StringRef callee, const llvm::ArrayRef<std::string> args) const override
    {
        return callee.str() + "(" + llvm::join(args, ", ") + ")";
    }

    // The dialect.

    [[nodiscard]] bool spellsInline(mlir::Operation* op) const override
    {
        return mlir::
            isa<mlir::dsdl::IsNullOp, mlir::dsdl::BufferOrEmptyOp, mlir::dsdl::MemberAddrOp, mlir::dsdl::ElementAddrOp>(
                op);
    }

    [[nodiscard]] std::string isNull(mlir::dsdl::IsNullOp op, const ValueNames& names) const override
    {
        // The object arrives by pointer and may be nil; a slice and a local are never null.
        const auto pointer = mlir::cast<mlir::dsdl::PtrType>(op.getPointer().getType());
        if (mlir::isa<mlir::dsdl::ObjectType>(pointer.getPointee()))
        {
            return names(op.getPointer()) + " == nil";
        }
        return "false";
    }

    [[nodiscard]] std::string bufferOrEmpty(mlir::dsdl::BufferOrEmptyOp op, const ValueNames& names) const override
    {
        return names(op.getBuffer());
    }

    [[nodiscard]] std::string bufferAt(mlir::dsdl::BufferAtOp op, const ValueNames& names) const override
    {
        // The plan bounds its reads and writes itself; a slice past the end would panic first.
        const std::string buffer = names(op.getBuffer());
        return buffer + "[dsdlruntime.ChooseMin(" + asInt(names(op.getByteOffset())) + ", len(" + buffer + ")):]";
    }

    [[nodiscard]] std::string loadScalar(mlir::dsdl::LoadScalarOp op, const ValueNames& names) const override
    {
        return typeName(op.getValue().getType()) + "(" + names(op.getPointer()) + ")";
    }

    void storeScalar(SourceWriter& w, mlir::dsdl::StoreScalarOp op, const ValueNames& names) const override
    {
        w.line(names(op.getPointer()) + " = " + asInt(names(op.getValue())));
    }

    [[nodiscard]] std::string local(SourceWriter&         w,
                                    mlir::dsdl::LocalOp   op,
                                    const llvm::StringRef name,
                                    const ValueNames&     names) const override
    {
        w.line(name.str() + " := " + asInt(names(op.getInit())));
        return name.str();
    }

    [[nodiscard]] std::string loadMember(mlir::dsdl::LoadMemberOp op, const ValueNames& names) const override
    {
        return loadedValue(memberAccess(op.getObject(), op.getMember(), names),
                           memberOf(op.getObject(), op.getMember()),
                           op.getValue().getType());
    }

    void storeMember(SourceWriter& w, mlir::dsdl::StoreMemberOp op, const ValueNames& names) const override
    {
        const Member member = memberOf(op.getObject(), op.getMember());
        w.line(memberAccess(op.getObject(), op.getMember(), names) + " = " +
               storedValue(names(op.getValue()), op.getValue().getType(), scalarType(member.io)));
    }

    [[nodiscard]] std::string loadElement(mlir::dsdl::LoadElementOp op, const ValueNames& names) const override
    {
        return loadedValue(elementAccess(op.getObject(), op.getMember(), names(op.getIndex()), names),
                           memberOf(op.getObject(), op.getMember()),
                           op.getValue().getType());
    }

    void storeElement(SourceWriter& w, mlir::dsdl::StoreElementOp op, const ValueNames& names) const override
    {
        const Member member = memberOf(op.getObject(), op.getMember());
        w.line(elementAccess(op.getObject(), op.getMember(), names(op.getIndex()), names) + " = " +
               storedValue(names(op.getValue()), op.getValue().getType(), scalarType(member.io)));
    }

    [[nodiscard]] std::string memberAddr(mlir::dsdl::MemberAddrOp op, const ValueNames& names) const override
    {
        return memberAccess(op.getObject(), op.getMember(), names);
    }

    [[nodiscard]] std::string elementAddr(mlir::dsdl::ElementAddrOp op, const ValueNames& names) const override
    {
        return elementAccess(op.getObject(), op.getMember(), names(op.getIndex()), names);
    }

    [[nodiscard]] std::string arrayLength(mlir::dsdl::ArrayLengthOp op, const ValueNames& names) const override
    {
        return "uint64(len(" + memberAccess(op.getObject(), op.getMember(), names) + "))";
    }

    void setArrayLength(SourceWriter& w, mlir::dsdl::SetArrayLengthOp op, const ValueNames& names) const override
    {
        // Sized within its capacity -- a count past it is what the plan's validation rejects
        // next -- with zeroed elements for the plan to store into.
        const Member      member = memberOf(op.getObject(), op.getMember());
        mlir::dsdl::IOOp  io     = member.io;
        const std::string access = memberAccess(op.getObject(), op.getMember(), names);
        const std::string count  = fresh("count");
        w.line(count + " := dsdlruntime.ChooseMin(" + asInt(names(op.getValue())) + ", " +
               std::to_string(io.getArrayCapacity()) + ")");
        w.line(access + " = dsdlruntime.Resize(" + access + ", " + count + ")");
    }

    [[nodiscard]] std::string unionTag(mlir::dsdl::UnionTagOp op, const ValueNames& names) const override
    {
        return "uint64(" + names(op.getObject()) + ".Tag)";
    }

    void setUnionTag(SourceWriter& w, mlir::dsdl::SetUnionTagOp op, const ValueNames& names) const override
    {
        const Plan& plan = planOf(op.getObject());
        w.line(names(op.getObject()) + ".Tag = " + unsignedStorageType(static_cast<std::uint32_t>(plan.unionTagBits)) +
               "(" + names(op.getValue()) + ")");
    }

    [[nodiscard]] std::string writeBits(mlir::dsdl::WriteBitsOp op, const ValueNames& names) const override
    {
        const mlir::Type  valueType = op.getValue().getType();
        const auto        width     = static_cast<std::int64_t>(op.getWidth());
        const std::string value     = names(op.getValue());
        const std::string prefix    = names(op.getBuffer()) + ", " + asInt(names(op.getBitOffset())) + ", ";
        if (mlir::isa<mlir::FloatType>(valueType))
        {
            return "dsdlruntime.SetF" + std::to_string(width) + "(" + prefix + value + ")";
        }
        if (width == 1 && !op.getIsSigned())
        {
            return "dsdlruntime.SetBit(" + prefix + (isBool(valueType) ? value : value + " != uint64(0)") + ")";
        }
        if (op.getIsSigned())
        {
            return "dsdlruntime.SetIxx(" + prefix + asSigned(value, valueType) + ", uint8(" + std::to_string(width) +
                   "))";
        }
        return "dsdlruntime.SetUxx(" + prefix + value + ", uint8(" + std::to_string(width) + "))";
    }

    [[nodiscard]] std::string readBits(mlir::dsdl::ReadBitsOp op, const ValueNames& names) const override
    {
        const mlir::Type  valueType = op.getValue().getType();
        const auto        width     = static_cast<std::int64_t>(op.getWidth());
        const std::string prefix    = names(op.getBuffer()) + ", " + asInt(names(op.getBitOffset()));
        if (mlir::isa<mlir::FloatType>(valueType))
        {
            return "dsdlruntime.GetF" + std::to_string(width) + "(" + prefix + ")";
        }
        const std::string result = typeName(valueType);
        if (width == 1 && !op.getIsSigned())
        {
            // The bit read answers a number, which is what the plan holds it as.
            return result + "(dsdlruntime.GetU8(" + prefix + ", uint8(1)))";
        }
        // The runtime answers in the narrowest standard width that holds the field; a signed read
        // arrives sign-extended and keeps its value across the widening.
        const unsigned holder = holderWidthFor(width);
        return result + "(dsdlruntime.Get" + std::string(op.getIsSigned() ? "I" : "U") + std::to_string(holder) + "(" +
               prefix + ", uint8(" + std::to_string(width) + ")))";
    }

    void bitWrite(SourceWriter& w, mlir::dsdl::BitWriteOp op, const ValueNames& names) const override
    {
        // A bool array is a container of bools, so a run of its bits goes one element at a time.
        const auto        container = boolContainerOf(op.getSource(), names);
        const std::string index     = fresh("bit");
        const std::string at        = fresh("at");
        const std::string from      = fresh("from");
        w.open("for " + index + " := 0; " + index + " < " + asInt(names(op.getWidth())) + "; " + index + "++ {");
        w.line(at + " := " + asInt(names(op.getDestinationBitOffset())) + " + " + index);
        w.line(from + " := " + container.second + " + " + asInt(names(op.getSourceBitOffset())) + " + " + index);
        w.line("_ = dsdlruntime.SetBit(" + names(op.getDestination()) + ", " + at + ", " + container.first + "[" +
               from + "])");
        w.close("}");
    }

    void bitRead(SourceWriter& w, mlir::dsdl::BitReadOp op, const ValueNames& names) const override
    {
        const auto        container = boolContainerOf(op.getDestination(), names);
        const std::string index     = fresh("bit");
        const std::string at        = fresh("at");
        const std::string into      = fresh("into");
        w.open("for " + index + " := 0; " + index + " < " + asInt(names(op.getWidth())) + "; " + index + "++ {");
        w.line(at + " := " + asInt(names(op.getBitOffset())) + " + " + index);
        w.line(into + " := " + container.second + " + " + index);
        w.line(container.first + "[" + into + "] = dsdlruntime.GetBit(" + names(op.getBuffer()) + ", " + at + ")");
        w.close("}");
    }

    [[nodiscard]] std::string callSerdes(mlir::dsdl::CallSerdesOp /*op*/, const ValueNames& /*names*/) const override
    {
        llvm::report_fatal_error("Go spelling: a nested call is a statement");
    }

    void declareCallSerdes(SourceWriter&            w,
                           const llvm::StringRef    name,
                           mlir::dsdl::CallSerdesOp op,
                           const ValueNames&        names) const override
    {
        // The nested value serialises itself into the slice from the buffer's offset, bounded by
        // the size the plan handed in; it answers the code and the size it used, and the size is
        // written back through the local where the plan reads it.
        const bool        serialize = op.getDirection() == "serialize";
        const std::string buffer    = names(op.getBuffer());
        const std::string size      = names(op.getSize());
        const std::string bound     = fresh("bound");
        const std::string used      = isRead(op.getSize()) ? fresh("used") : "_";
        w.line(bound + " := dsdlruntime.ChooseMin(" + size + ", len(" + buffer + "))");
        w.line((name.empty() ? "_" : name.str()) + ", " + used + (name.empty() && used == "_" ? " = " : " := ") +
               names(op.getObject()) + (serialize ? ".Serialize(" : ".Deserialize(") + buffer + "[:" + bound + "])");
        if (used != "_")
        {
            w.line(size + " = " + used);
        }
    }

private:
    struct Member final
    {
        std::string      goName;
        mlir::dsdl::IOOp io;
    };

    struct Plan final
    {
        std::string             typeName;
        std::int64_t            unionTagBits{0};
        llvm::StringMap<Member> members;
    };

    /// @brief The plan the object a pointer names belongs to.
    const Plan& planOf(const mlir::Value object) const
    {
        const auto pointer = mlir::dyn_cast<mlir::dsdl::PtrType>(object.getType());
        const auto identity =
            pointer ? mlir::dyn_cast<mlir::dsdl::ObjectType>(pointer.getPointee()) : mlir::dsdl::ObjectType{};
        const auto found = identity ? plans_.find(identity.getIdentity()) : plans_.end();
        if (found == plans_.end())
        {
            llvm::report_fatal_error("Go spelling: an object that no plan of this schema describes");
        }
        return found->second;
    }

    Member memberOf(const mlir::Value object, const llvm::StringRef member) const
    {
        const Plan& plan  = planOf(object);
        const auto  found = plan.members.find(member);
        if (found == plan.members.end())
        {
            llvm::report_fatal_error("Go spelling: a member the schema does not declare: " + member);
        }
        return found->second;
    }

    std::string memberAccess(const mlir::Value object, const llvm::StringRef member, const ValueNames& names) const
    {
        return names(object) + "." + memberOf(object, member).goName;
    }

    std::string elementAccess(const mlir::Value     object,
                              const llvm::StringRef member,
                              const std::string&    index,
                              const ValueNames&     names) const
    {
        return memberAccess(object, member, names) + "[" + asInt(index) + "]";
    }

    /// @brief Whether the plan reads the size @p pointer addresses back after handing it out.
    static bool isRead(const mlir::Value pointer)
    {
        return llvm::any_of(pointer.getUsers(),
                            [](mlir::Operation* user) { return mlir::isa<mlir::dsdl::LoadScalarOp>(user); });
    }

    /// @brief The container expression and element base of the bool array @p address names.
    std::pair<std::string, std::string> boolContainerOf(const mlir::Value address, const ValueNames& names) const
    {
        auto element = address.getDefiningOp<mlir::dsdl::ElementAddrOp>();
        if (!element)
        {
            llvm::report_fatal_error("Go spelling: a bit copy whose storage is not an array element");
        }
        return std::make_pair(memberAccess(element.getObject(), element.getMember(), names),
                              asInt(names(element.getIndex())));
    }

    /// @brief The Go type the struct declares a scalar field or element as.
    static std::string scalarType(mlir::dsdl::IOOp io)
    {
        const llvm::StringRef category = io.getScalarCategory();
        const auto            bits     = static_cast<std::uint32_t>(io.getBitLength());
        if (category == "bool")
        {
            return "bool";
        }
        if (category == "signed")
        {
            return signedStorageType(bits);
        }
        if (category == "float")
        {
            return bits == 64U ? "float64" : "float32";
        }
        return unsignedStorageType(bits);
    }

    /// @brief @p access read as a value of @p type.
    static std::string loadedValue(const std::string& access, const Member& member, const mlir::Type type)
    {
        const mlir::dsdl::IOOp io = member.io;
        if (scalarType(io) == "bool" && !isBool(type))
        {
            return typeName(type) + "(dsdlruntime.BoolToUint64(" + access + "))";
        }
        return typeName(type) + "(" + access + ")";
    }

    /// @brief @p value, of @p type, converted for storage in a field of @p storage.
    static std::string storedValue(const std::string& value, const mlir::Type type, const std::string& storage)
    {
        if (storage == "bool")
        {
            return isBool(type) ? value : value + " != uint64(0)";
        }
        return storage + "(" + value + ")";
    }

    // Types.

    static bool isBool(const mlir::Type type)
    {
        return type.isInteger(1);
    }

    /// @brief Whether @p type is spelled as a signed Go type.
    static bool isSignedSpelt(const mlir::Type type)
    {
        return type.isInteger(8);
    }

    static unsigned widthOf(const mlir::Type type)
    {
        if (const auto integer = mlir::dyn_cast<mlir::IntegerType>(type))
        {
            return integer.getWidth();
        }
        return 64U;
    }

    /// @brief @p value read as the signed type of its width.
    ///
    /// A literal is re-spelled as a signed literal: Go checks a constant conversion against the
    /// target's range, so the two's complement form of a negative value cannot be converted.
    static std::string asSigned(const llvm::StringRef value, const mlir::Type type)
    {
        const std::string target  = signedStorageType(widthOf(type));
        const std::string literal = literalDigits(value);
        if (!literal.empty())
        {
            const auto digits = llvm::APInt(widthOf(type), literal, 10);
            return target + "(" + std::to_string(digits.getSExtValue()) + ")";
        }
        return target + "(" + value.str() + ")";
    }

    static std::string asUnsigned(const llvm::StringRef value, const mlir::Type type)
    {
        return unsignedStorageType(widthOf(type)) + "(" + value.str() + ")";
    }

    static std::string asInt(const std::string& value)
    {
        return "int(" + value + ")";
    }

    /// @brief The digits of an unsigned literal `uintN(digits)`, else empty.
    static std::string literalDigits(const llvm::StringRef value)
    {
        if (!value.starts_with("uint") || !value.ends_with(")"))
        {
            return {};
        }
        const auto open = value.find('(');
        if (open == llvm::StringRef::npos)
        {
            return {};
        }
        const llvm::StringRef digits = value.slice(open + 1, value.size() - 1);
        return llvm::all_of(digits, [](const char c) { return c >= '0' && c <= '9'; }) && !digits.empty()
                   ? digits.str()
                   : std::string{};
    }

    static std::string typeName(const mlir::Type type)
    {
        if (mlir::isa<mlir::IndexType>(type))
        {
            return "int";
        }
        if (type.isF32())
        {
            return "float32";
        }
        if (type.isF64())
        {
            return "float64";
        }
        if (const auto integer = mlir::dyn_cast<mlir::IntegerType>(type))
        {
            if (integer.getWidth() == 1)
            {
                return "bool";
            }
            return isSignedSpelt(type) ? signedStorageType(integer.getWidth())
                                       : unsignedStorageType(integer.getWidth());
        }
        if (const auto pointer = mlir::dyn_cast<mlir::dsdl::PtrType>(type))
        {
            if (mlir::isa<mlir::dsdl::ByteType>(pointer.getPointee()))
            {
                return "[]byte";
            }
            if (mlir::isa<mlir::dsdl::SizeType>(pointer.getPointee()))
            {
                return "int";
            }
        }
        llvm::report_fatal_error("Go spelling: a value of a type no plan body carries");
    }

    static std::string operatorToken(const BinaryOperator op)
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

    static std::string comparisonToken(const Comparison comparison)
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

    /// @brief A name for a local the spelling introduces on its own, apart from the values.
    std::string fresh(const std::string& stem) const
    {
        return "_" + stem + std::to_string(counter_++) + "_";
    }

    llvm::StringMap<Plan> plans_;
    mutable std::size_t   counter_{0};
    mutable bool          inBody_{false};
};

/// @brief The two bodies `lower-dsdl-bodies` built for one section.
struct SectionBodies final
{
    mlir::func::FuncOp serialize;
    mlir::func::FuncOp deserialize;
};

llvm::Error emitSectionType(SourceWriter&                             w,
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
                            const mlir::dsdl::SerializationPlanOp     plan,
                            const GoSpelling&                         spelling,
                            const SectionBodies&                      bodies)
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
    const auto [zohAliasEligible, zohAliasReason] = aliasVerdict(plan);
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
        members.push_back(GoStructMember{"Tag", unsignedStorageType(unionTagBits(plan)), {}});
    }
    if (section.fields.empty())
    {
        members.push_back(GoStructMember{"_", "uint8", {}});
    }
    emitAlignedStructMembers(w, members);
    w.close("}");
    w.blank();

    if (!bodies.serialize || !bodies.deserialize)
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "no plan bodies for %s in the lowered module",
                                       fullName.c_str());
    }
    if (auto err = translateFunction(bodies.serialize, spelling, w))
    {
        return err;
    }
    w.blank();
    return translateFunction(bodies.deserialize, spelling, w);
}

llvm::Expected<std::string> renderDefinitionFile(const SemanticDefinition& def,
                                                 const EmitterContext&     ctx,
                                                 const std::string&        moduleName,
                                                 mlir::ModuleOp            module)
{
    mlir::dsdl::SchemaOp schema = schemaOf(module, def);
    if (!schema)
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "no schema for %s in the lowered module",
                                       def.info.fullName.c_str());
    }
    GoSpelling                           spelling(schema);
    std::vector<mlir::func::FuncOp>      helpers;
    std::map<std::string, SectionBodies> bodies;
    for (const mlir::func::FuncOp fn : schemaFunctions(module, schema.getSymName()))
    {
        const auto direction = planBodyDirection(fn);
        if (!direction)
        {
            helpers.push_back(fn);
            continue;
        }
        const auto     sectionAttr = fn->getAttrOfType<mlir::StringAttr>("llvmdsdl.section");
        SectionBodies& entry       = bodies[sectionAttr ? sectionAttr.getValue().str() : std::string{}];
        (*direction == "serialize" ? entry.serialize : entry.deserialize) = fn;
    }

    const auto currentPackagePath = EmitterContext::packagePath(def.info);
    const auto packageName        = packageNameFromPath(currentPackagePath);
    const auto imports            = computeImportAliases(def, ctx);
    const auto baseType           = ctx.goTypeName(def.info);
    const auto reqType            = baseType + renderSectionTypeSuffix(CodegenNamingLanguage::Go, "request");
    const auto respType           = baseType + renderSectionTypeSuffix(CodegenNamingLanguage::Go, "response");
    spelling.setTypeName(planIdentity(def.info.fullName, def.info.majorVersion, def.info.minorVersion, {}), baseType);
    spelling.setTypeName(planIdentity(def.info.fullName, def.info.majorVersion, def.info.minorVersion, "request"),
                         reqType);
    spelling.setTypeName(planIdentity(def.info.fullName, def.info.majorVersion, def.info.minorVersion, "response"),
                         respType);

    // The declarations and bodies first: whether the runtime is imported depends on whether a
    // body calls it, and Go rejects an import nothing uses.
    std::ostringstream body;
    SourceWriter       w = makeGoWriter(body);
    for (const mlir::func::FuncOp helper : helpers)
    {
        if (auto err = translateFunction(helper, spelling, w))
        {
            return std::move(err);
        }
        w.blank();
    }
    if (!def.isService)
    {
        if (auto err = emitSectionType(w,
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
                                       sectionPlan(schema, ""),
                                       spelling,
                                       bodies[""]))
        {
            return std::move(err);
        }
    }
    else
    {
        if (auto err = emitSectionType(w,
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
                                       sectionPlan(schema, "request"),
                                       spelling,
                                       bodies["request"]))
        {
            return std::move(err);
        }
        w.blank();
        if (def.response)
        {
            if (auto err = emitSectionType(w,
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
                                           sectionPlan(schema, "response"),
                                           spelling,
                                           bodies["response"]))
            {
                return std::move(err);
            }
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
    }

    std::ostringstream out;
    SourceWriter       head = makeGoWriter(out);
    head.line(generatedCommentLine("Go backend"));
    head.line("// Source: " + def.info.fullName + "." + std::to_string(def.info.majorVersion) + "." +
              std::to_string(def.info.minorVersion));
    head.blank();
    head.line("package " + packageName);
    head.blank();
    const bool usesRuntime = llvm::StringRef(body.str()).contains("dsdlruntime.");
    if (usesRuntime || !imports.empty())
    {
        head.open("import (");
        if (usesRuntime)
        {
            head.line("dsdlruntime \"" + moduleName + "/dsdlruntime\"");
        }
        for (const auto& [path, alias] : imports)
        {
            // NOLINTNEXTLINE(performance-inefficient-string-concatenation)
            head.line(alias + " \"" + moduleName + "/" + path + "\"");
        }
        head.close(")");
        head.blank();
    }
    out << body.str();
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

std::string renderGoMod(const Options& options)
{
    std::ostringstream out;
    out << generatedCommentLine("Go backend module metadata") << "\n";
    out << "module " << options.moduleName << "\n\n";
    out << "go 1.22\n";
    return out.str();
}

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

    // Go alone cannot express unversioned names where it matters. A DSDL namespace
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

    const EmitterContext ctx(semantic, options.typeNameVersioning);

    for (const auto& def : semantic.definitions)
    {
        if (!shouldEmitDefinition(def.info, selectedTypeKeys, options.supportGeneration))
        {
            continue;
        }
        const std::vector<std::string> requiredTypeKeys{definitionTypeKey(def.info)};

        const auto            dirRel = EmitterContext::packagePath(def.info);
        std::filesystem::path dir    = outRoot;
        if (!dirRel.empty())
        {
            dir /= dirRel;
        }
        auto file = renderDefinitionFile(def, ctx, options.moduleName, module);
        if (!file)
        {
            return file.takeError();
        }
        if (auto err = writeGeneratedFile(dir / EmitterContext::goFileName(def.info),
                                          *file,
                                          options.writePolicy,
                                          requiredTypeKeys))
        {
            return err;
        }
    }

    return llvm::Error::success();
}

}  // namespace llvmdsdl::emitter::go
