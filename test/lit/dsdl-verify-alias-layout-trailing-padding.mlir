// A plan whose fields each begin on a byte boundary while the payload as a whole does not: a
// trailing `void1` leaves nine bits. Every field passing the boundary test says nothing about what
// follows the last one, so the total is tested too.
//
// RUN: not %dsdl-opt --pass-pipeline='builtin.module(dsdl-verify-alias-layout)' %s 2>&1 | FileCheck %s

module {
  dsdl.schema @test_Trail_1_0 attributes {full_name = "test.Trail", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Trail__deserialize_", c_serialize_symbol = "test__Trail__serialize_", c_type_name = "test__Trail", fixed_size, max_bits = 9 : i64, min_bits = 9 : i64, sealed, wire_flat} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "a", cast_mode = "saturated", kind = "field", max_bits = 8 : i64, min_bits = 8 : i64, name = "a", scalar_category = "unsigned", type_name = "saturated uint8", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
      dsdl.io {alignment_bits = 1 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 1 : i64, c_name = "", cast_mode = "saturated", kind = "padding", max_bits = 1 : i64, min_bits = 1 : i64, name = "", scalar_category = "void", type_name = "void1", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: error: wire_flat holds but the payload is not a whole number of bytes
