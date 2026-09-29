//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/SurfaceLookup.h"
#include "llvmdsdl/Support/SurfacePlan.h"

#include "UnitTests.h"

namespace
{

using llvmdsdl::Language;
using llvmdsdl::NameClass;
using llvmdsdl::SiteRelation;
using llvmdsdl::SurfaceDeclKind;
using llvmdsdl::SurfaceItem;
using llvmdsdl::SurfaceScopeKind;
using llvmdsdl::SurfaceSite;

/// @brief A plan built by hand, a scope and a declaration at a time.
class Tree final
{
public:
    explicit Tree(std::string rootName)
    {
        plan.scopes.push_back(llvmdsdl::SurfaceScope{.kind   = SurfaceScopeKind::Root,
                                                     .name   = std::move(rootName),
                                                     .path   = {},
                                                     .parent = std::nullopt,
                                                     .of     = std::nullopt,
                                                     .items  = {}});
    }

    std::size_t scope(const std::size_t      parent,
                      const SurfaceScopeKind kind,
                      std::string            name,
                      const std::string&     schema = {})
    {
        const std::size_t index = plan.scopes.size();
        plan.scopes.push_back(llvmdsdl::SurfaceScope{.kind   = kind,
                                                     .name   = std::move(name),
                                                     .path   = {},
                                                     .parent = parent,
                                                     .of     = schema.empty()
                                                                   ? std::nullopt
                                                                   : std::optional<llvmdsdl::SurfaceEntity>(
                                                                         llvmdsdl::SurfaceEntity{.schema   = schema,
                                                                                                 .section  = {},
                                                                                                 .member   = {},
                                                                                                 .function = {}}),
                                                     .items  = {}});
        plan.scopes[parent].items.push_back(SurfaceItem{.scope = true, .index = index});
        return index;
    }

    SurfaceItem decl(const std::size_t                scope,
                     std::string                      name,
                     const SurfaceDeclKind            kind,
                     const NameClass                  nameClass,
                     const std::string&               schema = {},
                     const std::optional<std::size_t> binds  = std::nullopt)
    {
        const std::size_t index = plan.decls.size();
        plan.decls.push_back(llvmdsdl::SurfaceDecl{.name       = std::move(name),
                                                   .kind       = kind,
                                                   .nameClass  = nameClass,
                                                   .visibility = llvmdsdl::SurfaceVisibility::Public,
                                                   .origin     = llvmdsdl::NameOrigin::Definition,
                                                   .of = schema.empty() ? std::nullopt
                                                                        : std::optional<llvmdsdl::SurfaceEntity>(
                                                                              llvmdsdl::SurfaceEntity{.schema  = schema,
                                                                                                      .section = {},
                                                                                                      .member  = {},
                                                                                                      .function = {}}),
                                                   .fact  = std::nullopt,
                                                   .scope = scope,
                                                   .binds = binds});
        const SurfaceItem item{.scope = false, .index = index};
        plan.scopes[scope].items.push_back(item);
        return item;
    }

    llvmdsdl::SurfacePlan plan;
};

SurfaceItem scopeItem(const std::size_t index)
{
    return SurfaceItem{.scope = true, .index = index};
}

std::string spell(const llvmdsdl::LanguageTraits& row,
                  const Tree&                     tree,
                  const std::size_t               scope,
                  const SiteRelation              relation,
                  const SurfaceItem&              target)
{
    return llvmdsdl::spellReference(row, tree.plan, SurfaceSite{.scope = scope, .relation = relation}, target)
        .value_or("<none>");
}

bool expect(const std::string& got, const std::string& want, const std::string& what)
{
    if (got != want)
    {
        std::cerr << "surface lookup: " << what << " is " << got << ", want " << want << "\n";
        return false;
    }
    return true;
}

}  // namespace

