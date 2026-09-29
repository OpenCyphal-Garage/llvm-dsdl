//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements the declaration renderer of DeclarationRenderer.h.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/DeclarationRenderer.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/ErrorHandling.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/SymbolTable.h>

#include "llvmdsdl/CodeGen/BodyTranslator.h"
#include "llvmdsdl/CodeGen/SchemaLookup.h"
#include "llvmdsdl/CodeGen/SectionNaming.h"
#include "llvmdsdl/CodeGen/SourceWriter.h"
#include "llvmdsdl/CodeGen/TypeMetadata.h"
#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/PlanSymbol.h"
#include "llvmdsdl/Support/SurfacePlan.h"
#include "llvmdsdl/Transforms/SurfaceTree.h"

namespace llvmdsdl
{

DefinitionFacts::DefinitionFacts(const SemanticDefinition& definition, mlir::dsdl::SchemaOp schema)
    : definition_(&definition)
    , key_(renderDefinitionKey(definitionRef(definition.info)))
    , schema_(schema)
{
    if (definition.isService)
    {
        sections_.emplace_back("request");
        metadata_.push_back(sectionMetadata(definition.info, definition.request, schema, "request"));
        if (definition.response)
        {
            sections_.emplace_back("response");
            metadata_.push_back(sectionMetadata(definition.info, *definition.response, schema, "response"));
        }
        return;
    }
    sections_.emplace_back();
    metadata_.push_back(sectionMetadata(definition.info, definition.request, schema, {}));
}

const SemanticDefinition& DefinitionFacts::definition() const
{
    return *definition_;
}

const std::string& DefinitionFacts::key() const
{
    return key_;
}

mlir::dsdl::SchemaOp DefinitionFacts::schema() const
{
    return schema_;
}

llvm::ArrayRef<std::string> DefinitionFacts::sections() const
{
    return sections_;
}

std::size_t DefinitionFacts::position(const llvm::StringRef name) const
{
    const auto found = std::ranges::find(sections_, name);
    if (found == sections_.end())
    {
        llvm::report_fatal_error(llvm::Twine("a definition has no section '") + name + "'");
    }
    return static_cast<std::size_t>(found - sections_.begin());
}

const SemanticSection& DefinitionFacts::section(const llvm::StringRef name) const
{
    return (name == "response") ? *definition_->response : definition_->request;
}

const SectionMetadata& DefinitionFacts::metadata(const llvm::StringRef name) const
{
    return metadata_[position(name)];
}

mlir::dsdl::SerializationPlanOp DefinitionFacts::plan(const llvm::StringRef name) const
{
    return sectionPlan(schema_, name);
}

mlir::func::FuncOp DefinitionFacts::function(const SurfaceDecl& decl) const
{
    mlir::dsdl::SchemaOp schema = schema_;
    if (!decl.of || decl.of->function.empty() || !schema)
    {
        return {};
    }
    return mlir::SymbolTable::lookupNearestSymbolFrom<mlir::func::FuncOp>(schema,
                                                                          mlir::StringAttr::get(schema.getContext(),
                                                                                                decl.of->function));
}

DeclarationSite::DeclarationSite(SourceWriter&          w,
                                 const SurfaceTree&     tree,
                                 const std::size_t      file,
                                 const DefinitionFacts& facts,
                                 const std::size_t      layoutFile)
    : w_(&w)
    , tree_(tree)
    , file_(file)
    , facts_(facts)
    , layoutFile_(layoutFile)
{
}

SourceWriter& DeclarationSite::writer()
{
    return *w_;
}

const SurfaceTree& DeclarationSite::tree() const
{
    return tree_;
}

std::size_t DeclarationSite::file() const
{
    return file_;
}

std::size_t DeclarationSite::layoutFile() const
{
    return layoutFile_;
}

const DefinitionFacts& DeclarationSite::facts() const
{
    return facts_;
}

const std::optional<std::string>& DeclarationSite::section() const
{
    return section_;
}

std::size_t DeclarationSite::typeScope() const
{
    return tree_.typeScope(facts_.key(), section_.value_or(std::string{}));
}

std::vector<const SurfaceDecl*> DeclarationSite::declarations(const SurfaceDeclKind kind) const
{
    // A section's declarations are made in the file scope, or in the section's type where the
    // language places them there.
    const llvm::StringRef           section = section_ ? llvm::StringRef(*section_) : llvm::StringRef{};
    std::vector<const SurfaceDecl*> found;
    const auto                      collect = [&](const std::size_t scope) {
        for (const SurfaceItem& item : tree_.scope(scope).items)
        {
            if (item.scope)
            {
                continue;
            }
            const SurfaceDecl& decl = tree_.plan().decls[item.index];
            if ((decl.kind == kind) && decl.of && (decl.of->section == section))
            {
                found.push_back(&decl);
            }
        }
    };
    collect(file_);
    if (section_)
    {
        collect(typeScope());
    }
    return found;
}

std::vector<const SurfaceDecl*> DeclarationSite::fileDeclarations(const SurfaceDeclKind kind) const
{
    std::vector<const SurfaceDecl*> found;
    for (const SurfaceItem& item : tree_.scope(file_).items)
    {
        if (!item.scope && (tree_.plan().decls[item.index].kind == kind))
        {
            found.push_back(&tree_.plan().decls[item.index]);
        }
    }
    return found;
}

void DeclarationSite::separate()
{
    w_->separate();
}

void DeclarationSite::forward(const SurfaceDecl& published, const llvm::StringRef callee, mlir::func::FuncOp fn)
{
    w_->separate();
    spelling_->forward(*w_, published, callee, fn);
}

DeclarationRenderer::DeclarationRenderer(const SurfaceTree&         tree,
                                         const DeclarationLayout&   layout,
                                         const DeclarationSpelling& spelling)
    : tree_(tree)
    , layout_(layout)
    , spelling_(spelling)
{
}

llvm::Expected<std::string> DeclarationRenderer::render(const std::size_t      file,
                                                        const std::size_t      layoutFile,
                                                        const DefinitionFacts& facts,
                                                        const FunctionBodies*  bodies) const
{
    // The parts ahead of the import block, then the parts after it: the block holds what the parts
    // after it named, so it is written once they are.
    std::ostringstream headOut;
    std::ostringstream bodyOut;
    SourceWriter       head(headOut, layout_.indent);
    SourceWriter       body(bodyOut, layout_.indent);
    DeclarationSite    site(head, tree_, file, facts, layoutFile);
    site.spelling_ = &spelling_;
    for (const LayoutPart layoutPart : layout_.files.at(layoutFile))
    {
        if (layoutPart == LayoutPart::Imports)
        {
            site.w_ = &body;
            continue;
        }
        site.separate();
        if (auto err = part(site, layoutPart, bodies))
        {
            return std::move(err);
        }
    }
    // One empty line between the three, where each holds a line.
    std::string text;
    for (const std::string& segment : {headOut.str(), spelling_.imports(site), bodyOut.str()})
    {
        if (segment.empty())
        {
            continue;
        }
        text += text.empty() ? "" : "\n";
        text += segment;
    }
    return text;
}

llvm::Error DeclarationRenderer::part(DeclarationSite& site, const LayoutPart part, const FunctionBodies* bodies) const
{
    switch (part)
    {
    case LayoutPart::Sections:
        for (const std::string& section : site.facts().sections())
        {
            site.section_ = section;
            for (const LayoutPart sectionPart : layout_.section)
            {
                site.separate();
                if (auto err = this->part(site, sectionPart, bodies))
                {
                    return err;
                }
            }
        }
        site.section_.reset();
        return llvm::Error::success();
    case LayoutPart::Prototypes:
        declareFunctions(site, SurfaceDeclKind::Entry);
        return llvm::Error::success();
    case LayoutPart::Wrappers:
        forwardFunctions(site, SurfaceDeclKind::Entry);
        return llvm::Error::success();
    case LayoutPart::Accessors:
        accessors(site);
        return llvm::Error::success();
    case LayoutPart::HelperPrototypes:
        if (bodies != nullptr)
        {
            for (mlir::func::FuncOp fn : bodies->functions)
            {
                if (const SurfaceDecl* const decl = tree_.declarationOf(fn.getSymName(), SurfaceDeclKind::Helper))
                {
                    spelling_.prototype(site.writer(), spelling_.signature(*decl, fn));
                }
            }
        }
        return llvm::Error::success();
    case LayoutPart::HelperDefinitions:
        return (bodies != nullptr) ? defineHelpers(site, *bodies) : llvm::Error::success();
    case LayoutPart::Definitions:
        return (bodies != nullptr) ? defineFunctions(site, *bodies) : llvm::Error::success();
    case LayoutPart::SectionDefinitions:
        return (bodies != nullptr) ? defineSectionFunctions(site, *bodies) : llvm::Error::success();
    default:
        spelling_.write(site, part);
        return llvm::Error::success();
    }
}

void DeclarationRenderer::declareFunctions(DeclarationSite& site, const SurfaceDeclKind kind) const
{
    for (const SurfaceDecl* const decl : site.declarations(kind))
    {
        spelling_.prototype(site.writer(), spelling_.signature(*decl, site.facts().function(*decl)));
    }
}

void DeclarationRenderer::forwardFunctions(DeclarationSite& site, const SurfaceDeclKind kind) const
{
    for (const SurfaceDecl* const decl : site.declarations(kind))
    {
        if (const SurfaceDecl* const published = tree_.declarationOf(decl->of->function, SurfaceDeclKind::Wrapper))
        {
            site.forward(*published, decl->name, site.facts().function(*decl));
        }
    }
}

void DeclarationRenderer::accessors(DeclarationSite& site) const
{
    // A field's accessors are declared together, then each is published; the next field's follow.
    const std::vector<const SurfaceDecl*> accessors = site.declarations(SurfaceDeclKind::Accessor);
    for (auto first = accessors.begin(); first != accessors.end();)
    {
        const auto last = std::find_if(first, accessors.end(), [&](const SurfaceDecl* const decl) {
            return decl->of->member != (*first)->of->member;
        });
        site.separate();
        for (auto at = first; at != last; ++at)
        {
            spelling_.prototype(site.writer(), spelling_.signature(**at, site.facts().function(**at)));
        }
        for (auto at = first; at != last; ++at)
        {
            if (const SurfaceDecl* const published = tree_.declarationOf((*at)->of->function, SurfaceDeclKind::Wrapper))
            {
                site.forward(*published, (*at)->name, site.facts().function(**at));
            }
        }
        first = last;
    }
}

llvm::Error DeclarationRenderer::defineFunctions(DeclarationSite& site, const FunctionBodies& bodies) const
{
    for (mlir::func::FuncOp fn : bodies.functions)
    {
        const std::optional<PlanSymbol> symbol = parsePlanSymbol(fn.getSymName());
        const SurfaceDecl* const        decl =
            symbol ? tree_.declarationOf(fn.getSymName(), loweredFunctionKind(symbol->function)) : nullptr;
        if (decl == nullptr)
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "the surface declares no function for %s",
                                           fn.getSymName().str().c_str());
        }
        if (auto err = define(site, *decl, fn, bodies))
        {
            return err;
        }
    }
    return llvm::Error::success();
}

