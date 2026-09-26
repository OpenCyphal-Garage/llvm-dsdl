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
  let summary = "A normalised DSDL schema declaration";
  let description = [{
    Contains field/constant metadata and a serialisation plan region.
  }];

  let arguments = (ins SymbolNameAttr:$sym_name, StrAttr:$full_name,
      I32Attr:$major, I32Attr:$minor, UnitAttr:$sealed,
      OptionalAttr<DSDL_I64Attr>:$extent_bits,
      OptionalAttr<DSDL_I64Attr>:$fixed_port_id, UnitAttr:$service,
      UnitAttr:$deprecated, OptionalAttr<StrAttr>:$doc,
      OptionalAttr<StrAttr>:$c_type_name, OptionalAttr<StrAttr>:$header_path);

  let regions = (region AnyRegion:$body);
  let assemblyFormat = "$sym_name attr-dict-with-keyword $body";
  let hasVerifier = 1;
}

def DSDL_FieldOp : DSDL_Op<"field", [NoMemoryEffect]> {
  let summary =
      "A field declaration in schema space (introspection/documentation)";
  let description = [{
    Carries a field's schema-level identity (name, resolved type name, optional
    generated C name, docs, owning service section, and union option index) for
    introspection and comment propagation. It is NOT consumed by the
    serialisation path — the wire layout lives in `dsdl.serialization_plan` —
    but it IS a first-class, verified op so malformed schema IR is rejected
    during lowering.

    `union_option_index` is the tag value that selects this field, and it is
    carried here as well as on the section's `dsdl.io` step because this is
    where the field's name is. A backend that gives an option a symbolic name
    reads both halves of the pair from one op. `dsdl.schema` checks the two
    against each other.
  }];
  let arguments = (ins StrAttr:$name, StrAttr:$type_name,
      OptionalAttr<StrAttr>:$c_name, UnitAttr:$padding,
      OptionalAttr<StrAttr>:$doc, OptionalAttr<StrAttr>:$section,
      OptionalAttr<DSDL_I64Attr>:$union_option_index);
  let assemblyFormat = "attr-dict";
  let hasVerifier = 1;
}

def DSDL_ConstantOp : DSDL_Op<"constant", [NoMemoryEffect]> {
  let summary =
      "A constant declaration in schema space (introspection/documentation)";
  let description = [{
    Carries a constant's schema-level identity (name, type name, rendered value
    text, docs, owning service section) for introspection and comment
    propagation. Not consumed by the serialisation path, but verified.
  }];
  let arguments = (ins StrAttr:$name, StrAttr:$type_name, StrAttr:$value_text,
      OptionalAttr<StrAttr>:$doc, OptionalAttr<StrAttr>:$section);
  let assemblyFormat = "attr-dict";
  let hasVerifier = 1;
}

def DSDL_SerializationPlanOp
    : DSDL_Op<"serialization_plan", [NoMemoryEffect, NoTerminator]> {
  let summary = "Container for explicit wire-layout operations";
  let description = [{
    One section's wire layout: the `dsdl.align` and `dsdl.io` steps in order, and the facts
    about the section as a whole. The `lowered_*` attributes are stamped by
    `lower-dsdl-exec`, which also names the helper functions it synthesises for the section.
  }];
  let arguments = (ins StrAttr:$c_type_name, StrAttr:$c_serialize_symbol,
      StrAttr:$c_deserialize_symbol, DSDL_I64Attr:$min_bits,
      DSDL_I64Attr:$max_bits, OptionalAttr<StrAttr>:$section,
      OptionalAttr<DSDL_I64Attr>:$extent_bits, UnitAttr:$sealed,
      UnitAttr:$is_union, UnitAttr:$fixed_size,
      OptionalAttr<DSDL_I64Attr>:$union_tag_bits,
      OptionalAttr<DSDL_I64Attr>:$union_option_count, UnitAttr:$lowered,
      OptionalAttr<DSDL_I64Attr>:$lowered_min_bits,
      OptionalAttr<DSDL_I64Attr>:$lowered_max_bits,
      OptionalAttr<DSDL_I64Attr>:$lowered_step_count,
      OptionalAttr<DSDL_I64Attr>:$lowered_field_count,
      OptionalAttr<DSDL_I64Attr>:$lowered_padding_count,
      OptionalAttr<DSDL_I64Attr>:$lowered_align_count,
      OptionalAttr<StrAttr>:$lowered_capacity_check_helper,
      OptionalAttr<StrAttr>:$lowered_union_tag_validate_helper,
      OptionalAttr<StrAttr>:$lowered_ser_union_tag_helper,
      OptionalAttr<StrAttr>:$lowered_deser_union_tag_helper,
      // Two layout verdicts, decided once during analysis and carried here for
      // every backend. `wire_flat` says the serialised form is a contiguous
      // byte image; `host_image` says the generated structure is that same
      // image. The reason names the property that blocked it.
      UnitAttr:$wire_flat, OptionalAttr<StrAttr>:$wire_flat_reason,
      UnitAttr:$host_image, OptionalAttr<StrAttr>:$host_image_reason,
      // The section asserts `@aliasable`; `wire_flat` then holds, as analysis
      // has checked.
      UnitAttr:$aliasable);
  let regions = (region AnyRegion:$body);
  let assemblyFormat = "attr-dict-with-keyword $body";
  let hasVerifier = 1;
  // The steps are read only once they have been verified themselves.
  let hasRegionVerifier = 1;
}

