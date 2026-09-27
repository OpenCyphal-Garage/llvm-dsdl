// RUN: %dsdl-opt --pass-pipeline='builtin.module(lower-dsdl-serialization)' %s | FileCheck %s

module {
  dsdl.schema @test.U64.1.0 attributes {full_name = "test.U64", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__U64__deserialize_", c_serialize_symbol = "test__U64__serialize_", c_type_name = "test__U64", max_bits = 64 : i64, min_bits = 64 : i64} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 64 : i64, c_name = "value", cast_mode = "truncated", kind = "field", max_bits = 64 : i64, min_bits = 64 : i64, name = "value", scalar_category = "unsigned", type_name = "truncated uint64", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK-DAG: llvmdsdl.lowered_contract_producer = "lower-dsdl-exec"
// CHECK-DAG: llvmdsdl.lowered_contract_version = 2 : i64
// CHECK-DAG: lowered_ser_unsigned_helper = "test.U64.1.0.plan.scalar_unsigned.{{[0-9]+}}.ser"
// CHECK-DAG: lowered_deser_unsigned_helper = "test.U64.1.0.plan.scalar_unsigned.{{[0-9]+}}.deser"
// CHECK-LABEL: func.func @test.U64.1.0.plan.scalar_unsigned.{{[0-9]+}}.ser(
// CHECK-SAME: %[[V:[^:]+]]: i64
// CHECK: return %[[V]] : i64
// CHECK-LABEL: func.func @test.U64.1.0.plan.scalar_unsigned.{{[0-9]+}}.deser(
// CHECK-SAME: %[[DV:[^:]+]]: i64
// CHECK: return %[[DV]] : i64
