// RUN: not %dsdl-opt --pass-pipeline='builtin.module(lower-dsdl-serialization)' %s 2>&1 | FileCheck %s

module {
  dsdl.schema @test.VarPrefixContract.1.0 attributes {full_name = "test.VarPrefixContract", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__VarPrefixContract__deserialize_", c_serialize_symbol = "test__VarPrefixContract__serialize_", c_type_name = "test__VarPrefixContract", max_bits = 16 : i64, min_bits = 0 : i64} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 5 : i64, array_kind = "variable_inclusive", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "arr", cast_mode = "truncated", kind = "field", max_bits = 16 : i64, min_bits = 0 : i64, name = "arr", scalar_category = "unsigned", type_name = "truncated uint8[<=5]", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: error: 'dsdl.io' op variable array field requires positive prefix width
