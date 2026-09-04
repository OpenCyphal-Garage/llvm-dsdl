// RUN: not %dsdl-opt --pass-pipeline='builtin.module(lower-dsdl-serialization)' %s 2>&1 | FileCheck %s

module {
  dsdl.schema @test_BadPlanBounds_1_0 attributes {full_name = "test.BadPlanBounds", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__BadPlanBounds__deserialize_", c_serialize_symbol = "test__BadPlanBounds__serialize_", c_type_name = "test__BadPlanBounds"} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "value", cast_mode = "truncated", kind = "field", max_bits = 8 : i64, min_bits = 8 : i64, name = "value", scalar_category = "unsigned", type_name = "truncated uint8", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: error: 'dsdl.serialization_plan' op requires attribute 'max_bits'
