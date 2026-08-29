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
include "llvmdsdl/IR/DSDLTypes.td"

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

def DSDL_BitWriteOp : DSDL_Op<"bit_write"> {
  let summary = "Copy a run of bits from object storage into the wire buffer";
  let description = [{
    Copies `width` bits from `source` at `source_bit_offset` to `destination` at
    `destination_bit_offset` -- the operand order of `dsdl_runtime_copy_bits`, read as a
    destination-first assignment.

    This is how a bool array travels: it is stored bitpacked rather than as elements, so it
    moves as one run rather than a loop. `width` is an operand because that run's length is
    the array's count, which is not known here.

    The operation carries memory effects. It is the write, and a plan whose writes folded
    away has serialized nothing.
  }];
  let arguments = (ins DSDL_PtrType:$destination, I64:$destination_bit_offset, I64:$width,
      DSDL_PtrType:$source, I64:$source_bit_offset);
  let assemblyFormat = [{
    $destination `[` $destination_bit_offset `]` `,` $width `,`
    $source `[` $source_bit_offset `]`
    attr-dict `:` type($destination) `,` type($source)
  }];
}

def DSDL_BitReadOp : DSDL_Op<"bit_read"> {
  let summary = "Copy a run of bits from the wire buffer into object storage";
  let description = [{
    Not the inverse of `dsdl.bit_write` in operands, because the runtime's read is not the
    inverse of its write. Reading takes the buffer's size and tolerates a run that reaches
    past it, zero-extending the remainder, which is the tolerance a deserializer is required
    to have; writing past the end is an error instead. The operands are what
    `dsdl_runtime_get_bits` needs, and the destination is filled from its own beginning.
  }];
  let arguments = (ins DSDL_PtrType:$destination, DSDL_PtrType:$buffer,
      I64:$buffer_size_bytes, I64:$bit_offset, I64:$width);
  let assemblyFormat = [{
    $destination `,` $buffer `[` $bit_offset `]` `,` $width `,` `size` $buffer_size_bytes
    attr-dict `:` type($destination) `,` type($buffer)
  }];
}

def DSDL_WriteBitsOp : DSDL_Op<"write_bits"> {
  let summary = "Write a scalar value into the wire buffer at a bit offset";
  let description = [{
    The scalar write a serialization plan performs: a value already normalised by the
    plan's saturation helper, placed at `bit_offset` with `width` bits, answering with the
    runtime's error code.

    `buffer_size_bytes` travels with the buffer because the runtime bounds-checks against
    it rather than trusting the offset. `width` is an attribute rather than an operand
    because a DSDL field's width is fixed by the schema, and the target selects its
    primitive from the width and the value type together.

    `is_signed` is carried because MLIR integers are signless while the wire encoding is
    not: the same `i64` is written differently depending on it.
  }];
  let arguments = (ins DSDL_PtrType:$buffer, I64:$buffer_size_bytes, I64:$bit_offset,
      AnyTypeOf<[AnyInteger, AnyFloat]>:$value, I64Attr:$width, UnitAttr:$is_signed);
  let results = (outs I8:$error);
  let assemblyFormat = [{
    $buffer `[` $bit_offset `]` `,` $value `,` `size` $buffer_size_bytes
    attr-dict `:` type($buffer) `,` type($value)
  }];
}

def DSDL_ReadBitsOp : DSDL_Op<"read_bits"> {
  let summary = "Read a scalar value out of the wire buffer at a bit offset";
  let description = [{
    The inverse of `dsdl.write_bits`. It answers with the value alone: the runtime's read
    primitives report a short buffer by zero-extending rather than by an error code, which
    is the tolerance a deserializer is required to have.
  }];
  let arguments = (ins DSDL_PtrType:$buffer, I64:$buffer_size_bytes, I64:$bit_offset,
      I64Attr:$width, UnitAttr:$is_signed);
  let results = (outs AnyTypeOf<[AnyInteger, AnyFloat]>:$value);
  let assemblyFormat = [{
    $buffer `[` $bit_offset `]` `,` `size` $buffer_size_bytes
    attr-dict `:` type($buffer) `->` type($value)
  }];
}

