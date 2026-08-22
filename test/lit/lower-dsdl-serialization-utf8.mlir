// RUN: %dsdl-opt --pass-pipeline='builtin.module(lower-dsdl-serialization)' %s | FileCheck %s

module {
  dsdl.schema @test_Utf8_1_0 attributes {full_name = "test.Utf8", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Utf8__deserialize_", c_serialize_symbol = "test__Utf8__serialize_", c_type_name = "test__Utf8", max_bits = 8 : i64, min_bits = 8 : i64} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "ch", cast_mode = "truncated", kind = "field", max_bits = 8 : i64, min_bits = 8 : i64, name = "ch", scalar_category = "utf8", type_name = "utf8", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK-DAG: lowered_ser_unsigned_helper = "llvmdsdl_plan_scalar_unsigned__test_Utf8_1_0__{{[0-9]+}}__ser"
// CHECK-DAG: lowered_deser_unsigned_helper = "llvmdsdl_plan_scalar_unsigned__test_Utf8_1_0__{{[0-9]+}}__deser"
// CHECK: func.func @llvmdsdl_plan_scalar_unsigned__test_Utf8_1_0__{{[0-9]+}}__ser(
// CHECK: func.func @llvmdsdl_plan_scalar_unsigned__test_Utf8_1_0__{{[0-9]+}}__deser(
