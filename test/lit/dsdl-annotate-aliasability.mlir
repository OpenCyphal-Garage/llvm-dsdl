// One schema per verdict the annotator can reach. A reason names the field property that
// blocked the layout, so a schema here changes its reason only when that property changes.
//
// RUN: %dsdl-opt --pass-pipeline='builtin.module(dsdl-annotate-aliasability)' %s | FileCheck %s

module {
  dsdl.schema @test_Aliasable_1_0 attributes {full_name = "test.Aliasable", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Aliasable__deserialize_", c_serialize_symbol = "test__Aliasable__serialize_", c_type_name = "test__Aliasable", fixed_size, max_bits = 16 : i64, min_bits = 16 : i64, sealed} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 16 : i64, c_name = "value", cast_mode = "saturated", kind = "field", max_bits = 16 : i64, min_bits = 16 : i64, name = "value", scalar_category = "unsigned", type_name = "saturated uint16", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
  // A fixed array advances the cursor by its whole length, not by one element, so the field
  // after it is measured at the offset an encoder would put it at.
  dsdl.schema @test_FixedArray_1_0 attributes {full_name = "test.FixedArray", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__FixedArray__deserialize_", c_serialize_symbol = "test__FixedArray__serialize_", c_type_name = "test__FixedArray", fixed_size, max_bits = 32 : i64, min_bits = 32 : i64, sealed} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 3 : i64, array_kind = "fixed", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "head", cast_mode = "saturated", kind = "field", max_bits = 24 : i64, min_bits = 24 : i64, name = "head", scalar_category = "unsigned", type_name = "saturated uint8[3]", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "tail", cast_mode = "saturated", kind = "field", max_bits = 8 : i64, min_bits = 8 : i64, name = "tail", scalar_category = "unsigned", type_name = "saturated uint8", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
  // A composite carries its width in min_bits/max_bits and leaves bit_length zero, so it is
  // answered before the width tests read that zero.
  dsdl.schema @test_Composite_1_0 attributes {full_name = "test.Composite", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Composite__deserialize_", c_serialize_symbol = "test__Composite__serialize_", c_type_name = "test__Composite", fixed_size, max_bits = 16 : i64, min_bits = 16 : i64, sealed} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 0 : i64, c_name = "nested", cast_mode = "saturated", composite_c_type_name = "test__Aliasable", composite_extent_bits = 16 : i64, composite_full_name = "test.Aliasable", composite_major = 1 : i64, composite_minor = 0 : i64, composite_sealed = true, kind = "field", max_bits = 16 : i64, min_bits = 16 : i64, name = "nested", scalar_category = "composite", type_name = "test.Aliasable.1.0", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
  // A union's steps are its options; the field walk never runs.
  dsdl.schema @test_Union_1_0 attributes {full_name = "test.Union", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.field {c_name = "a", name = "a", type_name = "saturated uint16", union_option_index = 0 : i64}
    dsdl.field {c_name = "b", name = "b", type_name = "saturated uint16", union_option_index = 1 : i64}
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Union__deserialize_", c_serialize_symbol = "test__Union__serialize_", c_type_name = "test__Union", fixed_size, is_union, max_bits = 24 : i64, min_bits = 24 : i64, sealed, union_option_count = 2 : i64, union_tag_bits = 8 : i64} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 16 : i64, c_name = "a", cast_mode = "saturated", kind = "field", max_bits = 16 : i64, min_bits = 16 : i64, name = "a", scalar_category = "unsigned", type_name = "saturated uint16", union_option_index = 0 : i64, union_tag_bits = 8 : i64}
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 16 : i64, c_name = "b", cast_mode = "saturated", kind = "field", max_bits = 16 : i64, min_bits = 16 : i64, name = "b", scalar_category = "unsigned", type_name = "saturated uint16", union_option_index = 1 : i64, union_tag_bits = 8 : i64}
    }
  }
  // Refused for its length; widening the element cannot fix that.
  dsdl.schema @test_VariableArray_1_0 attributes {full_name = "test.VariableArray", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__VariableArray__deserialize_", c_serialize_symbol = "test__VariableArray__serialize_", c_type_name = "test__VariableArray", max_bits = 16 : i64, min_bits = 8 : i64, sealed} {
      dsdl.io {alignment_bits = 1 : i64, array_capacity = 7 : i64, array_kind = "variable_inclusive", array_length_prefix_bits = 8 : i64, bit_length = 1 : i64, c_name = "bits", cast_mode = "saturated", kind = "field", max_bits = 15 : i64, min_bits = 8 : i64, name = "bits", scalar_category = "unsigned", type_name = "saturated uint1[<=7]", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
  dsdl.schema @test_SubByte_1_0 attributes {full_name = "test.SubByte", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__SubByte__deserialize_", c_serialize_symbol = "test__SubByte__serialize_", c_type_name = "test__SubByte", fixed_size, max_bits = 2 : i64, min_bits = 2 : i64, sealed} {
      dsdl.io {alignment_bits = 1 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 2 : i64, c_name = "health", cast_mode = "saturated", kind = "field", max_bits = 2 : i64, min_bits = 2 : i64, name = "health", scalar_category = "unsigned", type_name = "saturated uint2", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
  // Sub-byte padding leaves the field after it off a byte boundary.
  dsdl.schema @test_Unaligned_1_0 attributes {full_name = "test.Unaligned", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Unaligned__deserialize_", c_serialize_symbol = "test__Unaligned__serialize_", c_type_name = "test__Unaligned", fixed_size, max_bits = 12 : i64, min_bits = 12 : i64, sealed} {
      dsdl.io {alignment_bits = 1 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 4 : i64, c_name = "", cast_mode = "saturated", kind = "padding", max_bits = 4 : i64, min_bits = 4 : i64, name = "", scalar_category = "void", type_name = "void4", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
      dsdl.io {alignment_bits = 1 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "value", cast_mode = "saturated", kind = "field", max_bits = 8 : i64, min_bits = 8 : i64, name = "value", scalar_category = "unsigned", type_name = "saturated uint8", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
  dsdl.schema @test_Delimited_1_0 attributes {extent_bits = 64 : i64, full_name = "test.Delimited", major = 1 : i32, minor = 0 : i32} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Delimited__deserialize_", c_serialize_symbol = "test__Delimited__serialize_", c_type_name = "test__Delimited", extent_bits = 64 : i64, fixed_size, max_bits = 16 : i64, min_bits = 16 : i64} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 16 : i64, c_name = "value", cast_mode = "saturated", kind = "field", max_bits = 16 : i64, min_bits = 16 : i64, name = "value", scalar_category = "unsigned", type_name = "saturated uint16", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
  dsdl.schema @test_Empty_1_0 attributes {full_name = "test.Empty", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Empty__deserialize_", c_serialize_symbol = "test__Empty__serialize_", c_type_name = "test__Empty", fixed_size, max_bits = 0 : i64, min_bits = 0 : i64, sealed} {
      dsdl.align {bits = 1 : i32}
    }
  }
}

// CHECK: dsdl.schema @test_Aliasable_1_0
// CHECK: dsdl.serialization_plan attributes {
// CHECK-SAME: zoh_alias_eligible
// CHECK-NOT: zoh_alias_reason

// CHECK: dsdl.schema @test_FixedArray_1_0
// CHECK: dsdl.serialization_plan attributes {
// CHECK-SAME: zoh_alias_eligible
// CHECK-NOT: zoh_alias_reason

// CHECK: dsdl.schema @test_Composite_1_0
// CHECK: dsdl.serialization_plan attributes {
// CHECK-SAME: zoh_alias_reason = "composite-field"
// CHECK-NOT: zoh_alias_eligible

// CHECK: dsdl.schema @test_Union_1_0
// CHECK: dsdl.serialization_plan attributes {
// CHECK-SAME: zoh_alias_reason = "union-type"
// CHECK-NOT: zoh_alias_eligible

// CHECK: dsdl.schema @test_VariableArray_1_0
// CHECK: dsdl.serialization_plan attributes {
// CHECK-SAME: zoh_alias_reason = "variable-array"
// CHECK-NOT: zoh_alias_eligible

// CHECK: dsdl.schema @test_SubByte_1_0
// CHECK: dsdl.serialization_plan attributes {
// CHECK-SAME: zoh_alias_reason = "sub-byte-field"
// CHECK-NOT: zoh_alias_eligible

// CHECK: dsdl.schema @test_Unaligned_1_0
// CHECK: dsdl.serialization_plan attributes {
// CHECK-SAME: zoh_alias_reason = "unaligned-field"
// CHECK-NOT: zoh_alias_eligible

// CHECK: dsdl.schema @test_Delimited_1_0
// CHECK: dsdl.serialization_plan attributes {
// CHECK-SAME: zoh_alias_reason = "not-sealed"
// CHECK-NOT: zoh_alias_eligible

// CHECK: dsdl.schema @test_Empty_1_0
// CHECK: dsdl.serialization_plan attributes {
// CHECK-SAME: zoh_alias_reason = "empty-layout"
// CHECK-NOT: zoh_alias_eligible
