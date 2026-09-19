// A plan stating a length its steps do not add up to: one byte of field, two declared. What
// reads the plan takes the declared length at its word, so the fold would move a byte the object
// does not hold.
//
// RUN: not %dsdl-opt --pass-pipeline='builtin.module(dsdl-verify-alias-layout)' %s 2>&1 | FileCheck %s

module {
  // One byte of steps, but the plan declares two. The fold copies the declared length.
  dsdl.schema @test_Wide_1_0 attributes {full_name = "test.Wide", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Wide__deserialize_", c_serialize_symbol = "test__Wide__serialize_", c_type_name = "test__Wide", fixed_size, host_image, max_bits = 16 : i64, min_bits = 16 : i64, sealed, wire_flat} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "only", cast_mode = "saturated", kind = "field", max_bits = 8 : i64, min_bits = 8 : i64, name = "only", scalar_category = "unsigned", type_name = "saturated uint8", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: error: wire_flat holds but the steps do not add up to the plan's length
