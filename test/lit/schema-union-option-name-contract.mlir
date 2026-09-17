// The two halves of an option are paired by position, so the names are what confirm they describe
// the same options in the same order.

// RUN: not %dsdl-opt --pass-pipeline='builtin.module(lower-dsdl-serialization)' %s 2>&1 | FileCheck %s

module {
  dsdl.schema @test_OptionNameContract_1_0 attributes {full_name = "test.OptionNameContract", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.field {c_name = "first", name = "first", type_name = "truncated uint8", union_option_index = 0 : i64}
    dsdl.field {c_name = "second", name = "second", type_name = "truncated uint8", union_option_index = 1 : i64}
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__OptionNameContract__deserialize_", c_serialize_symbol = "test__OptionNameContract__serialize_", c_type_name = "test__OptionNameContract", is_union, max_bits = 16 : i64, min_bits = 16 : i64, union_option_count = 2 : i64, union_tag_bits = 8 : i64} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "first", cast_mode = "truncated", kind = "field", max_bits = 8 : i64, min_bits = 8 : i64, name = "first", scalar_category = "unsigned", type_name = "truncated uint8", union_option_index = 0 : i64, union_tag_bits = 8 : i64}
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "elsewhere", cast_mode = "truncated", kind = "field", max_bits = 8 : i64, min_bits = 8 : i64, name = "elsewhere", scalar_category = "unsigned", type_name = "truncated uint8", union_option_index = 1 : i64, union_tag_bits = 8 : i64}
    }
  }
}

// CHECK: error: 'dsdl.schema' op option 1 is 'second' in schema space and 'elsewhere' in its plan step
