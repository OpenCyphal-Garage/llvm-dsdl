//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// A definition's lowered functions, read from the module, and their helpers' names.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_HELPER_BINDING_NAMING_H
#define LLVMDSDL_CODEGEN_HELPER_BINDING_NAMING_H

#include <vector>

#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/Support/BodyNaming.h"
#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "mlir/IR/BuiltinOps.h"

namespace llvmdsdl
{

/// @brief The lowered functions @p schema owns, in the module's order.
///
/// A function whose symbol the plan grammar does not read is left out.
/// @param[in] module The lowered module.
/// @param[in] schema The definition's schema.
/// @return The functions.
[[nodiscard]] std::vector<BodyParts> schemaBodyParts(mlir::ModuleOp module, mlir::dsdl::SchemaOp schema);

/// @brief @ref declareHelperNames over the helpers @p schema owns in @p module.
/// @param[in] language Target naming policy language.
/// @param[in] module The lowered module.
/// @param[in] schema The definition's schema.
/// @param[in,out] scope The declaring scope, owned by the caller.
/// @param[in] qualifier A name every helper is prefixed with, or empty.
/// @return Each helper's lowered symbol, under the name the scope declared it as.
[[nodiscard]] llvm::StringMap<std::string> renderSchemaHelperNames(Language             language,
                                                                   mlir::ModuleOp       module,
                                                                   mlir::dsdl::SchemaOp schema,
                                                                   NamingScope&         scope,
                                                                   llvm::StringRef      qualifier = {});

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_HELPER_BINDING_NAMING_H
