// RUN: not %dsdl-opt --pass-pipeline='builtin.module(convert-dsdl-to-emitc)' %s 2>&1 | FileCheck %s --check-prefix=EMITC
// RUN: not %dsdl-opt --pass-pipeline='builtin.module(build-dsdl-plan-bodies)' %s 2>&1 | FileCheck %s --check-prefix=BUILD

// The C conversion renders nothing of its own: a plan whose body was not built as operations
// is an error, not a function spelled out as text. And the builder works from a backend's
// final names, which this module does not carry.

module attributes {llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64} {
  func.func private @llvmdsdl_plan_capacity_check__test_Unbuilt_1_0(i64) -> i8
  func.func private @llvmdsdl_plan_scalar_unsigned__test_Unbuilt_1_0__0__ser(i64) -> i64
  func.func private @llvmdsdl_plan_scalar_unsigned__test_Unbuilt_1_0__0__deser(i64) -> i64

  dsdl.schema @test_Unbuilt_1_0 attributes {full_name = "test.Unbuilt", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Unbuilt__deserialize_", c_serialize_symbol = "test__Unbuilt__serialize_", c_type_name = "test__Unbuilt", llvmdsdl.lowered_contract_producer = "lower-dsdl-exec", llvmdsdl.lowered_contract_version = 2 : i64, lowered, lowered_align_count = 0 : i64, lowered_capacity_check_helper = "llvmdsdl_plan_capacity_check__test_Unbuilt_1_0", lowered_field_count = 1 : i64, lowered_max_bits = 8 : i64, lowered_min_bits = 8 : i64, lowered_padding_count = 0 : i64, lowered_step_count = 1 : i64, max_bits = 8 : i64, min_bits = 8 : i64} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, c_name = "value", cast_mode = "truncated", kind = "field", lowered_bits = 8 : i64, lowered_deser_unsigned_helper = "llvmdsdl_plan_scalar_unsigned__test_Unbuilt_1_0__0__deser", lowered_ser_unsigned_helper = "llvmdsdl_plan_scalar_unsigned__test_Unbuilt_1_0__0__ser", max_bits = 8 : i64, min_bits = 8 : i64, name = "value", scalar_category = "unsigned", step_index = 0 : i64, type_name = "uint8", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// EMITC: error: 'dsdl.serialization_plan' op no serialize body was built for this plan; run build-dsdl-plan-bodies before convert-dsdl-to-emitc
// BUILD: error: 'dsdl.serialization_plan' op bodies are built from a backend's final C names, and this module carries none
