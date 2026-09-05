//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements initialisation of the DSDL MLIR dialect.
///
/// The dialect registration wires generated operation, attribute, and type classes into MLIR.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/IR/DSDLDialect.h"

#include "llvmdsdl/IR/DSDLAttrs.h"  // IWYU pragma: keep
#include "llvmdsdl/IR/DSDLOps.h"    // IWYU pragma: keep
#include "llvmdsdl/IR/DSDLTypes.h"  // IWYU pragma: keep

#include <llvm/ADT/TypeSwitch.h>         // IWYU pragma: keep
#include <llvm/Support/Casting.h>        // IWYU pragma: keep
#include <llvm/Support/LogicalResult.h>  // IWYU pragma: keep

#include "mlir/IR/Builders.h"               // IWYU pragma: keep
#include "mlir/IR/DialectImplementation.h"  // IWYU pragma: keep
#include "mlir/IR/OpImplementation.h"       // IWYU pragma: keep

using namespace mlir;
using namespace mlir::dsdl;

#include "llvmdsdl/IR/DSDLDialect.cpp.inc"  // IWYU pragma: keep

#define GET_TYPEDEF_CLASSES
#include "llvmdsdl/IR/DSDLTypes.cpp.inc"  // IWYU pragma: keep

#define GET_ATTRDEF_CLASSES
#include "llvmdsdl/IR/DSDLAttrs.cpp.inc"  // IWYU pragma: keep

void DSDLDialect::initialize()
{
    addOperations<
#define GET_OP_LIST
#include "llvmdsdl/IR/DSDLOps.cpp.inc"  // IWYU pragma: keep
        >();
    addTypes<
#define GET_TYPEDEF_LIST
#include "llvmdsdl/IR/DSDLTypes.cpp.inc"  // IWYU pragma: keep
        >();
    addAttributes<
#define GET_ATTRDEF_LIST
#include "llvmdsdl/IR/DSDLAttrs.cpp.inc"  // IWYU pragma: keep
        >();
}
