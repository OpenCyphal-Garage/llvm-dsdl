//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// TableGen type definitions for the DSDL MLIR dialect.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_IR_DSDL_TYPES_TD
#define LLVMDSDL_IR_DSDL_TYPES_TD

include "mlir/IR/AttrTypeBase.td"
include "llvmdsdl/IR/DSDLBase.td"

def DSDL_TypeRefType : TypeDef<DSDL_Dialect, "TypeRef"> {
  let mnemonic = "type_ref";
  let summary = "A symbolic reference to a DSDL type";
  let parameters = (ins StringRefParameter<"fully-qualified type name">:$name);
  let assemblyFormat = "`<` $name `>`";
}

def DSDL_BitLengthSetType : TypeDef<DSDL_Dialect, "BitLengthSet"> {
  let mnemonic = "bls";
  let summary = "A symbolic bit-length-set type token";
  let parameters = (ins StringRefParameter<"canonical expression">:$expr);
  let assemblyFormat = "`<` $expr `>`";
}

def DSDL_OpaqueType : TypeDef<DSDL_Dialect, "Opaque"> {
  let mnemonic = "opaque";
  let summary = "A target type named by its spelling";
  let description = [{
    Names a type the target already has: the generated struct a plan reads and writes, and
    the `uint8_t` and `size_t` the published signature is declared with. C accepts no
    substitute for those spellings, so a plan states them and the C path passes them
    through unchanged.

    Object emission ignores the name. A pointer is a pointer, and a member resolves to an
    offset in the published layout via the index the access operations carry.
  }];
  let parameters = (ins StringRefParameter<"target spelling of the type">:$name);
  let assemblyFormat = "`<` $name `>`";
}

def DSDL_PtrType : TypeDef<DSDL_Dialect, "Ptr"> {
  let mnemonic = "ptr";
  let summary = "A pointer to object or wire-buffer storage";
  let description = [{
    Addresses the storage a serialization plan reads and writes: the object, the wire
    buffer, and the in/out size. The dialect carries its own pointer so that a plan body
    is expressed once and mapped per target -- `!emitc.ptr` for the C path, `!llvm.ptr`
    for object emission.
  }];
  let parameters = (ins "::mlir::Type":$pointee);
  let assemblyFormat = "`<` $pointee `>`";
}

#endif // LLVMDSDL_IR_DSDL_TYPES_TD
