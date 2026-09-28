//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements writing a surface plan into a module and reading it back.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Transforms/SurfaceTree.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/ErrorHandling.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/Location.h>
#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/Operation.h>

#include "llvmdsdl/IR/DSDLAttrs.h"
#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/Support/GeneratedFact.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/PlanSymbol.h"
#include "llvmdsdl/Support/SurfacePlan.h"

namespace llvmdsdl
{
namespace
{

mlir::dsdl::ScopeKind irScopeKind(const SurfaceScopeKind kind)
{
    switch (kind)
    {
    case SurfaceScopeKind::Root:
        return mlir::dsdl::ScopeKind::Root;
    case SurfaceScopeKind::Namespace:
        return mlir::dsdl::ScopeKind::Namespace;
    case SurfaceScopeKind::Module:
        return mlir::dsdl::ScopeKind::Module;
    case SurfaceScopeKind::Package:
        return mlir::dsdl::ScopeKind::Package;
    case SurfaceScopeKind::File:
        return mlir::dsdl::ScopeKind::File;
    case SurfaceScopeKind::Type:
        break;
    }
    return mlir::dsdl::ScopeKind::Type;
}

mlir::dsdl::DeclKind irDeclKind(const SurfaceDeclKind kind)
{
    switch (kind)
    {
    case SurfaceDeclKind::Field:
        return mlir::dsdl::DeclKind::Field;
    case SurfaceDeclKind::Constant:
        return mlir::dsdl::DeclKind::Constant;
    case SurfaceDeclKind::Option:
        return mlir::dsdl::DeclKind::Option;
    case SurfaceDeclKind::Entry:
        return mlir::dsdl::DeclKind::Entry;
    case SurfaceDeclKind::Accessor:
        return mlir::dsdl::DeclKind::Accessor;
    case SurfaceDeclKind::Helper:
        return mlir::dsdl::DeclKind::Helper;
    case SurfaceDeclKind::Method:
        return mlir::dsdl::DeclKind::Method;
    case SurfaceDeclKind::Wrapper:
        return mlir::dsdl::DeclKind::Wrapper;
    case SurfaceDeclKind::Alias:
        return mlir::dsdl::DeclKind::Alias;
    case SurfaceDeclKind::Tag:
        return mlir::dsdl::DeclKind::Tag;
    case SurfaceDeclKind::Import:
        return mlir::dsdl::DeclKind::Import;
    case SurfaceDeclKind::Module:
        return mlir::dsdl::DeclKind::Module;
    case SurfaceDeclKind::Guard:
        break;
    }
    return mlir::dsdl::DeclKind::Guard;
}

mlir::dsdl::NameClass irNameClass(const NameClass nameClass)
{
    switch (nameClass)
    {
    case NameClass::Value:
        return mlir::dsdl::NameClass::Value;
    case NameClass::Type:
        return mlir::dsdl::NameClass::Type;
    case NameClass::Module:
        return mlir::dsdl::NameClass::Module;
    case NameClass::Tag:
        return mlir::dsdl::NameClass::Tag;
    case NameClass::Field:
        return mlir::dsdl::NameClass::Field;
    case NameClass::Macro:
        break;
    }
    return mlir::dsdl::NameClass::Macro;
}

mlir::dsdl::GeneratedFact irGeneratedFact(const GeneratedFact fact)
{
    switch (fact)
    {
    case GeneratedFact::FullName:
        return mlir::dsdl::GeneratedFact::FullName;
    case GeneratedFact::FullNameAndVersion:
        return mlir::dsdl::GeneratedFact::FullNameAndVersion;
    case GeneratedFact::IsDeprecated:
        return mlir::dsdl::GeneratedFact::IsDeprecated;
    case GeneratedFact::ExtentBytes:
        return mlir::dsdl::GeneratedFact::ExtentBytes;
    case GeneratedFact::SerializationBufferSizeBytes:
        return mlir::dsdl::GeneratedFact::SerializationBufferSizeBytes;
    case GeneratedFact::WireFlat:
        return mlir::dsdl::GeneratedFact::WireFlat;
    case GeneratedFact::WireFlatReason:
        return mlir::dsdl::GeneratedFact::WireFlatReason;
    case GeneratedFact::HostImage:
        return mlir::dsdl::GeneratedFact::HostImage;
    case GeneratedFact::HostImageReason:
        return mlir::dsdl::GeneratedFact::HostImageReason;
    case GeneratedFact::UnionOptionCount:
        return mlir::dsdl::GeneratedFact::UnionOptionCount;
    case GeneratedFact::HasFixedPortId:
        return mlir::dsdl::GeneratedFact::HasFixedPortId;
    case GeneratedFact::FixedPortId:
        return mlir::dsdl::GeneratedFact::FixedPortId;
    case GeneratedFact::MemoryMode:
        return mlir::dsdl::GeneratedFact::MemoryMode;
    case GeneratedFact::InlineThresholdBytes:
        return mlir::dsdl::GeneratedFact::InlineThresholdBytes;
    case GeneratedFact::PoolClass:
        return mlir::dsdl::GeneratedFact::PoolClass;
    case GeneratedFact::ArrayCapacity:
        return mlir::dsdl::GeneratedFact::ArrayCapacity;
    case GeneratedFact::ArrayIsVariableLength:
        return mlir::dsdl::GeneratedFact::ArrayIsVariableLength;
    case GeneratedFact::UnionTag:
        return mlir::dsdl::GeneratedFact::UnionTag;
    case GeneratedFact::Placeholder:
        return mlir::dsdl::GeneratedFact::Placeholder;
    case GeneratedFact::IncludeGuard:
        return mlir::dsdl::GeneratedFact::IncludeGuard;
    case GeneratedFact::SelectedType:
        return mlir::dsdl::GeneratedFact::SelectedType;
    case GeneratedFact::SelectedVersion:
        return mlir::dsdl::GeneratedFact::SelectedVersion;
    case GeneratedFact::OptionTest:
        return mlir::dsdl::GeneratedFact::OptionTest;
    case GeneratedFact::OptionSelect:
        return mlir::dsdl::GeneratedFact::OptionSelect;
    case GeneratedFact::Serialize:
        return mlir::dsdl::GeneratedFact::Serialize;
    case GeneratedFact::Deserialize:
        return mlir::dsdl::GeneratedFact::Deserialize;
    case GeneratedFact::Initialize:
        return mlir::dsdl::GeneratedFact::Initialize;
    case GeneratedFact::AppendWireImage:
        return mlir::dsdl::GeneratedFact::AppendWireImage;
    case GeneratedFact::WireImage:
        return mlir::dsdl::GeneratedFact::WireImage;
    case GeneratedFact::FromWireImage:
        return mlir::dsdl::GeneratedFact::FromWireImage;
    case GeneratedFact::GeneratorVersion:
        return mlir::dsdl::GeneratedFact::GeneratorVersion;
    case GeneratedFact::VersionMajor:
        return mlir::dsdl::GeneratedFact::VersionMajor;
    case GeneratedFact::VersionMinor:
        return mlir::dsdl::GeneratedFact::VersionMinor;
    case GeneratedFact::MemoryResource:
        break;
    }
    return mlir::dsdl::GeneratedFact::MemoryResource;
}

GeneratedFact generatedFactOf(const mlir::dsdl::GeneratedFact fact)
{
    switch (fact)
    {
    case mlir::dsdl::GeneratedFact::FullName:
        return GeneratedFact::FullName;
    case mlir::dsdl::GeneratedFact::FullNameAndVersion:
        return GeneratedFact::FullNameAndVersion;
    case mlir::dsdl::GeneratedFact::IsDeprecated:
        return GeneratedFact::IsDeprecated;
    case mlir::dsdl::GeneratedFact::ExtentBytes:
        return GeneratedFact::ExtentBytes;
    case mlir::dsdl::GeneratedFact::SerializationBufferSizeBytes:
        return GeneratedFact::SerializationBufferSizeBytes;
    case mlir::dsdl::GeneratedFact::WireFlat:
        return GeneratedFact::WireFlat;
    case mlir::dsdl::GeneratedFact::WireFlatReason:
        return GeneratedFact::WireFlatReason;
    case mlir::dsdl::GeneratedFact::HostImage:
        return GeneratedFact::HostImage;
    case mlir::dsdl::GeneratedFact::HostImageReason:
        return GeneratedFact::HostImageReason;
    case mlir::dsdl::GeneratedFact::UnionOptionCount:
        return GeneratedFact::UnionOptionCount;
    case mlir::dsdl::GeneratedFact::HasFixedPortId:
        return GeneratedFact::HasFixedPortId;
    case mlir::dsdl::GeneratedFact::FixedPortId:
        return GeneratedFact::FixedPortId;
    case mlir::dsdl::GeneratedFact::MemoryMode:
        return GeneratedFact::MemoryMode;
    case mlir::dsdl::GeneratedFact::InlineThresholdBytes:
        return GeneratedFact::InlineThresholdBytes;
    case mlir::dsdl::GeneratedFact::PoolClass:
        return GeneratedFact::PoolClass;
    case mlir::dsdl::GeneratedFact::ArrayCapacity:
        return GeneratedFact::ArrayCapacity;
    case mlir::dsdl::GeneratedFact::ArrayIsVariableLength:
        return GeneratedFact::ArrayIsVariableLength;
    case mlir::dsdl::GeneratedFact::UnionTag:
        return GeneratedFact::UnionTag;
    case mlir::dsdl::GeneratedFact::Placeholder:
        return GeneratedFact::Placeholder;
    case mlir::dsdl::GeneratedFact::IncludeGuard:
        return GeneratedFact::IncludeGuard;
    case mlir::dsdl::GeneratedFact::SelectedType:
        return GeneratedFact::SelectedType;
    case mlir::dsdl::GeneratedFact::SelectedVersion:
        return GeneratedFact::SelectedVersion;
    case mlir::dsdl::GeneratedFact::OptionTest:
        return GeneratedFact::OptionTest;
    case mlir::dsdl::GeneratedFact::OptionSelect:
        return GeneratedFact::OptionSelect;
    case mlir::dsdl::GeneratedFact::Serialize:
        return GeneratedFact::Serialize;
    case mlir::dsdl::GeneratedFact::Deserialize:
        return GeneratedFact::Deserialize;
    case mlir::dsdl::GeneratedFact::Initialize:
        return GeneratedFact::Initialize;
    case mlir::dsdl::GeneratedFact::AppendWireImage:
        return GeneratedFact::AppendWireImage;
    case mlir::dsdl::GeneratedFact::WireImage:
        return GeneratedFact::WireImage;
    case mlir::dsdl::GeneratedFact::FromWireImage:
        return GeneratedFact::FromWireImage;
    case mlir::dsdl::GeneratedFact::GeneratorVersion:
        return GeneratedFact::GeneratorVersion;
    case mlir::dsdl::GeneratedFact::VersionMajor:
        return GeneratedFact::VersionMajor;
    case mlir::dsdl::GeneratedFact::VersionMinor:
        return GeneratedFact::VersionMinor;
    case mlir::dsdl::GeneratedFact::MemoryResource:
        break;
    }
    return GeneratedFact::MemoryResource;
}

}  // namespace

llvm::StringRef generatedFactName(const GeneratedFact fact)
{
    return mlir::dsdl::stringifyGeneratedFact(irGeneratedFact(fact));
}

namespace
{

SurfaceScopeKind scopeKindOf(const mlir::dsdl::ScopeKind kind)
{
    switch (kind)
    {
    case mlir::dsdl::ScopeKind::Root:
        return SurfaceScopeKind::Root;
    case mlir::dsdl::ScopeKind::Namespace:
        return SurfaceScopeKind::Namespace;
    case mlir::dsdl::ScopeKind::Module:
        return SurfaceScopeKind::Module;
    case mlir::dsdl::ScopeKind::Package:
        return SurfaceScopeKind::Package;
    case mlir::dsdl::ScopeKind::File:
        return SurfaceScopeKind::File;
    case mlir::dsdl::ScopeKind::Type:
        break;
    }
    return SurfaceScopeKind::Type;
}

SurfaceDeclKind declKindOf(const mlir::dsdl::DeclKind kind)
{
    switch (kind)
    {
    case mlir::dsdl::DeclKind::Field:
        return SurfaceDeclKind::Field;
    case mlir::dsdl::DeclKind::Constant:
        return SurfaceDeclKind::Constant;
    case mlir::dsdl::DeclKind::Option:
        return SurfaceDeclKind::Option;
    case mlir::dsdl::DeclKind::Entry:
        return SurfaceDeclKind::Entry;
    case mlir::dsdl::DeclKind::Accessor:
        return SurfaceDeclKind::Accessor;
    case mlir::dsdl::DeclKind::Helper:
        return SurfaceDeclKind::Helper;
    case mlir::dsdl::DeclKind::Method:
        return SurfaceDeclKind::Method;
    case mlir::dsdl::DeclKind::Wrapper:
        return SurfaceDeclKind::Wrapper;
    case mlir::dsdl::DeclKind::Alias:
        return SurfaceDeclKind::Alias;
    case mlir::dsdl::DeclKind::Tag:
        return SurfaceDeclKind::Tag;
    case mlir::dsdl::DeclKind::Import:
        return SurfaceDeclKind::Import;
    case mlir::dsdl::DeclKind::Module:
        return SurfaceDeclKind::Module;
    case mlir::dsdl::DeclKind::Guard:
        break;
    }
    return SurfaceDeclKind::Guard;
}

NameClass nameClassOf(const mlir::dsdl::NameClass nameClass)
{
    switch (nameClass)
    {
    case mlir::dsdl::NameClass::Value:
        return NameClass::Value;
    case mlir::dsdl::NameClass::Type:
        return NameClass::Type;
    case mlir::dsdl::NameClass::Module:
        return NameClass::Module;
    case mlir::dsdl::NameClass::Tag:
        return NameClass::Tag;
    case mlir::dsdl::NameClass::Field:
        return NameClass::Field;
    case mlir::dsdl::NameClass::Macro:
        break;
    }
    return NameClass::Macro;
}

/// @brief The entity a scope or declaration names by @p of, @p section and @p member.
///
/// A declaration of a lowered function names the function alone, and its symbol states the rest.
std::optional<SurfaceEntity> entityOf(const mlir::FlatSymbolRefAttr        of,
                                      const std::optional<llvm::StringRef> section,
                                      const std::optional<llvm::StringRef> member)
{
    if (!of)
    {
        return std::nullopt;
    }
    if (const std::optional<PlanSymbol> function = parsePlanSymbol(of.getValue()))
    {
        return SurfaceEntity{.schema   = renderSchemaSymbol(function->schema),
                             .section  = function->section,
                             .member   = function->member,
                             .function = of.getValue().str()};
    }
    return SurfaceEntity{.schema   = of.getValue().str(),
                         .section  = section.value_or("").str(),
                         .member   = member.value_or("").str(),
                         .function = {}};
}

/// @brief Reads a scope and everything in it into @p plan, as the child of @p parent.
void readScope(SurfacePlan& plan, mlir::dsdl::ScopeOp op, const std::optional<std::size_t> parent)
{
    const std::size_t index = plan.scopes.size();
    plan.scopes.push_back(SurfaceScope{.kind   = scopeKindOf(op.getKind()),
                                       .name   = op.getName().str(),
                                       .path   = op.getPath().value_or("").str(),
                                       .parent = parent,
                                       .of     = entityOf(op.getOfAttr(), op.getSection(), std::nullopt),
                                       .items  = {}});
    if (parent)
    {
        plan.scopes[*parent].items.push_back(SurfaceItem{.scope = true, .index = index});
    }
    if (op.getBodyRegion().empty())
    {
        return;
    }
    for (mlir::Operation& child : *op.getBody())
    {
        if (auto scope = llvm::dyn_cast<mlir::dsdl::ScopeOp>(child))
        {
            readScope(plan, scope, index);
            continue;
        }
        auto              decl     = llvm::cast<mlir::dsdl::DeclOp>(child);
        const std::size_t position = plan.decls.size();
        plan.decls.push_back(
            SurfaceDecl{.name       = decl.getName().str(),
                        .kind       = declKindOf(decl.getKind()),
                        .nameClass  = nameClassOf(decl.declaredClass()),
                        .visibility = (decl.getVisibility() == mlir::dsdl::Visibility::Private)
                                          ? SurfaceVisibility::Private
                                          : SurfaceVisibility::Public,
                        .origin     = (decl.getOrigin() == mlir::dsdl::Origin::Generated) ? NameOrigin::Generated
                                                                                          : NameOrigin::Definition,
                        .of         = entityOf(decl.getOfAttr(), decl.getSection(), decl.getMember()),
                        .fact       = decl.getFact() ? std::optional(generatedFactOf(*decl.getFact())) : std::nullopt,
                        .scope      = index,
                        .binds      = std::nullopt});
        plan.scopes[index].items.push_back(SurfaceItem{.scope = false, .index = position});
    }
}

/// @brief Writes one plan as a `dsdl.surface`.
class SurfaceWriter final
{
public:
    SurfaceWriter(mlir::OpBuilder&      builder,
                  const mlir::Location  location,
                  const SurfacePlan&    plan,
                  const llvm::StringRef directory)
        : builder_(builder)
        , location_(location)
        , plan_(plan)
        , directory_(directory.str())
    {
    }

