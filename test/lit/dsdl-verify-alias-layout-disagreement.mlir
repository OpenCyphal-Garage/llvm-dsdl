// A plan that claims a verdict its own steps contradict. Analysis decides the verdicts and the
// lowering carries them, so a disagreement here means the two have drifted apart -- which is the
// failure this pass exists to catch, and the reason the answer is not computed twice.
//
// RUN: not %dsdl-opt --pass-pipeline='builtin.module(dsdl-verify-alias-layout)' %s 2>&1 | FileCheck %s

module {
  // A two-bit field cannot be part of a flat byte image.
  dsdl.schema @test_Lies_1_0 attributes {full_name = "test.Lies", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Lies__deserialize_", c_serialize_symbol = "test__Lies__serialize_", c_type_name = "test__Lies", fixed_size, max_bits = 2 : i64, min_bits = 2 : i64, sealed, wire_flat} {
      dsdl.io {alignment_bits = 1 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 2 : i64, c_name = "health", cast_mode = "saturated", kind = "field", max_bits = 2 : i64, min_bits = 2 : i64, name = "health", scalar_category = "unsigned", type_name = "saturated uint2", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: error: wire_flat holds but this field is not a whole number of bytes