def DSDL_AlignOp : DSDL_Op<"align", [NoMemoryEffect]> {
  let summary = "Alignment event in serialisation plan";
  let arguments = (ins I32Attr:$bits, OptionalAttr<DSDL_I64Attr>:$step_index);
  let assemblyFormat = "attr-dict";
  let hasVerifier = 1;
}

def DSDL_IOOp : DSDL_Op<"io", [NoMemoryEffect]> {
  let summary = "Wire I/O event in serialisation plan";
  let description = [{
    One step of a section's wire layout: a field, or a run of reserved bits when `kind` is
    `padding`. Every fact the lowering knows about the step is an attribute here, and the
    verifier holds them to the ranges the wire format allows.

    The `composite_*` attributes are present exactly when `scalar_category` is `composite`.
    `step_index`, `lowered_bits` and the `lowered_*_helper` symbols are stamped by
    `lower-dsdl-exec`; the helpers a step needs depend on its category and array kind.
  }];
  let arguments = (ins StrAttr:$kind, StrAttr:$name, StrAttr:$type_name,
      StrAttr:$scalar_category, StrAttr:$cast_mode, StrAttr:$array_kind,
      DSDL_I64Attr:$bit_length, DSDL_I64Attr:$array_capacity,
      DSDL_I64Attr:$array_length_prefix_bits, DSDL_I64Attr:$alignment_bits,
      DSDL_I64Attr:$min_bits, DSDL_I64Attr:$max_bits,
      DSDL_I64Attr:$union_option_index, DSDL_I64Attr:$union_tag_bits,
      OptionalAttr<StrAttr>:$c_name, OptionalAttr<StrAttr>:$doc,
      OptionalAttr<StrAttr>:$composite_c_type_name,
      OptionalAttr<StrAttr>:$composite_full_name,
      OptionalAttr<DSDL_I64Attr>:$composite_major,
      OptionalAttr<DSDL_I64Attr>:$composite_minor,
      OptionalAttr<BoolAttr>:$composite_sealed,
      OptionalAttr<DSDL_I64Attr>:$composite_extent_bits,
      OptionalAttr<DSDL_I64Attr>:$step_index,
      OptionalAttr<DSDL_I64Attr>:$lowered_bits,
      OptionalAttr<StrAttr>:$lowered_ser_unsigned_helper,
      OptionalAttr<StrAttr>:$lowered_deser_unsigned_helper,
      OptionalAttr<StrAttr>:$lowered_ser_signed_helper,
      OptionalAttr<StrAttr>:$lowered_deser_signed_helper,
      OptionalAttr<StrAttr>:$lowered_ser_float_helper,
      OptionalAttr<StrAttr>:$lowered_deser_float_helper,
      OptionalAttr<StrAttr>:$lowered_ser_array_length_prefix_helper,
      OptionalAttr<StrAttr>:$lowered_deser_array_length_prefix_helper,
      OptionalAttr<StrAttr>:$lowered_array_length_validate_helper,
      OptionalAttr<StrAttr>:$lowered_delimiter_validate_helper,
      // The member holds a view of the buffer: the field's bytes and their
      // count, in place of a decoded copy. On a scalar composite step whose
      // type asserts `@aliasable`.
      UnitAttr:$held_as_view);
  let assemblyFormat = "attr-dict";
  let hasVerifier = 1;
  let extraClassDeclaration = [{
    /// Whether this step reserves bits and holds no member.
    bool isPadding() { return getKind() == "padding"; }
    /// Whether the field is an array of any kind.
    bool isArray() { return getArrayKind() != "none"; }
    /// Whether the field is a variable-length array.
    bool isVariableArray() { return getArrayKind().starts_with("variable"); }
    /// Whether the field is a nested composite.
    bool isComposite() { return getScalarCategory() == "composite"; }
  }];
}

