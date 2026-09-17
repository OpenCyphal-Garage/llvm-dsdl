// An option's tag value is stamped on the field that names it and on the plan step that serialises
// it. The two are produced by one walk over one section, so a disagreement is a lowering bug; this
// is where it surfaces rather than in a backend that reads one half.

// RUN: not %dsdl-opt --pass-pipeline='builtin.module(lower-dsdl-serialization)' %s 2>&1 | FileCheck %s

module {
  dsdl.schema @test_OptionIndexContract_1_0 attributes {full_name = "test.OptionIndexContract", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.field {c_name = "first", name = "first", type_name = "truncated uint8", union_option_index = 0 : i64}
    dsdl.field {c_name = "second", name = "second", type_name = "truncated uint8", union_option_index = 1 : i64}
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__OptionIndexContract__deserialize_", c_serialize_symbol = "test__OptionIndexContract__serialize_", c_type_name = "test__OptionIndexContract", is_union, max_bits = 16 : i64, min_bits = 16 : i64, union_option_count = 2 : i64, union_tag_bits = 8 : i64} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "first", cast_mode = "truncated", kind = "field", max_bits = 8 : i64, min_bits = 8 : i64, name = "first", scalar_category = "unsigned", type_name = "truncated uint8", union_option_index = 0 : i64, union_tag_bits = 8 : i64}
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "second", cast_mode = "truncated", kind = "field", max_bits = 8 : i64, min_bits = 8 : i64, name = "second", scalar_category = "unsigned", type_name = "truncated uint8", union_option_index = 7 : i64, union_tag_bits = 8 : i64}
    }
  }
}

// CHECK: error: 'dsdl.schema' op option 'second' is index 1 in schema space and 7 in its plan step
