// RUN: %dsdl-opt --pass-pipeline='builtin.module(lower-dsdl-serialization,optimize-dsdl-lowered-serdes)' %s | FileCheck %s

module {
  dsdl.schema @test_Optimized_1_0 attributes {full_name = "test.Optimized", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Optimized__deserialize_", c_serialize_symbol = "test__Optimized__serialize_", c_type_name = "test__Optimized", max_bits = 8 : i64, min_bits = 8 : i64} {
      dsdl.align {bits = 1 : i32}
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "value", cast_mode = "truncated", kind = "field", max_bits = 8 : i64, min_bits = 8 : i64, name = "value", scalar_category = "unsigned", type_name = "truncated uint8", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: module attributes {llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64}
// CHECK: dsdl.serialization_plan attributes {
// CHECK-DAG: lowered
// CHECK-DAG: lowered_step_count = 1 : i64
// CHECK-DAG: lowered_capacity_check_helper = "llvmdsdl_plan_capacity_check__test_Optimized_1_0"
// CHECK-DAG: lowered_ser_unsigned_helper = "llvmdsdl_plan_scalar_unsigned__test_Optimized_1_0__0__ser"
// CHECK-DAG: lowered_deser_unsigned_helper = "llvmdsdl_plan_scalar_unsigned__test_Optimized_1_0__0__deser"
// CHECK-NOT: dsdl.align {bits = 1 : i32
// CHECK: func.func @llvmdsdl_plan_capacity_check__test_Optimized_1_0
