// RUN: %dsdl-opt --convert-dsdl-to-emitc %s | FileCheck %s

// A nested composite is not inlined into its container's layout. It is serialised by handing
// its own entry point a pointer to the member, the point in the buffer the container's
// encoding reached, and the space left -- which is why a plan needs addresses rather than
// values, and a local to receive what the callee used.
//
// The body calls the nested type's body by its own symbol; the C path calls the entry point
// the nested type's header publishes, found from the name the schema records for it.

// CHECK-LABEL: func.func @vendor_Outer_1_0__serialize_ir_
// CHECK-SAME: !emitc.ptr<!emitc.opaque<"const struct vendor__Outer">>

module attributes {llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64} {
  dsdl.schema @vendor_Outer_1_0 attributes {c_type_name = "vendor__Outer", extent_bits = 8 : i64, full_name = "vendor.Outer", header_path = "vendor/Outer_1_0.h", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.field {c_name = "inner", name = "inner", type_name = "vendor.Inner.1.0"}
    dsdl.serialization_plan attributes {c_deserialize_symbol = "vendor__Outer__deserialize_", c_serialize_symbol = "vendor__Outer__serialize_", c_type_name = "vendor__Outer", extent_bits = 8 : i64, fixed_size, llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64, lowered, lowered_align_count = 1 : i64, lowered_capacity_check_helper = "llvmdsdl_plan_capacity_check__vendor_Outer_1_0", lowered_field_count = 1 : i64, lowered_max_bits = 8 : i64, lowered_min_bits = 8 : i64, lowered_padding_count = 0 : i64, lowered_step_count = 2 : i64, max_bits = 8 : i64, min_bits = 8 : i64, sealed} {
      dsdl.align {bits = 8 : i32, step_index = 0 : i64}
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 0 : i64, c_name = "inner", cast_mode = "saturated", composite_c_type_name = "vendor__Inner", composite_extent_bits = 8 : i64, composite_full_name = "vendor.Inner", composite_major = 1 : i64, composite_minor = 0 : i64, composite_sealed = true, kind = "field", lowered_bits = 8 : i64, max_bits = 8 : i64, min_bits = 8 : i64, name = "inner", scalar_category = "composite", step_index = 1 : i64, type_name = "vendor.Inner.1.0", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
  func.func private @llvmdsdl_plan_capacity_check__vendor_Outer_1_0(i64) -> i8

  func.func @vendor_Outer_1_0__serialize_ir_(
      %obj: !dsdl.ptr<const !dsdl.object<"vendor.Outer.1.0">>,
      %buf: !dsdl.ptr<!dsdl.byte>,
      %sz:  !dsdl.ptr<!dsdl.size>) -> i8 {
    %byte_off = arith.constant 0 : i64
    %remaining = dsdl.load_scalar %sz : !dsdl.ptr<!dsdl.size> -> i64

    // The space available goes in by pointer, so the callee can write back what it used.
    // CHECK: %[[SLOT:.*]] = "emitc.variable"()
    // CHECK: emitc.assign
    // CHECK: %[[SZP:.*]] = emitc.apply "&"(%[[SLOT]])
    %sizep = dsdl.local %remaining : i64 -> !dsdl.ptr<!dsdl.size>

    // CHECK: "emitc.member_of_ptr"({{.*}}) <{member = "inner"}>
    // CHECK: emitc.apply "&"
    %objp = dsdl.member_addr %obj "inner" : !dsdl.ptr<const !dsdl.object<"vendor.Outer.1.0">> -> !dsdl.ptr<const !dsdl.object<"vendor.Inner.1.0">>

    // CHECK: emitc.subscript
    // CHECK: emitc.apply "&"
    %bufp = dsdl.buffer_at %buf[%byte_off] : !dsdl.ptr<!dsdl.byte> -> !dsdl.ptr<!dsdl.byte>

    // The callee is the nested type's published symbol, defined in its own translation unit.
    // CHECK: emitc.call_opaque "vendor__Inner__serialize_"
    %err = dsdl.call_serdes @vendor_Inner_1_0__serialize_ir_(%objp, %bufp, %sizep) {direction = "serialize", member = "inner"} : !dsdl.ptr<const !dsdl.object<"vendor.Inner.1.0">>, !dsdl.ptr<!dsdl.byte>, !dsdl.ptr<!dsdl.size>

    // What it used is read back out of the same local, and advances the container's offset.
    // CHECK: emitc.subscript
    // CHECK: emitc.load
    %used = dsdl.load_scalar %sizep : !dsdl.ptr<!dsdl.size> -> i64
    %sum = arith.addi %used, %byte_off : i64
    %tr = arith.trunci %sum : i64 to i8
    %keep = arith.addi %err, %tr : i8
    return %keep : i8
  }

  func.func @vendor_Outer_1_0__deserialize_ir_(
      %obj: !dsdl.ptr<!dsdl.object<"vendor.Outer.1.0">>,
      %buf: !dsdl.ptr<const !dsdl.byte>,
      %sz:  !dsdl.ptr<!dsdl.size>) -> i8 {
    %ok = arith.constant 0 : i8
    return %ok : i8
  }
}
