//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Reads a definition's lowered functions from the module.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/HelperBindingNaming.h"

#include <optional>
#include <string>
#include <vector>

#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/IR/BuiltinOps.h>

#include "llvmdsdl/CodeGen/BodyTranslator.h"
#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/Support/BodyNaming.h"
#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/Support/PlanSymbol.h"

namespace llvmdsdl
{

std::vector<BodyParts> schemaBodyParts(const mlir::ModuleOp module, mlir::dsdl::SchemaOp schema)
{
    std::vector<BodyParts> bodies;
    for (mlir::func::FuncOp fn : schemaFunctions(module, schema.getSymName()))
    {
        const llvm::StringRef           symbol = fn.getSymName();
        const std::optional<PlanSymbol> plan   = parsePlanSymbol(symbol);
        if (plan)
        {
            bodies.push_back(
                BodyParts{.symbol = symbol.str(), .plan = *plan, .unreferenced = fn->hasAttr("llvmdsdl.unreferenced")});
        }
    }
    return bodies;
}

llvm::StringMap<std::string> renderSchemaHelperNames(const Language        language,
                                                     const mlir::ModuleOp  module,
                                                     mlir::dsdl::SchemaOp  schema,
                                                     NamingScope&          scope,
                                                     const llvm::StringRef qualifier)
{
    return declareHelperNames(language, schemaBodyParts(module, schema), scope, qualifier);
}

}  // namespace llvmdsdl
