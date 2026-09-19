// A host image over a union. A union's steps are its options, one of which the wire carries, so
// walking them as a run of fields answers an extent no structure has.
//
// RUN: not %dsdl-opt --pass-pipeline='builtin.module(dsdl-verify-alias-layout)' %s 2>&1 | FileCheck %s

module {
  // A sealed union of two equal byte-wide options: its wire is a tag plus one option, so it is
  // neither wire-flat nor a host image, and analysis marks it union-type.
  dsdl.schema @test_Pick_1_0 attributes {full_name = "test.Pick", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Pick__deserialize_", c_serialize_symbol = "test__Pick__serialize_", c_type_name = "test__Pick", fixed_size, is_union, max_bits = 24 : i64, min_bits = 24 : i64, sealed, union_option_count = 2 : i64, union_tag_bits = 8 : i64} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 16 : i64, c_name = "a", cast_mode = "saturated", kind = "field", max_bits = 16 : i64, min_bits = 16 : i64, name = "a", scalar_category = "unsigned", type_name = "saturated uint16", union_option_index = 0 : i64, union_tag_bits = 8 : i64}
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 16 : i64, c_name = "b", cast_mode = "saturated", kind = "field", max_bits = 16 : i64, min_bits = 16 : i64, name = "b", scalar_category = "unsigned", type_name = "saturated uint16", union_option_index = 1 : i64, union_tag_bits = 8 : i64}
    }
  }
  // Claims both properties over it.
  dsdl.schema @test_Holder_1_0 attributes {full_name = "test.Holder", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Holder__deserialize_", c_serialize_symbol = "test__Holder__serialize_", c_type_name = "test__Holder", fixed_size, host_image, max_bits = 32 : i64, min_bits = 32 : i64, sealed, wire_flat} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 0 : i64, c_name = "pick", cast_mode = "saturated", composite_c_type_name = "test__Pick", composite_extent_bits = 32 : i64, composite_full_name = "test.Pick", composite_major = 1 : i64, composite_minor = 0 : i64, composite_sealed = true, kind = "field", max_bits = 32 : i64, min_bits = 32 : i64, name = "pick", scalar_category = "composite", type_name = "test.Pick.1.0", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: error: host_image holds but this field's type is a union
