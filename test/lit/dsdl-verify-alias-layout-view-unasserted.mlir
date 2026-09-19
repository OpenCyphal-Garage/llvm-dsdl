// A step held as a view names a type that does not assert `@aliasable`. The lowering marks a view
// only where the nested type asserts, so the mark and the assertion have drifted apart.
//
// RUN: not %dsdl-opt --pass-pipeline='builtin.module(dsdl-verify-alias-layout)' %s 2>&1 | FileCheck %s

module {
  dsdl.schema @test_Inner_1_0 attributes {full_name = "test.Inner", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Inner__deserialize_", c_serialize_symbol = "test__Inner__serialize_", c_type_name = "test__Inner", fixed_size, max_bits = 16 : i64, min_bits = 16 : i64, sealed, wire_flat, host_image} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 16 : i64, c_name = "value", cast_mode = "saturated", kind = "field", max_bits = 16 : i64, min_bits = 16 : i64, name = "value", scalar_category = "unsigned", type_name = "saturated uint16", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
  dsdl.schema @test_Holder_1_0 attributes {full_name = "test.Holder", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Holder__deserialize_", c_serialize_symbol = "test__Holder__serialize_", c_type_name = "test__Holder", fixed_size, max_bits = 16 : i64, min_bits = 16 : i64, sealed, wire_flat, host_image_reason = "view-member"} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 0 : i64, c_name = "inner", cast_mode = "saturated", composite_c_type_name = "test__Inner", composite_extent_bits = 16 : i64, composite_full_name = "test.Inner", composite_major = 1 : i64, composite_minor = 0 : i64, composite_sealed = true, held_as_view, kind = "field", max_bits = 16 : i64, min_bits = 16 : i64, name = "inner", scalar_category = "composite", type_name = "test.Inner.1.0", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: error: held_as_view on a composite whose type does not assert aliasable
