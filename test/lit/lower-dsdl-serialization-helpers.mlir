// RUN: %dsdl-opt --pass-pipeline='builtin.module(lower-dsdl-serialization)' %s | FileCheck %s

module {
  dsdl.schema @test_Helpers_1_0 attributes {full_name = "test.Helpers", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Helpers__deserialize_", c_serialize_symbol = "test__Helpers__serialize_", c_type_name = "test__Helpers", max_bits = 80 : i64, min_bits = 24 : i64} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 13 : i64, c_name = "a", cast_mode = "saturated", kind = "field", max_bits = 13 : i64, min_bits = 13 : i64, name = "a", scalar_category = "signed", type_name = "saturated int13", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 16 : i64, c_name = "b", cast_mode = "truncated", kind = "field", max_bits = 16 : i64, min_bits = 16 : i64, name = "b", scalar_category = "float", type_name = "float16", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 5 : i64, array_kind = "variable_inclusive", array_length_prefix_bits = 3 : i64, bit_length = 8 : i64, c_name = "c", cast_mode = "truncated", kind = "field", max_bits = 43 : i64, min_bits = 3 : i64, name = "c", scalar_category = "unsigned", type_name = "truncated uint8[<=5]", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 0 : i64, c_name = "d", cast_mode = "truncated", composite_c_type_name = "vendor__Widget", composite_extent_bits = 128 : i64, composite_sealed = false, kind = "field", max_bits = 160 : i64, min_bits = 32 : i64, name = "d", scalar_category = "composite", type_name = "vendor.Widget.1.0", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK-DAG: llvmdsdl.lowered_contract_producer = "lower-dsdl-exec"
// CHECK-DAG: llvmdsdl.lowered_contract_version = 2 : i64
// CHECK-DAG: lowered_ser_signed_helper = "__llvmdsdl_plan_scalar_signed__test_Helpers_1_0__{{[0-9]+}}__ser"
// CHECK-DAG: lowered_capacity_check_helper = "__llvmdsdl_plan_capacity_check__test_Helpers_1_0"
// CHECK-DAG: lowered_deser_signed_helper = "__llvmdsdl_plan_scalar_signed__test_Helpers_1_0__{{[0-9]+}}__deser"
// CHECK-DAG: lowered_ser_float_helper = "__llvmdsdl_plan_scalar_float__test_Helpers_1_0__{{[0-9]+}}__ser"
// CHECK-DAG: lowered_deser_float_helper = "__llvmdsdl_plan_scalar_float__test_Helpers_1_0__{{[0-9]+}}__deser"
// CHECK-DAG: lowered_ser_unsigned_helper = "__llvmdsdl_plan_scalar_unsigned__test_Helpers_1_0__{{[0-9]+}}__ser"
// CHECK-DAG: lowered_deser_unsigned_helper = "__llvmdsdl_plan_scalar_unsigned__test_Helpers_1_0__{{[0-9]+}}__deser"
// CHECK-DAG: lowered_ser_array_length_prefix_helper = "__llvmdsdl_plan_array_length_prefix__test_Helpers_1_0__{{[0-9]+}}__ser"
// CHECK-DAG: lowered_deser_array_length_prefix_helper = "__llvmdsdl_plan_array_length_prefix__test_Helpers_1_0__{{[0-9]+}}__deser"
// CHECK-DAG: lowered_array_length_validate_helper = "__llvmdsdl_plan_validate_array_length__test_Helpers_1_0__{{[0-9]+}}"
// CHECK-DAG: lowered_delimiter_validate_helper = "__llvmdsdl_plan_validate_delimiter_header__test_Helpers_1_0__{{[0-9]+}}"
// CHECK-DAG: func.func @__llvmdsdl_plan_scalar_unsigned__test_Helpers_1_0__{{[0-9]+}}__ser
// CHECK-DAG: func.func @__llvmdsdl_plan_scalar_unsigned__test_Helpers_1_0__{{[0-9]+}}__deser
// CHECK-DAG: func.func @__llvmdsdl_plan_array_length_prefix__test_Helpers_1_0__{{[0-9]+}}__ser(%{{[^:]+}}: i64) -> i64 attributes {llvmdsdl.array_length_prefix_helper
// CHECK-DAG: func.func @__llvmdsdl_plan_array_length_prefix__test_Helpers_1_0__{{[0-9]+}}__deser(%{{[^:]+}}: i64) -> i64 attributes {llvmdsdl.array_length_prefix_helper
// CHECK-DAG: func.func @__llvmdsdl_plan_scalar_signed__test_Helpers_1_0__{{[0-9]+}}__ser
// CHECK-DAG: func.func @__llvmdsdl_plan_scalar_signed__test_Helpers_1_0__{{[0-9]+}}__deser
// CHECK-DAG: func.func @__llvmdsdl_plan_scalar_float__test_Helpers_1_0__{{[0-9]+}}__ser
// CHECK-DAG: func.func @__llvmdsdl_plan_scalar_float__test_Helpers_1_0__{{[0-9]+}}__deser
// CHECK-DAG: func.func @__llvmdsdl_plan_validate_array_length__test_Helpers_1_0__{{[0-9]+}}(%{{[^:]+}}: i64) -> i8 attributes {llvmdsdl.array_length_validate
// CHECK-DAG: func.func @__llvmdsdl_plan_validate_delimiter_header__test_Helpers_1_0__{{[0-9]+}}(%{{[^:]+}}: i64, %{{[^:]+}}: i64) -> i8 attributes {llvmdsdl.delimiter_header_validate
