//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements `project-dsdl-surface`, which writes a target's surface plan into the module.
///
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/Support/CommandLine.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/DialectRegistry.h>
#include <mlir/IR/MLIRContext.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Pass/PassRegistry.h>

#include "llvmdsdl/IR/DSDLAttrs.h"
#include "llvmdsdl/IR/DSDLDialect.h"
#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/Support/BodyNaming.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/PlanSymbol.h"
#include "llvmdsdl/Support/SectionScopes.h"
#include "llvmdsdl/Support/SurfacePlan.h"
#include "llvmdsdl/Transforms/Passes.h"

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
    case NameClass::Macro:
        break;
    }
    return mlir::dsdl::NameClass::Macro;
}

/// @brief The parts of one section of @p schema, from its plan and its field and constant ops.
SectionParts sectionParts(mlir::dsdl::SchemaOp schema, mlir::dsdl::SerializationPlanOp plan)
{
    const llvm::StringRef section = plan.getSection().value_or("");
    llvm::StringSet<>     arrays;
    for (mlir::dsdl::IOOp step : plan.getBody().getOps<mlir::dsdl::IOOp>())
    {
        if (step.isArray())
        {
            arrays.insert(step.getName());
        }
    }
    SectionParts parts{.fields = {}, .constants = {}, .isUnion = plan.getIsUnion()};
    for (mlir::dsdl::FieldOp field : schema.getBody().getOps<mlir::dsdl::FieldOp>())
    {
        if (field.getSection().value_or("") == section)
        {
            parts.fields.push_back(
                FieldParts{.name             = field.getName().str(),
                           .padding          = field.getPadding(),
                           .array            = arrays.contains(field.getName()),
                           .unionOptionIndex = static_cast<std::uint32_t>(field.getUnionOptionIndex().value_or(0))});
        }
    }
    for (mlir::dsdl::ConstantOp constant : schema.getBody().getOps<mlir::dsdl::ConstantOp>())
    {
        if (constant.getSection().value_or("") == section)
        {
            parts.constants.push_back(constant.getName().str());
        }
    }
    return parts;
}

/// @brief The parts of @p schema that naming reads, with the lowered functions @p module holds for it.
DefinitionParts schemaParts(mlir::ModuleOp module, mlir::dsdl::SchemaOp schema)
{
    llvm::SmallVector<llvm::StringRef, 8> components;
    schema.getFullName().split(components, '.');
    DefinitionParts parts{.ref         = DefinitionRef{.namespaceComponents = {},
                                                       .shortName           = components.back().str(),
                                                       .majorVersion        = static_cast<std::uint32_t>(schema.getMajor()),
                                                       .minorVersion        = static_cast<std::uint32_t>(schema.getMinor())},
                          .fixedPortId = std::nullopt,
                          .service     = schema.getService(),
                          .deprecated  = schema.getDeprecated(),
                          .request     = SectionParts{},
                          .response    = std::nullopt,
                          .bodies      = {}};
    for (const llvm::StringRef component : llvm::ArrayRef<llvm::StringRef>(components).drop_back())
    {
        parts.ref.namespaceComponents.push_back(component.str());
    }
    if (const std::optional<std::int64_t> port = schema.getFixedPortId())
    {
        parts.fixedPortId = static_cast<std::uint32_t>(*port);
    }
    for (mlir::dsdl::SerializationPlanOp plan : schema.getBody().getOps<mlir::dsdl::SerializationPlanOp>())
    {
        if (plan.getSection().value_or("") == "response")
        {
            parts.response = sectionParts(schema, plan);
        }
        else
        {
            parts.request = sectionParts(schema, plan);
        }
    }
    for (mlir::func::FuncOp fn : module.getBodyRegion().front().getOps<mlir::func::FuncOp>())
    {
        const auto owner = fn->getAttrOfType<mlir::StringAttr>("llvmdsdl.schema_sym");
        if (!owner || (owner.getValue() != schema.getSymName()))
        {
            continue;
        }
        if (const std::optional<PlanSymbol> plan = parsePlanSymbol(fn.getSymName()))
        {
            parts.bodies.push_back(BodyParts{.symbol       = fn.getSymName().str(),
                                             .plan         = *plan,
                                             .unreferenced = fn->hasAttr("llvmdsdl.unreferenced")});
        }
    }
    return parts;
}

