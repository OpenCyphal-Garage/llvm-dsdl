//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// TableGen operation definitions for the DSDL MLIR dialect.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_IR_DSDL_OPS_TD
#define LLVMDSDL_IR_DSDL_OPS_TD

include "mlir/IR/OpBase.td"
include "mlir/IR/SymbolInterfaces.td"
include "mlir/Interfaces/SideEffectInterfaces.td"
include "llvmdsdl/IR/DSDLBase.td"

class DSDL_Op<string mnemonic, list<Trait> traits = []>
    : Op<DSDL_Dialect, mnemonic, traits>;

def DSDL_SchemaOp
    : DSDL_Op<"schema", [Symbol, IsolatedFromAbove, NoTerminator]> {
  let summary = "A normalized DSDL schema declaration";
  let description = [{
    Contains field/constant metadata and a serialization plan region.
  }];

  let arguments = (ins SymbolNameAttr:$sym_name, StrAttr:$full_name,
      I32Attr:$major, I32Attr:$minor, UnitAttr:$sealed,
      OptionalAttr<I64Attr>:$extent_bits, OptionalAttr<I64Attr>:$fixed_port_id,
      UnitAttr:$service, UnitAttr:$deprecated, OptionalAttr<StrAttr>:$doc);

  let regions = (region AnyRegion:$body);
  let assemblyFormat = "$sym_name attr-dict-with-keyword $body";
  let hasVerifier = 1;
}

def DSDL_FieldOp : DSDL_Op<"field", [NoMemoryEffect]> {
  let summary =
      "A field declaration in schema space (introspection/documentation)";
  let description = [{
    Carries a field's schema-level identity (name, resolved type name, optional
    generated C name, docs, and owning service section) for introspection and
    comment propagation. It is NOT consumed by the serialization path — the wire
    layout lives in `dsdl.serialization_plan` — but it IS a first-class,
    verified op so malformed schema IR is rejected during lowering.
  }];
  let arguments = (ins StrAttr:$name, StrAttr:$type_name,
      OptionalAttr<StrAttr>:$c_name, UnitAttr:$padding,
      OptionalAttr<StrAttr>:$doc, OptionalAttr<StrAttr>:$section);
  let assemblyFormat = "attr-dict";
  let hasVerifier = 1;
}

def DSDL_ConstantOp : DSDL_Op<"constant", [NoMemoryEffect]> {
  let summary =
      "A constant declaration in schema space (introspection/documentation)";
  let description = [{
    Carries a constant's schema-level identity (name, type name, rendered value
    text, docs, owning service section) for introspection and comment
    propagation. Not consumed by the serialization path, but verified.
  }];
  let arguments = (ins StrAttr:$name, StrAttr:$type_name, StrAttr:$value_text,
      OptionalAttr<StrAttr>:$doc, OptionalAttr<StrAttr>:$section);
  let assemblyFormat = "attr-dict";
  let hasVerifier = 1;
}

def DSDL_SerializationPlanOp
    : DSDL_Op<"serialization_plan", [NoMemoryEffect, NoTerminator]> {
  let summary = "Container for explicit wire-layout operations";
  let regions = (region AnyRegion:$body);
  let assemblyFormat = "attr-dict-with-keyword $body";
  let hasVerifier = 1;
}

def DSDL_AlignOp : DSDL_Op<"align", [NoMemoryEffect]> {
  let summary = "Alignment event in serialization plan";
  let arguments = (ins I32Attr:$bits);
  let assemblyFormat = "attr-dict";
  let hasVerifier = 1;
}

def DSDL_IOOp : DSDL_Op<"io", [NoMemoryEffect]> {
  let summary = "Wire I/O event in serialization plan";
  let arguments = (ins StrAttr:$kind, StrAttr:$name, StrAttr:$type_name,
      OptionalAttr<StrAttr>:$doc);
  let assemblyFormat = "attr-dict";
  let hasVerifier = 1;
}

def DSDL_ExecPlanOp : DSDL_Op<"exec_plan", [NoMemoryEffect, NoTerminator]> {
  let summary = "Container for executable wire operations";
  let regions = (region AnyRegion:$body);
  let assemblyFormat = "attr-dict-with-keyword $body";
}

def DSDL_BitReadOp : DSDL_Op<"bit_read", [NoMemoryEffect]> {
  let summary = "Executable bitstream read operation";
  let arguments = (ins I64Attr:$width, UnitAttr:$saturating);
  let assemblyFormat = "attr-dict";
}

def DSDL_BitWriteOp : DSDL_Op<"bit_write", [NoMemoryEffect]> {
  let summary = "Executable bitstream write operation";
  let arguments = (ins I64Attr:$width, UnitAttr:$saturating);
  let assemblyFormat = "attr-dict";
}

def DSDL_UnionSwitchOp
    : DSDL_Op<"union_switch", [NoMemoryEffect, NoTerminator]> {
  let summary = "Executable union tag dispatch operation";
  let arguments = (ins I64Attr:$tag_bits);
  let regions = (region AnyRegion:$body);
  let assemblyFormat = "attr-dict-with-keyword $body";
}

def DSDL_ZOHCheckOp : DSDL_Op<"zoh_check", [NoMemoryEffect]> {
  let summary = "Zero-overhead aliasability check";
  let arguments = (ins StrAttr:$result);
  let assemblyFormat = "attr-dict";
}

#endif // LLVMDSDL_IR_DSDL_OPS_TD