def DSDL_BitWriteOp : DSDL_Op<"bit_write"> {
  let summary = "Copy a run of bits from object storage into the wire buffer";
  let description = [{
    Copies `width` bits from `source` at `source_bit_offset` to `destination` at
    `destination_bit_offset` -- the operand order of `dsdl_runtime_copy_bits`, read as a
    destination-first assignment.

    The plan moves a bool array as one run, in the layout the wire wants. `width` is an
    operand because that run's length is the array's count, which is not known here.

    The operation carries memory effects. It is the write, and a plan whose writes folded
    away has serialised nothing.

    `variable_length` marks a run whose array's length varies, which a target may store
    differently from one of fixed length. A target that stores a bool per element has the
    run expanded by `dsdl-expand-bool-runs` into `dsdl.write_bit` over the elements.
  }];
  let arguments = (ins DSDL_PtrType:$destination, I64:$destination_bit_offset,
      I64:$width, DSDL_PtrType:$source, I64:$source_bit_offset,
      UnitAttr:$variable_length);
  let assemblyFormat = [{
    $destination `[` $destination_bit_offset `]` `,` $width `,`
    $source `[` $source_bit_offset `]`
    attr-dict `:` type($destination) `,` type($source)
  }];
}

def DSDL_ImageReadOp : DSDL_Op<"image_read"> {
  let summary = "Fill an object from the wire in one move, where the two are "
                "the same bytes";
  let description = [{
    Valid only where the section's `host_image` verdict holds: the generated structure is then
    byte-identical to the wire form, so the whole payload moves at once and no field is touched
    individually.

    It keeps the tolerance a deserialiser is required to have. A buffer shorter than `bytes` is a
    valid encoding, so this moves what is there and zeroes the rest, which is what reading each
    field with `dsdl.bit_read` would have produced -- the structure being the wire image is
    exactly what makes those two the same answer.

    No saturation is needed and none is emitted: a host-image field is stored in the width the wire
    carries it, so every value the structure can hold is one the wire can carry. That is the
    argument that this is complete, not merely quicker.

    The bytes are the target's, so this is only correct where the host orders them as the wire
    does. The backend that spells it says so.
  }];
  let arguments = (ins DSDL_PtrType:$object, DSDL_PtrType:$buffer,
      I64:$buffer_size_bytes, DSDL_I64Attr:$bytes);
  let assemblyFormat = [{
    $object `,` $buffer `,` `size` $buffer_size_bytes attr-dict `:` type($object) `,` type($buffer)
  }];
  let hasVerifier = 1;
}

def DSDL_ImageWriteOp : DSDL_Op<"image_write"> {
  let summary = "Write an object to the wire in one move, where the two are "
                "the same bytes";
  let description = [{
    The counterpart of `dsdl.image_read`, and valid under the same verdict. Writing has no
    tolerance to keep: a buffer too short is an error, which the plan has already rejected by the
    time this runs.

    It cannot leak what the structure does not hold, because holding no padding is part of the
    verdict that allows it.
  }];
  let arguments = (ins DSDL_PtrType:$buffer, DSDL_PtrType:$object,
      DSDL_I64Attr:$bytes);
  let assemblyFormat = [{
    $buffer `,` $object attr-dict `:` type($buffer) `,` type($object)
  }];
  let hasVerifier = 1;
}

