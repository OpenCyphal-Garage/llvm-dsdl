//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The declaration half of a backend: a renderer that walks one file scope of a target's surface
/// tree and asks a `DeclarationSpelling` for the language's syntax, as `translateFunction` asks a
/// `BodySpelling`.
///
/// The renderer holds the order of a file's parts, from the language's layout; the one empty line
/// between two parts; the import block, placed where the layout names it after the declarations
/// that recorded it; and every lowered function's declaration and definition, whose signature the
/// spelling writes once and whose body `translateFunction` writes. Names and placement come from
/// the surface tree, and what the tree does not hold -- a field's type, a constant's value, a
/// section's metadata, a doc -- from the definition's facts.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_DECLARATION_RENDERER_H
#define LLVMDSDL_CODEGEN_DECLARATION_RENDERER_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>

#include "llvmdsdl/CodeGen/BodyTranslator.h"
#include "llvmdsdl/CodeGen/SourceWriter.h"
#include "llvmdsdl/CodeGen/TypeMetadata.h"
#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/SurfacePlan.h"
#include "llvmdsdl/Transforms/SurfaceTree.h"

namespace llvmdsdl
{

/// @brief What a definition states that its surface tree does not: its sections, each section's
///        metadata and plan, and the lowered functions its declarations name.
class DefinitionFacts final
{
public:
    /// @param[in] definition The definition.
    /// @param[in] schema Its schema, in the module that holds its lowered functions.
    DefinitionFacts(const SemanticDefinition& definition, mlir::dsdl::SchemaOp schema);

    [[nodiscard]] const SemanticDefinition& definition() const;

    /// @brief The definition's key, which its schema and the surface tree name it by.
    [[nodiscard]] const std::string& key() const;

    [[nodiscard]] mlir::dsdl::SchemaOp schema() const;

    /// @brief The definition's sections as the surface tree keys them: a message's one, by the empty
    ///        name, or a service's request and response.
    [[nodiscard]] llvm::ArrayRef<std::string> sections() const;

    /// @brief The section keyed @p name.
    [[nodiscard]] const SemanticSection& section(llvm::StringRef name) const;

    /// @brief The metadata of the section keyed @p name.
    [[nodiscard]] const SectionMetadata& metadata(llvm::StringRef name) const;

    /// @brief The plan of the section keyed @p name.
    [[nodiscard]] mlir::dsdl::SerializationPlanOp plan(llvm::StringRef name) const;

    /// @brief The lowered function @p decl stands for; null where it names none.
    [[nodiscard]] mlir::func::FuncOp function(const SurfaceDecl& decl) const;

private:
    [[nodiscard]] std::size_t position(llvm::StringRef name) const;

    const SemanticDefinition*    definition_;
    std::string                  key_;
    mlir::dsdl::SchemaOp         schema_;
    std::vector<std::string>     sections_;
    std::vector<SectionMetadata> metadata_;
};

/// @brief One part of a file, in the order a language's layout names them.
///
/// The renderer writes the parts that declare and define lowered functions, and the import block.
/// The language's spelling writes the others.
enum class LayoutPart : std::uint8_t
{
    Prelude,              ///< What opens the file.
    Guards,               ///< What keeps the file from being read with one it conflicts with.
    Imports,              ///< What the file's declarations name from other files.
    DefinitionConstants,  ///< The generated constants a definition states of itself.
    Sections,             ///< Each section, in the order its layout names its parts.
    Alias,                ///< The name a service is known by, and what it states through it.
    DefinitionWrappers,   ///< The functions a service publishes over its request's.
    HelperPrototypes,     ///< A declaration of each helper, ahead of the definitions.
    HelperDefinitions,    ///< Each helper the file defines, with its body.
    Definitions,          ///< Each lowered function the file defines, with its body.
    Epilogue,             ///< What closes the file.
    SectionConstants,     ///< The generated constants a section states of itself.
    HostChecks,           ///< What the host must be for the section's bodies to hold.
    Constants,            ///< The section's DSDL constants.
    ArrayConstants,       ///< The generated constants each array field states.
    Options,              ///< A union's option tags.
    Type,                 ///< The section's type and its fields.
    SectionDefinitions,   ///< The section's entry points and accessors, with their bodies.
    TypeEnd,              ///< What closes a type that holds its section's functions.
    LayoutChecks,         ///< What the type's layout must be for its bodies to hold.
    OptionCount,          ///< How many options a union has.
    Prototypes,           ///< A declaration of each entry point, ahead of the functions that name it.
    Wrappers,             ///< The functions published over the entry points.
    Accessors,            ///< Each field's accessors, then the functions published over them.
    Methods,              ///< The functions a type offers that no body lowers.
};

/// @brief The files one file scope is written to, and the parts of each and of a section.
struct DeclarationLayout final
{
    /// @brief The parts of each file, by the file's position.
    std::vector<std::vector<LayoutPart>> files;

    /// @brief The parts of one section, where a file names @ref LayoutPart::Sections.
    std::vector<LayoutPart> section;

    /// @brief How the language indents a block.
    IndentPolicy indent;
};

class DeclarationSite;

/// @brief How one language writes declarations: the syntax the renderer asks for.
class DeclarationSpelling
{
public:
    DeclarationSpelling()                                      = default;
    DeclarationSpelling(const DeclarationSpelling&)            = delete;
    DeclarationSpelling& operator=(const DeclarationSpelling&) = delete;
    DeclarationSpelling(DeclarationSpelling&&)                 = delete;
    DeclarationSpelling& operator=(DeclarationSpelling&&)      = delete;
    virtual ~DeclarationSpelling()                             = default;

