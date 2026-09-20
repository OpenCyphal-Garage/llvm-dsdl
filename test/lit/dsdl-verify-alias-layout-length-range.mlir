// A fixed plan whose declared length is a range. A fixed length is one number, and what reads
// the plan is free to take either bound -- the fold takes the upper one.
//
// RUN: not %dsdl-opt --pass-pipeline='builtin.module(dsdl-verify-alias-layout)' %s 2>&1 | FileCheck %s

module {
  // One byte of steps, but the plan declares two. The fold copies the declared length.
  dsdl.schema @test_Bounds_1_0 attributes {full_name = "test.Bounds", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Bounds__deserialize_", c_serialize_symbol = "test__Bounds__serialize_", c_type_name = "test__Bounds", fixed_size, host_image, max_bits = 16 : i64, min_bits = 8 : i64, sealed, wire_flat} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "only", cast_mode = "saturated", kind = "field", max_bits = 8 : i64, min_bits = 8 : i64, name = "only", scalar_category = "unsigned", type_name = "saturated uint8", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: error: wire_flat holds but the plan's declared length is a range
