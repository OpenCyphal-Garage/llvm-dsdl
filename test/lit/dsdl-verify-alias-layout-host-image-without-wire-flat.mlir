// A plan claiming to be a host image while stating nothing about the wire. H is the stricter of the
// two and cannot hold where W does not, so the claim is refused rather than read as an unstamped
// plan: a plan that states one of the pair has stated a verdict.
//
// RUN: not %dsdl-opt --pass-pipeline='builtin.module(dsdl-verify-alias-layout)' %s 2>&1 | FileCheck %s

module {
  dsdl.schema @test_Claim_1_0 attributes {full_name = "test.Claim", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_deserialize_symbol = "test__Claim__deserialize_", c_serialize_symbol = "test__Claim__serialize_", c_type_name = "test__Claim", fixed_size, host_image, max_bits = 16 : i64, min_bits = 16 : i64, sealed} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 16 : i64, c_name = "value", cast_mode = "saturated", kind = "field", max_bits = 16 : i64, min_bits = 16 : i64, name = "value", scalar_category = "unsigned", type_name = "saturated uint16", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: error: host_image holds where wire_flat does not
