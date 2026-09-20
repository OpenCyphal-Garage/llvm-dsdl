// A host image holding a record that itself holds a view. The outer plan's own steps hold no view,
// so the check on them passes; the walk that re-derives the nested extent is where the view is
// reached, and the nested structure holds a pointer and a size where the wire holds the record.
//
// RUN: not %dsdl-opt --pass-pipeline='builtin.module(dsdl-verify-alias-layout)' %s 2>&1 | FileCheck %s

module {
  dsdl.schema @test_Leaf_1_0 attributes {full_name = "test.Leaf", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {aliasable, c_deserialize_symbol = "test__Leaf__deserialize_", c_serialize_symbol = "test__Leaf__serialize_", c_type_name = "test__Leaf", fixed_size, host_image, max_bits = 16 : i64, min_bits = 16 : i64, sealed, wire_flat} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 16 : i64, c_name = "value", cast_mode = "saturated", kind = "field", max_bits = 16 : i64, min_bits = 16 : i64, name = "value", scalar_category = "unsigned", type_name = "saturated uint16", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
  dsdl.schema @test_Middle_1_0 attributes {full_name = "test.Middle", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Middle__deserialize_", c_serialize_symbol = "test__Middle__serialize_", c_type_name = "test__Middle", fixed_size, max_bits = 16 : i64, min_bits = 16 : i64, sealed, wire_flat} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 0 : i64, c_name = "leaf", cast_mode = "saturated", composite_c_type_name = "test__Leaf", composite_extent_bits = 16 : i64, composite_full_name = "test.Leaf", composite_major = 1 : i64, composite_minor = 0 : i64, composite_sealed = true, held_as_view, kind = "field", max_bits = 16 : i64, min_bits = 16 : i64, name = "leaf", scalar_category = "composite", type_name = "test.Leaf.1.0", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
  dsdl.schema @test_Outer_1_0 attributes {full_name = "test.Outer", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Outer__deserialize_", c_serialize_symbol = "test__Outer__serialize_", c_type_name = "test__Outer", fixed_size, host_image, max_bits = 16 : i64, min_bits = 16 : i64, sealed, wire_flat} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 0 : i64, c_name = "middle", cast_mode = "saturated", composite_c_type_name = "test__Middle", composite_extent_bits = 16 : i64, composite_full_name = "test.Middle", composite_major = 1 : i64, composite_minor = 0 : i64, composite_sealed = true, kind = "field", max_bits = 16 : i64, min_bits = 16 : i64, name = "middle", scalar_category = "composite", type_name = "test.Middle.1.0", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: error: host_image holds but this field is held as a view
