// RUN: %dsdl-opt --dsdl-fold-nested-call-sizes %s | FileCheck %s

// A nested call is handed its space through a local and the plan reads what it used back out of
// the same local. For a target whose nested entry point takes the space as its buffer's length and
// answers what it used, the call takes the space by value and answers it as a result, and the read
// back becomes a conversion of that result to the width the plan counts in.

// CHECK-LABEL: func.func @read_back(
// CHECK-NOT:   dsdl.local
// CHECK:       %[[ERR:.*]], %[[CONSUMED:.*]] = dsdl.call_serdes_sized @inner_serialize(%arg0, %arg1, %arg2) {direction = "serialize", member = "inner"}
// CHECK-NEXT:  %[[USED:.*]] = arith.index_cast %[[CONSUMED]] : index to i64
// CHECK-NOT:   dsdl.load_scalar
// CHECK:       arith.muli %[[USED]]
// CHECK:       return %[[ERR]]
func.func @read_back(%obj: !dsdl.ptr<const !dsdl.object<"vendor.Inner.1.0">>, %buf: !dsdl.ptr<!dsdl.byte>, %available: i64) -> (i8, i64) {
  %eight = arith.constant 8 : i64
  %slot = dsdl.local %available : i64 -> !dsdl.ptr<!dsdl.size>
  %err = dsdl.call_serdes @inner_serialize(%obj, %buf, %slot) {direction = "serialize", member = "inner"} : !dsdl.ptr<const !dsdl.object<"vendor.Inner.1.0">>, !dsdl.ptr<!dsdl.byte>, !dsdl.ptr<!dsdl.size>
  %used = dsdl.load_scalar %slot : !dsdl.ptr<!dsdl.size> -> i64
  %bits = arith.muli %used, %eight : i64
  return %err, %bits : i8, i64
}

// The slot of a delimited field is made before the guard and the call made inside it. What the
// call answers is still read only after it.
// CHECK-LABEL: func.func @slot_outside_the_guard(
// CHECK:       scf.if
// CHECK:       %[[ERR:.*]], %[[CONSUMED:.*]] = dsdl.call_serdes_sized @inner_serialize(%arg0, %arg1, %arg2)
// CHECK-NEXT:  %[[USED:.*]] = arith.index_cast %[[CONSUMED]] : index to i64
// CHECK-NEXT:  scf.yield %[[ERR]], %[[USED]] : i8, i64
func.func @slot_outside_the_guard(%obj: !dsdl.ptr<const !dsdl.object<"vendor.Inner.1.0">>, %buf: !dsdl.ptr<!dsdl.byte>, %available: i64, %ready: i1) -> (i8, i64) {
  %slot = dsdl.local %available : i64 -> !dsdl.ptr<!dsdl.size>
  %r:2 = scf.if %ready -> (i8, i64) {
    %err = dsdl.call_serdes @inner_serialize(%obj, %buf, %slot) {direction = "serialize", member = "inner"} : !dsdl.ptr<const !dsdl.object<"vendor.Inner.1.0">>, !dsdl.ptr<!dsdl.byte>, !dsdl.ptr<!dsdl.size>
    %used = dsdl.load_scalar %slot : !dsdl.ptr<!dsdl.size> -> i64
    scf.yield %err, %used : i8, i64
  } else {
    %none = arith.constant -3 : i8
    scf.yield %none, %available : i8, i64
  }
  return %r#0, %r#1 : i8, i64
}

// A slot the plan never reads back leaves what the call used unconverted and unread.
// CHECK-LABEL: func.func @never_read_back(
// CHECK:       %[[ERR:.*]], %{{.*}} = dsdl.call_serdes_sized @inner_deserialize(%arg0, %arg1, %arg2) {direction = "deserialize", member = "inner"}
// CHECK-NOT:   arith.index_cast
// CHECK:       return %[[ERR]]
func.func @never_read_back(%obj: !dsdl.ptr<!dsdl.object<"vendor.Inner.1.0">>, %buf: !dsdl.ptr<const !dsdl.byte>, %declared: i64) -> i8 {
  %slot = dsdl.local %declared : i64 -> !dsdl.ptr<!dsdl.size>
  %err = dsdl.call_serdes @inner_deserialize(%obj, %buf, %slot) {direction = "deserialize", member = "inner"} : !dsdl.ptr<!dsdl.object<"vendor.Inner.1.0">>, !dsdl.ptr<const !dsdl.byte>, !dsdl.ptr<!dsdl.size>
  return %err : i8
}
