// RUN: %dsdl-opt --pass-pipeline='builtin.module(lower-dsdl-serialization)' %s | FileCheck %s

module {
  dsdl.schema @test_Service_1_0 attributes {full_name = "test.Service", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Service__Request__deserialize_", c_serialize_symbol = "test__Service__Request__serialize_", c_type_name = "test__Service__Request", max_bits = 8 : i64, min_bits = 8 : i64, section = "request"} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "value", cast_mode = "truncated", kind = "field", max_bits = 8 : i64, min_bits = 8 : i64, name = "value", scalar_category = "unsigned", type_name = "truncated uint8", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Service__Response__deserialize_", c_serialize_symbol = "test__Service__Response__serialize_", c_type_name = "test__Service__Response", max_bits = 16 : i64, min_bits = 16 : i64, section = "response"} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 16 : i64, c_name = "value", cast_mode = "truncated", kind = "field", max_bits = 16 : i64, min_bits = 16 : i64, name = "value", scalar_category = "unsigned", type_name = "truncated uint16", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK-DAG: module attributes {llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64}
// CHECK-DAG: lowered_capacity_check_helper = "llvmdsdl_plan_capacity_check__test_Service_1_0__request"
// CHECK-DAG: lowered_capacity_check_helper = "llvmdsdl_plan_capacity_check__test_Service_1_0__response"
// CHECK-LABEL: func.func @llvmdsdl_plan_capacity_check__test_Service_1_0__request(
// CHECK-LABEL: func.func @llvmdsdl_plan_capacity_check__test_Service_1_0__response(