def DSDL_BitReadOp : DSDL_Op<"bit_read"> {
  let summary = "Copy a run of bits from the wire buffer into object storage";
  let description = [{
    Not the inverse of `dsdl.bit_write` in operands. Reading takes the buffer's size and tolerates a run that reaches
    past it, zero-extending the remainder, which is the tolerance a deserialiser is required
    to have; writing past the end is an error instead. The operands are what
    `dsdl_runtime_get_bits` needs, and the destination is filled from its own beginning.

    `variable_length` is as on `dsdl.bit_write`, and a target that stores a bool per element
    has the run expanded into `dsdl.read_bit` over the elements.
  }];
  let arguments = (ins DSDL_PtrType:$destination, DSDL_PtrType:$buffer,
      I64:$buffer_size_bytes, I64:$bit_offset, I64:$width,
      UnitAttr:$variable_length);
  let assemblyFormat = [{
    $destination `,` $buffer `[` $bit_offset `]` `,` $width `,` `size` $buffer_size_bytes
    attr-dict `:` type($destination) `,` type($buffer)
  }];
}

def DSDL_WriteBitOp : DSDL_Op<"write_bit"> {
  let summary = "Write one bit of the wire buffer from a bool";
  let description = [{
    One element of a bool array stored a bool per element, at `bit_offset`. The run it
    belongs to was bounded before it began, so the write answers nothing.
    `dsdl-expand-bool-runs` builds it; the plan builder does not.
  }];
  let arguments = (ins DSDL_PtrType:$buffer, I64:$bit_offset, I1:$value);
  let assemblyFormat = [{
    $buffer `[` $bit_offset `]` `,` $value attr-dict `:` type($buffer)
  }];
}

def DSDL_ReadBitOp : DSDL_Op<"read_bit"> {
  let summary = "Read one bit of the wire buffer as a bool";
  let description = [{
    One element of a bool array stored a bool per element, from `bit_offset`. A bit past the
    end of the buffer reads as zero, as every read past the end does.
    `dsdl-expand-bool-runs` builds it; the plan builder does not.
  }];
  let arguments = (ins DSDL_PtrType:$buffer, I64:$buffer_size_bytes,
      I64:$bit_offset);
  let results = (outs I1:$value);
  let assemblyFormat = [{
    $buffer `[` $bit_offset `]` `,` `size` $buffer_size_bytes attr-dict `:` type($buffer)
  }];
}

def DSDL_WriteBitsOp : DSDL_Op<"write_bits"> {
  let summary = "Write a scalar value into the wire buffer at a bit offset";
  let description = [{
    The scalar write a serialisation plan performs: a value already normalised by the
    plan's saturation helper, placed at `bit_offset` with `width` bits, answering with the
    runtime's error code.

    `buffer_size_bytes` travels with the buffer because the runtime bounds-checks against
    it rather than trusting the offset. `width` is an attribute rather than an operand
    because a DSDL field's width is fixed by the schema, and the target selects its
    primitive from the width and the value type together.

    `is_signed` is carried because MLIR integers are signless while the wire encoding is
    not: the same `i64` is written differently depending on it.
  }];
  let arguments = (ins DSDL_PtrType:$buffer, I64:$buffer_size_bytes,
      I64:$bit_offset, AnyTypeOf<[AnyInteger, AnyFloat]>:$value, I64Attr:$width,
      UnitAttr:$is_signed);
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
    is the tolerance a deserialiser is required to have.
  }];
  let arguments = (ins DSDL_PtrType:$buffer, I64:$buffer_size_bytes,
      I64:$bit_offset, I64Attr:$width, UnitAttr:$is_signed);
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
    target.
  }];
  let arguments = (ins DSDL_PtrType:$pointer);
  let results = (outs I1:$result);
  let assemblyFormat = "$pointer attr-dict `:` type($pointer)";
}

def DSDL_IndexHoldsOp : DSDL_Op<"index_holds", [NoMemoryEffect]> {
  let summary = "Whether the index type of the target holds a count exactly";
  let description = [{
    True when `value`, an element count, survives the round trip through the target's index
    type as a signed quantity, which bounds it by the largest count a container there can hold.
    The answer depends on the target, so it is an operation of its own: a pass that folded a
    cast round trip on the assumption of a 64-bit index would answer for the host.
  }];
  let arguments = (ins I64:$value);
  let results = (outs I1:$holds);
  let assemblyFormat = "$value attr-dict";
}

