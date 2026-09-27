//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements spelling a reference from a site in a surface plan.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Support/SurfaceLookup.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <llvm/ADT/StringRef.h>

#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/SurfacePlan.h"

namespace llvmdsdl
{
namespace
{

/// @brief A name the lookup found, and the scope it found it in.
struct Found final
{
    SurfaceItem item;
    std::size_t scope{};
};

bool same(const SurfaceItem& a, const SurfaceItem& b)
{
    return (a.scope == b.scope) && (a.index == b.index);
}

/// @brief Repeats one language's lookup over a plan, from one site.
class Resolver final
{
public:
    Resolver(const LanguageTraits& row, const SurfacePlan& plan, const SurfaceSite& site)
        : row_(row)
        , lookup_(row.classification.lookup)
        , plan_(plan)
        , site_(site)
    {
        for (std::optional<std::size_t> scope = site.scope;
             scope && (plan.scopes[*scope].kind == SurfaceScopeKind::Type);
             scope = plan.scopes[*scope].parent)
        {
            types_.push_back(*scope);
        }
        namespace_ = namespaceOf(site.scope);
        for (std::optional<std::size_t> scope = site.scope; scope; scope = plan.scopes[*scope].parent)
        {
            const SurfaceScopeKind kind = plan.scopes[*scope].kind;
            if (!file_ && (kind == SurfaceScopeKind::File))
            {
                file_ = scope;
            }
            if (!definition_ && ((kind == SurfaceScopeKind::File) || (kind == SurfaceScopeKind::Module)))
            {
                definition_ = scope;
            }
        }
    }

    std::optional<std::string> spell(const SurfaceItem& target) const
    {
        if ((row_.composition.qualification == Qualification::Rooted) && !ofOwnType(target) &&
            !lookup_.separator.empty())
        {
            return rooted(target, true);
        }
        // A body names its own type as its language does from within, where the language has such a
        // name, ahead of the type's own name.
        const bool ownType = !types_.empty() && target.scope && (target.index == types_.front());
        if (ownType)
        {
            if (std::optional<std::string> spelt = receiver(target))
            {
                return spelt;
            }
        }
        if (std::optional<std::string> spelt = bare(target))
        {
            return spelt;
        }
        if (std::optional<std::string> spelt = receiver(target))
        {
            return spelt;
        }
        if (std::optional<std::string> spelt = qualified(target))
        {
            return spelt;
        }
        if (std::optional<std::string> spelt = imported(target))
        {
            return spelt;
        }
        return rooted(target, false);
    }

private:
    [[nodiscard]] std::optional<std::size_t> parentOf(const SurfaceItem& item) const
    {
        return item.scope ? plan_.scopes[item.index].parent : std::optional<std::size_t>(plan_.decls[item.index].scope);
    }

    [[nodiscard]] const std::string& nameOf(const SurfaceItem& item) const
    {
        return item.scope ? plan_.scopes[item.index].name : plan_.decls[item.index].name;
    }

