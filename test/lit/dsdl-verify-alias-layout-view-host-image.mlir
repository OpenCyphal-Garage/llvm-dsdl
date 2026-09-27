// A plan that holds a view and claims to be a host image. The structure holds a pointer where the
// wire holds the record, so the claim cannot hold, and analysis never makes it.
//
// RUN: not %dsdl-opt --pass-pipeline='builtin.module(dsdl-verify-alias-layout)' %s 2>&1 | FileCheck %s

module {
  dsdl.schema @test.Inner.1.0 attributes {full_name = "test.Inner", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {aliasable, fixed_size, max_bits = 16 : i64, min_bits = 16 : i64, sealed, wire_flat, host_image} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 16 : i64, cast_mode = "saturated", kind = "field", max_bits = 16 : i64, min_bits = 16 : i64, name = "value", scalar_category = "unsigned", type_name = "saturated uint16", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
  dsdl.schema @test.Holder.1.0 attributes {full_name = "test.Holder", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {fixed_size, max_bits = 16 : i64, min_bits = 16 : i64, sealed, wire_flat, host_image} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 0 : i64, cast_mode = "saturated", composite_extent_bits = 16 : i64, composite_full_name = "test.Inner", composite_major = 1 : i64, composite_minor = 0 : i64, composite_sealed = true, held_as_view, kind = "field", max_bits = 16 : i64, min_bits = 16 : i64, name = "inner", scalar_category = "composite", type_name = "test.Inner.1.0", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}

// CHECK: error: host_image holds for a plan that holds a view
