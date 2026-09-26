// RUN: %dsdl-opt --dsdl-fold-body-sizes %s | FileCheck %s

// A body reads the space it is handed from its size pointer and writes back what it used. For a
// target whose buffer carries its length, the body takes the buffer alone: each read becomes the
// buffer's length, and what it wrote back becomes a second result.

// What computes the size is computed unconditionally where it can be, and the branch that held
// only the write goes.
// CHECK-LABEL: func.func @hoisted(
// CHECK-SAME:    %arg0: !dsdl.ptr<const !dsdl.object<"vendor.Msg.1.0">>, %arg1: !dsdl.ptr<!dsdl.byte>) -> (i8, index)
// CHECK:       %[[LENGTH:.*]] = dsdl.buffer_length %arg1 : <!dsdl.byte>
// CHECK:       %[[BITS:.*]] = arith.muli %[[LENGTH]]
// CHECK:       %[[BYTES:.*]] = arith.divui %[[BITS]]
// CHECK-NEXT:  %[[USED:.*]] = arith.index_cast %[[BYTES]] : i64 to index
// CHECK-NOT:   scf.if
// CHECK-NOT:   dsdl.store_scalar
// CHECK:       return %{{.*}}, %[[USED]] : i8, index
func.func @hoisted(%obj: !dsdl.ptr<const !dsdl.object<"vendor.Msg.1.0">>, %buf: !dsdl.ptr<!dsdl.byte>, %size: !dsdl.ptr<!dsdl.size>) -> i8 attributes {llvmdsdl.plan_body = "serialize"} {
  %eight = arith.constant 8 : i64
  %ok = arith.constant 0 : i8
  %available = dsdl.load_scalar %size : !dsdl.ptr<!dsdl.size> -> i64
  %bits = arith.muli %available, %eight : i64
  %err = func.call @check(%bits) : (i64) -> i8
  %good = arith.cmpi eq, %err, %ok : i8
  scf.if %good {
    %bytes = arith.divui %bits, %eight : i64
    dsdl.store_scalar %size, %bytes : !dsdl.ptr<!dsdl.size>, i64
  }
  return %err : i8
}

// A size the arm computes with a call is answered out of the arm, and the other arm, which fails,
// answers zero.
// CHECK-LABEL: func.func @answered(
// CHECK-SAME:    -> (i8, index)
// CHECK:       %[[ZERO:.*]] = arith.constant 0 : index
// CHECK:       %[[BRANCH:.*]]:2 = scf.if %{{.*}} -> (i8, index) {
// CHECK-NEXT:    scf.yield %{{.*}}, %[[ZERO]] : i8, index
// CHECK-NEXT:  } else {
// CHECK:         %[[MEASURED:.*]] = func.call @measure
// CHECK-NEXT:    %[[USED:.*]] = arith.index_cast %[[MEASURED]] : i64 to index
// CHECK-NEXT:    scf.yield %{{.*}}, %[[USED]] : i8, index
// CHECK:       return %[[BRANCH]]#0, %[[BRANCH]]#1 : i8, index
func.func @answered(%obj: !dsdl.ptr<!dsdl.object<"vendor.Msg.1.0">>, %buf: !dsdl.ptr<const !dsdl.byte>, %size: !dsdl.ptr<!dsdl.size>) -> i8 attributes {llvmdsdl.plan_body = "deserialize"} {
  %rejected = arith.constant -2 : i8
  %ok = arith.constant 0 : i8
  %null = dsdl.is_null %obj : !dsdl.ptr<!dsdl.object<"vendor.Msg.1.0">>
  %err = scf.if %null -> (i8) {
    scf.yield %rejected : i8
  } else {
    %available = dsdl.load_scalar %size : !dsdl.ptr<!dsdl.size> -> i64
    %used = func.call @measure(%available) : (i64) -> i64
    dsdl.store_scalar %size, %used : !dsdl.ptr<!dsdl.size>, i64
    scf.yield %ok : i8
  }
  return %err : i8
}

// A body that reads nothing of its size takes no length, and a constant size is answered as one.
// CHECK-LABEL: func.func @constant(
// CHECK-SAME:    -> (i8, index)
// CHECK-NOT:   dsdl.buffer_length
// CHECK:       %[[SEVEN:.*]] = arith.constant 7 : index
// CHECK:       return %{{.*}}, %[[SEVEN]] : i8, index
func.func @constant(%obj: !dsdl.ptr<const !dsdl.object<"vendor.Msg.1.0">>, %buf: !dsdl.ptr<!dsdl.byte>, %size: !dsdl.ptr<!dsdl.size>) -> i8 attributes {llvmdsdl.plan_body = "serialize"} {
  %seven = arith.constant 7 : i64
  %ok = arith.constant 0 : i8
  %err = func.call @write(%buf) : (!dsdl.ptr<!dsdl.byte>) -> i8
  %good = arith.cmpi eq, %err, %ok : i8
  scf.if %good {
    dsdl.store_scalar %size, %seven : !dsdl.ptr<!dsdl.size>, i64
  }
  return %err : i8
}

func.func private @check(i64) -> i8
func.func private @measure(i64) -> i64
func.func private @write(!dsdl.ptr<!dsdl.byte>) -> i8
