// RUN: %dsdl-opt --pass-pipeline='builtin.module(dsdl-annotate-aliasability)' %s | FileCheck %s

module {
  dsdl.schema @test_Aliasable_1_0 attributes {full_name = "test.Aliasable", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Aliasable__deserialize_", c_serialize_symbol = "test__Aliasable__serialize_", c_type_name = "test__Aliasable", fixed_size, max_bits = 16 : i64, min_bits = 16 : i64, sealed} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 16 : i64, c_name = "value", cast_mode = "saturated", kind = "field", max_bits = 16 : i64, min_bits = 16 : i64, name = "value", scalar_category = "unsigned", type_name = "saturated uint16", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
  dsdl.schema @test_NonAliasable_1_0 attributes {full_name = "test.NonAliasable", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__NonAliasable__deserialize_", c_serialize_symbol = "test__NonAliasable__serialize_", c_type_name = "test__NonAliasable", max_bits = 16 : i64, min_bits = 8 : i64, sealed} {
      dsdl.io {alignment_bits = 1 : i64, array_capacity = 7 : i64, array_kind = "variable_inclusive", array_length_prefix_bits = 8 : i64, bit_length = 1 : i64, c_name = "bits", cast_mode = "saturated", kind = "field", max_bits = 15 : i64, min_bits = 8 : i64, name = "bits", scalar_category = "unsigned", type_name = "saturated uint1[<=7]", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: dsdl.schema @test_Aliasable_1_0
// CHECK: dsdl.serialization_plan attributes {
// CHECK-SAME: zoh_alias_eligible
// CHECK-NOT: zoh_alias_reason
// CHECK: dsdl.schema @test_NonAliasable_1_0
// CHECK: dsdl.serialization_plan attributes {
// CHECK-SAME: zoh_alias_reason = "sub-byte-field"
// CHECK-NOT: zoh_alias_eligible
