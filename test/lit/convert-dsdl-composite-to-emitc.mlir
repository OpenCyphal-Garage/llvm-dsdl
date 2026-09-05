// RUN: %dsdl-opt --convert-dsdl-to-emitc %s | FileCheck %s

// A nested composite is not inlined into its container's layout. It is serialized by handing
// its own entry point a pointer to the member, the point in the buffer the container's
// encoding reached, and the space left -- which is why a plan needs addresses rather than
// values, and a local to receive what the callee used.

// CHECK-LABEL: func.func @nest
func.func @nest(%obj: !dsdl.ptr<!dsdl.opaque<"const vendor__Outer">>,
                %buf: !dsdl.ptr<!dsdl.opaque<"uint8_t">>,
                %remaining: i64, %byte_off: i64) -> i8 {
  // The space available goes in by pointer, so the callee can write back what it used.
  // CHECK: %[[SLOT:.*]] = "emitc.variable"()
  // CHECK: emitc.assign
  // CHECK: %[[SZP:.*]] = emitc.apply "&"(%[[SLOT]])
  %sizep = dsdl.local %remaining : i64 -> !dsdl.ptr<!dsdl.opaque<"size_t">>

  // CHECK: "emitc.member_of_ptr"({{.*}}) <{member = "inner"}>
  // CHECK: emitc.apply "&"
  %objp = dsdl.member_addr %obj ["inner"] {indices = array<i64: 0>} : !dsdl.ptr<!dsdl.opaque<"const vendor__Outer">> -> !dsdl.ptr<!dsdl.opaque<"const vendor__Inner">>

  // CHECK: emitc.subscript
  // CHECK: emitc.apply "&"
  %bufp = dsdl.buffer_at %buf[%byte_off] : !dsdl.ptr<!dsdl.opaque<"uint8_t">> -> !dsdl.ptr<!dsdl.opaque<"uint8_t">>

  // The callee is the nested type's published symbol, defined in its own translation unit.
  // CHECK: emitc.call_opaque "vendor__Inner__serialize_"
  %err = dsdl.call_serdes "vendor__Inner__serialize_"(%objp, %bufp, %sizep) : !dsdl.ptr<!dsdl.opaque<"const vendor__Inner">>, !dsdl.ptr<!dsdl.opaque<"uint8_t">>, !dsdl.ptr<!dsdl.opaque<"size_t">>

  // What it used is read back out of the same local, and advances the container's offset.
  // CHECK: emitc.subscript
  // CHECK: emitc.load
  %used = dsdl.load_scalar %sizep : !dsdl.ptr<!dsdl.opaque<"size_t">> -> i64
  %sum = arith.addi %used, %byte_off : i64
  %tr = arith.trunci %sum : i64 to i8
  %keep = arith.addi %err, %tr : i8
  return %keep : i8
}