bool runSurfaceLookupTests()
{
    bool ok = true;

    // C declares every name at file scope, which is one global scope: the bare name reaches it.
    {
        const auto& row = llvmdsdl::languageTraits(Language::C);
        Tree        tree("out");
        const auto  list = tree.scope(0, SurfaceScopeKind::File, "List_0_2");
        (void) tree.scope(list, SurfaceScopeKind::Type, "ns__List");
        const auto limit = tree.decl(list, "ns__List_LIMIT", SurfaceDeclKind::Constant, NameClass::Macro);
        const auto other = tree.scope(0, SurfaceScopeKind::File, "Other_1_0");
        ok =
            expect(spell(row, tree, other, SiteRelation::Free, limit), "ns__List_LIMIT", "C: another header's macro") &&
            ok;
    }

    // C++ sees the completed class, each enclosing class and each enclosing namespace. The horizon is
    // the definition's own file and the namespace the definitions share; a name found beyond it is
    // spelt from the root, with `::` only where the root's name is shadowed.
    {
        const llvmdsdl::LanguageTraits& row = llvmdsdl::languageTraits(Language::Cpp);
        Tree                            tree("out");
        const auto                      uavcan  = tree.scope(0, SurfaceScopeKind::Namespace, "uavcan");
        const auto                      file    = tree.scope(uavcan, SurfaceScopeKind::Namespace, "file");
        const auto                      listHdr = tree.scope(file, SurfaceScopeKind::File, "List_0_2");
        const auto                      list    = tree.scope(listHdr, SurfaceScopeKind::Type, "List_0_2");
        const auto                      request = tree.scope(list, SurfaceScopeKind::Type, "Request");
        const auto extent = tree.decl(request, "EXTENT_BYTES", SurfaceDeclKind::Constant, NameClass::Value);
        (void) tree.decl(request, "Path", SurfaceDeclKind::Field, NameClass::Field);
        const auto pathHdr = tree.scope(file, SurfaceScopeKind::File, "Path_2_0");
        const auto path    = tree.scope(pathHdr, SurfaceScopeKind::Type, "Path");
        const auto si      = tree.scope(uavcan, SurfaceScopeKind::Namespace, "si");
        const auto unit    = tree.scope(si, SurfaceScopeKind::Namespace, "unit");
        const auto length  = tree.scope(unit, SurfaceScopeKind::Namespace, "length");
        const auto scHdr   = tree.scope(length, SurfaceScopeKind::File, "Scalar_1_0");
        const auto scalar  = tree.scope(scHdr, SurfaceScopeKind::Type, "Scalar");

        ok = expect(spell(row, tree, request, SiteRelation::Static, extent),
                    "EXTENT_BYTES",
                    "C++: a member inside its type") &&
             ok;
        ok = expect(spell(row, tree, listHdr, SiteRelation::Free, extent),
                    "List_0_2::Request::EXTENT_BYTES",
                    "C++: a member at namespace scope") &&
             ok;
        ok = expect(spell(row, tree, request, SiteRelation::Static, scopeItem(scalar)),
                    "uavcan::si::unit::length::Scalar",
                    "C++: a type in another namespace") &&
             ok;
        ok = expect(spell(row, tree, request, SiteRelation::Static, scopeItem(path)),
                    "uavcan::file::Path",
                    "C++: a type a field of its name captures") &&
             ok;

        (void) tree.decl(listHdr, "uavcan", SurfaceDeclKind::Constant, NameClass::Value);
        ok = expect(spell(row, tree, request, SiteRelation::Static, scopeItem(scalar)),
                    "::uavcan::si::unit::length::Scalar",
                    "C++: a type whose root is shadowed") &&
             ok;
    }

    // Rust sees the module's items and its `use` declarations; a body reaches its own type's members
    // through `Self`, and another module's items through an import or a path from `crate`.
    {
        const auto& row = llvmdsdl::languageTraits(Language::Rust);
        Tree        tree("crate");
        const auto  uavcan   = tree.scope(0, SurfaceScopeKind::Module, "uavcan");
        const auto  file     = tree.scope(uavcan, SurfaceScopeKind::Module, "file");
        const auto  listMod  = tree.scope(file, SurfaceScopeKind::Module, "list_0_2");
        const auto  request  = tree.scope(listMod, SurfaceScopeKind::Type, "Request");
        const auto  fullName = tree.decl(request, "FULL_NAME", SurfaceDeclKind::Constant, NameClass::Value);
        const auto  response = tree.scope(listMod, SurfaceScopeKind::Type, "Response");
        (void) tree.decl(listMod, "Path", SurfaceDeclKind::Import, NameClass::Type, "uavcan.file.Path.2.0");
        const auto pathMod = tree.scope(file, SurfaceScopeKind::Module, "path_2_0");
        const auto path    = tree.scope(pathMod, SurfaceScopeKind::Type, "Path", "uavcan.file.Path.2.0");
        const auto vecMod  = tree.scope(file, SurfaceScopeKind::Module, "vec_1_0");
        const auto vec     = tree.scope(vecMod, SurfaceScopeKind::Type, "Vec");

        ok = expect(spell(row, tree, request, SiteRelation::Instance, fullName),
                    "Self::FULL_NAME",
                    "Rust: own constant") &&
             ok;
        ok =
            expect(spell(row, tree, request, SiteRelation::Static, scopeItem(request)), "Self", "Rust: own type") && ok;
        ok = expect(spell(row, tree, request, SiteRelation::Static, scopeItem(response)),
                    "Response",
                    "Rust: a sibling type") &&
             ok;
        ok = expect(spell(row, tree, request, SiteRelation::Static, scopeItem(path)),
                    "Path",
                    "Rust: an imported type") &&
             ok;
        ok = expect(spell(row, tree, request, SiteRelation::Static, scopeItem(vec)),
                    "crate::uavcan::file::vec_1_0::Vec",
                    "Rust: a type it does not import") &&
             ok;
    }

    // Go sees the package across its files and the file's imports; another package is reached through
    // the name its import binds.
    {
        const auto& row = llvmdsdl::languageTraits(Language::Go);
        Tree        tree("m");
        const auto  uavcan   = tree.scope(0, SurfaceScopeKind::Package, "uavcan");
        const auto  file     = tree.scope(uavcan, SurfaceScopeKind::Package, "file");
        const auto  listFile = tree.scope(file, SurfaceScopeKind::File, "list_0_2");
        const auto  request  = tree.scope(listFile, SurfaceScopeKind::Type, "ListRequest");
        const auto  fullName = tree.decl(listFile, "ListRequestFullName", SurfaceDeclKind::Constant, NameClass::Value);
        const auto  pathFile = tree.scope(file, SurfaceScopeKind::File, "path_2_0");
        const auto  path     = tree.scope(pathFile, SurfaceScopeKind::Type, "Path");
        const auto  si       = tree.scope(uavcan, SurfaceScopeKind::Package, "si");
        const auto  length   = tree.scope(si, SurfaceScopeKind::Package, "length");
        const auto  scFile   = tree.scope(length, SurfaceScopeKind::File, "scalar_1_0");
        const auto  scalar   = tree.scope(scFile, SurfaceScopeKind::Type, "Scalar");
        (void) tree.decl(listFile, "pkg_uavcan_si_length", SurfaceDeclKind::Import, NameClass::Value, {}, length);

        ok = expect(spell(row, tree, request, SiteRelation::Instance, fullName),
                    "ListRequestFullName",
                    "Go: a package-level constant") &&
             ok;
        ok = expect(spell(row, tree, request, SiteRelation::Instance, scopeItem(path)),
                    "Path",
                    "Go: a type in another file") &&
             ok;
        ok = expect(spell(row, tree, request, SiteRelation::Instance, scopeItem(scalar)),
                    "pkg_uavcan_si_length.Scalar",
                    "Go: a type in another package") &&
             ok;
        ok = expect(spell(row, tree, pathFile, SiteRelation::Free, scopeItem(scalar)),
                    "<none>",
                    "Go: another file's import is not in scope") &&
             ok;
    }

    // TypeScript reaches a type's members through the companion `const` that has the type's name.
    {
        const auto& row = llvmdsdl::languageTraits(Language::TypeScript);
        Tree        tree("m");
        const auto  uavcan  = tree.scope(0, SurfaceScopeKind::Module, "uavcan");
        const auto  listMod = tree.scope(uavcan, SurfaceScopeKind::Module, "list_0_2");
        const auto  request = tree.scope(listMod, SurfaceScopeKind::Type, "ListRequest");
        const auto  limit   = tree.decl(request, "LIMIT", SurfaceDeclKind::Constant, NameClass::Value);
        ok                  = expect(spell(row, tree, request, SiteRelation::Static, limit),
                                     "ListRequest.LIMIT",
                                     "TypeScript: own constant") &&
                              ok;
    }

    // Python reaches a class's attributes through the instance or the class a method is handed, and
    // through the class's name in a static method.
    {
        const auto& row = llvmdsdl::languageTraits(Language::Python);
        Tree        tree("dsdl_gen");
        const auto  uavcan  = tree.scope(0, SurfaceScopeKind::Module, "uavcan");
        const auto  listMod = tree.scope(uavcan, SurfaceScopeKind::Module, "list_0_2");
        const auto  request = tree.scope(listMod, SurfaceScopeKind::Type, "ListRequest");
        const auto  limit   = tree.decl(request, "LIMIT", SurfaceDeclKind::Constant, NameClass::Value);
        ok = expect(spell(row, tree, request, SiteRelation::Instance, limit), "self.LIMIT", "Python: from a method") &&
             ok;
        ok =
            expect(spell(row, tree, request, SiteRelation::Class, limit), "cls.LIMIT", "Python: from a class method") &&
            ok;
        ok = expect(spell(row, tree, request, SiteRelation::Static, limit),
                    "ListRequest.LIMIT",
                    "Python: from a static method") &&
             ok;
        ok = expect(spell(row, tree, request, SiteRelation::Class, scopeItem(request)), "cls", "Python: own class") &&
             ok;
        ok = expect(spell(row, tree, request, SiteRelation::Instance, scopeItem(request)),
                    "ListRequest",
                    "Python: own class from a method") &&
             ok;
    }

    return ok;
}