def DSDL_IsNullOp : DSDL_Op<"is_null", [NoMemoryEffect]> {
  let summary = "Test a pointer the plan was handed against null";
  let description = [{
    A plan validates its arguments before touching them. The test is an operation rather
    than a comparison against a materialised null because null is spelled differently per
    target, and a plan should not have to know which.
  }];
  let arguments = (ins DSDL_PtrType:$pointer);
  let results = (outs I1:$result);
  let assemblyFormat = "$pointer attr-dict `:` type($pointer)";
}

def DSDL_BufferOrEmptyOp : DSDL_Op<"buffer_or_empty", [NoMemoryEffect]> {
  let summary = "The buffer, or a readable empty one when it is null";
  let description = [{
    A deserializer accepts a null buffer when the declared size is zero, and the runtime's
    read primitives require a non-null pointer even when they read nothing from it. This
    answers with a pointer that is always safe to pass, which each target supplies its own
    way.
  }];
  let arguments = (ins DSDL_PtrType:$buffer);
  let results = (outs DSDL_PtrType:$result);
  let assemblyFormat = "$buffer attr-dict `:` type($buffer) `->` type($result)";
}

def DSDL_LoadScalarOp : DSDL_Op<"load_scalar"> {
  let summary = "Read through a pointer to a scalar";
  let description = [{
    The in/out size a plan is handed arrives by pointer and is read once at entry.
  }];
  let arguments = (ins DSDL_PtrType:$pointer);
  let results = (outs AnyTypeOf<[AnyInteger, AnyFloat]>:$value);
  let assemblyFormat = "$pointer attr-dict `:` type($pointer) `->` type($value)";
}

def DSDL_StoreScalarOp : DSDL_Op<"store_scalar"> {
  let summary = "Write through a pointer to a scalar";
  let description = [{
    How a plan reports the size it consumed or produced.
  }];
  let arguments = (ins DSDL_PtrType:$pointer, AnyTypeOf<[AnyInteger, AnyFloat]>:$value);
  let assemblyFormat = "$pointer `,` $value attr-dict `:` type($pointer) `,` type($value)";
}

class DSDL_MemberAccessOp<string mnemonic, list<Trait> traits = []>
    : DSDL_Op<mnemonic, traits> {
  let description = [{
    `path` names the members to walk from the object, and `indices` their declaration
    positions. A variable-length array is a member holding a count and a pointer, so
    reaching either is two names rather than one, and a single-name form could not say it.

    A target resolves whichever of the two it can: the C path spells the names, and object
    emission walks the indices through the published layout.
  }];
  let arguments = (ins DSDL_PtrType:$object, StrArrayAttr:$path, DenseI64ArrayAttr:$indices);
}

def DSDL_LoadMemberOp : DSDL_MemberAccessOp<"load_member"> {
  let summary = "Read a member of the object a plan was handed";
  let results = (outs AnyTypeOf<[AnyInteger, AnyFloat]>:$value);
  let assemblyFormat = "$object $path attr-dict `:` type($object) `->` type($value)";
}

def DSDL_StoreMemberOp : DSDL_MemberAccessOp<"store_member"> {
  let summary = "Write a member of the object a plan was handed";
  let arguments = (ins DSDL_PtrType:$object, StrArrayAttr:$path, DenseI64ArrayAttr:$indices,
      AnyTypeOf<[AnyInteger, AnyFloat]>:$value);
  let assemblyFormat = "$object $path `,` $value attr-dict `:` type($object) `,` type($value)";
}

def DSDL_LoadElementOp : DSDL_MemberAccessOp<"load_element"> {
  let summary = "Read one element of an array member";
  let description = [{
    `path` reaches the element storage and `index` selects within it. The index is a value
    because it is a loop variable.

    `element_type` is the storage's own spelling, which is not the type the plan works in: a
    `uint8[<=255]` is held as `uint8_t` and normalised through helpers that take 64 bits.
    Addressing it as the working type would stride by the wrong width.
  }];
  let arguments = (ins DSDL_PtrType:$object, StrArrayAttr:$path, DenseI64ArrayAttr:$indices,
      I64:$index, StrAttr:$element_type);
  let results = (outs AnyTypeOf<[AnyInteger, AnyFloat]>:$value);
  let assemblyFormat = [{
    $object $path `[` $index `]` attr-dict `:` type($object) `->` type($value)
  }];
}