    /// @brief Writes the plan as the surface of @p target and @p profile.
    void write(const llvm::StringRef target, const llvm::StringRef profile)
    {
        auto surface =
            mlir::dsdl::SurfaceOp::create(builder_,
                                          location_,
                                          builder_.getStringAttr(target),
                                          profile.empty() ? mlir::StringAttr{} : builder_.getStringAttr(profile));
        const mlir::OpBuilder::InsertionGuard guard(builder_);
        builder_.setInsertionPointToEnd(&surface.getBodyRegion().emplaceBlock());
        writeScope(0);
    }

private:
    [[nodiscard]] mlir::StringAttr optionalString(const llvm::StringRef value) const
    {
        return value.empty() ? mlir::StringAttr{} : builder_.getStringAttr(value);
    }

    void writeScope(const std::size_t index)
    {
        const SurfaceScope&     scope = plan_.scopes[index];
        mlir::FlatSymbolRefAttr of;
        mlir::StringAttr        section;
        if (scope.of)
        {
            of      = mlir::FlatSymbolRefAttr::get(builder_.getContext(), scope.of->schema);
            section = optionalString(scope.of->section);
        }
        auto op =
            mlir::dsdl::ScopeOp::create(builder_,
                                        location_,
                                        mlir::dsdl::ScopeKindAttr::get(builder_.getContext(), irScopeKind(scope.kind)),
                                        builder_.getStringAttr(scope.name),
                                        scope.path.empty() ? mlir::StringAttr{}
                                                           : builder_.getStringAttr(directory_ + scope.path),
                                        of,
                                        section);
        const mlir::OpBuilder::InsertionGuard guard(builder_);
        builder_.setInsertionPointToEnd(&op.getBodyRegion().emplaceBlock());
        for (const SurfaceItem& item : scope.items)
        {
            if (item.scope)
            {
                writeScope(item.index);
            }
            else
            {
                writeDecl(plan_.decls[item.index]);
            }
        }
    }

