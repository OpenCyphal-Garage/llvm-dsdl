// RUN: %dsdl-opt --convert-dsdl-to-emitc %s | FileCheck %s

// A whole serialisation plan body carried as operations rather than as C text, to pin the
// shape the producer has to build: the published signature spelled from the schema's names,
// the argument checks, the size read through a pointer, and the scalar write.
//
// Control flow is structured: there is no cf-to-emitc conversion, so the C path cannot take a
// branch graph, and the early returns of the hand-written text become nested `scf.if`
// yielding the error code.
//
// The body names its object and members by their DSDL identity; the schema beside it is where
// the conversion finds what the C backend named them.

// CHECK-LABEL: func.func @vendor_Widget_1_0__serialize_ir_
// The signature is spelled from the schema, which is what lets the definition match the
// declaration the header already publishes.
// CHECK-SAME: !emitc.ptr<!emitc.opaque<"const struct vendor__Widget">>
// CHECK-SAME: !emitc.ptr<!emitc.opaque<"uint8_t">>
// CHECK-SAME: !emitc.ptr<!emitc.opaque<"size_t">>

// CHECK: %[[NULL:.*]] = "emitc.constant"() <{value = #emitc.opaque<"NULL">}>
// CHECK: emitc.cmp eq
// The size arrives by pointer and is read through a zero subscript, then cast from the
// target's spelling into the width the plan works in.
// CHECK: emitc.subscript
// CHECK: emitc.load
// CHECK: emitc.cast
// CHECK: "emitc.member_of_ptr"({{.*}}) <{member = "foo"}>
// CHECK: emitc.call_opaque "dsdl_runtime_set_uxx"
// Writing the consumed size back casts in the other direction.
// CHECK: emitc.cast
// CHECK: emitc.assign

// `member_of_ptr` needs an lvalue holding the pointer, which a function parameter is not, hence
// the slot a member access starts from.
// CHECK-LABEL: func.func @vendor_Widget_1_0__deserialize_ir_
// CHECK-SAME: (%[[OBJ:.*]]: !emitc.ptr<!emitc.opaque<"struct vendor__Widget">>
// CHECK: %[[SLOT:.*]] = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.ptr<!emitc.opaque<"struct vendor__Widget">>>
// CHECK: emitc.assign %[[OBJ]] : {{.*}} to %[[SLOT]]
// CHECK: %[[M:.*]] = "emitc.member_of_ptr"(%[[SLOT]]) <{member = "foo"}>
// CHECK: emitc.assign
// CHECK: "emitc.member_of_ptr"({{.*}}) <{member = "foo"}>
// CHECK: emitc.load
// CHECK-NOT: dsdl.

