// The validator tests the tag against each option in turn. The chain begins at the first option
// rather than at a false constant, so nothing spells `false || tag == 0` in front of every union.

// RUN: %dsdl-opt --pass-pipeline='builtin.module(lower-dsdl-serialization)' %s | FileCheck %s

module {
  dsdl.schema @test_Pick_1_0 attributes {full_name = "test.Pick", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Pick__deserialize_", c_serialize_symbol = "test__Pick__serialize_", c_type_name = "test__Pick", is_union, max_bits = 16 : i64, min_bits = 8 : i64, union_option_count = 2 : i64, union_tag_bits = 8 : i64} {
      dsdl.align {bits = 8 : i32}
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "opt_a", cast_mode = "saturated", kind = "field", max_bits = 8 : i64, min_bits = 0 : i64, name = "opt_a", scalar_category = "unsigned", type_name = "saturated uint8", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "opt_b", cast_mode = "saturated", kind = "field", max_bits = 8 : i64, min_bits = 0 : i64, name = "opt_b", scalar_category = "unsigned", type_name = "saturated uint8", union_option_index = 1 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: func.func @llvmdsdl_plan_validate_union_tag__test_Pick_1_0(%[[TAG:[^:]+]]: i64) -> i8
// CHECK-NOT: arith.constant false
// CHECK: %[[A:[^ ]+]] = arith.constant 0 : i64
// CHECK: %[[EQA:[^ ]+]] = arith.cmpi eq, %[[TAG]], %[[A]] : i64
// CHECK: %[[B:[^ ]+]] = arith.constant 1 : i64
// CHECK: %[[EQB:[^ ]+]] = arith.cmpi eq, %[[TAG]], %[[B]] : i64
// CHECK: %[[ANY:[^ ]+]] = arith.ori %[[EQA]], %[[EQB]] : i1
// CHECK: scf.if %[[ANY]] -> (i8)