    void writeDecl(const SurfaceDecl& decl)
    {
        mlir::MLIRContext* const context = builder_.getContext();
        mlir::FlatSymbolRefAttr  of;
        mlir::StringAttr         section;
        mlir::StringAttr         member;
        if (decl.of)
        {
            // A declaration of a lowered function names the function; any other names its schema.
            const bool function = !decl.of->function.empty();
            of                  = mlir::FlatSymbolRefAttr::get(context, function ? decl.of->function : decl.of->schema);
            if (!function)
            {
                section = optionalString(decl.of->section);
                member  = optionalString(decl.of->member);
            }
        }
        auto op =
            mlir::dsdl::DeclOp::create(builder_,
                                       location_,
                                       builder_.getStringAttr(decl.name),
                                       mlir::dsdl::DeclKindAttr::get(context, irDeclKind(decl.kind)),
                                       mlir::dsdl::NameClassAttr{},
                                       mlir::dsdl::VisibilityAttr::get(context,
                                                                       (decl.visibility == SurfaceVisibility::Private)
                                                                           ? mlir::dsdl::Visibility::Private
                                                                           : mlir::dsdl::Visibility::Public),
                                       mlir::dsdl::OriginAttr::get(context,
                                                                   (decl.origin == NameOrigin::Generated)
                                                                       ? mlir::dsdl::Origin::Generated
                                                                       : mlir::dsdl::Origin::Definition),
                                       decl.fact
                                           ? mlir::dsdl::GeneratedFactAttr::get(context, irGeneratedFact(*decl.fact))
                                           : mlir::dsdl::GeneratedFactAttr{},
                                       of,
                                       section,
                                       member);
        // The class is written only where the kind does not imply it, which it never does for an
        // import.
        if ((decl.kind == SurfaceDeclKind::Import) || (op.declaredClass() != irNameClass(decl.nameClass)))
        {
            op.setNameClassAttr(mlir::dsdl::NameClassAttr::get(context, irNameClass(decl.nameClass)));
        }
    }

