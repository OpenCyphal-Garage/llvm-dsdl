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

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/Support/CommandLine.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/DialectRegistry.h>
#include <mlir/IR/MLIRContext.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Pass/PassRegistry.h>

#include "llvmdsdl/IR/DSDLDialect.h"
#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/Support/BodyNaming.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/PlanSymbol.h"
#include "llvmdsdl/Support/SectionScopes.h"
#include "llvmdsdl/Support/SurfacePlan.h"
#include "llvmdsdl/Transforms/Passes.h"
#include "llvmdsdl/Transforms/SurfaceTree.h"

namespace llvmdsdl
{
namespace
{

/// @brief The definition @p step holds, where it holds a composite.
std::optional<DefinitionRef> compositeOf(mlir::dsdl::IOOp step)
{
    const std::optional<llvm::StringRef> fullName = step.getCompositeFullName();
    if (!step.isComposite() || !fullName)
    {
        return std::nullopt;
    }
    llvm::SmallVector<llvm::StringRef, 8> components;
    fullName->split(components, '.');
    DefinitionRef ref{.namespaceComponents = {},
                      .shortName           = components.back().str(),
                      .majorVersion        = static_cast<std::uint32_t>(step.getCompositeMajor().value_or(0)),
                      .minorVersion        = static_cast<std::uint32_t>(step.getCompositeMinor().value_or(0))};
    for (const llvm::StringRef component : llvm::ArrayRef<llvm::StringRef>(components).drop_back())
    {
        ref.namespaceComponents.push_back(component.str());
    }
    return ref;
}

/// @brief The parts of one section of @p schema, from its plan and its field and constant ops.
SectionParts sectionParts(mlir::dsdl::SchemaOp schema, mlir::dsdl::SerializationPlanOp plan)
{
    const llvm::StringRef             section = plan.getSection().value_or("");
    llvm::StringMap<mlir::dsdl::IOOp> steps;
    for (mlir::dsdl::IOOp step : plan.getBody().getOps<mlir::dsdl::IOOp>())
    {
        if (!step.isPadding())
        {
            steps.try_emplace(step.getName(), step);
        }
    }
    SectionParts parts{.fields = {}, .constants = {}, .isUnion = plan.getIsUnion()};
    for (mlir::dsdl::FieldOp field : schema.getBody().getOps<mlir::dsdl::FieldOp>())
    {
        if (field.getSection().value_or("") == section)
        {
            const auto       found = steps.find(field.getName());
            mlir::dsdl::IOOp step  = (found != steps.end()) ? found->second : mlir::dsdl::IOOp{};
            parts.fields.push_back(
                FieldParts{.name             = field.getName().str(),
                           .padding          = field.getPadding(),
                           .array            = step && step.isArray(),
                           .unionOptionIndex = static_cast<std::uint32_t>(field.getUnionOptionIndex().value_or(0)),
                           .composite        = step ? compositeOf(step) : std::nullopt,
                           .view             = step && step.getHeldAsView()});
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
        if (profiles_.empty())
        {
            writeSurface(builder, module.getLoc(), plan, row->name, {}, {});
            return;
        }
        // A run of several profiles writes each to a directory the profile names.
        for (const std::string& profile : profiles_)
        {
            writeSurface(builder,
                         module.getLoc(),
                         plan,
                         row->name,
                         profile,
                         (profiles_.size() > 1) ? profile + "/" : std::string{});
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
