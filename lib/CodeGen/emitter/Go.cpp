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
#include "llvmdsdl/CodeGen/DeclarationRenderer.h"
#include "llvmdsdl/CodeGen/EmitCommon.h"
#include "llvmdsdl/CodeGen/ImportSet.h"
#include "llvmdsdl/CodeGen/SectionNaming.h"
#include "llvmdsdl/CodeGen/EmbeddedSources.h"
#include "llvmdsdl/CodeGen/emitter/Go.h"
#include "llvmdsdl/CodeGen/emitter/GoPackaging.h"

#include <llvm/ADT/StringRef.h>
#include <array>
#include <algorithm>
#include <cassert>
#include <cctype>  // IWYU pragma: keep -- libstdc++ reaches this transitively; libc++ needs it named.
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "llvmdsdl/CodeGen/ConstantLiteralRender.h"
#include "llvmdsdl/CodeGen/DefinitionIndex.h"
#include "llvmdsdl/CodeGen/SchemaLookup.h"
#include "llvmdsdl/CodeGen/InitializerRender.h"
#include "llvmdsdl/CodeGen/TypeMetadata.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/CodeGen/StorageTypeTokens.h"
#include "llvmdsdl/CodeGen/TypeStorage.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include "llvmdsdl/CodeGen/SourceWriter.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Frontend/SourceLocation.h"
#include "llvmdsdl/Semantics/Evaluator.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Version.h"
#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/IR/DSDLTypes.h"
#include "llvmdsdl/Transforms/PlanSteps.h"
#include "llvmdsdl/Transforms/SurfaceTree.h"
#include "llvmdsdl/Support/GeneratedFact.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/PlanSymbol.h"
#include "llvmdsdl/Support/SurfacePlan.h"
#include "llvmdsdl/Support/Language.h"
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
#include <mlir/IR/OwningOpRef.h>
#include <iomanip>
#include "mlir/IR/BuiltinOps.h"