    /// @brief Writes @p part of the file @p site renders, for the section it is at.
    virtual void write(DeclarationSite& site, LayoutPart part) const = 0;

    /// @brief The import block of the file @p site renders, from what its declarations recorded;
    ///        empty for none.
    [[nodiscard]] virtual std::string imports(const DeclarationSite& site) const = 0;

    /// @brief The signature of the lowered function @p fn, as @p decl declares it: the same text
    ///        declares the function and opens its definition.
    [[nodiscard]] virtual std::string signature(const SurfaceDecl& decl, mlir::func::FuncOp fn) const = 0;

    /// @brief Writes a declaration of a function whose signature is @p signature.
    virtual void prototype(SourceWriter& w, const std::string& signature) const = 0;

    /// @brief Writes what opens the definition of @p decl's function, whose signature is
    ///        @p signature, ahead of the body `translateFunction` writes.
    virtual void openDefinition(SourceWriter& w, const SurfaceDecl& decl, const std::string& signature) const = 0;

    /// @brief Writes @p published, a function published over the one named @p callee, whose
    ///        lowered function is @p fn.
    virtual void forward(SourceWriter&      w,
                         const SurfaceDecl& published,
                         llvm::StringRef    callee,
                         mlir::func::FuncOp fn) const = 0;
};

/// @brief The lowered functions a file defines, and what spells their bodies.
struct FunctionBodies final
{
    /// @brief The functions, in the order the file defines them. A section's declaration of a
    ///        function left out is the spelling's to write, as Go's constructor is.
    std::vector<mlir::func::FuncOp> functions;

    const BodySpelling& spelling;

    PlanBodyLookups& lookups;
};

/// @brief Where the renderer is in a file, as a spelling reads it.
class DeclarationSite final
{
public:
    DeclarationSite(SourceWriter&          w,
                    const SurfaceTree&     tree,
                    std::size_t            file,
                    const DefinitionFacts& facts,
                    std::size_t            layoutFile);

    [[nodiscard]] SourceWriter& writer();

    [[nodiscard]] const SurfaceTree& tree() const;

    /// @brief The file scope rendered.
    [[nodiscard]] std::size_t file() const;

    /// @brief Which of the layout's files is rendered.
    [[nodiscard]] std::size_t layoutFile() const;

    [[nodiscard]] const DefinitionFacts& facts() const;

    /// @brief The section a section's part is written for; none for a part of the file.
    [[nodiscard]] const std::optional<std::string>& section() const;

    /// @brief The type scope of @ref section.
    [[nodiscard]] std::size_t typeScope() const;

    /// @brief The declarations of @p kind the file scope makes for @ref section, then those the
    ///        section's type makes, or those for the definition itself outside a section, in the
    ///        tree's order.
    [[nodiscard]] std::vector<const SurfaceDecl*> declarations(SurfaceDeclKind kind) const;

    /// @brief The declarations of @p kind the file scope makes, for any section, in the tree's order.
    [[nodiscard]] std::vector<const SurfaceDecl*> fileDeclarations(SurfaceDeclKind kind) const;

    /// @brief Begins a new unit of the file, one empty line after the last.
    void separate();

    /// @brief Writes a function published over the one @p callee names, whose lowered function is
    ///        @p fn, through the spelling.
    void forward(const SurfaceDecl& published, llvm::StringRef callee, mlir::func::FuncOp fn);

private:
    friend class DeclarationRenderer;

    SourceWriter*              w_;
    const SurfaceTree&         tree_;
    std::size_t                file_;
    const DefinitionFacts&     facts_;
    std::size_t                layoutFile_;
    std::optional<std::string> section_;
    const DeclarationSpelling* spelling_{nullptr};
};

/// @brief Writes a file scope's files from a language's layout and spelling.
class DeclarationRenderer final
{
public:
    DeclarationRenderer(const SurfaceTree& tree, const DeclarationLayout& layout, const DeclarationSpelling& spelling);

    /// @brief Renders the layout's file @p layoutFile of the file scope @p file.
    /// @param[in] facts The facts of the definition the scope declares.
    /// @param[in] bodies The functions the file defines; null for a file that defines none.
    /// @return The file's text, or the error a body's translation met.
    [[nodiscard]] llvm::Expected<std::string> render(std::size_t            file,
                                                     std::size_t            layoutFile,
                                                     const DefinitionFacts& facts,
                                                     const FunctionBodies*  bodies) const;

private:
    [[nodiscard]] llvm::Error part(DeclarationSite& site, LayoutPart part, const FunctionBodies* bodies) const;

    void declareFunctions(DeclarationSite& site, SurfaceDeclKind kind) const;

    void forwardFunctions(DeclarationSite& site, SurfaceDeclKind kind) const;

    void accessors(DeclarationSite& site) const;

    [[nodiscard]] llvm::Error defineFunctions(DeclarationSite& site, const FunctionBodies& bodies) const;

    [[nodiscard]] llvm::Error defineHelpers(DeclarationSite& site, const FunctionBodies& bodies) const;

    [[nodiscard]] llvm::Error defineSectionFunctions(DeclarationSite& site, const FunctionBodies& bodies) const;

    [[nodiscard]] llvm::Error define(DeclarationSite&      site,
                                     const SurfaceDecl&    decl,
                                     mlir::func::FuncOp    fn,
                                     const FunctionBodies& bodies) const;

    const SurfaceTree&         tree_;
    const DeclarationLayout&   layout_;
    const DeclarationSpelling& spelling_;
};

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_DECLARATION_RENDERER_H
