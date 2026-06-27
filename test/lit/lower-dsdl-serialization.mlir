// RUN: %dsdl-opt --pass-pipeline='builtin.module(lower-dsdl-serialization)' %s | FileCheck %s

module {
  dsdl.schema @test_Widget_1_0 attributes {full_name = "test.Widget", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Widget__deserialize_", c_serialize_symbol = "test__Widget__serialize_", c_type_name = "test__Widget", is_union, max_bits = 16 : i64, min_bits = 8 : i64, union_option_count = 1 : i64, union_tag_bits = 8 : i64} {
      dsdl.align {bits = 1 : i32}
      dsdl.align {bits = 8 : i32}
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "opt_a", cast_mode = "saturated", kind = "field", max_bits = 8 : i64, min_bits = 0 : i64, name = "opt_a", scalar_category = "unsigned", type_name = "saturated uint8", union_option_index = 3 : i64, union_tag_bits = 0 : i64}
      dsdl.io {alignment_bits = 1 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 0 : i64, c_name = "_pad", cast_mode = "saturated", kind = "padding", max_bits = 0 : i64, min_bits = 0 : i64, name = "_pad", scalar_category = "void", type_name = "void0", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: module attributes {llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64}
// CHECK: dsdl.serialization_plan attributes {
// CHECK-DAG: lowered,
// CHECK-DAG: llvmdsdl.lowered_contract_producer = "lower-dsdl-exec"
// CHECK-DAG: llvmdsdl.lowered_contract_version = 2 : i64
// CHECK-DAG: lowered_align_count = 1 : i64
// CHECK-DAG: lowered_capacity_check_helper = "__llvmdsdl_plan_capacity_check__test_Widget_1_0"
// CHECK-DAG: lowered_field_count = 1 : i64
// CHECK-DAG: lowered_max_bits = 16 : i64
// CHECK-DAG: lowered_min_bits = 8 : i64
// CHECK-DAG: lowered_padding_count = 0 : i64
// CHECK-DAG: lowered_deser_union_tag_helper = "__llvmdsdl_plan_union_tag__test_Widget_1_0__deser"
// CHECK-DAG: lowered_ser_union_tag_helper = "__llvmdsdl_plan_union_tag__test_Widget_1_0__ser"
// CHECK-DAG: lowered_union_tag_validate_helper = "__llvmdsdl_plan_validate_union_tag__test_Widget_1_0"
// CHECK-DAG: lowered_step_count = 2 : i64
// CHECK-DAG: union_option_count = 1 : i64
// CHECK-DAG: union_tag_bits = 8 : i64
// CHECK: dsdl.align {bits = 8 : i32, step_index = 0 : i64}
// CHECK: dsdl.io
// CHECK-SAME: lowered_bits = 8 : i64
// CHECK-SAME: lowered_deser_unsigned_helper = "__llvmdsdl_plan_scalar_unsigned__test_Widget_1_0__1__deser"
// CHECK-SAME: lowered_ser_unsigned_helper = "__llvmdsdl_plan_scalar_unsigned__test_Widget_1_0__1__ser"
// CHECK-SAME: min_bits = 0 : i64
// CHECK-SAME: step_index = 1 : i64
// CHECK-NOT: dsdl.align {bits = 1 : i32
// CHECK-NOT: kind = "padding"
// CHECK: func.func @__llvmdsdl_plan_capacity_check__test_Widget_1_0(%[[CAP:[^:]+]]: i64) -> i8 attributes {llvmdsdl.plan_capacity_check
// CHECK: %[[REQ:[^ ]+]] = arith.constant 16 : i64
// CHECK: %[[CMP:[^ ]+]] = arith.cmpi ugt, %[[REQ]], %[[CAP]] : i64
// CHECK: %[[SEL:[^ ]+]] = scf.if %[[CMP]] -> (i8)
// CHECK: return %[[SEL]] : i8
// CHECK: func.func @__llvmdsdl_plan_validate_union_tag__test_Widget_1_0(%[[TAG:[^:]+]]: i64) -> i8 attributes {llvmdsdl.plan_origin = "lower-dsdl-exec", llvmdsdl.schema_sym = "test_Widget_1_0", llvmdsdl.union_tag_validate
// CHECK: %[[OPT:[^ ]+]] = arith.constant 3 : i64
// CHECK: %[[EQ:[^ ]+]] = arith.cmpi eq, %[[TAG]], %[[OPT]] : i64
// CHECK: %[[MASK:[^ ]+]] = arith.ori %[[ANY:[^ ]+]], %[[EQ]] : i1
// CHECK: %[[TAGSEL:[^ ]+]] = scf.if %[[MASK]] -> (i8)
// CHECK: return %[[TAGSEL]] : i8
// CHECK: func.func @__llvmdsdl_plan_scalar_unsigned__test_Widget_1_0__1__ser(%[[VAL:[^:]+]]: i64) -> i64 attributes {llvmdsdl.scalar_unsigned_helper
// CHECK: %[[CM:[^ ]+]] = arith.cmpi ugt, %[[VAL]], %[[MASK63:[^ ]+]] : i64
// CHECK: %[[SV:[^ ]+]] = arith.select %[[CM]], %[[MASK63]], %[[VAL]] : i64
// CHECK: return %[[SV]] : i64
// CHECK: func.func @__llvmdsdl_plan_scalar_unsigned__test_Widget_1_0__1__deser(%[[DVAL:[^:]+]]: i64) -> i64 attributes {llvmdsdl.scalar_unsigned_helper
// CHECK: %[[DM:[^ ]+]] = arith.andi %[[DVAL]], %[[DMASK:[^ ]+]] : i64
// CHECK: return %[[DM]] : i64
// CHECK: func.func @__llvmdsdl_plan_union_tag__test_Widget_1_0__ser(%[[TV:[^:]+]]: i64) -> i64 attributes {
// CHECK-SAME: llvmdsdl.union_tag_helper
// CHECK: %[[TM:[^ ]+]] = arith.andi %[[TV]], %[[TMSK:[^ ]+]] : i64
// CHECK: return %[[TM]] : i64
// CHECK: func.func @__llvmdsdl_plan_union_tag__test_Widget_1_0__deser(%[[TDV:[^:]+]]: i64) -> i64 attributes {
// CHECK-SAME: llvmdsdl.union_tag_helper
// CHECK: %[[TDM:[^ ]+]] = arith.andi %[[TDV]], %[[TDMSK:[^ ]+]] : i64
// CHECK: return %[[TDM]] : i64
