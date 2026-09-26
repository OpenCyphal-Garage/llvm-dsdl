//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Finds a definition's schema and section plans in the lowered module, reads from a plan what a
/// declaration takes from it: the alias verdict and the width of a union's tag, and names the
/// definition a schema or a composite step refers to.
///
//===----------------------------------------------------------------------===//

#ifndef LLVMDSDL_CODEGEN_SCHEMA_LOOKUP_H
#define LLVMDSDL_CODEGEN_SCHEMA_LOOKUP_H

#include <cstdint>
#include <string>

#include <llvm/ADT/StringRef.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/OwningOpRef.h>

#include "llvmdsdl/IR/DSDLOps.h"

namespace llvmdsdl
{

struct SemanticDefinition;
struct SemanticTypeRef;

/// @brief The schema of @p def in @p module; null when the module holds none.
mlir::dsdl::SchemaOp schemaOf(mlir::ModuleOp module, const SemanticDefinition& def);

/// @brief The definition @p schema is the schema of.
SemanticTypeRef typeRefOf(mlir::dsdl::SchemaOp schema);

/// @brief The definition the composite step @p io holds.
SemanticTypeRef typeRefOf(mlir::dsdl::IOOp io);

/// @brief The serialisation plan of a section of @p schema: "" for a message, "request" or
/// "response" for a service; null when the schema holds none.
mlir::dsdl::SerializationPlanOp sectionPlan(mlir::dsdl::SchemaOp schema, llvm::StringRef section);

/// @brief A union's tag as a step named `_tag_`: an unsigned field of the tag's width, which the
///        wire holds ahead of the option and the plan does not list among its steps.
///
/// The accessors of an equal-length union reach the tag as they reach a member, and a spelling
/// reads a member's shape off its step. The step is detached, belonging to no plan, and lives as
/// long as the reference; no DSDL field can be named `_tag_`, so it collides with none.
mlir::OwningOpRef<mlir::dsdl::IOOp> unionTagStep(mlir::MLIRContext* context, std::int64_t tagBits);

/// @brief One of the layout verdicts a plan carries, as the generated constants state it.
struct AliasVerdict final
{
    bool        holds{false};
    std::string reason{"unknown"};
};

/// @brief Whether the plan's serialised form is a contiguous byte image.
AliasVerdict wireFlatVerdict(mlir::dsdl::SerializationPlanOp plan);

/// @brief Whether the generated structure is that same byte image.
AliasVerdict hostImageVerdict(mlir::dsdl::SerializationPlanOp plan);

/// @brief The width of the tag a union's plan writes, in bits.
std::uint32_t unionTagBits(mlir::dsdl::SerializationPlanOp plan);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_SCHEMA_LOOKUP_H