module attributes {llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64} {
  dsdl.schema @vendor_Widget_1_0 attributes {c_type_name = "vendor__Widget", extent_bits = 8 : i64, full_name = "vendor.Widget", header_path = "vendor/Widget_1_0.h", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.field {c_name = "foo", name = "foo", type_name = "saturated uint8"}
    dsdl.serialization_plan attributes {c_deserialize_symbol = "vendor__Widget__deserialize_", c_serialize_symbol = "vendor__Widget__serialize_", c_type_name = "vendor__Widget", extent_bits = 8 : i64, fixed_size, llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64, lowered, lowered_align_count = 0 : i64, lowered_capacity_check_helper = "llvmdsdl_plan_capacity_check__vendor_Widget_1_0", lowered_field_count = 1 : i64, lowered_max_bits = 8 : i64, lowered_min_bits = 8 : i64, lowered_padding_count = 0 : i64, lowered_step_count = 1 : i64, max_bits = 8 : i64, min_bits = 8 : i64, sealed} {
      dsdl.io {alignment_bits = 1 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "foo", cast_mode = "saturated", kind = "field", lowered_bits = 8 : i64, lowered_deser_unsigned_helper = "llvmdsdl_plan_scalar_unsigned__vendor_Widget_1_0__0__deser", lowered_ser_unsigned_helper = "llvmdsdl_plan_scalar_unsigned__vendor_Widget_1_0__0__ser", max_bits = 8 : i64, min_bits = 8 : i64, name = "foo", scalar_category = "unsigned", step_index = 0 : i64, type_name = "saturated uint8", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
  func.func private @llvmdsdl_plan_capacity_check__vendor_Widget_1_0(i64) -> i8
  func.func private @llvmdsdl_plan_scalar_unsigned__vendor_Widget_1_0__0__ser(i64) -> i64
  func.func private @llvmdsdl_plan_scalar_unsigned__vendor_Widget_1_0__0__deser(i64) -> i64

  func.func @vendor_Widget_1_0__serialize_ir_(
      %obj: !dsdl.ptr<const !dsdl.object<"vendor.Widget.1.0">>,
      %buf: !dsdl.ptr<!dsdl.byte>,
      %sz:  !dsdl.ptr<!dsdl.size>) -> i8 {
    %c0   = arith.constant 0 : i64
    %c8   = arith.constant 8 : i64
    %ok   = arith.constant 0 : i8
    %inval = arith.constant -2 : i8

    %n1 = dsdl.is_null %obj : !dsdl.ptr<const !dsdl.object<"vendor.Widget.1.0">>
    %n2 = dsdl.is_null %buf : !dsdl.ptr<!dsdl.byte>
    %n3 = dsdl.is_null %sz  : !dsdl.ptr<!dsdl.size>
    %a  = arith.ori %n1, %n2 : i1
    %anynull = arith.ori %a, %n3 : i1

    %res = scf.if %anynull -> (i8) {
      scf.yield %inval : i8
    } else {
      %cap = dsdl.load_scalar %sz : !dsdl.ptr<!dsdl.size> -> i64
      %capbits = arith.muli %cap, %c8 : i64
      %cerr = func.call @llvmdsdl_plan_capacity_check__vendor_Widget_1_0(%capbits) : (i64) -> i8
      %bad = arith.cmpi slt, %cerr, %ok : i8
      %r = scf.if %bad -> (i8) {
        scf.yield %cerr : i8
      } else {
        %foo = dsdl.load_member %obj "foo" : !dsdl.ptr<const !dsdl.object<"vendor.Widget.1.0">> -> i64
        %norm = func.call @llvmdsdl_plan_scalar_unsigned__vendor_Widget_1_0__0__ser(%foo) : (i64) -> i64
        %werr = dsdl.write_bits %buf[%c0], %norm, size %cap {width = 8 : i64} : !dsdl.ptr<!dsdl.byte>, i64
        %wbad = arith.cmpi slt, %werr, %ok : i8
        %r2 = scf.if %wbad -> (i8) {
          scf.yield %werr : i8
        } else {
          %consumed = arith.constant 1 : i64
          dsdl.store_scalar %sz, %consumed : !dsdl.ptr<!dsdl.size>, i64
          scf.yield %ok : i8
        }
        scf.yield %r2 : i8
      }
      scf.yield %r : i8
    }
    return %res : i8
  }

  func.func @vendor_Widget_1_0__deserialize_ir_(
      %obj: !dsdl.ptr<!dsdl.object<"vendor.Widget.1.0">>,
      %buf: !dsdl.ptr<const !dsdl.byte>,
      %sz:  !dsdl.ptr<!dsdl.size>) -> i8 {
    %c0 = arith.constant 0 : i64
    %ok = arith.constant 0 : i8
    %cap = dsdl.load_scalar %sz : !dsdl.ptr<!dsdl.size> -> i64
    %raw = dsdl.read_bits %buf[%c0], size %cap {width = 8 : i64} : !dsdl.ptr<const !dsdl.byte> -> i64
    %v = func.call @llvmdsdl_plan_scalar_unsigned__vendor_Widget_1_0__0__deser(%raw) : (i64) -> i64
    dsdl.store_member %obj "foo", %v : !dsdl.ptr<!dsdl.object<"vendor.Widget.1.0">>, i64
    %back = dsdl.load_member %obj "foo" : !dsdl.ptr<!dsdl.object<"vendor.Widget.1.0">> -> i64
    %tr = arith.trunci %back : i64 to i8
    %keep = arith.addi %ok, %tr : i8
    return %keep : i8
  }
}