namespace llvmdsdl::emitter::go
{

namespace
{

/// @brief The receiver of @p typeName's methods: the initial of its head noun, the name's last word,
///        as Go names a receiver for what its type is. `ListRequest` is `r` and `NodeID` is `i`.
std::string goReceiverName(const llvm::StringRef typeName)
{
    std::size_t last = typeName.size();
    while ((last > 0) && !llvm::isUpper(typeName[last - 1]))
    {
        --last;
    }
    if (last == 0)
    {
        return "o";
    }
    --last;
    // A capital that a lower-case letter follows starts a word; one that ends the name, or a digit
    // follows, ends an acronym, which is one word.
    std::size_t start = last;
    if ((last + 1 == typeName.size()) || !llvm::isLower(typeName[last + 1]))
    {
        while ((start > 0) && llvm::isUpper(typeName[start - 1]))
        {
            --start;
        }
    }
    return std::string(1, llvm::toLower(typeName[start]));
}

std::string unsignedStorageType(const std::uint32_t bitLength)
{
    return renderUnsignedStorageToken(Language::Go, bitLength);
}

std::string signedStorageType(const std::uint32_t bitLength)
{
    return renderSignedStorageToken(Language::Go, bitLength);
}

std::string goConstValue(const TypeExprAST& type, const Value& value)
{
    return renderConstantLiteral(Language::Go, value, makeConstantTypeInfo(type));
}

// gofmt is not configurable and indents with tabs; generated files that disagree churn in any
// consumer with format-on-save or a gofmt gate.
SourceWriter makeGoWriter(std::ostringstream& out)
{
    return SourceWriter{out, IndentPolicy::tabs()};
}

/// @brief The line that marks a file as generated, in the form Go's tools read as that mark.
std::string generatedCommentLine(llvm::StringRef detail)
{
    return llvm::formatv("// Code generated by llvmdsdl {0} ({1}). DO NOT EDIT.", llvmdsdl::kVersionString, detail)
        .str();
}

/// @brief A package's doc file: its package clause, under the comment Go reads as the package's
///        documentation.
/// @param[in] name The package's name.
/// @param[in] dsdlNamespace The DSDL namespace the package holds, dotted.
std::string renderPackageDoc(const llvm::StringRef name, const llvm::StringRef dsdlNamespace)
{
    std::ostringstream out;
    SourceWriter       w = makeGoWriter(out);
    w.line(generatedCommentLine("Go backend"));
    w.blank();
    w.line("// Package " + name.str() + " holds the definitions of DSDL namespace " + dsdlNamespace.str() + ".");
    w.line("package " + name.str());
    return out.str();
}

/// @brief @p doc, opened by the sentence a Go doc comment begins with: the name it documents, and
///        what that is. A declaration with no doc keeps none.
/// @param[in] name The Go name the comment documents.
/// @param[in] entity What the name is, as a noun phrase: `uavcan.node.Heartbeat.1.0`.
/// @param[in] doc The documentation.
AttachedDoc nameLedDoc(const std::string& name, const std::string& entity, AttachedDoc doc)
{
    if (doc.lines.empty())
    {
        return doc;
    }
    // The location is synthetic: doc emission renders only the text of each line.
    const SourceLocation location{"<go-doc>", 1, 1};
    doc.lines.insert(doc.lines.begin(),
                     {AttachedDocLine{location, name + " is " + entity + "."}, AttachedDocLine{location, ""}});
    return doc;
}

/// @brief What @p section's type is, as a noun phrase: the definition, or a section of it.
std::string sectionEntity(const SemanticDefinition& def, const SectionMetadata& metadata, const std::string& section)
{
    const std::string definition =
        def.info.fullName + "." + std::to_string(metadata.majorVersion) + "." + std::to_string(metadata.minorVersion);
    return section.empty() ? definition : "the " + section + " of " + definition;
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

/// @brief The key of the definition @p info describes, which its schema and the surface name it by.
std::string keyOf(const DiscoveredDefinition& info)
{
    return renderDefinitionKey(definitionRef(info));
}

/// @brief The key of the definition @p ref names.
std::string keyOf(const SemanticTypeRef& ref)
{
    return renderDefinitionKey(definitionRef(ref));
}

/// @brief The names Go's output declares and the files it writes, as the surface declares them.
class GoSurface final
{
public:
    explicit GoSurface(const SurfaceTree& tree)
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

    /// @brief The file the definition keyed @p key is written to, from the module's root.
    [[nodiscard]] const std::string& path(const llvm::StringRef key) const
    {
        return tree_.scope(file(key)).path;
    }

    /// @brief The directory of the definition's package, from the module's root; empty for the
    ///        root's own.
    [[nodiscard]] std::string packageDirectory(const llvm::StringRef key) const
    {
        const std::string& written = path(key);
        const std::size_t  slash   = written.rfind('/');
        return (slash == std::string::npos) ? std::string{} : written.substr(0, slash);
    }

    /// @brief The file beside the definition's own whose name ends in @p suffix instead.
    [[nodiscard]] std::string besideFile(const llvm::StringRef key, const llvm::StringRef suffix) const
    {
        const std::string& written = path(key);
        return written.substr(0, written.size() - languageTraits(Language::Go).composition.fileExtension.size()) +
               suffix.str();
    }

    /// @brief The name of the package the definition is declared in.
    [[nodiscard]] const std::string& packageName(const llvm::StringRef key) const
    {
        return tree_.scope(*tree_.scope(file(key)).parent).name;
    }

    /// @brief The name of @p section's type.
    [[nodiscard]] const std::string& typeName(const llvm::StringRef key, const llvm::StringRef section) const
    {
        return tree_.scope(tree_.typeScope(key, section)).name;
    }

    /// @brief The name the definition is reached by: its type's, or a service's alias of its request.
    [[nodiscard]] const std::string& definitionTypeName(const llvm::StringRef key) const
    {
        const SurfaceDecl* const alias =
            tree_.find(file(key), SurfaceDeclKind::Alias, SurfaceEntity{key.str(), {}, {}, {}});
        return (alias != nullptr) ? alias->name : typeName(key, {});
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

    /// @brief The name of the declaration of @p kind that stands for the lowered function @p symbol,
    ///        stating @p fact.
    [[nodiscard]] const std::string& function(const llvm::StringRef              symbol,
                                              const SurfaceDeclKind              kind,
                                              const std::optional<GeneratedFact> fact = std::nullopt) const
    {
        return tree_.nameOf(symbol, kind, fact);
    }

    /// @brief The local name the definition's file imports each other package under, by the
    ///        package's directory.
    [[nodiscard]] std::map<std::string, std::string> imports(const llvm::StringRef key) const
    {
        std::map<std::string, std::string> out;
        for (const SurfaceItem& item : tree_.scope(file(key)).items)
        {
            const SurfaceDecl* const decl = item.scope ? nullptr : &tree_.plan().decls[item.index];
            if ((decl != nullptr) && (decl->kind == SurfaceDeclKind::Import) && decl->of)
            {
                out.emplace(packageDirectory(decl->of->schema), decl->name);
            }
        }
        return out;
    }

private:
    const SurfaceTree& tree_;
};

class EmitterContext final
{
public:
    EmitterContext(const SemanticModule& semantic, const GoSurface& names, const bool accessorsOnly)
        : index_(semantic)
        , names_(names)
        , accessorsOnly_(accessorsOnly)
    {
    }

    /// @brief The names the output declares.
    const GoSurface& names() const
    {
        return names_;
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
    DefinitionIndex  index_;
    const GoSurface& names_;
    bool             accessorsOnly_{false};
};

/// @brief How one Go file names what it takes from other packages, recording each import.
///
/// Every symbol the file writes from another package is named here, so the import block written
/// from the set once the file is rendered holds what the file names and nothing else; Go refuses to
/// compile an import nothing uses.
class GoFileNames final
{
public:
    GoFileNames(const EmitterContext&              ctx,
                ImportSet&                         imports,
                std::string                        moduleName,
                std::string                        ownPackage,
                std::map<std::string, std::string> packageAliases)
        : ctx_(ctx)
        , imports_(imports)
        , moduleName_(std::move(moduleName))
        , ownPackage_(std::move(ownPackage))
        , packageAliases_(std::move(packageAliases))
    {
    }

    /// @brief The type of the definition @p ref, qualified by its package's alias where the package
    ///        is another.
    [[nodiscard]] std::string type(const SemanticTypeRef& ref) const
    {
        const std::string  key  = keyOf(ref);
        const std::string  path = ctx_.names().packageDirectory(key);
        const std::string& name = ctx_.names().typeName(key, {});
        if (path.empty() || (path == ownPackage_))
        {
            return name;
        }
        const auto alias = packageAliases_.find(path);
        if (alias == packageAliases_.end())
        {
            return name;
        }
        return imports_.module(ImportOrigin::Definition, moduleName_ + "/" + path, alias->second) + "." + name;
    }

    /// @brief The runtime package the bodies call.
    [[nodiscard]] std::string runtime() const
    {
        return imports_.module(ImportOrigin::Runtime, moduleName_ + "/dsdlruntime", "dsdlruntime");
    }

    /// @brief The standard library's package @p package.
    [[nodiscard]] std::string standard(const llvm::StringRef package) const
    {
        return imports_.module(ImportOrigin::Standard, package, package);
    }

private:
    const EmitterContext&              ctx_;
    ImportSet&                         imports_;
    std::string                        moduleName_;
    std::string                        ownPackage_;
    std::map<std::string, std::string> packageAliases_;
};

/// @brief Writes the import declaration of a Go file that names @p imports: the standard library's
///        packages, then the module's own, each group in the order of its paths, as gofmt sorts it.
void writeGoImports(SourceWriter& w, const ImportSet& imports)
{
    std::vector<std::string> standard;
    // A group of the module's own sorts by path, which is past the alias each line opens with.
    std::vector<std::pair<std::string, std::string>> own;
    for (const ImportedModule& module : imports.modules())
    {
        if (module.origin == ImportOrigin::Standard)
        {
            standard.push_back("\"" + module.path + "\"");
        }
        else
        {
            own.emplace_back(module.path, module.binding + " \"" + module.path + "\"");
        }
    }
    if (standard.empty() && own.empty())
    {
        return;
    }
    std::ranges::sort(own);
    w.open("import (");
    for (const std::string& line : standard)
    {
        w.line(line);
    }
    if (!standard.empty() && !own.empty())
    {
        w.blank();
    }
    for (const auto& [path, line] : own)
    {
        w.line(line);
    }
    w.close(")");
    w.blank();
}

std::string goBaseFieldType(const SemanticFieldType& type, const GoFileNames& file)
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
            return file.type(*type.compositeType);
        }
        return "uint8";
    }
    return "uint8";
}

/// @brief The type a member held as a view is: a slice of the buffer, or an array of them.
std::string goViewType(const SemanticFieldType& type)
{
    if (type.arrayKind == ArrayKind::None)
    {
        return "[]byte";
    }
    if (type.arrayKind == ArrayKind::Fixed)
    {
        return "[" + std::to_string(type.arrayCapacity) + "][]byte";
    }
    return "[][]byte";
}

std::string goFieldType(const SemanticFieldType& type, const GoFileNames& file)
{
    auto base = goBaseFieldType(type, file);
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
class GoSpelling final : public BodySpelling, public WireImageSpelling
{
public:
    GoSpelling(mlir::dsdl::SchemaOp schema, const GoSurface& names, const GoFileNames& file)
        : names_(names)
        , file_(file)
    {
        if (schema.getBody().empty())
        {
            return;
        }
        const std::string key = schema.getSymName().str();
        for (mlir::dsdl::SerializationPlanOp plan : schema.getBody().front().getOps<mlir::dsdl::SerializationPlanOp>())
        {
            const llvm::StringRef section = plan.getSection().value_or(llvm::StringRef{});
            Plan                  entry;
            entry.unionTagBits = plan.getUnionTagBits().value_or(0);
            entry.typeName     = names.typeName(key, section);
            if (!plan.getBody().empty())
            {
                for (mlir::dsdl::IOOp io : plan.getBody().front().getOps<mlir::dsdl::IOOp>())
                {
                    if (!io.isPadding())
                    {
                        entry.members[io.getName()] = Member{names.member(key, section, io.getName()), io};
                    }
                }
            }
            // The union's tag, reached by its accessors as a member is: the wire holds it ahead
            // of the option, and no field can be named `_tag_`.
            if (plan.getIsUnion())
            {
                tagSteps_.push_back(unionTagStep(schema->getContext(), plan.getUnionTagBits().value_or(0)));
                entry.members[kPlanUnionTagMember] =
                    Member{names.member(key, section, {}, GeneratedFact::UnionTag), tagSteps_.back().get()};
            }
            plans_[planIdentity(schema, plan)] = std::move(entry);
        }
    }

    // Functions.

    /// @brief Opens a body under the signature `GoDeclarations` wrote, binding what the body reads
    ///        in the plan's types where the signature speaks the member's.
    std::vector<std::string> openFunction(SourceWriter& w, mlir::func::FuncOp fn) const override
    {
        const auto direction = planBodyDirection(fn);
        accessor_            = Accessor::None;
        answersError_        = false;
        cannotFail_          = fn->hasAttr("llvmdsdl.infallible");
        if (!direction)
        {
            std::vector<std::string> parameters;
            for (const auto [index, argument] : llvm::enumerate(fn.getArguments()))
            {
                parameters.push_back("p" + std::to_string(index));
            }
            return parameters;
        }
        if (*direction == "get" || *direction == "set")
        {
            return openAccessor(w, fn, *direction == "get");
        }
        const std::string receiver = goReceiverName(planOf(fn.getArgument(0)).typeName);
        if ((*direction == "append_wire_image") || (*direction == "wire_image") || (*direction == "read_wire_image"))
        {
            const bool               reader = *direction == "read_wire_image";
            std::vector<std::string> parameters{receiver};
            for (std::size_t index = 1; index < fn.getNumArguments(); ++index)
            {
                parameters.emplace_back(reader ? "data" : "buffer");
            }
            answersError_ = reader;
            return parameters;
        }
        return {receiver, "buffer"};
    }

    /// @brief The signature of @p fn, declared as @p name: its receiver where it is a method, its
    ///        parameters, and what it answers.
    [[nodiscard]] std::string signature(mlir::func::FuncOp fn, const std::string& name) const
    {
        const auto direction = planBodyDirection(fn);
        if (!direction)
        {
            std::string list;
            for (const auto [index, argument] : llvm::enumerate(fn.getArguments()))
            {
                list += (list.empty() ? "" : ", ") + ("p" + std::to_string(index)) + " " + typeName(argument.getType());
            }
            return "func " + name + "(" + list + ") " + typeName(fn.getResultTypes().front());
        }
        if (*direction == "get" || *direction == "set")
        {
            return accessorSignature(fn, name, *direction == "get");
        }
        const Plan&       plan   = planOf(fn.getArgument(0));
        const std::string method = "func (" + goReceiverName(plan.typeName) + " *" + plan.typeName + ") " + name;
        if ((*direction == "append_wire_image") || (*direction == "wire_image") || (*direction == "read_wire_image"))
        {
            // The encoding package's interfaces: the bytes an encoder appends to or a reader reads,
            // and the bytes and the runtime's error it answers.
            std::string list;
            for (const mlir::Type type : fn.getArgumentTypes().drop_front())
            {
                list += (list.empty() ? "" : ", ") +
                        std::string(*direction == "read_wire_image" ? "data " : "buffer ") + typeName(type);
            }
            std::vector<std::string> results;
            for (const mlir::Type type : fn.getResultTypes())
            {
                results.push_back(type.isInteger(8) ? std::string{"error"} : typeName(type));
            }
            return method + "(" + list + ") " +
                   ((results.size() == 1) ? results.front() : "(" + llvm::join(results, ", ") + ")");
        }
        return method + "(buffer []byte) (int, error)";
    }

    void closeFunction(SourceWriter& w, mlir::func::FuncOp /*fn*/) const override
    {
        w.close("}");
    }

    [[nodiscard]] std::string valueName(const ValueRole       role,
                                        const llvm::StringRef member,
                                        const std::size_t     ordinal) const override
    {
        // The snake rendering, projected as Go names a local: the same fold as every other
        // language's, with Go's initialisms and its case.
        return codegenProjectIdentifier(Language::Go, IdentifierRole::LocalName, snakeValueName(role, member, ordinal));
    }

    [[nodiscard]] llvm::ArrayRef<llvm::StringRef> reservedLocals() const override
    {
        // Go's predeclared identifiers, whole: a body converts with the type names, measures with
        // len and compares against nil, and any of them is a name a local may legally take.
        static const llvm::StringRef names[] = {"any",
                                                "bool",
                                                "byte",
                                                "comparable",
                                                "complex64",
                                                "complex128",
                                                "error",
                                                "float32",
                                                "float64",
                                                "int",
                                                "int8",
                                                "int16",
                                                "int32",
                                                "int64",
                                                "rune",
                                                "string",
                                                "uint",
                                                "uint8",
                                                "uint16",
                                                "uint32",
                                                "uint64",
                                                "uintptr",
                                                "true",
                                                "false",
                                                "iota",
                                                "nil",
                                                "append",
                                                "cap",
                                                "clear",
                                                "close",
                                                "complex",
                                                "copy",
                                                "delete",
                                                "imag",
                                                "len",
                                                "make",
                                                "max",
                                                "min",
                                                "new",
                                                "panic",
                                                "print",
                                                "println",
                                                "real",
                                                "recover",
                                                // The runtime package, reached by its name.
                                                "dsdlruntime"};
        return names;
    }

    [[nodiscard]] std::string functionName(const llvm::StringRef callee) const override
    {
        return names_.function(callee, SurfaceDeclKind::Helper);
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

    /// @brief A getter's or a setter's signature: a package-level function named after the type and
    ///        the member, taking the buffer as a slice and speaking the member's own type.
    [[nodiscard]] std::string accessorSignature(mlir::func::FuncOp fn, const std::string& name, const bool getter) const
    {
        const std::string storage = scalarType(accessed(fn).member->io);
        const mlir::Type  answer  = fn.getResultTypes().front();
        const bool        indexed = fn.getNumArguments() == (getter ? 3U : 4U);
        const std::string index   = indexed ? ", elementIndex int" : "";
        if (getter && mlir::isa<mlir::dsdl::PtrType>(answer))
        {
            // The nested type's buffer, as a slice, which carries its own length.
            return "func " + name + "(buffer []byte" + index + ") []byte";
        }
        if (getter)
        {
            return "func " + name + "(buffer []byte" + index + ") " + storage;
        }
        const bool integer = mlir::isa<mlir::IntegerType>(fn.getArgument(indexed ? 3 : 2).getType());
        return "func " + name + "(buffer []byte" + index + ", " + (integer ? "memberValue " : "value ") + storage +
               ") error";
    }

    /// @brief Opens a getter or a setter. The plan holds the size, an index and an integer in a
    ///        `uint64`: the size is the slice's own length, an expression rather than a local, since
    ///        a slice read needs no size beside it; an index and a value are bound at entry.
    std::vector<std::string> openAccessor(SourceWriter& w, mlir::func::FuncOp fn, const bool getter) const
    {
        const std::string storage = scalarType(accessed(fn).member->io);
        const mlir::Type  answer  = fn.getResultTypes().front();
        const bool        indexed = fn.getNumArguments() == (getter ? 3U : 4U);
        const mlir::Type  held    = getter ? answer : fn.getArgument(indexed ? 3 : 2).getType();
        const bool        integer = mlir::isa<mlir::IntegerType>(held);
        accessor_                 = getter ? Accessor::Getter : Accessor::Setter;
        returnCast_.clear();
        if (getter && !mlir::isa<mlir::dsdl::PtrType>(answer))
        {
            if (storage == "bool")
            {
                returnCast_ = " != 0";
            }
            else if (integer)
            {
                returnCast_ = storage;
            }
        }
        std::vector<std::string> parameters{"buffer", "uint64(len(buffer))"};
        if (indexed)
        {
            w.line("index := uint64(elementIndex)");
            parameters.emplace_back("index");
        }
        if (!getter)
        {
            if (storage == "bool")
            {
                w.line("value := " + file_.runtime() + ".BoolToUint64(memberValue)");
            }
            else if (integer)
            {
                w.line("value := uint64(memberValue)");
            }
            parameters.emplace_back("value");
        }
        return parameters;
    }

    void returnValue(SourceWriter& w, const llvm::StringRef expr) const override
    {
        // A getter answers the value in the member's own type; a setter answers the code alone.
        if (accessor_ == Accessor::Getter)
        {
            if (returnCast_ == " != 0")
            {
                w.line("return " + expr.str() + " != 0");
            }
            else if (!returnCast_.empty())
            {
                w.line("return " + returnCast_ + "(" + expr.str() + ")");
            }
            else
            {
                w.line("return " + expr.str());
            }
            return;
        }
        // A setter and a reader answer the runtime's error, and nothing where marked unable to fail.
        if ((accessor_ == Accessor::Setter) || answersError_)
        {
            w.line(cannotFail_ ? std::string{"return nil"}
                               : "return " + file_.runtime() + ".ErrorOf(" + expr.str() + ")");
            return;
        }
        w.line("return " + expr.str());
    }

    void returnWithSize(SourceWriter& w, const llvm::StringRef error, const llvm::StringRef used) const override
    {
        // The runtime's error on failure, and the size used on success. Where the body is marked
        // unable to fail there is no failure to test for.
        if (!cannotFail_)
        {
            w.open("if " + error.str() + " != int8(0) {");
            w.line("return 0, " + file_.runtime() + ".ErrorOf(" + error.str() + ")");
            w.close("}");
        }
        w.line("return " + used.str() + ", nil");
    }

    [[nodiscard]] std::string bufferLength(mlir::dsdl::BufferLengthOp op, const ValueNames& names) const override
    {
        return "uint64(len(" + names(op.getBuffer()) + "))";
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
            return typeName(to) + "(" + file_.runtime() + ".BoolToUint64(" + value.str() + "))";
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
        return mlir::isa<mlir::dsdl::IsNullOp,
                         mlir::dsdl::BufferOrEmptyOp,
                         mlir::dsdl::MemberAddrOp,
                         mlir::dsdl::ElementAddrOp,
                         mlir::dsdl::BytesAtOp,
                         mlir::dsdl::BytesTruncateOp,
                         mlir::dsdl::CopyBufferOp>(op);
    }

    [[nodiscard]] std::string indexHolds(mlir::dsdl::IndexHoldsOp op, const ValueNames& names) const override
    {
        // Through int and back: a count past the range of the target's int does not come back as
        // itself.
        const std::string value = names(op.getValue());
        return "(uint64(int(" + value + ")) == " + value + ")";
    }

    [[nodiscard]] std::string isNotNull(mlir::dsdl::IsNullOp op, const ValueNames& names) const override
    {
        const auto pointer = mlir::cast<mlir::dsdl::PtrType>(op.getPointer().getType());
        if (mlir::isa<mlir::dsdl::ObjectType>(pointer.getPointee()))
        {
            return names(op.getPointer()) + " != nil";
        }
        return "true";
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
        return buffer + "[" + file_.runtime() + ".ChooseMin(" + asInt(names(op.getByteOffset())) + ", len(" + buffer +
               ")):]";
    }

    [[nodiscard]] std::string loadScalar(mlir::dsdl::LoadScalarOp /*op*/, const ValueNames& /*names*/) const override
    {
        // A body's size reaches Go as its buffer's length and a second result.
        llvm::report_fatal_error("Go spelling: a size pointer reaches Go only folded");
    }

    void storeScalar(SourceWriter& /*w*/, mlir::dsdl::StoreScalarOp /*op*/, const ValueNames& /*names*/) const override
    {
        llvm::report_fatal_error("Go spelling: a size pointer reaches Go only folded");
    }

    [[nodiscard]] std::string local(SourceWriter& /*w*/,
                                    mlir::dsdl::LocalOp /*op*/,
                                    const llvm::StringRef /*name*/,
                                    const ValueNames& /*names*/) const override
    {
        // A body's only local is the size of a nested call, which reaches Go folded to
        // `dsdl.call_serdes_sized`.
        llvm::report_fatal_error("Go spelling: a local reaches Go only as a nested call's size");
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
        // Sized to the count the plan validated, with zeroed elements for the plan to store into.
        const std::string access = memberAccess(op.getObject(), op.getMember(), names);
        w.line(access + " = " + file_.runtime() + ".Resize(" + access + ", " + asInt(names(op.getValue())) + ")");
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
            return file_.runtime() + ".SetF" + std::to_string(width) + "(" + prefix + value + ")";
        }
        if (width == 1 && !op.getIsSigned())
        {
            return file_.runtime() + ".SetBit(" + prefix + (isBool(valueType) ? value : value + " != uint64(0)") + ")";
        }
        if (op.getIsSigned())
        {
            return file_.runtime() + ".SetIxx(" + prefix + asSigned(value, valueType) + ", uint8(" +
                   std::to_string(width) + "))";
        }
        return file_.runtime() + ".SetUxx(" + prefix + value + ", uint8(" + std::to_string(width) + "))";
    }

    [[nodiscard]] std::string readBits(mlir::dsdl::ReadBitsOp op, const ValueNames& names) const override
    {
        const mlir::Type  valueType = op.getValue().getType();
        const auto        width     = static_cast<std::int64_t>(op.getWidth());
        const std::string prefix    = names(op.getBuffer()) + ", " + asInt(names(op.getBitOffset()));
        if (mlir::isa<mlir::FloatType>(valueType))
        {
            return file_.runtime() + ".GetF" + std::to_string(width) + "(" + prefix + ")";
        }
        const std::string result = typeName(valueType);
        if (width == 1 && !op.getIsSigned())
        {
            // The bit read answers a number, which is what the plan holds it as.
            return result + "(" + file_.runtime() + ".GetU8(" + prefix + ", uint8(1)))";
        }
        // The runtime answers in the narrowest standard width that holds the field; a signed read
        // arrives sign-extended and keeps its value across the widening.
        const unsigned holder = holderWidthFor(width);
        return result + "(" + file_.runtime() + ".Get" + std::string(op.getIsSigned() ? "I" : "U") +
               std::to_string(holder) + "(" + prefix + ", uint8(" + std::to_string(width) + ")))";
    }

    void bitWrite(SourceWriter& /*w*/, mlir::dsdl::BitWriteOp /*op*/, const ValueNames& /*names*/) const override
    {
        // A bool array holds a bool per element, so each of its runs reaches Go expanded.
        llvm::report_fatal_error("Go spelling: a bool run reaches Go expanded to dsdl.write_bit");
    }

    void bitRead(SourceWriter& /*w*/, mlir::dsdl::BitReadOp /*op*/, const ValueNames& /*names*/) const override
    {
        llvm::report_fatal_error("Go spelling: a bool run reaches Go expanded to dsdl.read_bit");
    }

    void writeBit(SourceWriter& w, mlir::dsdl::WriteBitOp op, const ValueNames& names) const override
    {
        w.line("_ = " + file_.runtime() + ".SetBit(" + names(op.getBuffer()) + ", " + asInt(names(op.getBitOffset())) +
               ", " + names(op.getValue()) + ")");
    }

    [[nodiscard]] std::string readBit(mlir::dsdl::ReadBitOp op, const ValueNames& names) const override
    {
        return file_.runtime() + ".GetBit(" + names(op.getBuffer()) + ", " + asInt(names(op.getBitOffset())) + ")";
    }

    void imageRead(SourceWriter& w, mlir::dsdl::ImageReadOp op, const ValueNames& names) const override
    {
        // The object's bytes, viewed in place: every field of a host image is an integer, a float,
        // a fixed array of those or a nested host image, so any bytes are a valid value of it. A
        // short buffer is a valid encoding, so what is there is moved and the rest zeroed, which is
        // what reading each field would have produced.
        const std::string bytes = std::to_string(op.getBytes());
        const std::string image = fresh("image");
        const std::string avail = fresh("avail");
        w.line(image + " := " + file_.standard("unsafe") + ".Slice((*byte)(" + file_.standard("unsafe") + ".Pointer(" +
               names(op.getObject()) + ")), " + bytes + ")");
        w.line(avail + " := " + file_.runtime() + ".ChooseMin(" + asInt(names(op.getBufferSizeBytes())) + ", len(" +
               names(op.getBuffer()) + "))");
        // The object and the buffer may be the same storage, so the bytes present are copied before
        // what follows them is cleared: clearing first would clear the source. `copy` is defined
        // for overlapping slices, so the copy itself needs nothing else.
        w.open("if " + avail + " >= " + bytes + " {");
        w.line("copy(" + image + ", " + names(op.getBuffer()) + "[:" + bytes + "])");
        w.midway("} else {");
        w.line("copy(" + image + ", " + names(op.getBuffer()) + "[:" + avail + "])");
        w.line("clear(" + image + "[" + avail + ":])");
        w.close("}");
    }

    void imageWrite(SourceWriter& w, mlir::dsdl::ImageWriteOp op, const ValueNames& names) const override
    {
        // The buffer has been checked to hold the payload by the time this runs.
        const std::string bytes = std::to_string(op.getBytes());
        w.line("copy(" + names(op.getBuffer()) + "[:" + bytes + "], " + file_.standard("unsafe") + ".Slice((*byte)(" +
               file_.standard("unsafe") + ".Pointer(" + names(op.getObject()) + ")), " + bytes + "))");
    }

    // A view member is a slice of the buffer, or one element of an array of them.
    /// @brief A view member, or the element of an array of views that @p index names.
    std::string viewTarget(const mlir::Value     object,
                           const llvm::StringRef member,
                           const mlir::Value     index,
                           const ValueNames&     names) const
    {
        return index ? elementAccess(object, member, names(index), names) : memberAccess(object, member, names);
    }

    [[nodiscard]] std::string viewBytes(mlir::dsdl::LoadViewOp op, const ValueNames& names) const override
    {
        return viewTarget(op.getObject(), op.getMember(), op.getIndex(), names);
    }

    [[nodiscard]] std::string viewSize(mlir::dsdl::LoadViewOp op, const ValueNames& names) const override
    {
        return "uint64(len(" + viewTarget(op.getObject(), op.getMember(), op.getIndex(), names) + "))";
    }

    void storeView(SourceWriter& w, mlir::dsdl::StoreViewOp op, const ValueNames& names) const override
    {
        const std::string bytes = names(op.getBytes());
        w.line(viewTarget(op.getObject(), op.getMember(), op.getIndex(), names) + " = " + bytes +
               "[:" + file_.runtime() + ".ChooseMin(" + asInt(names(op.getSizeBytes())) + ", len(" + bytes + "))]");
    }

    void clearView(SourceWriter& w, mlir::dsdl::ClearViewOp op, const ValueNames& names) const override
    {
        // A fixed array of views is its zero value, every element nil; a slice is nil.
        mlir::dsdl::IOOp io = memberOf(op.getObject(), op.getMember()).io;
        w.line(memberAccess(op.getObject(), op.getMember(), names) + " = " +
               ((io.getArrayKind() == "fixed") ? "[" + std::to_string(io.getArrayCapacity()) + "][]byte{}" : "nil"));
    }

    void copyBytes(SourceWriter& w, mlir::dsdl::CopyBytesOp op, const ValueNames& names) const override
    {
        // What the view holds, up to the width, then zeros to the width. The plan's capacity check
        // established the width at the destination.
        const std::string destination = names(op.getDestination());
        const std::string source      = names(op.getSource());
        const std::string width       = std::to_string(op.getBytes());
        const std::string copied      = fresh("copied");
        const std::string index       = fresh("i");
        w.line(copied + " := copy(" + destination + "[:" + width + "], " + source + "[:" + file_.runtime() +
               ".ChooseMin(" + asInt(names(op.getSourceSizeBytes())) + ", len(" + source + "))])");
        w.open("for " + index + " := " + copied + "; " + index + " < " + width + "; " + index + "++ {");
        w.line(destination + "[" + index + "] = 0");
        w.close("}");
    }

    [[nodiscard]] std::string callSerdes(mlir::dsdl::CallSerdesOp /*op*/, const ValueNames& /*names*/) const override
    {
        llvm::report_fatal_error("Go spelling: a nested call reaches Go folded to dsdl.call_serdes_sized");
    }

    void declareCallSerdes(SourceWriter& /*w*/,
                           const llvm::StringRef /*name*/,
                           mlir::dsdl::CallSerdesOp /*op*/,
                           const ValueNames& /*names*/) const override
    {
        llvm::report_fatal_error("Go spelling: a nested call reaches Go folded to dsdl.call_serdes_sized");
    }

    void declareCallSerdesSized(SourceWriter&                 w,
                                const llvm::StringRef         error,
                                const llvm::StringRef         consumed,
                                mlir::dsdl::CallSerdesSizedOp op,
                                const ValueNames&             names) const override
    {
        // The nested value serialises itself into the slice from the buffer's offset, bounded by the
        // space the plan offers, and answers what it used and its error, which the plan carries as
        // the runtime's code.
        const std::string buffer = names(op.getBuffer());
        const std::string slice  = op.getAvailable() ? buffer + "[:" + file_.runtime() + ".ChooseMin(" +
                                                           asInt(names(op.getAvailable())) + ", len(" + buffer + "))]"
                                                     : buffer;
        const std::string call =
            names(op.getObject()) + (op.getDirection() == "serialize" ? ".Serialize(" : ".Deserialize(") + slice + ")";
        const std::string used   = consumed.empty() ? std::string{"_"} : consumed.str();
        const std::string bound  = error.empty() ? used + ", _" : error.str() + ", " + used;
        const std::string answer = error.empty() ? call : file_.runtime() + ".Coded(" + call + ")";
        w.line(bound + ((error.empty() && consumed.empty()) ? " = " : " := ") + answer);
    }

    [[nodiscard]] const WireImageSpelling* wireImages() const override
    {
        return this;
    }

    // A whole wire image, in a byte slice.

    [[nodiscard]] std::string bytesZeroed(mlir::dsdl::BytesZeroedOp op, const ValueNames& names) const override
    {
        return "make([]byte, " + names(op.getLength()) + ")";
    }

    [[nodiscard]] std::string bytesLength(mlir::dsdl::BytesLengthOp op, const ValueNames& names) const override
    {
        return "uint64(len(" + names(op.getBytes()) + "))";
    }

    [[nodiscard]] std::string bytesGrow(mlir::dsdl::BytesGrowOp op, const ValueNames& names) const override
    {
        // Appending a made slice extends in place where the capacity allows, and allocates no
        // temporary.
        return "append(" + names(op.getBytes()) + ", make([]byte, " + names(op.getCount()) + ")...)";
    }

    [[nodiscard]] std::string bytesAt(mlir::dsdl::BytesAtOp op, const ValueNames& names) const override
    {
        return op.getOffset() ? names(op.getBytes()) + "[" + names(op.getOffset()) + ":]" : names(op.getBytes());
    }

    [[nodiscard]] std::string bytesTruncate(mlir::dsdl::BytesTruncateOp op, const ValueNames& names) const override
    {
        return names(op.getBytes()) + "[:" + names(op.getLength()) + "]";
    }

    [[nodiscard]] std::string copyBuffer(mlir::dsdl::CopyBufferOp op, const ValueNames& names) const override
    {
        return file_.standard("bytes") + ".Clone(" + names(op.getBuffer()) + ")";
    }

    void returnImage(SourceWriter& w, const llvm::StringRef bytes, const llvm::StringRef error) const override
    {
        w.line("return " + bytes.str() + ", " + file_.runtime() + ".ErrorOf(" + error.str() + ")");
    }

    [[nodiscard]] std::string makeObject(mlir::dsdl::MakeObjectOp op, const ValueNames& /*names*/) const override
    {
        llvm::report_fatal_error(llvm::Twine("Go spelling: Go's row makes no object from a wire image; '") +
                                 op.getInitializer() + "' reached one");
    }

    void returnObject(SourceWriter& /*w*/,
                      const llvm::StringRef /*object*/,
                      const llvm::StringRef /*used*/,
                      const llvm::StringRef /*error*/) const override
    {
        llvm::report_fatal_error("Go spelling: Go's row answers no object read from a wire image");
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
    [[nodiscard]] std::string loadedValue(const std::string& access, const Member& member, const mlir::Type type) const
    {
        if (isBool(type))
        {
            return access;
        }
        if (scalarType(member.io) == "bool")
        {
            return typeName(type) + "(" + file_.runtime() + ".BoolToUint64(" + access + "))";
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
        if (mlir::isa<mlir::dsdl::BytesType>(type))
        {
            return "[]byte";
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

    const GoSurface&      names_;
    llvm::StringMap<Plan> plans_;
    /// @brief The tag steps of the union plans, which belong to no plan and live here.
    std::vector<mlir::OwningOpRef<mlir::dsdl::IOOp>> tagSteps_;

    /// @brief The plan and the member an accessor reaches, through its schema and section name.
    struct Accessed final
    {
        const Plan*   plan;
        const Member* member;
    };
    Accessed accessed(mlir::func::FuncOp fn) const
    {
        auto       module     = fn->getParentOfType<mlir::ModuleOp>();
        const auto schemaSym  = fn->getAttrOfType<mlir::StringAttr>("llvmdsdl.schema_sym");
        const auto section    = fn->getAttrOfType<mlir::StringAttr>("llvmdsdl.section");
        const auto memberName = fn->getAttrOfType<mlir::StringAttr>("llvmdsdl.member");
        auto       schema =
            schemaSym ? module.lookupSymbol<mlir::dsdl::SchemaOp>(schemaSym.getValue()) : mlir::dsdl::SchemaOp{};
        if (!schema || !memberName)
        {
            llvm::report_fatal_error("Go spelling: an accessor that names no schema or no member");
        }
        const auto plan  = sectionPlan(schema, section ? section.getValue() : llvm::StringRef{});
        const auto found = plans_.find(planIdentity(schema, plan));
        if (found == plans_.end())
        {
            llvm::report_fatal_error("Go spelling: an accessor of a plan this schema does not describe");
        }
        const auto member = found->second.members.find(memberName.getValue());
        if (member == found->second.members.end())
        {
            llvm::report_fatal_error("Go spelling: an accessor of a member the plan does not declare");
        }
        return Accessed{&found->second, &member->second};
    }

    /// @brief Which accessor, if any, the function being opened is; how its return is spelt.
    enum class Accessor : std::uint8_t
    {
        None,
        Getter,
        Setter
    };
    mutable Accessor    accessor_{Accessor::None};
    mutable std::string returnCast_;

    /// @brief Whether the function being opened answers the runtime's error alone, as a reader does.
    mutable bool answersError_{false};

    /// @brief How the file names what it takes from other packages.
    const GoFileNames& file_;

    /// @brief Whether the function being spelt is marked unable to fail, by `dsdl-mark-infallible-bodies`.
    mutable bool        cannotFail_{false};
    mutable std::size_t counter_{0};
};

/// @brief Whether a stored constant is its type's zero. No constant -- a length of nought, a bool
/// array -- is zero.
bool goStoredValueIsZero(const mlir::TypedAttr value)
{
    if (const auto integer = mlir::dyn_cast_or_null<mlir::IntegerAttr>(value))
    {
        return integer.getInt() == 0;
    }
    if (const auto real = mlir::dyn_cast_or_null<mlir::FloatAttr>(value))
    {
        return real.getValueAsDouble() == 0.0;
    }
    return true;
}

/// @brief The Go literal of a stored constant, for a member of @p type.
std::string goStoredLiteral(const mlir::TypedAttr value, const SemanticFieldType& type)
{
    const auto integer = mlir::dyn_cast_or_null<mlir::IntegerAttr>(value);
    if (type.scalarCategory == SemanticScalarCategory::Bool)
    {
        return (integer && integer.getInt() != 0) ? "true" : "false";
    }
    if (integer)
    {
        return std::to_string(integer.getInt());
    }
    const auto real = mlir::dyn_cast_or_null<mlir::FloatAttr>(value);
    return std::to_string(real ? real.getValueAsDouble() : 0.0);
}

/// @brief The statement `lhs<op>rhs`.
std::string goAssignment(const std::string& lhs, const char* const op, const std::string& rhs)
{
    std::string line = lhs;
    line += op;
    line += rhs;
    return line;
}

/// @brief The call that constructs a value of the definition @p ref names, through the package the
///        file imports it from.
std::string goConstructorOf(const SemanticTypeRef& ref, const GoFileNames& file, const GoSurface& names)
{
    const std::string  qualified = file.type(ref);
    const auto         dot       = qualified.rfind('.');
    const std::string& made =
        names
            .function(renderPlanSymbol(
                          planFunction(ref.fullName, ref.majorVersion, ref.minorVersion, {}, PlanFunction::Initialize)),
                      SurfaceDeclKind::Entry);
    return ((dot == std::string::npos) ? std::string{} : qualified.substr(0, dot + 1)) + made + "()";
}

/// @brief Whether an initialise body, and every nested body it calls, stores only zeros.
///
/// Go's zero value stands in for such a body. A nested member is zero only if the body it is
/// initialised through is, so the question is asked of that body in turn.
llvm::Expected<bool> goInitializerIsZero(const InitializerShape& shape, mlir::ModuleOp module)
{
    if (shape.unionTag != 0)
    {
        return false;
    }
    for (const auto& entry : shape.members)
    {
        switch (entry.kind)
        {
        case MemberDefault::Kind::Scalar:
        case MemberDefault::Kind::FixedScalarArray:
            if (!goStoredValueIsZero(entry.value))
            {
                return false;
            }
            break;
        case MemberDefault::Kind::VariableArrayEmpty:
        case MemberDefault::Kind::BoolArray:
        case MemberDefault::Kind::View:
            break;
        case MemberDefault::Kind::Composite:
        case MemberDefault::Kind::FixedCompositeArray: {
            auto body = module.lookupSymbol<mlir::func::FuncOp>(entry.callee);
            if (!body)
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "an initialise body calls %s, which the lowered module does not hold",
                                               entry.callee.c_str());
            }
            auto nested = readInitializer(body);
            if (!nested)
            {
                return nested.takeError();
            }
            auto zero = goInitializerIsZero(*nested, module);
            if (!zero || !*zero)
            {
                return zero;
            }
            break;
        }
        }
    }
    return true;
}

/// @brief A section's constructor, as its initialise body states it.
struct GoConstructor final
{
    /// @brief The initialise body's symbol, which the constructor is declared by.
    std::string symbol;

    InitializerShape shape;

    /// @brief Whether each nested initialiser a member is set through stores only zeros, by callee.
    std::map<std::string, bool> nestedZero;
};

/// @brief The constructor @p initialize states for the definition @p fullName; none where every store
///        is its type's zero, which Go's zero value stands in for.
///
/// A body that stores anything else has no zero value to lean on and gets a constructor that sets
/// what the body sets, member by member and element by element.
llvm::Expected<std::optional<GoConstructor>> readGoConstructor(mlir::func::FuncOp initialize,
                                                               const std::string& fullName)
{
    auto init = readInitializer(initialize);
    if (!init)
    {
        return init.takeError();
    }
    auto module  = initialize->getParentOfType<mlir::ModuleOp>();
    auto allZero = goInitializerIsZero(*init, module);
    if (!allZero)
    {
        return allZero.takeError();
    }
    if (*allZero)
    {
        return std::nullopt;
    }
    GoConstructor constructor{initialize.getSymName().str(), *init, {}};
    for (const auto& entry : init->members)
    {
        if ((entry.kind != MemberDefault::Kind::Composite) && (entry.kind != MemberDefault::Kind::FixedCompositeArray))
        {
            continue;
        }
        auto body = module.lookupSymbol<mlir::func::FuncOp>(entry.callee);
        if (!body)
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "the initialise body of %s calls %s, which the lowered module does not hold",
                                           fullName.c_str(),
                                           entry.callee.c_str());
        }
        auto nested = readInitializer(body);
        if (!nested)
        {
            return nested.takeError();
        }
        auto zero = goInitializerIsZero(*nested, module);
        if (!zero)
        {
            return zero.takeError();
        }
        constructor.nestedZero[entry.callee] = *zero;
    }
    return constructor;
}

/// @brief Go's layout: a file per definition, holding its helpers, then each section's constants,
///        type, layout checks, constructor and functions, then a service's alias and constants.
const DeclarationLayout& goLayout()
{
    static const DeclarationLayout layout{
        .files   = {{LayoutPart::Prelude,
                     LayoutPart::Imports,
                     LayoutPart::HelperDefinitions,
                     LayoutPart::Sections,
                     LayoutPart::Alias}},
        .section = {LayoutPart::SectionConstants,
                    LayoutPart::Type,
                    LayoutPart::LayoutChecks,
                    LayoutPart::Methods,
                    LayoutPart::SectionDefinitions},
        .indent  = IndentPolicy::tabs(),
    };
    return layout;
}

/// @brief Spells Go's declarations: a file per definition, holding each section's constants, its
///        struct and its methods, and the accessors and constructor beside it.
class GoDeclarations final : public DeclarationSpelling
{
public:
    /// @param[in] ctx The run.
    /// @param[in] file How the file names what it takes from other packages.
    /// @param[in] imports What the file named, which @p file records.
    /// @param[in] types The Go spelling of the IR's types and signatures.
    /// @param[in] constructors Each section's constructor, by section, where it has one.
    GoDeclarations(const EmitterContext&                       ctx,
                   const GoFileNames&                          file,
                   const ImportSet&                            imports,
                   const GoSpelling&                           types,
                   const std::map<std::string, GoConstructor>& constructors)
        : ctx_(ctx)
        , file_(file)
        , imports_(imports)
        , types_(types)
        , constructors_(constructors)
    {
    }

    void write(DeclarationSite& site, const LayoutPart part) const override
    {
        switch (part)
        {
        case LayoutPart::Prelude:
            prelude(site);
            return;
        case LayoutPart::SectionConstants:
            constants(site);
            return;
        case LayoutPart::Type:
            type(site);
            return;
        case LayoutPart::LayoutChecks:
            layoutChecks(site);
            return;
        case LayoutPart::Methods:
            constructor(site);
            return;
        case LayoutPart::Alias:
            alias(site);
            return;
        default:
            return;
        }
    }

    [[nodiscard]] std::string imports(const DeclarationSite& /*site*/) const override
    {
        std::ostringstream out;
        SourceWriter       w = makeGoWriter(out);
        writeGoImports(w, imports_);
        // No empty line after the block, which the renderer writes.
        std::string block = out.str();
        while (block.ends_with("\n\n"))
        {
            block.pop_back();
        }
        return block;
    }

    [[nodiscard]] std::string signature(const SurfaceDecl& decl, mlir::func::FuncOp fn) const override
    {
        return types_.signature(fn, decl.name);
    }

    void prototype(SourceWriter& /*w*/, const std::string& /*signature*/) const override
    {
        llvm::report_fatal_error("Go declares no function ahead of its definition");
    }

    void openDefinition(SourceWriter& w, const SurfaceDecl& decl, const std::string& signature) const override
    {
        // The encoding package's interfaces say what each is for.
        const std::optional<PlanSymbol> symbol = decl.of ? parsePlanSymbol(decl.of->function) : std::nullopt;
        if (symbol)
        {
            const std::string receiver = goReceiverName(ctx_.names().typeName(decl.of->schema, decl.of->section));
            switch (symbol->function)
            {
            case PlanFunction::AppendWireImage:
                w.line("// " + decl.name + " appends the wire image of " + receiver +
                       " to buffer, as encoding.BinaryAppender asks.");
                break;
            case PlanFunction::WireImage:
                w.line("// " + decl.name + " answers the wire image of " + receiver +
                       ", as encoding.BinaryMarshaler asks.");
                break;
            case PlanFunction::ReadWireImage:
                w.line("// " + decl.name + " reads " + receiver +
                       " from its wire image, as encoding.BinaryUnmarshaler asks.");
                break;
            default:
                break;
            }
        }
        w.open(signature + " {");
    }

    void forward(SourceWriter& /*w*/,
                 const SurfaceDecl& /*published*/,
                 llvm::StringRef /*callee*/,
                 mlir::func::FuncOp /*fn*/) const override
    {
        llvm::report_fatal_error("Go publishes no function over another");
    }

private:
    void prelude(DeclarationSite& site) const
    {
        const DiscoveredDefinition& info = site.facts().definition().info;
        SourceWriter&               w    = site.writer();
        w.line(generatedCommentLine("Go backend"));
        w.line("// Source: " + info.fullName + "." + std::to_string(info.majorVersion) + "." +
               std::to_string(info.minorVersion));
        w.blank();
        w.line("package " + ctx_.names().packageName(site.facts().key()));
    }

    /// @brief The section's facts, a union's option tags, then its DSDL constants.
    void constants(DeclarationSite& site) const
    {
        const GoSurface&       names    = ctx_.names();
        const std::string&     key      = site.facts().key();
        const std::string&     section  = *site.section();
        const SectionMetadata& metadata = site.facts().metadata(section);
        SourceWriter&          w        = site.writer();
        const auto             meta     = [&](const GeneratedFact fact) {
            return names.declared(key, SurfaceDeclKind::Constant, section, {}, fact);
        };
        const auto boolean = [](const bool value) { return std::string(value ? "true" : "false"); };
        w.line("const " + meta(GeneratedFact::FullName) + " = \"" + metadata.fullName + "\"");
        w.line("const " + meta(GeneratedFact::IsDeprecated) + " = " + boolean(metadata.deprecated));
        w.line("const " + meta(GeneratedFact::FullNameAndVersion) + " = \"" + metadata.fullName + "." +
               std::to_string(metadata.majorVersion) + "." + std::to_string(metadata.minorVersion) + "\"");
        w.line("const " + meta(GeneratedFact::ExtentBytes) + " = " + std::to_string(metadata.extentBytes));
        w.line("const " + meta(GeneratedFact::SerializationBufferSizeBytes) + " = " +
               std::to_string(metadata.serializationBufferSizeBytes));
        w.line("const " + meta(GeneratedFact::WireFlat) + " = " + boolean(metadata.wireFlat.holds));
        w.line("const " + meta(GeneratedFact::WireFlatReason) + " = \"" + metadata.wireFlat.reason + "\"");
        w.line("const " + meta(GeneratedFact::HostImage) + " = " + boolean(metadata.hostImage.holds));
        w.line("const " + meta(GeneratedFact::HostImageReason) + " = \"" + metadata.hostImage.reason + "\"");
        if (metadata.declaresPortId)
        {
            w.line("const " + meta(GeneratedFact::HasFixedPortId) + " = " + boolean(metadata.fixedPortId.has_value()));
            if (metadata.fixedPortId)
            {
                w.line("const " + meta(GeneratedFact::FixedPortId) + " = " + std::to_string(*metadata.fixedPortId));
            }
        }
        if (metadata.isUnion)
        {
            w.line("const " + meta(GeneratedFact::UnionOptionCount) + " = " +
                   std::to_string(metadata.unionOptions.size()));
            for (const auto& option : metadata.unionOptions)
            {
                w.line("const " + names.declared(key, SurfaceDeclKind::Option, section, option.name) + " " +
                       unsignedStorageType(metadata.unionTagBits) + " = " + std::to_string(option.tag));
            }
        }
        for (const auto& c : site.facts().section(section).constants)
        {
            // gofmt separates a documented declaration from whatever precedes it, so a doc
            // comment landing directly under another constant needs the blank line first.
            if (!c.doc.lines.empty())
            {
                w.blank();
            }
            const std::string& name = names.declared(key, SurfaceDeclKind::Constant, section, c.name);
            emitAttachedDocGo(w,
                              nameLedDoc(name,
                                         c.name + " of " + sectionEntity(site.facts().definition(), metadata, section),
                                         c.doc));
            w.line("const " + name + " = " + goConstValue(c.type, c.value));
        }
    }

    /// @brief The section's struct, which an accessors-only run leaves out.
    void type(DeclarationSite& site) const
    {
        if (ctx_.accessorsOnly())
        {
            return;
        }
        const GoSurface&          names    = ctx_.names();
        const std::string&        key      = site.facts().key();
        const std::string&        section  = *site.section();
        const SectionMetadata&    metadata = site.facts().metadata(section);
        const SemanticSection&    parts    = site.facts().section(section);
        const SemanticDefinition& def      = site.facts().definition();
        SourceWriter&             w        = site.writer();
        emitAttachedDocGo(w,
                          nameLedDoc(names.typeName(key, section),
                                     sectionEntity(def, metadata, section),
                                     docWithDeprecationNotice(def.doc,
                                                              parts.deprecated,
                                                              def.info.fullName,
                                                              metadata.majorVersion,
                                                              metadata.minorVersion)));
        w.open("type " + names.typeName(key, section) + " struct {");
        // gofmt aligns a struct's types into a column, and a doc comment starts a fresh
        // one: the members are collected first so each run's width is known before any of
        // it is written.
        std::vector<GoStructMember> members;
        for (const auto& field : parts.fields)
        {
            if (field.isPadding)
            {
                continue;
            }
            members.push_back(GoStructMember{names.member(key, section, field.name),
                                             field.heldAsView ? goViewType(field.resolvedType)
                                                              : goFieldType(field.resolvedType, file_),
                                             field.doc});
        }
        if (parts.isUnion)
        {
            // Tag storage must match the wire tag width (uint8 for <=256 options, uint16 for
            // 257..65536, etc.); a hardcoded uint8 truncates a wide tag and mis-dispatches.
            members.push_back(GoStructMember{names.member(key, section, {}, GeneratedFact::UnionTag),
                                             unsignedStorageType(unionTagBits(site.facts().plan(section))),
                                             {}});
        }
        if (parts.fields.empty())
        {
            members.push_back(GoStructMember{"_", "uint8", {}});
        }
        emitAlignedStructMembers(w, members);
        w.close("}");
    }

    /// @brief The layout the struct's host image was decided under, pinned on the architecture the
    ///        package is compiled for. A mismatch is an index out of bounds, or a uintptr overflow.
    void layoutChecks(DeclarationSite& site) const
    {
        const std::string&     section  = *site.section();
        const SectionMetadata& metadata = site.facts().metadata(section);
        if (ctx_.accessorsOnly() || !metadata.hostImage.holds || metadata.hostImageMembers.empty())
        {
            return;
        }
        const GoSurface&   names    = ctx_.names();
        const std::string& key      = site.facts().key();
        const std::string& typeName = names.typeName(key, section);
        SourceWriter&      w        = site.writer();
        // NOLINTBEGIN(performance-inefficient-string-concatenation)
        w.line("var _ = [1]struct{}{}[" + file_.standard("unsafe") + ".Sizeof(" + typeName + "{})-" +
               std::to_string(metadata.serializationBufferSizeBytes) + "]");
        for (const auto& member : metadata.hostImageMembers)
        {
            w.line("var _ = [1]struct{}{}[" + file_.standard("unsafe") + ".Offsetof(" + typeName + "{}." +
                   names.member(key, section, member.fieldName) + ")-" + std::to_string(member.offsetBytes) + "]");
        }
        // NOLINTEND(performance-inefficient-string-concatenation)
    }

    /// @brief The section's constructor, where its initialiser stores anything but zeros.
    void constructor(DeclarationSite& site) const
    {
        const std::string& section = *site.section();
        const auto         found   = constructors_.find(section);
        if (found == constructors_.end())
        {
            return;
        }
        const GoConstructor& made     = found->second;
        const GoSurface&     names    = ctx_.names();
        const std::string&   key      = site.facts().key();
        const std::string&   typeName = names.typeName(key, section);
        SourceWriter&        w        = site.writer();
        w.open("func " + names.function(made.symbol, SurfaceDeclKind::Entry) + "() " + typeName + " {");
        w.line("var obj " + typeName);
        for (const auto& field : site.facts().section(section).fields)
        {
            if (field.isPadding)
            {
                continue;
            }
            for (const auto& entry : made.shape.members)
            {
                if (entry.member != field.name)
                {
                    continue;
                }
                const auto member = "obj." + names.member(key, section, field.name);
                const auto stored = goStoredLiteral(entry.value, field.resolvedType);
                switch (entry.kind)
                {
                case MemberDefault::Kind::Scalar:
                    if (!goStoredValueIsZero(entry.value))
                    {
                        w.line(goAssignment(member, " = ", stored));
                    }
                    break;
                case MemberDefault::Kind::FixedScalarArray:
                    if (!goStoredValueIsZero(entry.value))
                    {
                        w.open("for i := range " + member + " {");
                        w.line(goAssignment(member, "[i] = ", stored));
                        w.close("}");
                    }
                    break;
                case MemberDefault::Kind::Composite:
                case MemberDefault::Kind::FixedCompositeArray: {
                    if (made.nestedZero.at(entry.callee))
                    {
                        break;
                    }
                    const auto value = goConstructorOf(*field.resolvedType.compositeType, file_, names);
                    if (entry.kind == MemberDefault::Kind::Composite)
                    {
                        w.line(goAssignment(member, " = ", value));
                    }
                    else
                    {
                        w.open("for i := range " + member + " {");
                        w.line(goAssignment(member, "[i] = ", value));
                        w.close("}");
                    }
                    break;
                }
                case MemberDefault::Kind::VariableArrayEmpty:
                case MemberDefault::Kind::BoolArray:
                case MemberDefault::Kind::View:
                    break;
                }
            }
        }
        if (made.shape.isUnion && made.shape.unionTag != 0)
        {
            w.line("obj." + names.member(key, section, {}, GeneratedFact::UnionTag) + " = " +
                   std::to_string(made.shape.unionTag));
        }
        w.line("return obj");
        w.close("}");
    }

    /// @brief A service's alias, the name the service is known by, and the constants it states
    ///        through it. An accessors-only run declares no alias.
    void alias(DeclarationSite& site) const
    {
        const SemanticDefinition& def = site.facts().definition();
        if (!def.isService)
        {
            return;
        }
        const GoSurface&   names = ctx_.names();
        const std::string& key   = site.facts().key();
        SourceWriter&      w     = site.writer();
        if (!ctx_.accessorsOnly())
        {
            w.line("type " + names.definitionTypeName(key) + " = " + names.typeName(key, "request"));
        }
        // gofmt separates top-level declarations of different kinds, so the alias and the
        // constants that follow it do not sit together.
        site.separate();
        const auto service = [&](const GeneratedFact fact) {
            return names.declared(key, SurfaceDeclKind::Constant, {}, {}, fact);
        };
        w.line("const " + service(GeneratedFact::HasFixedPortId) + " = " +
               std::string(def.info.fixedPortId ? "true" : "false"));
        if (def.info.fixedPortId)
        {
            w.line("const " + service(GeneratedFact::FixedPortId) + " = " + std::to_string(*def.info.fixedPortId));
        }
    }

    const EmitterContext&                       ctx_;
    const GoFileNames&                          file_;
    const ImportSet&                            imports_;
    const GoSpelling&                           types_;
    const std::map<std::string, GoConstructor>& constructors_;
};

llvm::Expected<std::string> renderDefinitionFile(const SemanticDefinition& def,
                                                 const EmitterContext&     ctx,
                                                 const std::string&        moduleName,
                                                 mlir::ModuleOp            module,
                                                 PlanBodyLookups&          lookups)
{
    mlir::dsdl::SchemaOp schema = schemaOf(module, def);
    if (!schema)
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "no schema for %s in the lowered module",
                                       def.info.fullName.c_str());
    }
    // The declarations and bodies first, naming what they take from other packages as they write
    // it; the import declaration is written after, from what was named.
    const GoSurface&  names = ctx.names();
    const std::string key   = keyOf(def.info);
    ImportSet         imports;
    const GoFileNames file(ctx, imports, moduleName, names.packageDirectory(key), names.imports(key));
    const GoSpelling  spelling(schema, names, file);
    // The functions the file translates: every one but a helper nothing calls, which an
    // accessors-only run has many of, and an initialiser, which is Go's zero value or a constructor
    // the declarations write.
    FunctionBodies                       bodies{.functions = {},
                                                .spelling  = spelling,
                                                .lookups   = lookups,
                                                .row       = languageTraits(Language::Go)};
    std::map<std::string, GoConstructor> constructors;
    for (const mlir::func::FuncOp fn : schemaFunctions(module, schema.getSymName()))
    {
        if (fn->hasAttr("llvmdsdl.unreferenced"))
        {
            continue;
        }
        if (planBodyDirection(fn) == "initialize")
        {
            auto made = readGoConstructor(fn, def.info.fullName);
            if (!made)
            {
                return made.takeError();
            }
            if (*made)
            {
                const auto section = fn->getAttrOfType<mlir::StringAttr>("llvmdsdl.section");
                constructors.emplace(section ? section.getValue().str() : std::string{}, std::move(**made));
            }
            continue;
        }
        bodies.functions.push_back(fn);
    }
    const GoDeclarations declarations(ctx, file, imports, spelling, constructors);
    return DeclarationRenderer(names.tree(), goLayout(), declarations)
        .render(names.file(key), 0, DefinitionFacts(def, schema), &bodies);
}

llvm::Expected<std::string> loadGoRuntime()
{
    if (const auto data = embedded_sources::find("go/dsdl_runtime.go"))
    {
        return std::string(*data);
    }
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "embedded runtime source missing: go/dsdl_runtime.go");
}

}  // namespace

