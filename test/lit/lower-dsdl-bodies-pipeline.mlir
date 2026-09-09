// RUN: %dsdl-opt --pass-pipeline='builtin.module(lower-dsdl-bodies)' %s | FileCheck %s

// The pipeline every backend's bodies are translations of: lowering, aliasability annotation and
// body building in one step, from a module carrying a backend's final names.

module attributes {llvmdsdl.names_final} {
  dsdl.schema @test_Bodies_1_0 attributes {c_type_name = "test__Bodies", full_name = "test.Bodies", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.field {c_name = "value", name = "value", type_name = "saturated uint8"}
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Bodies__deserialize_", c_serialize_symbol = "test__Bodies__serialize_", c_type_name = "test__Bodies", extent_bits = 8 : i64, fixed_size, max_bits = 8 : i64, min_bits = 8 : i64, sealed} {
      dsdl.align {bits = 1 : i32}
      dsdl.io {alignment_bits = 1 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "value", cast_mode = "saturated", kind = "field", max_bits = 8 : i64, min_bits = 8 : i64, name = "value", scalar_category = "unsigned", type_name = "saturated uint8", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: module attributes {
// CHECK-SAME: llvmdsdl.lowered_contract_producer = "lower-dsdl-exec"
// CHECK: dsdl.serialization_plan attributes {
// CHECK-SAME: lowered_step_count = 1 : i64
// CHECK-DAG: func.func @test_Bodies_1_0__serialize_ir_(
// CHECK-DAG: func.func @test_Bodies_1_0__deserialize_ir_(
// CHECK-DAG: dsdl.write_bits
// CHECK-DAG: dsdl.read_bits
