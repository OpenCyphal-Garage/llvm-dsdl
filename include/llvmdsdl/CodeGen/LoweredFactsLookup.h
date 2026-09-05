//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared lookup helpers for lowered MLIR section facts.
///
/// This utility centralises deterministic lookup of per-definition section facts
/// used by backend emitters.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_LOWERED_FACTS_LOOKUP_H
#define LLVMDSDL_CODEGEN_LOWERED_FACTS_LOOKUP_H

#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvm/ADT/StringRef.h"

namespace llvmdsdl
{

/// @brief Finds lowered facts for one semantic definition section.
/// @param[in] loweredFacts Lowered facts map keyed by type and section.
/// @param[in] def Semantic definition.
/// @param[in] sectionKey Section selector (`""`, `"request"`, or `"response"`).
/// @return Pointer to section facts, or `nullptr` when unavailable.
const LoweredSectionFacts* lookupLoweredSectionFacts(const LoweredFactsMap&    loweredFacts,
                                                     const SemanticDefinition& def,
                                                     llvm::StringRef           sectionKey);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_LOWERED_FACTS_LOOKUP_H