    mlir::OpBuilder&   builder_;
    mlir::Location     location_;
    const SurfacePlan& plan_;

    /// @brief The directory, ending in `/`, the plan's paths are under; empty for the output directory.
    std::string directory_;
};

}  // namespace

void writeSurface(mlir::OpBuilder&      builder,
                  const mlir::Location  location,
                  const SurfacePlan&    plan,
                  const llvm::StringRef target,
                  const llvm::StringRef profile,
                  const llvm::StringRef directory)
{
    SurfaceWriter(builder, location, plan, directory).write(target, profile);
}

SurfaceTree::SurfaceTree(SurfacePlan plan)
    : plan_(std::move(plan))
{
    for (const auto [index, decl] : llvm::enumerate(plan_.decls))
    {
        // An import of another file's function is found in the file that makes it.
        if (decl.of && !decl.of->function.empty() && (decl.kind != SurfaceDeclKind::Import))
        {
            functions_.try_emplace({decl.of->function, decl.kind, decl.fact}, index);
        }
    }
}

llvm::Expected<SurfaceTree> SurfaceTree::read(mlir::ModuleOp        module,
                                              const LanguageTraits& row,
                                              const llvm::StringRef profile)
{
    for (mlir::dsdl::SurfaceOp surface : module.getBodyRegion().front().getOps<mlir::dsdl::SurfaceOp>())
    {
        if ((surface.getTarget() != row.name) || (surface.getProfile().value_or("") != profile))
        {
            continue;
        }
        SurfacePlan plan;
        readScope(plan, llvm::cast<mlir::dsdl::ScopeOp>(surface.getBody()->front()), std::nullopt);
        return SurfaceTree(std::move(plan));
    }
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "the lowered module holds no surface of target '%s' and profile '%s'",
                                   row.name.str().c_str(),
                                   profile.str().c_str());
}

