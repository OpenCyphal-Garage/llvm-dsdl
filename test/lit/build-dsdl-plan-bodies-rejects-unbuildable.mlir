// RUN: not %dsdl-opt --pass-pipeline='builtin.module(build-dsdl-plan-bodies)' %s 2>&1 | FileCheck %s

// A plan the builder cannot express fails the pass, in every lane. A body that is not
// operations is a body that does not exist.

module attributes {llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64} {
  func.func private @test.WideBool.1.0.plan.capacity_check(i64) -> i8

  dsdl.schema @test.WideBool.1.0 attributes {full_name = "test.WideBool", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64, lowered, lowered_align_count = 0 : i64, lowered_capacity_check_helper = "test.WideBool.1.0.plan.capacity_check", lowered_field_count = 1 : i64, lowered_max_bits = 2 : i64, lowered_min_bits = 2 : i64, lowered_padding_count = 0 : i64, lowered_step_count = 1 : i64, max_bits = 2 : i64, min_bits = 2 : i64} {
      dsdl.io {alignment_bits = 1 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 2 : i64, cast_mode = "saturated", kind = "field", lowered_bits = 2 : i64, max_bits = 2 : i64, min_bits = 2 : i64, name = "flag", scalar_category = "bool", step_index = 0 : i64, type_name = "bool", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: error: 'dsdl.serialization_plan' op cannot be built as operations: field 'flag' is a bool of 2 bits
