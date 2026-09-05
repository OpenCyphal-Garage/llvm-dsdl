//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared TableGen base definitions used by DSDL dialect ops, attrs, and types.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_IR_DSDL_BASE_TD
#define LLVMDSDL_IR_DSDL_BASE_TD

include "mlir/IR/OpBase.td"

def DSDL_Dialect : Dialect {
  let name = "dsdl";
  let cppNamespace = "::mlir::dsdl";
  let useDefaultTypePrinterParser = 1;
  let useDefaultAttributePrinterParser = 1;
}

// A signless i64 attribute read back as a signed integer. Widths, counts and
// offsets are signed quantities in every consumer, and a verifier that rejects
// a negative one needs to see it as negative rather than as a very large
// unsigned value.
def DSDL_I64Attr
    : SignlessIntegerAttrBase<I64, "64-bit signless integer attribute"> {
  let returnType = [{ ::std::int64_t }];
  let convertFromStorage = [{ $_self.getValue().getSExtValue() }];
}

#endif // LLVMDSDL_IR_DSDL_BASE_TD
