//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements the schema and plan lookups of SchemaLookup.h.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/SchemaLookup.h"

#include <cstdint>
#include <string>

#include <llvm/ADT/StringRef.h>
#include <mlir/IR/BuiltinOps.h>

#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/Semantics/Model.h"

namespace llvmdsdl
{

mlir::dsdl::SchemaOp schemaOf(mlir::ModuleOp module, const SemanticDefinition& def)
{
    for (mlir::dsdl::SchemaOp schema : module.getBodyRegion().front().getOps<mlir::dsdl::SchemaOp>())
    {
        if (schema.getFullName() == def.info.fullName &&
            static_cast<std::uint32_t>(schema.getMajor()) == def.info.majorVersion &&
            static_cast<std::uint32_t>(schema.getMinor()) == def.info.minorVersion)
        {
            return schema;
        }
    }
    return {};
}

mlir::dsdl::SerializationPlanOp sectionPlan(mlir::dsdl::SchemaOp schema, const llvm::StringRef section)
{
    if (!schema || schema.getBody().empty())
    {
        return {};
    }
    for (mlir::dsdl::SerializationPlanOp plan : schema.getBody().front().getOps<mlir::dsdl::SerializationPlanOp>())
    {
        if (plan.getSection().value_or(llvm::StringRef{}) == section)
        {
            return plan;
        }
    }
    return {};
}

AliasVerdict aliasVerdict(mlir::dsdl::SerializationPlanOp plan)
{
    AliasVerdict verdict;
    if (!plan)
    {
        return verdict;
    }
    verdict.eligible = plan.getZohAliasEligible();
    if (verdict.eligible)
    {
        verdict.reason = "eligible";
        return verdict;
    }
    const std::string reason = plan.getZohAliasReason().value_or(llvm::StringRef{}).str();
    if (!reason.empty())
    {
        verdict.reason = reason;
    }
    return verdict;
}

std::uint32_t unionTagBits(mlir::dsdl::SerializationPlanOp plan)
{
    if (plan && plan.getUnionTagBits())
    {
        return static_cast<std::uint32_t>(*plan.getUnionTagBits());
    }
    return 8U;
}

}  // namespace llvmdsdl
