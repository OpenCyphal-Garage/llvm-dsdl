// RUN: not %dsdl-opt --pass-pipeline='builtin.module(convert-dsdl-to-emitc)' %s 2>&1 | FileCheck %s

module attributes {llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64} {
  dsdl.schema @test_CapacityHelper_1_0 attributes {full_name = "test.CapacityHelper", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__CapacityHelper__deserialize_", c_serialize_symbol = "test__CapacityHelper__serialize_", c_type_name = "test__CapacityHelper", llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64, lowered, lowered_align_count = 0 : i64, lowered_capacity_check_helper = "llvmdsdl_missing_capacity_helper", lowered_field_count = 1 : i64, lowered_max_bits = 8 : i64, lowered_min_bits = 8 : i64, lowered_padding_count = 0 : i64, lowered_step_count = 1 : i64, max_bits = 8 : i64, min_bits = 8 : i64} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "value", cast_mode = "truncated", kind = "field", lowered_bits = 8 : i64, lowered_deser_unsigned_helper = "llvmdsdl_plan_scalar_unsigned__test_CapacityHelper_1_0__0__deser", lowered_ser_unsigned_helper = "llvmdsdl_plan_scalar_unsigned__test_CapacityHelper_1_0__0__ser", max_bits = 8 : i64, min_bits = 8 : i64, name = "value", scalar_category = "unsigned", step_index = 0 : i64, type_name = "uint8", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: error: 'dsdl.serialization_plan' op missing lowered capacity-check helper symbol: llvmdsdl_missing_capacity_helper