/// @brief Writes one plan as a `dsdl.surface`.
class SurfaceWriter final
{
public:
    SurfaceWriter(mlir::OpBuilder& builder, const mlir::Location location, const SurfacePlan& plan)
        : builder_(builder)
        , location_(location)
        , plan_(plan)
    {
    }

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
                                        optionalString(scope.path),
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
                                       of,
                                       section,
                                       member);
        // The class is written only where the kind does not imply it.
        if (op.declaredClass() != irNameClass(decl.nameClass))
        {
            op.setNameClassAttr(mlir::dsdl::NameClassAttr::get(context, irNameClass(decl.nameClass)));
        }
    }

    mlir::OpBuilder&   builder_;
    mlir::Location     location_;
    const SurfacePlan& plan_;
};

/// Writes the surface plan of one target into the module: the scopes its output opens and every name
/// declared in them, one `dsdl.surface` per profile.
struct ProjectDSDLSurfacePass final
    : public mlir::PassWrapper<ProjectDSDLSurfacePass, mlir::OperationPass<mlir::ModuleOp>>
{
    ProjectDSDLSurfacePass() = default;
    ProjectDSDLSurfacePass(const ProjectDSDLSurfacePass& other)
        : PassWrapper(other)
    {
    }
    ProjectDSDLSurfacePass(ProjectDSDLSurfacePass&&)                 = delete;
    ProjectDSDLSurfacePass& operator=(const ProjectDSDLSurfacePass&) = delete;
    ProjectDSDLSurfacePass& operator=(ProjectDSDLSurfacePass&&)      = delete;
    ~ProjectDSDLSurfacePass() override                               = default;
    explicit ProjectDSDLSurfacePass(const SurfaceProjection& projection)
    {
        target_.setValue(projection.target);
        for (const std::string& profile : projection.profiles)
        {
            profiles_.push_back(profile);
        }
        package_.setValue(projection.packageName);
        versioned_.setValue(projection.versioning == TypeNameVersioning::Versioned);
    }

    llvm::StringRef getArgument() const final
    {
        return "project-dsdl-surface";
    }
    llvm::StringRef getDescription() const final
    {
        return "Write the scopes a target's output opens, and every name declared in them";
    }
    void getDependentDialects(mlir::DialectRegistry& registry) const override
    {
        registry.insert<mlir::dsdl::DSDLDialect>();
    }

    // NOLINTNEXTLINE(misc-override-with-different-visibility) -- MLIR declares passes this way.
    void runOnOperation() override
    {
        mlir::ModuleOp              module = getOperation();
        const LanguageTraits* const row    = languageTraitsNamed(target_.getValue());
        if (row == nullptr)
        {
            module.emitError("project-dsdl-surface: no language is spelt '") << target_.getValue() << "'";
            signalPassFailure();
            return;
        }
        std::vector<DefinitionParts> parts;
        for (const mlir::dsdl::SchemaOp schema : module.getBodyRegion().front().getOps<mlir::dsdl::SchemaOp>())
        {
            parts.push_back(schemaParts(module, schema));
        }
        const SurfacePlan plan =
            allocateSurface(*row,
                            parts,
                            SurfaceOptions{.packageName = package_.getValue(),
                                           .versioning  = versioned_ ? TypeNameVersioning::Versioned
                                                                     : TypeNameVersioning::Unversioned});
        mlir::OpBuilder builder = mlir::OpBuilder::atBlockEnd(&module.getBodyRegion().front());
        SurfaceWriter   writer(builder, module.getLoc(), plan);
        if (profiles_.empty())
        {
            writer.write(row->name, {});
            return;
        }
        for (const std::string& profile : profiles_)
        {
            writer.write(row->name, profile);
        }
    }

    Option<std::string>     target_{*this,
                                    "target",
                                    llvm::cl::desc(
                                        "The --target-language spelling of the language whose surface is written")};
    ListOption<std::string> profiles_{*this,
                                      "profiles",
                                      llvm::cl::desc("The profiles, a surface each; none writes one without")};
    Option<std::string>     package_{*this, "package", llvm::cl::desc("The generated package's name")};
    Option<bool>            versioned_{*this,
                                       "versioned-type-names",
                                       llvm::cl::desc("Whether a type's name carries its version"),
                                       llvm::cl::init(false)};
};

}  // namespace

std::unique_ptr<mlir::Pass> createProjectDSDLSurfacePass(const SurfaceProjection& projection)
{
    return std::make_unique<ProjectDSDLSurfacePass>(projection);
}

void registerProjectDSDLSurfacePass()
{
    static mlir::PassRegistration<ProjectDSDLSurfacePass> const registration;
}

}  // namespace llvmdsdl
