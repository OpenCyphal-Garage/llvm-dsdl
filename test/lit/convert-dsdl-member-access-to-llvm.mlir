// RUN: %dsdl-opt --convert-dsdl-to-llvm %s | FileCheck %s

// Addressing a member without a name to address it by. The C path spells `obj->bar`; here the
// struct is derived from the schema and the member's position indexes it, and LLVM computes
// the offset from its own data layout rather than anything here adding up bytes.
//
// That the derived struct agrees with the one the C backend emits is not assumed: member
// order and member widths are held against the generated headers by
// llvmdsdl-member-layout-crosscheck, which compiles offsetof to ask what the layout is.

module {
  dsdl.schema @demo_T_1_0 attributes {c_type_name = "demo__T", full_name = "demo.T", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.serialization_plan attributes {c_type_name = "demo__T", max_bits = 88 : i64, min_bits = 24 : i64, sealed} {
      dsdl.io {array_capacity = 0 : i64, array_kind = "none", bit_length = 8 : i64, c_name = "foo", cast_mode = "saturated", max_bits = 64 : i64, min_bits = 0 : i64, alignment_bits = 1 : i64, array_length_prefix_bits = 0 : i64, union_option_index = 0 : i64, union_tag_bits = 0 : i64, kind = "field", name = "foo", scalar_category = "unsigned", type_name = "saturated uint8"}
      // Eleven bits are held in two, which is the width the offset of anything after it depends on.
      dsdl.io {array_capacity = 0 : i64, array_kind = "none", bit_length = 11 : i64, c_name = "bar", cast_mode = "saturated", max_bits = 64 : i64, min_bits = 0 : i64, alignment_bits = 1 : i64, array_length_prefix_bits = 0 : i64, union_option_index = 0 : i64, union_tag_bits = 0 : i64, kind = "field", name = "bar", scalar_category = "unsigned", type_name = "saturated uint11"}
      // Padding reserves wire bits and takes no member, so it shifts no position.
      dsdl.io {array_capacity = 0 : i64, array_kind = "none", bit_length = 5 : i64, cast_mode = "saturated", max_bits = 64 : i64, min_bits = 0 : i64, alignment_bits = 1 : i64, array_length_prefix_bits = 0 : i64, union_option_index = 0 : i64, union_tag_bits = 0 : i64, kind = "padding", name = "", scalar_category = "void", type_name = "void5"}
      dsdl.io {array_capacity = 4 : i64, array_kind = "variable_inclusive", array_length_prefix_bits = 8 : i64, bit_length = 8 : i64, c_name = "tail", cast_mode = "saturated", max_bits = 64 : i64, min_bits = 0 : i64, alignment_bits = 1 : i64, union_option_index = 0 : i64, union_tag_bits = 0 : i64, kind = "field", name = "tail", scalar_category = "unsigned", type_name = "saturated uint8[<=4]"}
    }
  }

  // CHECK-LABEL: func.func @access
  func.func @access(%obj: !dsdl.ptr<!dsdl.opaque<"demo__T">>, %i: i64) -> i64 {
    // The first member. A GEP's leading zero steps through the pointer, then the position.
    // CHECK: llvm.getelementptr %{{.*}}[0, 0] : (!llvm.ptr) -> !llvm.ptr, !llvm.struct<(i8, i16, struct<(array<4 x i8>, i64)>)>
    %a = dsdl.load_member %obj ["foo"] {indices = array<i64: 0>} : !dsdl.ptr<!dsdl.opaque<"demo__T">> -> i64

    // The second: position one, not two. The padding between them holds nothing.
    // CHECK: llvm.getelementptr %{{.*}}[0, 1]
    %b = dsdl.load_member %obj ["bar"] {indices = array<i64: 1>} : !dsdl.ptr<!dsdl.opaque<"demo__T">> -> i64

    // An array's count is the second half of its own member, hence two positions.
    // CHECK: llvm.getelementptr %{{.*}}[0, 2, 1]
    %n = dsdl.load_member %obj ["tail", "count"] {indices = array<i64: 2, 1>} : !dsdl.ptr<!dsdl.opaque<"demo__T">> -> i64

    // And an element is a third step, the only one not known until the loop runs.
    // CHECK: llvm.getelementptr %{{.*}}[0, 2, 0, %{{.*}}]
    %e = dsdl.load_element %obj ["tail", "elements"] [%i] {indices = array<i64: 2, 0>, element_type = "uint8_t"} : !dsdl.ptr<!dsdl.opaque<"demo__T">> -> i64

    // CHECK-NOT: dsdl.
    %s = arith.addi %a, %b : i64
    %t = arith.addi %s, %n : i64
    %u = arith.addi %t, %e : i64
    return %u : i64
  }
}