/// @brief A file that fails to compile on a big-endian architecture, naming the reason.
///
/// A folded body moves the object as the wire's bytes, which holds only where the host orders them
/// as the wire does. Go decides the architecture when the package is built, so the refusal is a
/// build constraint. The list is the standard library's own, from `encoding/binary`'s native
/// order, and it is the little-endian list rather than its complement: an architecture on neither
/// is refused, not trusted.
namespace
{

std::string renderHostImageGuard(const SemanticDefinition& def, const EmitterContext& ctx)
{
    const std::string key = keyOf(def.info);
    const std::string version =
        def.info.fullName + "." + std::to_string(def.info.majorVersion) + "." + std::to_string(def.info.minorVersion);
    const std::string ident =
        codegenProjectIdentifier(Language::Go, IdentifierRole::ConstantName, ctx.names().definitionTypeName(key));
    std::ostringstream out;
    SourceWriter       w = makeGoWriter(out);
    w.line(generatedCommentLine("Go backend"));
    w.line("// Source: " + version);
    w.line("//go:build !(386 || amd64 || amd64p32 || alpha || arm || arm64 || loong64 || mipsle || mips64le || "
           "mips64p32le || nios2 || ppc64le || riscv || riscv64 || sh || wasm)");
    w.blank();
    w.line("package " + ctx.names().packageName(key));
    w.blank();
    w.line("// " + version + ": its serialisation moves the object as the wire's bytes, which holds only on a");
    w.line("// little-endian target. Regenerate with --target-triple naming this target.");
    // A field of the empty struct rather than a package-scope name: the type has no fields and no
    // declaration can give it one, whereas a package-scope name is one a definition's own constant
    // can spell, which would leave this file compiling and the refusal gone.
    w.line("var _ = struct{}{}." + ident + "_SERIALISATION_HOLDS_ONLY_ON_A_LITTLE_ENDIAN_TARGET");
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
            if (auto err = writeGeneratedFile(outRoot / "go.mod",
                                              renderGoMod(options, generatedCommentLine("Go backend module metadata")),
                                              options.writePolicy))
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

    auto tree = SurfaceTree::read(module, languageTraits(Language::Go));
    if (!tree)
    {
        return tree.takeError();
    }
    const GoSurface      names(*tree);
    const EmitterContext ctx(semantic, names, options.accessorsOnly);
    PlanBodyLookups      lookups(module);
    // Each package scope a written file is in: the DSDL namespace it holds, and the definitions.
    std::map<std::size_t, std::pair<std::string, std::vector<std::string>>> packages;
    for (const auto& def : semantic.definitions)
    {
        if (!shouldEmitDefinition(def.info, selectedTypeKeys, options.supportGeneration))
        {
            continue;
        }
        const std::vector<std::string> requiredTypeKeys{definitionTypeKey(def.info)};

        const std::string key  = keyOf(def.info);
        auto              file = renderDefinitionFile(def, ctx, options.moduleName, module, lookups);
        if (!file)
        {
            return file.takeError();
        }
        if (auto err = writeGeneratedFile(outRoot / names.path(key), *file, options.writePolicy, requiredTypeKeys))
        {
            return err;
        }
        const bool folded = options.hostImageFolded && !options.accessorsOnly &&
                            (def.request.hostImage.holds || (def.response && def.response->hostImage.holds));
        if (folded)
        {
            if (auto err = writeGeneratedFile(outRoot / names.besideFile(key, "_host_image.go"),
                                              renderHostImageGuard(def, ctx),
                                              options.writePolicy,
                                              requiredTypeKeys))
            {
                return err;
            }
        }
        const std::size_t package = *tree->scope(names.file(key)).parent;
        if (!tree->scope(package).path.empty())
        {
            packages[package].first = llvm::join(def.info.namespaceComponents, ".");
            packages[package].second.push_back(definitionTypeKey(def.info));
        }
    }

    // Each package this run writes a file of is documented by its doc file.
    for (const auto& [package, held] : packages)
    {
        if (auto err = writeGeneratedFile(outRoot / tree->scope(package).path,
                                          renderPackageDoc(tree->scope(package).name, held.first),
                                          options.writePolicy,
                                          held.second))
        {
            return err;
        }
    }

    return llvm::Error::success();
}

}  // namespace llvmdsdl::emitter::go