const SurfacePlan& SurfaceTree::plan() const
{
    return plan_;
}

const SurfaceScope& SurfaceTree::scope(const std::size_t index) const
{
    return plan_.scopes.at(index);
}

std::size_t SurfaceTree::typeScope(const llvm::StringRef key, const llvm::StringRef section) const
{
    for (const auto [index, scope] : llvm::enumerate(plan_.scopes))
    {
        if ((scope.kind == SurfaceScopeKind::Type) && scope.of && (scope.of->schema == key) &&
            (scope.of->section == section))
        {
            return index;
        }
    }
    llvm::report_fatal_error(llvm::Twine("the surface declares no type of ") + key +
                             (section.empty() ? llvm::Twine() : llvm::Twine(" ") + section));
}

std::size_t SurfaceTree::definitionScope(const llvm::StringRef key) const
{
    for (const SurfaceScope& scope : plan_.scopes)
    {
        if ((scope.kind == SurfaceScopeKind::Type) && scope.of && (scope.of->schema == key))
        {
            return *scope.parent;
        }
    }
    llvm::report_fatal_error(llvm::Twine("the surface declares no type of ") + key);
}

const SurfaceDecl* SurfaceTree::find(const std::size_t                  scope,
                                     const SurfaceDeclKind              kind,
                                     const SurfaceEntity&               of,
                                     const std::optional<GeneratedFact> fact) const
{
    for (const SurfaceItem& item : plan_.scopes.at(scope).items)
    {
        if (item.scope)
        {
            continue;
        }
        const SurfaceDecl& decl = plan_.decls[item.index];
        if ((decl.kind == kind) && decl.of && (*decl.of == of) && (decl.fact == fact))
        {
            return &decl;
        }
    }
    return nullptr;
}

