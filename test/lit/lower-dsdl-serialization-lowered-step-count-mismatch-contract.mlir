// RUN: not %dsdl-opt --pass-pipeline='builtin.module(lower-dsdl-serialization)' %s 2>&1 | FileCheck %s

module {
  dsdl.schema @test_LoweredStepCountMismatch_1_0 attributes {full_name = "test.LoweredStepCountMismatch", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__LoweredStepCountMismatch__deserialize_", c_serialize_symbol = "test__LoweredStepCountMismatch__serialize_", c_type_name = "test__LoweredStepCountMismatch", lowered, llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64, lowered_align_count = 0 : i64, lowered_field_count = 1 : i64, lowered_max_bits = 8 : i64, lowered_min_bits = 0 : i64, lowered_padding_count = 0 : i64, lowered_step_count = 2 : i64, max_bits = 8 : i64, min_bits = 0 : i64} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, cast_mode = "truncated", kind = "field", lowered_bits = 8 : i64, max_bits = 8 : i64, min_bits = 0 : i64, name = "value", scalar_category = "unsigned", step_index = 0 : i64, type_name = "truncated uint8", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: error: 'dsdl.serialization_plan' op lowered step counters do not match serialisation plan body