    /// @brief The class an item's name is declared in, or none for a scope that declares no name.
    [[nodiscard]] std::optional<NameClass> classOf(const SurfaceItem& item) const
    {
        if (!item.scope)
        {
            return plan_.decls[item.index].nameClass;
        }
        switch (plan_.scopes[item.index].kind)
        {
        case SurfaceScopeKind::Type:
            return NameClass::Type;
        case SurfaceScopeKind::Namespace:
        case SurfaceScopeKind::Module:
        case SurfaceScopeKind::Package:
            return NameClass::Module;
        case SurfaceScopeKind::Root:
        case SurfaceScopeKind::File:
            break;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<NamePartition> partitionOf(const SurfaceItem& item) const
    {
        const std::optional<NameClass> nameClass = classOf(item);
        return nameClass ? namePartition(row_.classification.nameClasses, *nameClass) : std::nullopt;
    }

    /// @brief The namespace a declaration made in @p scope is made in.
    [[nodiscard]] std::size_t namespaceOf(std::size_t scope) const
    {
        while ((plan_.scopes[scope].kind == SurfaceScopeKind::File) ||
               (plan_.scopes[scope].kind == SurfaceScopeKind::Type))
        {
            scope = *plan_.scopes[scope].parent;
        }
        return scope;
    }

    [[nodiscard]] bool within(std::size_t scope, const std::size_t ancestor) const
    {
        for (std::optional<std::size_t> at = scope; at; at = plan_.scopes[*at].parent)
        {
            if (*at == ancestor)
            {
                return true;
            }
        }
        return false;
    }

    /// @brief Whether @p target is the site's own type, a type enclosing it, or a member of one.
    [[nodiscard]] bool ofOwnType(const SurfaceItem& target) const
    {
        return std::ranges::any_of(types_, [&](const std::size_t type) {
            return (target.scope && (target.index == type)) || (parentOf(target) == type);
        });
    }

    /// @brief The first item of @p scope named @p name in @p partition. A namespace's search takes
    ///        in its files, since a file declares into its namespace, and a file's imports only where
    ///        the site is in that file.
    [[nodiscard]] std::optional<SurfaceItem> findIn(const std::size_t   scope,
                                                    const std::string&  name,
                                                    const NamePartition partition,
                                                    const bool          intoFiles) const
    {
        for (const SurfaceItem& item : plan_.scopes[scope].items)
        {
            if ((nameOf(item) == name) && (partitionOf(item) == partition))
            {
                return item;
            }
            if (intoFiles && item.scope && (plan_.scopes[item.index].kind == SurfaceScopeKind::File))
            {
                for (const SurfaceItem& inFile : plan_.scopes[item.index].items)
                {
                    const bool fileLocal = !inFile.scope && (plan_.decls[inFile.index].kind == SurfaceDeclKind::Import);
                    if (fileLocal && (file_ != item.index))
                    {
                        continue;
                    }
                    if ((nameOf(inFile) == name) && (partitionOf(inFile) == partition))
                    {
                        return inFile;
                    }
                }
            }
        }
        return std::nullopt;
    }

    /// @brief The declaration @p name reaches from the site in @p partition, and where it is found.
    [[nodiscard]] std::optional<Found> lookup(const std::string& name, const NamePartition partition) const
    {
        if (lookup_.ownMembers == MemberReach::Bare)
        {
            for (const std::size_t type : types_)
            {
                if (const std::optional<SurfaceItem> item = findIn(type, name, partition, false))
                {
                    return Found{*item, type};
                }
            }
        }
        for (std::optional<std::size_t> space = namespace_; space;
             space = lookup_.enclosingNamespaces ? plan_.scopes[*space].parent : std::nullopt)
        {
            if (const std::optional<SurfaceItem> item = findIn(*space, name, partition, true))
            {
                return Found{*item, *space};
            }
        }
        return std::nullopt;
    }

    /// @brief Whether @p name, looked up from the site, reaches @p target.
    [[nodiscard]] std::optional<Found> reaches(const SurfaceItem& target) const
    {
        const std::optional<NamePartition> partition = partitionOf(target);
        if (!partition)
        {
            return std::nullopt;
        }
        const std::optional<Found> found = lookup(nameOf(target), *partition);
        return (found && same(found->item, target)) ? found : std::nullopt;
    }

    /// @brief Whether the plan holds every name declared in @p scope.
    [[nodiscard]] bool inHorizon(const std::size_t scope) const
    {
        return (definition_ && within(scope, *definition_)) ||
               (row_.composition.definitionsShareNamespaceScope && (scope == namespace_));
    }

    /// @brief The items from below @p outer down to @p target, which names each.
    [[nodiscard]] std::vector<SurfaceItem> pathBelow(const std::optional<std::size_t> outer,
                                                     const SurfaceItem&               target) const
    {
        std::vector<SurfaceItem> path{target};
        for (std::optional<std::size_t> at = parentOf(target); at && (at != outer); at = plan_.scopes[*at].parent)
        {
            if (classOf(SurfaceItem{.scope = true, .index = *at}))
            {
                path.insert(path.begin(), SurfaceItem{.scope = true, .index = *at});
            }
        }
        return path;
    }

    /// @brief Whether each item of @p path is found by its name in the scope before it.
    [[nodiscard]] bool descends(const std::vector<SurfaceItem>& path) const
    {
        for (std::size_t i = 1; i < path.size(); ++i)
        {
            const std::optional<NamePartition> partition = partitionOf(path[i]);
            const std::optional<SurfaceItem>   found =
                partition ? findIn(path[i - 1].index, nameOf(path[i]), *partition, true) : std::nullopt;
            if (!found || !same(*found, path[i]))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::string join(const std::vector<SurfaceItem>& path) const
    {
        std::string out;
        for (const SurfaceItem& item : path)
        {
            out += (out.empty() ? "" : lookup_.separator.str()) + nameOf(item);
        }
        return out;
    }

    [[nodiscard]] std::optional<std::string> bare(const SurfaceItem& target) const
    {
        return reaches(target) ? std::optional<std::string>(nameOf(target)) : std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> receiver(const SurfaceItem& target) const
    {
        if (types_.empty())
        {
            return std::nullopt;
        }
        const std::size_t  own     = types_.front();
        const SurfaceItem  ownItem = SurfaceItem{.scope = true, .index = own};
        const std::string& ownName = plan_.scopes[own].name;
        const bool handed = (site_.relation == SiteRelation::Instance) || (site_.relation == SiteRelation::Class);
        const std::string byClass = (site_.relation == SiteRelation::Class) ? lookup_.selfClass.str() : "";
        if (target.scope && (target.index == own))
        {
            if (!lookup_.selfType.empty())
            {
                return lookup_.selfType.str();
            }
            return byClass.empty() ? std::nullopt : std::optional<std::string>(byClass);
        }
        const bool member =
            (parentOf(target) == own) && (target.scope || (plan_.decls[target.index].kind != SurfaceDeclKind::Field));
        if (!member)
        {
            return std::nullopt;
        }
        const std::string separator = lookup_.separator.str();
        switch (lookup_.ownMembers)
        {
        case MemberReach::SelfType:
            return lookup_.selfType.str() + separator + nameOf(target);
        case MemberReach::Instance:
            if (handed)
            {
                const std::string through =
                    (site_.relation == SiteRelation::Class) ? lookup_.selfClass.str() : lookup_.selfInstance.str();
                return through + separator + nameOf(target);
            }
            return reaches(ownItem) ? std::optional<std::string>(ownName + separator + nameOf(target)) : std::nullopt;
        case MemberReach::TypeName:
            return reaches(ownItem) ? std::optional<std::string>(ownName + separator + nameOf(target)) : std::nullopt;
        case MemberReach::None:
        case MemberReach::Bare:
            break;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> qualified(const SurfaceItem& target) const
    {
        if (lookup_.separator.empty())
        {
            return std::nullopt;
        }
        for (std::optional<std::size_t> at = parentOf(target); at; at = plan_.scopes[*at].parent)
        {
            const SurfaceItem outer = SurfaceItem{.scope = true, .index = *at};
            if (!classOf(outer))
            {
                continue;
            }
            const std::optional<Found> found = reaches(outer);
            if (!found || !inHorizon(found->scope))
            {
                continue;
            }
            std::vector<SurfaceItem> path = pathBelow(at, target);
            path.insert(path.begin(), outer);
            if (descends(path))
            {
                return join(path);
            }
        }
        return std::nullopt;
    }

    /// @brief The scope an import binds: the scope it names, or the type scope of the definition it
    ///        names.
    [[nodiscard]] std::optional<std::size_t> boundBy(const SurfaceDecl& import) const
    {
        if (import.binds)
        {
            return import.binds;
        }
        if (!import.of)
        {
            return std::nullopt;
        }
        for (std::size_t index = 0; index < plan_.scopes.size(); ++index)
        {
            const SurfaceScope& scope = plan_.scopes[index];
            if ((scope.kind == SurfaceScopeKind::Type) && scope.of && (scope.of->schema == import.of->schema) &&
                (scope.of->section == import.of->section))
            {
                return index;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> imported(const SurfaceItem& target) const
    {
        std::vector<std::size_t> holders{namespace_};
        if (file_)
        {
            holders.push_back(*file_);
        }
        for (const std::size_t holder : holders)
        {
            for (const SurfaceItem& item : plan_.scopes[holder].items)
            {
                if (item.scope || (plan_.decls[item.index].kind != SurfaceDeclKind::Import))
                {
                    continue;
                }
                const std::optional<std::size_t> bound = boundBy(plan_.decls[item.index]);
                const bool                       binds =
                    bound && ((target.scope && (target.index == *bound)) || within(*parentOf(target), *bound));
                if (!binds || !reaches(item))
                {
                    continue;
                }
                if (target.scope && (target.index == *bound))
                {
                    return nameOf(item);
                }
                std::vector<SurfaceItem> path = pathBelow(bound, target);
                path.insert(path.begin(), SurfaceItem{.scope = true, .index = *bound});
                if (!descends(path))
                {
                    continue;
                }
                path.front() = item;
                return join(path);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> rooted(const SurfaceItem& target, const bool prefixed) const
    {
        if (lookup_.separator.empty())
        {
            return std::nullopt;
        }
        const std::vector<SurfaceItem> path = pathBelow(0, target);
        if (!descendsFromRoot(path))
        {
            return std::nullopt;
        }
        if (!prefixed && lookup_.rootPrefixOnlyWhenShadowed && reaches(path.front()))
        {
            return join(path);
        }
        return lookup_.rootPrefix.empty() ? std::nullopt
                                          : std::optional<std::string>(lookup_.rootPrefix.str() + join(path));
    }

    /// @brief Whether @p path, read from the root, reaches its last item.
    [[nodiscard]] bool descendsFromRoot(const std::vector<SurfaceItem>& path) const
    {
        std::vector<SurfaceItem> fromRoot = path;
        fromRoot.insert(fromRoot.begin(), SurfaceItem{.scope = true, .index = 0});
        return descends(fromRoot);
    }

    const LanguageTraits&      row_;
    const Lookup&              lookup_;
    const SurfacePlan&         plan_;
    const SurfaceSite&         site_;
    std::vector<std::size_t>   types_;
    std::size_t                namespace_{};
    std::optional<std::size_t> file_;
    std::optional<std::size_t> definition_;
};

}  // namespace

std::optional<std::string> spellReference(const LanguageTraits& row,
                                          const SurfacePlan&    plan,
                                          const SurfaceSite&    site,
                                          const SurfaceItem&    target)
{
    return Resolver(row, plan, site).spell(target);
}

}  // namespace llvmdsdl
