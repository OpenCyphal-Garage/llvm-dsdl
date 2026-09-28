// RUN: %dsdl-opt --lower-dsdl-bodies="target=ts package=m" %s | FileCheck %s --check-prefix=TS
// RUN: not %dsdl-opt --lower-dsdl-bodies="target=rust package=m" %s 2>&1 | FileCheck %s --check-prefix=RUST

// A definition `ns.File.1.0` beside a namespace `ns.file_1_0`: the definition's module and the
// namespace's directory take one name. TypeScript keeps a file and a directory apart, so the tree
// holds both, with the namespace's definitions inside the namespace rather than inside the
// definition's module. Rust declares either as one `mod`, and the tree is refused.

// TS:      dsdl.scope namespace "ns" {
// TS-NEXT:   dsdl.scope module "file_1_0" {
// TS-NEXT:     dsdl.scope type "File" of = @ns.File.1.0 {
// TS:        dsdl.scope namespace "file_1_0" {
// TS-NEXT:     dsdl.scope module "x_1_0" {
// TS-NEXT:       dsdl.scope type "X" of = @ns.file_1_0.X.1.0 {

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