def DSDL_StoreElementOp : DSDL_MemberAccessOp<"store_element"> {
  let summary = "Write one element of an array member";
  let arguments = (ins DSDL_PtrType:$object, StrArrayAttr:$path, DenseI64ArrayAttr:$indices,
      I64:$index, AnyTypeOf<[AnyInteger, AnyFloat]>:$value, StrAttr:$element_type);
  let assemblyFormat = [{
    $object $path `[` $index `]` `,` $value attr-dict `:` type($object) `,` type($value)
  }];
}

def DSDL_MemberAddrOp : DSDL_Op<"member_addr", [NoMemoryEffect]> {
  let summary = "Address of a member of the object a plan was handed";
  let description = [{
    A nested composite is serialized by handing its own entry point a pointer to its member,
    rather than by inlining its layout here. That keeps one definition of each type's wire
    format, and it is why a plan needs an address and not a value.
  }];
  let arguments = (ins DSDL_PtrType:$object, StrArrayAttr:$path, DenseI64ArrayAttr:$indices);
  let results = (outs DSDL_PtrType:$address);
  let assemblyFormat = "$object $path attr-dict `:` type($object) `->` type($address)";
}

def DSDL_ElementAddrOp : DSDL_Op<"element_addr", [NoMemoryEffect]> {
  let summary = "Address of one element of an array member";
  let description = [{
    An array of composites is encoded element by element through the nested type's own entry
    point, so each iteration needs the address of that element rather than of the array.
  }];
  let arguments = (ins DSDL_PtrType:$object, StrArrayAttr:$path, DenseI64ArrayAttr:$indices,
      I64:$index, StrAttr:$element_type);
  let results = (outs DSDL_PtrType:$address);
  let assemblyFormat = [{
    $object $path `[` $index `]` attr-dict `:` type($object) `->` type($address)
  }];
}

def DSDL_BufferAtOp : DSDL_Op<"buffer_at", [NoMemoryEffect]> {
  let summary = "Address of a byte offset within the wire buffer";
  let description = [{
    A nested type serializes from the start of the buffer it is given, so the caller hands
    it the point its own encoding reached. The offset is in bytes because a composite
    begins on a byte boundary -- the plan aligns to one before this.
  }];
  let arguments = (ins DSDL_PtrType:$buffer, I64:$byte_offset);
  let results = (outs DSDL_PtrType:$address);
  let assemblyFormat = "$buffer `[` $byte_offset `]` attr-dict `:` type($buffer) `->` type($address)";
}

def DSDL_LocalOp : DSDL_Op<"local"> {
  let summary = "A local holding an initial value, addressed";
  let description = [{
    The nested entry point takes the space available by pointer and writes back what it
    used, so the caller needs somewhere to put both. This is that somewhere.
  }];
  let arguments = (ins AnyTypeOf<[AnyInteger, AnyFloat]>:$init);
  let results = (outs DSDL_PtrType:$address);
  let assemblyFormat = "$init attr-dict `:` type($init) `->` type($address)";
}

def DSDL_CallSerdesOp : DSDL_Op<"call_serdes"> {
  let summary = "Invoke a nested type's own serialize or deserialize entry point";
  let description = [{
    `callee` names the target's public symbol for that type rather than an operation in
    this module, because the definition lives in the nested type's own translation unit.
  }];
  let arguments = (ins StrAttr:$callee, DSDL_PtrType:$object, DSDL_PtrType:$buffer,
      DSDL_PtrType:$size);
  let results = (outs I8:$error);
  let assemblyFormat = [{
    $callee `(` $object `,` $buffer `,` $size `)` attr-dict `:`
    type($object) `,` type($buffer) `,` type($size)
  }];
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
