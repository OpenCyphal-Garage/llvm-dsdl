// RUN: not %dsdl-opt --pass-pipeline='builtin.module(lower-dsdl-serialization)' %s 2>&1 | FileCheck %s

module {
  dsdl.schema @test.BadArrayPrefixWidth.1.0 attributes {full_name = "test.BadArrayPrefixWidth", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__BadArrayPrefixWidth__deserialize_", c_serialize_symbol = "test__BadArrayPrefixWidth__serialize_", c_type_name = "test__BadArrayPrefixWidth", max_bits = 8 : i64, min_bits = 0 : i64} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 3 : i64, array_kind = "variable_exclusive", array_length_prefix_bits = 65 : i64, bit_length = 8 : i64, c_name = "values", cast_mode = "truncated", kind = "field", max_bits = 8 : i64, min_bits = 0 : i64, name = "values", scalar_category = "unsigned", type_name = "truncated uint8[<=3]", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: error: 'dsdl.io' op variable array field prefix width exceeds 64 bits
