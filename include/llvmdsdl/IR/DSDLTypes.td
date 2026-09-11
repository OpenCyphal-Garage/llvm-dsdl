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

def DSDL_ObjectType : TypeDef<DSDL_Dialect, "Object"> {
  let mnemonic = "object";
  let summary = "The object one serialisation plan reads and writes";
  let description = [{
    Named by the plan's identity -- the definition's full name and version, and the section
    of a service -- which every target resolves to the struct or class it declares for that
    plan. A body says which object it works on and nothing about how a language holds it.
  }];
  let parameters = (ins StringRefParameter<"plan identity">:$identity);
  let assemblyFormat = "`<` $identity `>`";
}

def DSDL_ByteType : TypeDef<DSDL_Dialect, "Byte"> {
  let mnemonic = "byte";
  let summary = "One byte of the wire buffer";
}

def DSDL_SizeType : TypeDef<DSDL_Dialect, "Size"> {
  let mnemonic = "size";
  let summary =
      "The buffer size a plan is handed and the length it reports back";
}

def DSDL_PtrType : TypeDef<DSDL_Dialect, "Ptr"> {
  let mnemonic = "ptr";
  let summary = "A pointer to object or wire-buffer storage";
  let description = [{
    Addresses the storage a serialisation plan reads and writes: the object, the wire
    buffer, and the in/out size. The dialect carries its own pointer so that a plan body
    is expressed once and mapped per target -- `!emitc.ptr` for the C path, `!llvm.ptr`
    for object emission. `const` marks storage the plan only reads: the object a serialiser
    is handed, and the buffer a deserialiser is.
  }];
  let parameters = (ins "::mlir::Type":$pointee, "bool":$isConst);
  let assemblyFormat = "`<` custom<Pointee>($isConst, $pointee) `>`";
  let builders = [TypeBuilder<(ins "::mlir::Type":$pointee), [{
      return $_get($_ctxt, pointee, false);
    }]>];
}

#endif // LLVMDSDL_IR_DSDL_TYPES_TD
