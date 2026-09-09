// RUN: %dsdl-opt --convert-dsdl-to-llvm %s | FileCheck %s

// A plan body of the shape build-dsdl-plan-bodies produces, lowered for object emission. The
// same operations reach the C path through convert-dsdl-to-emitc; what differs is only how a
// target answers them, which is the point of the plan being operations at all.

// CHECK-LABEL: func.func @widget_serialize
// An LLVM pointer holds no pointee, so every dialect pointer converts to the same type and
// the object a pointer names is not consulted until a member of it is reached.
// CHECK-SAME: (%[[OBJ:.*]]: !llvm.ptr, %[[BUF:.*]]: !llvm.ptr, %[[SZ:.*]]: !llvm.ptr)
func.func @widget_serialize(
    %obj: !dsdl.ptr<const !dsdl.object<"fixtures.vendor.Widget.1.0">>,
    %buf: !dsdl.ptr<!dsdl.byte>,
    %sz:  !dsdl.ptr<!dsdl.size>) -> i8 {
  %c0 = arith.constant 0 : i64
  %c8 = arith.constant 8 : i64

  // CHECK: %[[NULL:.*]] = llvm.mlir.zero : !llvm.ptr
  // CHECK: llvm.icmp "eq" %[[OBJ]], %[[NULL]]
  %n = dsdl.is_null %obj : !dsdl.ptr<const !dsdl.object<"fixtures.vendor.Widget.1.0">>

  // The size arrives by pointer and is loaded; there is no cast between spellings.
  // CHECK: llvm.load %[[SZ]] : !llvm.ptr -> i64
  %cap = dsdl.load_scalar %sz : !dsdl.ptr<!dsdl.size> -> i64

  // The space left goes into a stack slot the callee can write back through.
  // CHECK: %[[ONE:.*]] = llvm.mlir.constant(1 : i64)
  // CHECK: %[[SLOT:.*]] = llvm.alloca %[[ONE]] x i64
  // CHECK: llvm.store %{{.*}}, %[[SLOT]]
  %slot = dsdl.local %cap : i64 -> !dsdl.ptr<!dsdl.size>

  // A byte offset into the wire is a byte-addressed walk, not a subscript.
  // CHECK: llvm.getelementptr %[[BUF]][%{{.*}}] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %at = dsdl.buffer_at %buf[%c8] : !dsdl.ptr<!dsdl.byte> -> !dsdl.ptr<!dsdl.byte>

  // The runtime is called by symbol. Its definitions are not in this module, and what
  // resolves them is the object lane's to decide.
  // CHECK: llvm.call @dsdl_runtime_set_uxx
  %w = arith.constant 42 : i64
  %e = dsdl.write_bits %buf[%c0], %w, size %cap {width = 8 : i64} : !dsdl.ptr<!dsdl.byte>, i64

  // A nested type is called by its body's own symbol; one not in this module is declared.
  // CHECK: llvm.call @vendor_Inner_1_0__serialize_ir_
  %e2 = dsdl.call_serdes @vendor_Inner_1_0__serialize_ir_(%obj, %at, %slot) {direction = "serialize", member = "inner"} : !dsdl.ptr<const !dsdl.object<"fixtures.vendor.Widget.1.0">>, !dsdl.ptr<!dsdl.byte>, !dsdl.ptr<!dsdl.size>

  // CHECK: llvm.store %{{.*}}, %[[SZ]]
  dsdl.store_scalar %sz, %cap : !dsdl.ptr<!dsdl.size>, i64

  // CHECK-NOT: dsdl.
  // CHECK-NOT: emitc.
  return %e : i8
}

// A null buffer is legal when nothing will be read from it, and the runtime still wants a
// pointer. C reaches for a string literal; here it is a constant of this module's own.
// CHECK-LABEL: func.func @readable
// CHECK: llvm.mlir.addressof @llvmdsdl_empty_buffer
// CHECK: llvm.select
func.func @readable(%buf: !dsdl.ptr<const !dsdl.byte>) -> !dsdl.ptr<const !dsdl.byte> {
  %r = dsdl.buffer_or_empty %buf : !dsdl.ptr<const !dsdl.byte> -> !dsdl.ptr<const !dsdl.byte>
  return %r : !dsdl.ptr<const !dsdl.byte>
}
