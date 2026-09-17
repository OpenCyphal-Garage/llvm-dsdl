// The options a section declares and the count its plan carries are the same number.

// RUN: not %dsdl-opt --pass-pipeline='builtin.module(lower-dsdl-serialization)' %s 2>&1 | FileCheck %s

module {
  dsdl.schema @test_OptionCountContract_1_0 attributes {full_name = "test.OptionCountContract", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.field {c_name = "only", name = "only", type_name = "truncated uint8", union_option_index = 0 : i64}
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__OptionCountContract__deserialize_", c_serialize_symbol = "test__OptionCountContract__serialize_", c_type_name = "test__OptionCountContract", is_union, max_bits = 16 : i64, min_bits = 16 : i64, union_option_count = 4 : i64, union_tag_bits = 8 : i64} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "only", cast_mode = "truncated", kind = "field", max_bits = 8 : i64, min_bits = 8 : i64, name = "only", scalar_category = "unsigned", type_name = "truncated uint8", union_option_index = 0 : i64, union_tag_bits = 8 : i64}
    }
  }
}

// CHECK: error: 'dsdl.schema' op 'union_option_count' is 4, but the section declares 1 options
