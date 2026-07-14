// RUN: not %dsdl-opt %s 2>&1 | FileCheck %s

// The dsdl.field verifier requires a non-empty name for non-padding fields
// (padding fields such as void8 are legitimately anonymous). A named field with
// an empty name is malformed schema IR and must be rejected — this verifier now
// runs on every lowered module (lowerToMLIR verifies its own output), for all
// backends, not just the C/EmitC pass path.

module {
  dsdl.schema @test_BadField_1_0 attributes {full_name = "test.BadField", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.field {c_name = "value", name = "", type_name = "saturated uint8"}
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__BadField__deserialize_", c_serialize_symbol = "test__BadField__serialize_", c_type_name = "test__BadField", max_bits = 8 : i64, min_bits = 8 : i64} {
      dsdl.align {bits = 8 : i32}
    }
  }
}

// CHECK: error: 'dsdl.field' op requires a non-empty 'name' for non-padding fields
