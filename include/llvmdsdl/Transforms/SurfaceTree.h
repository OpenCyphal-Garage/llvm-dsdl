//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// A surface plan written into a module as `dsdl.surface`, and read back from one.
///
/// `project-dsdl-surface` writes the plan `allocateSurface` answers for a target; an emitter reads
/// the names and placements its output declares from what the pass wrote, rather than composing
/// them.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_TRANSFORMS_SURFACE_TREE_H
#define LLVMDSDL_TRANSFORMS_SURFACE_TREE_H

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/Location.h>

#include "llvmdsdl/Support/GeneratedFact.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/SurfacePlan.h"

namespace llvmdsdl
{

/// @brief Writes @p plan at @p builder's insertion point, as the surface of @p target and @p profile.
/// @param[in] builder Where the surface is written.
/// @param[in] location The location its ops carry.
/// @param[in] plan The plan.
/// @param[in] target The `--target-language` spelling of the plan's language.
/// @param[in] profile The profile the plan is of; empty for none.
/// @param[in] directory The directory, ending in `/`, the plan's paths are under; empty for the
///            output directory.
void writeSurface(mlir::OpBuilder&   builder,
                  mlir::Location     location,
                  const SurfacePlan& plan,
                  llvm::StringRef    target,
                  llvm::StringRef    profile,
                  llvm::StringRef    directory);

/// @brief The name `dsdl.decl` spells @p fact with.
[[nodiscard]] llvm::StringRef generatedFactName(GeneratedFact fact);

/// @brief A target's surface, read from the module the lowering wrote it into.
///
/// The tree reads into the plan it was written from: every scope and declaration at its place, in
/// its order. A declaration of a lowered function names the function, and its entity carries the
/// schema, section and member the function's symbol states.
class SurfaceTree final
{
public:
    /// @brief Reads the surface @p module holds for @p row's target and @p profile.
    /// @param[in] module The lowered module.
    /// @param[in] row The target's row.
    /// @param[in] profile The profile; empty for the surface of a language without profiles.
    /// @return The surface, or an error where the module holds none.
    [[nodiscard]] static llvm::Expected<SurfaceTree> read(mlir::ModuleOp        module,
                                                          const LanguageTraits& row,
                                                          llvm::StringRef       profile = {});

    /// @brief The tree.
    [[nodiscard]] const SurfacePlan& plan() const;

    /// @brief The scope at @p index.
    [[nodiscard]] const SurfaceScope& scope(std::size_t index) const;

    /// @brief The type scope that declares @p section of the definition keyed @p key; for a service,
    ///        an empty @p section is the type that encloses its sections, where the language declares
    ///        one.
    /// @return Its index; a fatal error where the tree holds none.
    [[nodiscard]] std::size_t typeScope(llvm::StringRef key, llvm::StringRef section) const;

    /// @brief The file or module scope a definition keyed @p key is declared in.
    /// @return Its index; a fatal error where the tree holds none.
    [[nodiscard]] std::size_t definitionScope(llvm::StringRef key) const;

    /// @brief The declaration of @p kind made in @p scope for @p of, stating @p fact.
    /// @return The declaration, or null where the scope makes none.
    [[nodiscard]] const SurfaceDecl* find(std::size_t                  scope,
                                          SurfaceDeclKind              kind,
                                          const SurfaceEntity&         of,
                                          std::optional<GeneratedFact> fact = std::nullopt) const;

    /// @brief The name of the declaration of @p kind made in @p scope for @p of, stating @p fact.
    /// @return The name; a fatal error where the scope makes no such declaration.
    [[nodiscard]] const std::string& nameOf(std::size_t                  scope,
                                            SurfaceDeclKind              kind,
                                            const SurfaceEntity&         of,
                                            std::optional<GeneratedFact> fact = std::nullopt) const;

    /// @brief The name of the declaration of @p kind that stands for the lowered function @p symbol,
    ///        stating @p fact.
    /// @return The name; a fatal error where the surface makes no such declaration.
    [[nodiscard]] const std::string& nameOf(llvm::StringRef              symbol,
                                            SurfaceDeclKind              kind,
                                            std::optional<GeneratedFact> fact = std::nullopt) const;

    /// @brief The declaration of @p kind that stands for the lowered function @p symbol, stating
    ///        @p fact.
    /// @return The declaration, or null where the surface makes none.
    [[nodiscard]] const SurfaceDecl* declarationOf(llvm::StringRef              symbol,
                                                   SurfaceDeclKind              kind,
                                                   std::optional<GeneratedFact> fact = std::nullopt) const;

    /// @brief The index in the plan of the declaration of @p kind that stands for the lowered
    ///        function @p symbol, stating @p fact.
    /// @return The index, or none where the surface makes no such declaration.
    [[nodiscard]] std::optional<std::size_t> declarationIndex(llvm::StringRef              symbol,
                                                              SurfaceDeclKind              kind,
                                                              std::optional<GeneratedFact> fact = std::nullopt) const;

    /// @brief The scopes from the root's child to @p scope, outermost first.
    [[nodiscard]] std::vector<std::size_t> pathTo(std::size_t scope) const;

private:
    explicit SurfaceTree(SurfacePlan plan);

    SurfacePlan plan_;

    /// @brief Each declaration of a lowered function, by the function's symbol, the kind, and the fact
    ///        it states.
    std::map<std::tuple<std::string, SurfaceDeclKind, std::optional<GeneratedFact>>, std::size_t> functions_;
};

}  // namespace llvmdsdl

#endif  // LLVMDSDL_TRANSFORMS_SURFACE_TREE_H
