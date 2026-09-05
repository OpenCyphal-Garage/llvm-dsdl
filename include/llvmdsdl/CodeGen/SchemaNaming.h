//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Final C names for a lowered schema.
///
/// Lowering stamps the unscoped, unversioned spelling of every C name it puts on a schema,
/// because it does not know a backend's naming options and produces one module every backend
/// reads. A backend clones the schema and rewrites those names here, through the same scopes
/// and renderers it names its own output with.
///
/// Both the C backend and an object lowering need this. What they take from it differs: C
/// reads the member names, and an object lowering addresses members by position and needs the
/// symbol names, which the header it links against declares.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_SCHEMA_NAMING_H
#define LLVMDSDL_CODEGEN_SCHEMA_NAMING_H

#include <cstddef>
#include <string>

#include "mlir/IR/BuiltinOps.h"

#include "llvmdsdl/IR/DSDLOps.h"

#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/DefinitionNaming.h"

namespace llvmdsdl
{

/// @brief The C type name @p info is declared under.
/// @param[in] info The definition's discovered identity.
/// @param[in] versioning Whether the name carries the version.
/// @return The rendered type name.
[[nodiscard]] std::string cTypeNameFromInfo(const DiscoveredDefinition& info, TypeNameVersioning versioning);

/// @brief Rewrites, on @p schema, every C name lowering could only guess at.
///
/// Member names come from the section scope that also names the struct declaration, so two
/// fields whose C projections collide are told apart the same way in both. Type and symbol
/// names are re-rendered under @p versioning rather than patched, which is what keeps a header
/// declaring `Foo_1_0` from meeting an implementation defining `Foo`.
/// @param[in,out] schema The schema op to stamp, ordinarily a clone the caller owns.
/// @param[in] def The semantic definition the schema was lowered from.
/// @param[in] versioning Whether type names carry the version.
void stampCNames(mlir::dsdl::SchemaOp schema, const SemanticDefinition& def, TypeNameVersioning versioning);

/// @brief Stamps every schema in @p module against the definition it was lowered from.
///
/// Schemas are matched to definitions by full name and version, which the schema carries and
/// the definition is identified by. A schema with no definition in @p semantic keeps the names
/// lowering gave it.
/// @param[in,out] module The lowered module.
/// @param[in] semantic The model the module was lowered from.
/// @param[in] versioning Whether type names carry the version.
/// @return How many schemas were stamped.
std::size_t stampCNames(mlir::ModuleOp module, const SemanticModule& semantic, TypeNameVersioning versioning);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_SCHEMA_NAMING_H