const std::string& SurfaceTree::nameOf(const std::size_t                  scope,
                                       const SurfaceDeclKind              kind,
                                       const SurfaceEntity&               of,
                                       const std::optional<GeneratedFact> fact) const
{
    if (const SurfaceDecl* const decl = find(scope, kind, of, fact))
    {
        return decl->name;
    }
    llvm::report_fatal_error(llvm::Twine("scope '") + plan_.scopes.at(scope).name + "' of the surface declares no " +
                             "name for " + of.schema + " " + of.section + " " + of.member + of.function);
}

const std::string& SurfaceTree::nameOf(const llvm::StringRef              symbol,
                                       const SurfaceDeclKind              kind,
                                       const std::optional<GeneratedFact> fact) const
{
    const auto found = functions_.find({symbol.str(), kind, fact});
    if (found == functions_.end())
    {
        llvm::report_fatal_error(llvm::Twine("the surface declares no name for ") + symbol);
    }
    return plan_.decls[found->second].name;
}

std::vector<std::size_t> SurfaceTree::pathTo(const std::size_t scope) const
{
    std::vector<std::size_t> path;
    for (std::optional<std::size_t> at = scope; at && plan_.scopes[*at].parent; at = plan_.scopes[*at].parent)
    {
        path.push_back(*at);
    }
    std::ranges::reverse(path);
    return path;
}

}  // namespace llvmdsdl
