// RUN: not %dsdl-opt --lower-dsdl-bodies="target=ts package=m" %s 2>&1 | FileCheck %s --check-prefix=TS
// RUN: not %dsdl-opt --lower-dsdl-bodies="target=rust package=m" %s 2>&1 | FileCheck %s --check-prefix=RUST

// A definition `ns.File.1.0` beside a namespace `ns.file_1_0`: the definition's module and the
// namespace's directory take one name. Rust declares either as one `mod`, and a TypeScript
// directory's index re-exports either under its name, so the tree is refused.

// TS: error: 'dsdl.scope' op declares 'file_1_0' as a module in scope 'ns', which already declares it as a module
// RUST: error: 'dsdl.scope' op declares 'file_1_0' as a module in scope 'ns', which already declares it as a module

module {
  dsdl.schema @ns.File.1.0 attributes {full_name = "ns.File", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.field {name = "a", type_name = "saturated uint8"}
    dsdl.serialization_plan attributes {max_bits = 8 : i64, min_bits = 8 : i64} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, cast_mode = "saturated", kind = "field", max_bits = 8 : i64, min_bits = 8 : i64, name = "a", scalar_category = "unsigned", type_name = "saturated uint8", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
  dsdl.schema @ns.file_1_0.X.1.0 attributes {full_name = "ns.file_1_0.X", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.field {name = "b", type_name = "saturated uint8"}
    dsdl.serialization_plan attributes {max_bits = 8 : i64, min_bits = 8 : i64} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 0 : i64, array_kind = "none", array_length_prefix_bits = 0 : i64, bit_length = 8 : i64, cast_mode = "saturated", kind = "field", max_bits = 8 : i64, min_bits = 8 : i64, name = "b", scalar_category = "unsigned", type_name = "saturated uint8", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }
}
