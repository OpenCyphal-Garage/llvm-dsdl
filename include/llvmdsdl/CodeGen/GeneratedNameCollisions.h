//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Rejects a definition that meets a name another definition generates beside its type.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_GENERATED_NAME_COLLISIONS_H
#define LLVMDSDL_CODEGEN_GENERATED_NAME_COLLISIONS_H

#include "llvm/ADT/ArrayRef.h"

#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/LanguageTraits.h"

namespace llvmdsdl
{

class DiagnosticEngine;

/// @brief Rejects a name one definition generates beside its type that another definition's
///        names reach.
///
/// C declares every type in one global scope, and C++ and Go every type of a namespace in one
/// namespace or package. Beside each type they declare names of their own: C its entry points,
/// accessors and macros, C++ its free entry points and a service's constants, Go its constants
/// and accessors. A definition may be called one of them -- `ns.A_EXTENT_BYTES_` beside `ns.A` in
/// C, `ns.MsgExtentBytes` beside `ns.Msg` in Go -- and then the two are one identifier.
///
/// Every name is claimed in the scope the language declares it in and compared with every other
/// definition's; a pair of type names is @ref checkScopedTypeNameCollisions's to report. The
/// names come from the composers and scopes the emitters name them with. An accessor is claimed
/// for every field, whether or not the section's shape gives it one, so what a corpus may be
/// called does not change with a field's type.
///
/// The check runs on the analysed module because a section's constants, array metadata and
/// option tags are named by scopes that take the analysed section.
/// @param[in] module The analysed definitions.
/// @param[in] outputLanguages Languages whose output names are checked.
/// @param[in] versioning Whether generated type names carry the version.
/// @param[in,out] diagnostics Diagnostic sink.
void checkGeneratedNameCollisions(const SemanticModule&          module,
                                  llvm::ArrayRef<LanguageTraits> outputLanguages,
                                  TypeNameVersioning             versioning,
                                  DiagnosticEngine&              diagnostics);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_GENERATED_NAME_COLLISIONS_H
