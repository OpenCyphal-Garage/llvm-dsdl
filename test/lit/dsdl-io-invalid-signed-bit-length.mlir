// RUN: not %dsdl-opt --pass-pipeline='builtin.module(lower-dsdl-serialization)' %s 2>&1 | FileCheck %s

// The Cyphal Specification restricts signed integers to a bit length of [2, 64]
// (a one-bit signed integer is not a valid type; the single-bit case is `bool`).
// The dsdl.io verifier enforces this as defense-in-depth behind the frontend, so
// a 1-bit signed scalar must be rejected before it can reach codegen.

module {
  dsdl.schema @test_BadSignedWidth_1_0 attributes {full_name = "test.BadSignedWidth", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__BadSignedWidth__deserialize_", c_serialize_symbol = "test__BadSignedWidth__serialize_", c_type_name = "test__BadSignedWidth", max_bits = 1 : i64, min_bits = 1 : i64} {
      dsdl.io {alignment_bits = 1 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 1 : i64, c_name = "value", cast_mode = "saturated", kind = "field", max_bits = 1 : i64, min_bits = 1 : i64, name = "value", scalar_category = "signed", type_name = "saturated int1", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: error: 'dsdl.io' op invalid scalar bit_length metadata; signed integer widths must be in [2, 64]
