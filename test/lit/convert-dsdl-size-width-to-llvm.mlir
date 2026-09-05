// RUN: %dsdl-opt --convert-dsdl-to-llvm %s | FileCheck %s --check-prefix=HOST
// RUN: %dsdl-opt --convert-dsdl-to-llvm=size-bits=32 %s | FileCheck %s --check-prefix=SMALL

// `size_t` is the target's, not the host's. A variable-length array holds its count in one, and
// every runtime primitive takes the buffer size and bit offset in one, so the struct a member is
// addressed within and the callee it is passed to have to answer the same width. A count held
// wider than the target's shifts every member after it.
//
// The pass records what it resolved, so the struct it derived and the calls it emitted cannot
// come from different answers.

// HOST-DAG: llvmdsdl.size_bits = 64
// SMALL-DAG: llvmdsdl.size_bits = 32

// The struct is printed by the access that indexes it; the count sits in the member's second
// position.
// HOST-DAG: llvm.struct<(struct<(array<4 x i8>, i64)>)>
// SMALL-DAG: llvm.struct<(struct<(array<4 x i8>, i32)>)>

// The value a primitive carries is its own fixed width -- `uint64_t`, whatever the target --
// and only the size and the offset beside it follow the target.
// HOST-DAG: llvm.func @dsdl_runtime_set_uxx(!llvm.ptr, i64, i64, i64, i8)
// SMALL-DAG: llvm.func @dsdl_runtime_set_uxx(!llvm.ptr, i32, i32, i64, i8)

module attributes {llvmdsdl.names_final} {
  dsdl.schema @demo_T_1_0 attributes {c_type_name = "demo__T", full_name = "demo.T", header_path = "demo/T_1_0.h", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.field {c_name = "tail", name = "tail", type_name = "saturated uint8[<=4]"}
    dsdl.serialization_plan attributes {c_deserialize_symbol = "demo__T__deserialize_", c_serialize_symbol = "demo__T__serialize_", c_type_name = "demo__T", max_bits = 40 : i64, min_bits = 8 : i64, sealed} {
      dsdl.io {alignment_bits = 8 : i64, array_capacity = 4 : i64, array_kind = "variable_inclusive", array_length_prefix_bits = 8 : i64, bit_length = 8 : i64, c_name = "tail", cast_mode = "saturated", kind = "field", max_bits = 40 : i64, min_bits = 8 : i64, name = "tail", scalar_category = "unsigned", type_name = "saturated uint8[<=4]", union_option_index = 0 : i64, union_tag_bits = 0 : i64}
    }
  }

  func.func @count(%obj: !dsdl.ptr<!dsdl.opaque<"const demo__T">>) -> i64 {
    %n = dsdl.load_member %obj ["tail", "count"] {indices = array<i64: 0, 1>} : <!dsdl.opaque<"const demo__T">> -> i64
    return %n : i64
  }

  func.func @write(%buf: !dsdl.ptr<!dsdl.opaque<"uint8_t">>, %off: i64, %v: i64, %cap: i64) -> i8 {
    %e = dsdl.write_bits %buf[%off], %v, size %cap {width = 8 : i64} : !dsdl.ptr<!dsdl.opaque<"uint8_t">>, i64
    return %e : i8
  }
}
