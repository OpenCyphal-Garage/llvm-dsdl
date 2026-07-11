// RUN: not %dsdl-opt --pass-pipeline='builtin.module(lower-dsdl-serialization)' %s 2>&1 | FileCheck %s

// The dsdl.io verifier enforces the DSDL v1.0 primitive bit-length ranges
// (specification §3.7) as defense-in-depth behind the frontend: integer and
// void widths must be in [1, 64], floating-point widths must be 16/32/64. A
// non-conformant scalar (here, a 100-bit unsigned integer) must be rejected
// before it can reach codegen.

module {
  dsdl.schema @test_BadScalarWidth_1_0 attributes {full_name = "test.BadScalarWidth", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__BadScalarWidth__deserialize_", c_serialize_symbol = "test__BadScalarWidth__serialize_", c_type_name = "test__BadScalarWidth", max_bits = 100 : i64, min_bits = 100 : i64} {
      dsdl.io {alignment_bits = 1 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 100 : i64, c_name = "value", cast_mode = "truncated", kind = "field", max_bits = 100 : i64, min_bits = 100 : i64, name = "value", scalar_category = "unsigned", type_name = "truncated uint100", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: error: 'dsdl.io' op invalid scalar bit_length metadata; unsigned integer widths must be in [1, 64]
