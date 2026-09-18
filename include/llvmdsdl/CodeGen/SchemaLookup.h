//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Finds a definition's schema and section plans in the lowered module, and reads from a plan
/// what a declaration takes from it: the alias verdict and the width of a union's tag.
///
//===----------------------------------------------------------------------===//

#ifndef LLVMDSDL_CODEGEN_SCHEMA_LOOKUP_H
#define LLVMDSDL_CODEGEN_SCHEMA_LOOKUP_H

#include <cstdint>
#include <string>

#include <llvm/ADT/StringRef.h>
#include <mlir/IR/BuiltinOps.h>

#include "llvmdsdl/IR/DSDLOps.h"

namespace llvmdsdl
{

struct SemanticDefinition;

/// @brief The schema of @p def in @p module; null when the module holds none.
mlir::dsdl::SchemaOp schemaOf(mlir::ModuleOp module, const SemanticDefinition& def);

/// @brief The serialisation plan of a section of @p schema: "" for a message, "request" or
/// "response" for a service; null when the schema holds none.
mlir::dsdl::SerializationPlanOp sectionPlan(mlir::dsdl::SchemaOp schema, llvm::StringRef section);

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
