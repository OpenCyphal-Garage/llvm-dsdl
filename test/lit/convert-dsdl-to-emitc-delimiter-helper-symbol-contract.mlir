// RUN: not %dsdl-opt --pass-pipeline='builtin.module(convert-dsdl-to-emitc)' %s 2>&1 | FileCheck %s

module attributes {llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64} {
  func.func private @llvmdsdl_plan_capacity_check__test_DelimiterHelperSymbol_1_0(i64) -> i8

  dsdl.schema @test_DelimiterHelperSymbol_1_0 attributes {full_name = "test.DelimiterHelperSymbol", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__DelimiterHelperSymbol__deserialize_", c_serialize_symbol = "test__DelimiterHelperSymbol__serialize_", c_type_name = "test__DelimiterHelperSymbol", llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64, lowered, lowered_align_count = 0 : i64, lowered_capacity_check_helper = "llvmdsdl_plan_capacity_check__test_DelimiterHelperSymbol_1_0", lowered_field_count = 1 : i64, lowered_max_bits = 64 : i64, lowered_min_bits = 32 : i64, lowered_padding_count = 0 : i64, lowered_step_count = 1 : i64, max_bits = 64 : i64, min_bits = 32 : i64} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 0 : i64, c_name = "value", cast_mode = "truncated", composite_c_type_name = "vendor__Widget", composite_extent_bits = 128 : i64, composite_full_name = "vendor.Widget.1.0", composite_sealed = false, kind = "field", lowered_bits = 64 : i64, lowered_delimiter_validate_helper = "llvmdsdl_missing_delimiter_validate", max_bits = 64 : i64, min_bits = 32 : i64, name = "value", scalar_category = "composite", step_index = 0 : i64, type_name = "vendor.Widget.1.0", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: error: 'dsdl.io' op missing lowered delimiter-validate helper symbol: llvmdsdl_missing_delimiter_validate