def DSDL_BufferOrEmptyOp : DSDL_Op<"buffer_or_empty", [NoMemoryEffect]> {
  let summary = "The buffer, or a readable empty one when it is null";
  let description = [{
    A deserialiser accepts a null buffer when the declared size is zero, a getter reads one
    as empty whatever size it is handed, and the runtime's read primitives require a non-null
    pointer even when they read nothing from it. This answers with a pointer that is always
    safe to pass, which each target supplies its own way.
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
  let assemblyFormat =
      "$pointer attr-dict `:` type($pointer) `->` type($value)";
}

def DSDL_StoreScalarOp : DSDL_Op<"store_scalar"> {
  let summary = "Write through a pointer to a scalar";
  let description = [{
    How a plan reports the size it consumed or produced.
  }];
  let arguments = (ins DSDL_PtrType:$pointer,
      AnyTypeOf<[AnyInteger, AnyFloat]>:$value);
  let assemblyFormat =
      "$pointer `,` $value attr-dict `:` type($pointer) `,` type($value)";
}

class DSDL_MemberAccessOp<string mnemonic, list<Trait> traits = []>
    : DSDL_Op<mnemonic, traits> {
  let description = [{
    `member` is the DSDL name of the field the access reaches. Each target spells the
    storage it declares for that field: the C path through the member's C name, object
    emission through the member's position in the published layout, and every other
    language through its own naming.
  }];
  let arguments = (ins DSDL_PtrType:$object, StrAttr:$member);
}

def DSDL_LoadMemberOp : DSDL_MemberAccessOp<"load_member"> {
  let summary = "Read a member of the object a plan was handed";
  let results = (outs AnyTypeOf<[AnyInteger, AnyFloat]>:$value);
  let assemblyFormat =
      "$object $member attr-dict `:` type($object) `->` type($value)";
}

def DSDL_StoreMemberOp : DSDL_MemberAccessOp<"store_member"> {
  let summary = "Write a member of the object a plan was handed";
  let arguments = (ins DSDL_PtrType:$object, StrAttr:$member,
      AnyTypeOf<[AnyInteger, AnyFloat]>:$value);
  let assemblyFormat =
      "$object $member `,` $value attr-dict `:` type($object) `,` type($value)";
}

def DSDL_LoadElementOp : DSDL_MemberAccessOp<"load_element"> {
  let summary = "Read one element of an array member";
  let description = [{
    `member` is an array field and `index` selects within its elements; the index is a
    value because it is a loop variable.

    `storage_category` and `storage_bits` describe the element storage, which is not the
    type the plan works in: a `uint8[<=255]` is held as eight-bit unsigned and normalised
    through helpers that take 64 bits. Addressing it as the working type would stride by
    the wrong width. A bool array is bit-packed, so its elements are bytes.
  }];
  let arguments = (ins DSDL_PtrType:$object, StrAttr:$member, I64:$index,
      StrAttr:$storage_category, DSDL_I64Attr:$storage_bits);
  let results = (outs AnyTypeOf<[AnyInteger, AnyFloat]>:$value);
  let assemblyFormat = [{
    $object $member `[` $index `]` attr-dict `:` type($object) `->` type($value)
  }];
}

def DSDL_StoreElementOp : DSDL_MemberAccessOp<"store_element"> {
  let summary = "Write one element of an array member";
  let arguments = (ins DSDL_PtrType:$object, StrAttr:$member, I64:$index,
      AnyTypeOf<[AnyInteger, AnyFloat]>:$value, StrAttr:$storage_category,
      DSDL_I64Attr:$storage_bits);
  let assemblyFormat = [{
    $object $member `[` $index `]` `,` $value attr-dict `:` type($object) `,` type($value)
  }];
}

def DSDL_MemberAddrOp : DSDL_Op<"member_addr", [NoMemoryEffect]> {
  let summary = "Address of a member of the object a plan was handed";
  let description = [{
    A nested composite is serialised by handing its own entry point a pointer to its member,
    rather than by inlining its layout here. That keeps one definition of each type's wire
    format, and it is why a plan needs an address and not a value.
  }];
  let arguments = (ins DSDL_PtrType:$object, StrAttr:$member);
  let results = (outs DSDL_PtrType:$address);
  let assemblyFormat =
      "$object $member attr-dict `:` type($object) `->` type($address)";
}

def DSDL_ElementAddrOp : DSDL_Op<"element_addr", [NoMemoryEffect]> {
  let summary = "Address of one element of an array member";
  let description = [{
    An array of composites is encoded element by element through the nested type's own entry
    point, so each iteration needs the address of that element rather than of the array.
  }];
  let arguments = (ins DSDL_PtrType:$object, StrAttr:$member, I64:$index,
      StrAttr:$storage_category, DSDL_I64Attr:$storage_bits);
  let results = (outs DSDL_PtrType:$address);
  let assemblyFormat = [{
    $object $member `[` $index `]` attr-dict `:` type($object) `->` type($address)
  }];
}

def DSDL_ArrayLengthOp : DSDL_Op<"array_length"> {
  let summary = "The element count a variable-length array member holds";
  let arguments = (ins DSDL_PtrType:$object, StrAttr:$member);
  let results = (outs I64:$value);
  let assemblyFormat = "$object $member attr-dict `:` type($object)";
}

def DSDL_SetArrayLengthOp : DSDL_Op<"set_array_length"> {
  let summary = "Set the element count of a variable-length array member";
  let description = [{
    Sizes the member to `value` elements. A body hands over a validated length: at most the
    declared capacity, and held exactly by the index type of the target, so a spelling narrows
    the value without bounding it.
  }];
  let arguments = (ins DSDL_PtrType:$object, StrAttr:$member, I64:$value);
  let assemblyFormat = "$object $member `,` $value attr-dict `:` type($object)";
}

def DSDL_UnionTagOp : DSDL_Op<"union_tag"> {
  let summary = "The option a union object holds";
  let arguments = (ins DSDL_PtrType:$object);
  let results = (outs I64:$value);
  let assemblyFormat = "$object attr-dict `:` type($object)";
}

def DSDL_SetUnionTagOp : DSDL_Op<"set_union_tag"> {
  let summary = "Select the option a union object holds";
  let arguments = (ins DSDL_PtrType:$object, I64:$value);
  let assemblyFormat = "$object `,` $value attr-dict `:` type($object)";
}

def DSDL_BufferAtOp : DSDL_Op<"buffer_at", [NoMemoryEffect]> {
  let summary = "Address of a byte offset within the wire buffer";
  let description = [{
    A nested type serialises from the start of the buffer it is given, so the caller hands
    it the point its own encoding reached. The offset is in bytes because a composite
    begins on a byte boundary -- the plan aligns to one before this.

    At a constant offset of nought the address is the buffer, and the operation folds to it
    where the two pointer types agree.
  }];
  let arguments = (ins DSDL_PtrType:$buffer, I64:$byte_offset);
  let results = (outs DSDL_PtrType:$address);
  let hasFolder = 1;
  let assemblyFormat = "$buffer `[` $byte_offset `]` attr-dict `:` "
                       "type($buffer) `->` type($address)";
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
  let summary =
      "Invoke a nested type's own serialize or deserialize entry point";
  let description = [{
    `callee` is the nested type's body for `direction`, as `build-dsdl-plan-bodies` names
    it, and `member` the field whose type is called. A target calls whatever its own
    emission of that type publishes: the C path its header's entry point, object emission
    the body itself.
  }];
  let arguments = (ins FlatSymbolRefAttr:$callee, StrAttr:$member,
      StrAttr:$direction, DSDL_PtrType:$object, DSDL_PtrType:$buffer,
      DSDL_PtrType:$size);
  let results = (outs I8:$error);
  let assemblyFormat = [{
    $callee `(` $object `,` $buffer `,` $size `)` attr-dict `:`
    type($object) `,` type($buffer) `,` type($size)
  }];
}

def DSDL_CallSerdesSizedOp : DSDL_Op<"call_serdes_sized"> {
  let summary = "Invoke a nested type's entry point, handing it a size and "
                "taking back what it used";
  let description = [{
    What `dsdl.call_serdes` becomes for a target whose nested entry point takes the bytes
    available as the length of the buffer it is handed, and answers the bytes it consumed
    beside its error, rather than reading and writing a size through a pointer. `available`
    is what the plan offers. `consumed` is what the callee used, counted as the target counts
    a length, and means something only where `error` is zero.
    `dsdl-fold-nested-call-sizes` builds it; the plan builder does not.
  }];
  let arguments = (ins FlatSymbolRefAttr:$callee, StrAttr:$member,
      StrAttr:$direction, DSDL_PtrType:$object, DSDL_PtrType:$buffer,
      I64:$available);
  let results = (outs I8:$error, Index:$consumed);
  let assemblyFormat = [{
    $callee `(` $object `,` $buffer `,` $available `)` attr-dict `:`
    type($object) `,` type($buffer)
  }];
}

def DSDL_StoreViewOp : DSDL_Op<"store_view"> {
  let summary = "Hold a run of the wire buffer in a member, as a view";
  let description = [{
    A member held as a view (`held_as_view` on its step) takes the bytes of its field from the
    buffer the container was handed, and the count of them that are there, in place of a decoded
    copy. The container's deserialise builds this where it would call the nested type; the nested
    type's own accessors read the member. With `index`, the member is an array of views and the
    store is to one element of it.
  }];
  let arguments = (ins DSDL_PtrType:$object, StrAttr:$member,
      Optional<I64>:$index, DSDL_PtrType:$bytes, I64:$size_bytes);
  let assemblyFormat = [{
    $object $member (`[` $index^ `]`)? `=` $bytes `,` $size_bytes attr-dict `:`
    type($object) `,` type($bytes)
  }];
}

def DSDL_ClearViewOp : DSDL_Op<"clear_view"> {
  let summary = "Hold nothing in a view member";
  let description = [{
    What the initialise body stores in a member held as a view: no bytes, in every element where
    the member is a fixed array of views. Serialising an empty view writes the field's width of
    zeros, which is the nested type's default.
  }];
  let arguments = (ins DSDL_PtrType:$object, StrAttr:$member);
  let assemblyFormat = "$object $member attr-dict `:` type($object)";
}

def DSDL_LoadViewOp : DSDL_Op<"load_view"> {
  let summary = "The bytes a view member holds, and how many";
  let description = [{
    With `index`, the member is an array of views and the load is of one element of it.
  }];
  let arguments = (ins DSDL_PtrType:$object, StrAttr:$member,
      Optional<I64>:$index);
  let results = (outs DSDL_PtrType:$bytes, I64:$size_bytes);
  let assemblyFormat = [{
    $object $member (`[` $index^ `]`)? attr-dict `:` type($object) `->` type($bytes)
  }];
}

def DSDL_CopyBytesOp : DSDL_Op<"copy_bytes"> {
  let summary =
      "Copy a run of bytes into the wire buffer, zero-filled to a width";
  let description = [{
    Writes `bytes` bytes at `destination`: what `source` holds, up to `source_size_bytes`, and
    zeros for the rest. A container's serialise moves a view member with this, so a short or an
    empty view serialises as the nested type would with the missing bytes zero. The plan has
    checked that the buffer holds the layout by the time this runs.
  }];
  let arguments = (ins DSDL_PtrType:$destination, DSDL_PtrType:$source,
      I64:$source_size_bytes, DSDL_I64Attr:$bytes);
  let assemblyFormat = [{
    $destination `,` $source `,` `size` $source_size_bytes attr-dict `:`
    type($destination) `,` type($source)
  }];
}

def DSDL_CallInitializeOp : DSDL_Op<"call_initialize"> {
  let summary = "Invoke a nested type's own initialise entry point";
  let description = [{
    `callee` is the nested type's initialise body, as `build-dsdl-plan-bodies` names it,
    and `member` the field whose type is called. It is `dsdl.call_serdes` without a wire:
    an initialiser reads and writes no buffer, and a call that carried one would be handing
    the nested type two operands it has to ignore. A target calls whatever its own emission
    of that type publishes, as it does for the serdes pair.
  }];
  let arguments = (ins FlatSymbolRefAttr:$callee, StrAttr:$member,
      DSDL_PtrType:$object);
  let results = (outs I8:$error);
  let assemblyFormat = [{
    $callee `(` $object `)` attr-dict `:` type($object)
  }];
}

#endif // LLVMDSDL_IR_DSDL_OPS_TD
