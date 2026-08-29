// RUN: %dsdl-opt --convert-dsdl-to-llvm %s | FileCheck %s

// The same plan body convert-dsdl-plan-body-to-emitc.mlir carries, lowered the other way.
// Both start from the operations build-dsdl-plan-bodies produces; what differs is only how a
// target answers them, which is the point of the plan being operations at all.

// CHECK-LABEL: func.func @widget_serialize
// An LLVM pointer holds no pointee, so every dialect pointer converts to the same type and
// the spelling !dsdl.opaque carries is simply not consulted here.
// CHECK-SAME: (%[[OBJ:.*]]: !llvm.ptr, %[[BUF:.*]]: !llvm.ptr, %[[SZ:.*]]: !llvm.ptr)
func.func @widget_serialize(
    %obj: !dsdl.ptr<!dsdl.opaque<"const fixtures__vendor__Widget">>,
    %buf: !dsdl.ptr<!dsdl.opaque<"uint8_t">>,
    %sz:  !dsdl.ptr<!dsdl.opaque<"size_t">>) -> i8 {
  %c0 = arith.constant 0 : i64
  %c8 = arith.constant 8 : i64

  // CHECK: %[[NULL:.*]] = llvm.mlir.zero : !llvm.ptr
  // CHECK: llvm.icmp "eq" %[[OBJ]], %[[NULL]]
  %n = dsdl.is_null %obj : !dsdl.ptr<!dsdl.opaque<"const fixtures__vendor__Widget">>

  // The size arrives by pointer and is simply loaded; there is no cast between spellings
  // because there are no spellings.
  // CHECK: llvm.load %[[SZ]] : !llvm.ptr -> i64
  %cap = dsdl.load_scalar %sz : !dsdl.ptr<!dsdl.opaque<"size_t">> -> i64

  // The space left goes into a stack slot the callee can write back through.
  // CHECK: %[[ONE:.*]] = llvm.mlir.constant(1 : i64)
  // CHECK: %[[SLOT:.*]] = llvm.alloca %[[ONE]] x i64
  // CHECK: llvm.store %{{.*}}, %[[SLOT]]
  %slot = dsdl.local %cap : i64 -> !dsdl.ptr<!dsdl.opaque<"size_t">>

  // A byte offset into the wire is a byte-addressed walk, not a subscript.
  // CHECK: llvm.getelementptr %[[BUF]][%{{.*}}] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %at = dsdl.buffer_at %buf[%c8] : !dsdl.ptr<!dsdl.opaque<"uint8_t">> -> !dsdl.ptr<!dsdl.opaque<"uint8_t">>

  // The runtime is called by symbol. Its definitions are not in this module, and what
  // resolves them is the object lane's to decide.
  // CHECK: llvm.call @dsdl_runtime_set_uxx
  %w = arith.constant 42 : i64
  %e = dsdl.write_bits %buf[%c0], %w, size %cap {width = 8 : i64} : !dsdl.ptr<!dsdl.opaque<"uint8_t">>, i64

  // CHECK: llvm.call @vendor__Inner__serialize_
  %e2 = dsdl.call_serdes "vendor__Inner__serialize_"(%obj, %at, %slot) : !dsdl.ptr<!dsdl.opaque<"const fixtures__vendor__Widget">>, !dsdl.ptr<!dsdl.opaque<"uint8_t">>, !dsdl.ptr<!dsdl.opaque<"size_t">>

  // CHECK: llvm.store %{{.*}}, %[[SZ]]
  dsdl.store_scalar %sz, %cap : !dsdl.ptr<!dsdl.opaque<"size_t">>, i64

  // CHECK-NOT: dsdl.
  // CHECK-NOT: emitc.
  return %e : i8
}

// A null buffer is legal when nothing will be read from it, and the runtime still wants a
// pointer. C reaches for a string literal; here it is a constant of this module's own.
// CHECK-LABEL: func.func @readable
// CHECK: llvm.mlir.addressof @llvmdsdl_empty_buffer
// CHECK: llvm.select
func.func @readable(%buf: !dsdl.ptr<!dsdl.opaque<"const uint8_t">>) -> !dsdl.ptr<!dsdl.opaque<"const uint8_t">> {
  %r = dsdl.buffer_or_empty %buf : !dsdl.ptr<!dsdl.opaque<"const uint8_t">> -> !dsdl.ptr<!dsdl.opaque<"const uint8_t">>
  return %r : !dsdl.ptr<!dsdl.opaque<"const uint8_t">>
}
