// RUN: %dsdl-opt --pass-pipeline='builtin.module(lower-dsdl-exec)' %s | FileCheck %s

module {
  dsdl.schema @test.Alias.1.0 attributes {full_name = "test.Alias", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {max_bits = 8 : i64, min_bits = 8 : i64} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, cast_mode = "truncated", kind = "field", max_bits = 8 : i64, min_bits = 8 : i64, name = "value", scalar_category = "unsigned", type_name = "uint8", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: module attributes {llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64}
// CHECK: dsdl.serialization_plan attributes {
// CHECK-SAME: llvmdsdl.lowered_contract_producer = "lower-dsdl-exec"
// CHECK-SAME: llvmdsdl.lowered_contract_version = 2 : i64
// CHECK-SAME: lowered