llvm::Error DeclarationRenderer::defineHelpers(DeclarationSite& site, const FunctionBodies& bodies) const
{
    // A helper is defined where it is declared: in the file's scope, or in a section's type.
    const std::size_t scope  = site.section() ? site.typeScope() : site.file();
    bool              opened = !site.section();
    for (mlir::func::FuncOp fn : bodies.functions)
    {
        const SurfaceDecl* const decl = tree_.declarationOf(fn.getSymName(), SurfaceDeclKind::Helper);
        if ((decl == nullptr) || (decl->scope != scope))
        {
            continue;
        }
        // The first of a type's helpers follows the line that opens them directly.
        const bool first = !opened;
        if (first)
        {
            site.separate();
            spelling_.openMembers(site.writer(), decl->visibility);
            opened = true;
        }
        if (auto err = define(site, *decl, fn, bodies, /*separated=*/!first))
        {
            return err;
        }
    }
    return llvm::Error::success();
}

llvm::Error DeclarationRenderer::defineSectionFunctions(DeclarationSite& site, const FunctionBodies& bodies) const
{
    // A declaration whose function the file does not translate is the spelling's to write.
    for (const SurfaceDeclKind kind : {SurfaceDeclKind::Entry, SurfaceDeclKind::Accessor})
    {
        for (const SurfaceDecl* const decl : site.declarations(kind))
        {
            const mlir::func::FuncOp fn = site.facts().function(*decl);
            if (!llvm::is_contained(bodies.functions, fn))
            {
                continue;
            }
            if (auto err = define(site, *decl, fn, bodies))
            {
                return err;
            }
        }
    }
    return llvm::Error::success();
}

llvm::Error DeclarationRenderer::define(DeclarationSite&      site,
                                        const SurfaceDecl&    decl,
                                        mlir::func::FuncOp    fn,
                                        const FunctionBodies& bodies,
                                        const bool            separated) const
{
    if (separated)
    {
        site.separate();
    }
    spelling_.openDefinition(site.writer(), decl, spelling_.signature(decl, fn));
    return translateFunction(fn, bodies.spelling, site.writer(), bodies.lookups);
}

}  // namespace llvmdsdl
