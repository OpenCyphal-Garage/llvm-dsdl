// RUN: not %dsdl-opt --pass-pipeline='builtin.module(convert-dsdl-to-emitc)' %s 2>&1 | FileCheck %s

module attributes {llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64} {
  func.func private @llvmdsdl_plan_capacity_check__test_ArrayHelperSymbol_1_0(i64) -> i8
  func.func private @llvmdsdl_plan_scalar_unsigned__test_ArrayHelperSymbol_1_0__0__ser(i64) -> i64
  func.func private @llvmdsdl_plan_scalar_unsigned__test_ArrayHelperSymbol_1_0__0__deser(i64) -> i64

  dsdl.schema @test_ArrayHelperSymbol_1_0 attributes {full_name = "test.ArrayHelperSymbol", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__ArrayHelperSymbol__deserialize_", c_serialize_symbol = "test__ArrayHelperSymbol__serialize_", c_type_name = "test__ArrayHelperSymbol", llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64, lowered, lowered_align_count = 0 : i64, lowered_capacity_check_helper = "llvmdsdl_plan_capacity_check__test_ArrayHelperSymbol_1_0", lowered_field_count = 1 : i64, lowered_max_bits = 16 : i64, lowered_min_bits = 8 : i64, lowered_padding_count = 0 : i64, lowered_step_count = 1 : i64, max_bits = 16 : i64, min_bits = 8 : i64} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 2 : i64, array_kind = "variable_inclusive", array_length_prefix_bits = 2 : i64, bit_length = 8 : i64, c_name = "value", cast_mode = "truncated", kind = "field", lowered_array_length_validate_helper = "llvmdsdl_missing_array_validate", lowered_bits = 16 : i64, lowered_deser_array_length_prefix_helper = "llvmdsdl_missing_array_prefix_deser", lowered_deser_unsigned_helper = "llvmdsdl_plan_scalar_unsigned__test_ArrayHelperSymbol_1_0__0__deser", lowered_ser_array_length_prefix_helper = "llvmdsdl_missing_array_prefix_ser", lowered_ser_unsigned_helper = "llvmdsdl_plan_scalar_unsigned__test_ArrayHelperSymbol_1_0__0__ser", max_bits = 16 : i64, min_bits = 8 : i64, name = "value", scalar_category = "unsigned", step_index = 0 : i64, type_name = "uint8[<=2]", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: error: 'dsdl.io' op missing lowered array-length-prefix helper symbol: llvmdsdl_missing_array_prefix_ser
