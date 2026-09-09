// RUN: %dsdl-opt --convert-dsdl-to-emitc %s | FileCheck %s

// Array access. A variable-length array is a member holding a count and element storage, so
// the C path reaches both by a path rather than a single name; the body names only the field,
// and how the storage is laid out is the schema's and the target's to know.

// CHECK-LABEL: func.func @vendor_Msg_1_0__serialize_ir_

module attributes {llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64} {
  dsdl.schema @vendor_Msg_1_0 attributes {c_type_name = "vendor__Msg", extent_bits = 112 : i64, full_name = "vendor.Msg", header_path = "vendor/Msg_1_0.h", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.field {c_name = "path", name = "path", type_name = "saturated uint8[<=4]"}
    dsdl.field {c_name = "value", name = "value", type_name = "saturated int16[<=4]"}
    dsdl.serialization_plan attributes {c_deserialize_symbol = "vendor__Msg__deserialize_", c_serialize_symbol = "vendor__Msg__serialize_", c_type_name = "vendor__Msg", extent_bits = 112 : i64, llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64, lowered, lowered_align_count = 0 : i64, lowered_capacity_check_helper = "llvmdsdl_plan_capacity_check__vendor_Msg_1_0", lowered_field_count = 2 : i64, lowered_max_bits = 112 : i64, lowered_min_bits = 16 : i64, lowered_padding_count = 0 : i64, lowered_step_count = 2 : i64, max_bits = 112 : i64, min_bits = 16 : i64, sealed} {
      dsdl.io {alignment_bits = 1 : i64, array_capacity = 4 : i64, array_kind = "variable_inclusive", array_length_prefix_bits = 8 : i64, bit_length = 8 : i64, c_name = "path", cast_mode = "saturated", kind = "field", lowered_array_length_validate_helper = "llvmdsdl_plan_validate_array_length__vendor_Msg_1_0__0", lowered_bits = 40 : i64, lowered_deser_array_length_prefix_helper = "llvmdsdl_plan_array_length_prefix__vendor_Msg_1_0__0__deser", lowered_deser_unsigned_helper = "llvmdsdl_plan_scalar_unsigned__vendor_Msg_1_0__0__deser", lowered_ser_array_length_prefix_helper = "llvmdsdl_plan_array_length_prefix__vendor_Msg_1_0__0__ser", lowered_ser_unsigned_helper = "llvmdsdl_plan_scalar_unsigned__vendor_Msg_1_0__0__ser", max_bits = 40 : i64, min_bits = 8 : i64, name = "path", scalar_category = "unsigned", step_index = 0 : i64, type_name = "saturated uint8[<=4]", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
      dsdl.io {alignment_bits = 1 : i64, array_capacity = 4 : i64, array_kind = "variable_inclusive", array_length_prefix_bits = 8 : i64, bit_length = 16 : i64, c_name = "value", cast_mode = "saturated", kind = "field", lowered_array_length_validate_helper = "llvmdsdl_plan_validate_array_length__vendor_Msg_1_0__1", lowered_bits = 72 : i64, lowered_deser_array_length_prefix_helper = "llvmdsdl_plan_array_length_prefix__vendor_Msg_1_0__1__deser", lowered_deser_signed_helper = "llvmdsdl_plan_scalar_signed__vendor_Msg_1_0__1__deser", lowered_ser_array_length_prefix_helper = "llvmdsdl_plan_array_length_prefix__vendor_Msg_1_0__1__ser", lowered_ser_signed_helper = "llvmdsdl_plan_scalar_signed__vendor_Msg_1_0__1__ser", max_bits = 72 : i64, min_bits = 8 : i64, name = "value", scalar_category = "signed", step_index = 1 : i64, type_name = "saturated int16[<=4]", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
  func.func private @llvmdsdl_plan_capacity_check__vendor_Msg_1_0(i64) -> i8
  func.func private @llvmdsdl_plan_scalar_unsigned__vendor_Msg_1_0__0__ser(i64) -> i64
  func.func private @llvmdsdl_plan_scalar_unsigned__vendor_Msg_1_0__0__deser(i64) -> i64
  func.func private @llvmdsdl_plan_scalar_signed__vendor_Msg_1_0__1__ser(i64) -> i64
  func.func private @llvmdsdl_plan_scalar_signed__vendor_Msg_1_0__1__deser(i64) -> i64
  func.func private @llvmdsdl_plan_validate_array_length__vendor_Msg_1_0__0(i64) -> i8
  func.func private @llvmdsdl_plan_validate_array_length__vendor_Msg_1_0__1(i64) -> i8
  func.func private @llvmdsdl_plan_array_length_prefix__vendor_Msg_1_0__0__ser(i64) -> i64
  func.func private @llvmdsdl_plan_array_length_prefix__vendor_Msg_1_0__0__deser(i64) -> i64
  func.func private @llvmdsdl_plan_array_length_prefix__vendor_Msg_1_0__1__ser(i64) -> i64
  func.func private @llvmdsdl_plan_array_length_prefix__vendor_Msg_1_0__1__deser(i64) -> i64

  func.func @vendor_Msg_1_0__serialize_ir_(
      %obj: !dsdl.ptr<const !dsdl.object<"vendor.Msg.1.0">>,
      %buf: !dsdl.ptr<!dsdl.byte>,
      %sz:  !dsdl.ptr<!dsdl.size>) -> i8 {
    %i = arith.constant 0 : i64
    // Two hops: the first leaves a pointer, the rest are within a value.
    // CHECK: "emitc.member_of_ptr"({{.*}}) <{member = "path"}>
    // CHECK: "emitc.member"({{.*}}) <{member = "count"}>
    // CHECK: emitc.load
    %n = dsdl.array_length %obj "path" : !dsdl.ptr<const !dsdl.object<"vendor.Msg.1.0">>

    // The storage is reached at the qualification it is declared with -- a serialiser holds the
    // object by pointer-to-const -- and then taken unqualified, because the element read out of
    // it is assigned to a variable that a const declaration would not survive.
    // CHECK: "emitc.member"({{.*}}) <{member = "elements"}>
    // CHECK: emitc.cast %{{.*}} : !emitc.ptr<!emitc.opaque<"const uint8_t">> to !emitc.ptr<!emitc.opaque<"uint8_t">>
    // CHECK: emitc.subscript
    // The element is addressed at its own width, not the width the plan works in: striding an
    // array of uint8_t by 64 bits would read the wrong bytes entirely.
    // CHECK: emitc.load %{{.*}} : <!emitc.opaque<"uint8_t">>
    // CHECK: emitc.cast %{{.*}} : !emitc.opaque<"uint8_t"> to i64
    %v = dsdl.load_element %obj "path"[%i] {storage_bits = 8 : i64, storage_category = "unsigned"} : !dsdl.ptr<const !dsdl.object<"vendor.Msg.1.0">> -> i64

    // A wider element keeps its own spelling, which is what the stride depends on.
    // CHECK: emitc.load %{{.*}} : <!emitc.opaque<"int16_t">>
    %w = dsdl.load_element %obj "value"[%i] {storage_bits = 16 : i64, storage_category = "signed"} : !dsdl.ptr<const !dsdl.object<"vendor.Msg.1.0">> -> i64

    %s = arith.addi %n, %v : i64
    %t = arith.addi %s, %w : i64
    %tr = arith.trunci %t : i64 to i8
    return %tr : i8
  }

  // CHECK-LABEL: func.func @vendor_Msg_1_0__deserialize_ir_
  func.func @vendor_Msg_1_0__deserialize_ir_(
      %obj: !dsdl.ptr<!dsdl.object<"vendor.Msg.1.0">>,
      %buf: !dsdl.ptr<const !dsdl.byte>,
      %sz:  !dsdl.ptr<!dsdl.size>) -> i8 {
    %i = arith.constant 0 : i64
    %v = arith.constant 7 : i64
    %ok = arith.constant 0 : i8
    // CHECK: emitc.subscript
    // CHECK: emitc.cast %{{.*}} : i64 to !emitc.opaque<"uint8_t">
    // CHECK: emitc.assign
    dsdl.store_element %obj "path"[%i], %v {storage_bits = 8 : i64, storage_category = "unsigned"} : !dsdl.ptr<!dsdl.object<"vendor.Msg.1.0">>, i64
    // The count is the member's own, written as the value it was read as.
    // CHECK: "emitc.member"({{.*}}) <{member = "count"}>
    // CHECK: emitc.assign
    dsdl.set_array_length %obj "path", %v : !dsdl.ptr<!dsdl.object<"vendor.Msg.1.0">>
    return %ok : i8
  }
}
