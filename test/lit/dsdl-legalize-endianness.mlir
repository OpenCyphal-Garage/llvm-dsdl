// RUN: %dsdl-opt --pass-pipeline='builtin.module(dsdl-legalize-endianness)' %s | FileCheck %s

module attributes {llvmdsdl.target_endianness = "big"} {
  dsdl.schema @test_Widget_1_0 attributes {full_name = "test.Widget", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Widget__deserialize_", c_serialize_symbol = "test__Widget__serialize_", c_type_name = "test__Widget", max_bits = 8 : i64, min_bits = 8 : i64} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "value", cast_mode = "saturated", kind = "field", max_bits = 8 : i64, min_bits = 8 : i64, name = "value", scalar_category = "unsigned", type_name = "saturated uint8", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: module attributes {
// CHECK-SAME: llvmdsdl.target_endianness = "big"
// CHECK-SAME: llvmdsdl.target_endianness_legalized
