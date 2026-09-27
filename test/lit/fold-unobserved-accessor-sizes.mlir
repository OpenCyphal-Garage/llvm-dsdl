// RUN: %dsdl-opt --dsdl-fold-unobserved-accessor-sizes %s | FileCheck %s

// A composite getter answers a pointer to the nested type's bytes and writes their length through
// its last argument. For a target whose getter answers a view carrying that length, the write, what
// computed it and the parameter it went through are erased.

// CHECK-LABEL: func.func @get_nested(
// CHECK-SAME:    %arg0: !dsdl.ptr<const !dsdl.byte>, %arg1: i64) -> !dsdl.ptr<const !dsdl.byte>
// CHECK-NOT:   dsdl.store_scalar
// CHECK-NOT:   arith.subi
// CHECK:       dsdl.buffer_at
func.func @get_nested(%buf: !dsdl.ptr<const !dsdl.byte>, %size: i64, %out: !dsdl.ptr<!dsdl.size>) -> !dsdl.ptr<const !dsdl.byte> attributes {llvmdsdl.member = "nested", llvmdsdl.plan_body = "get"} {
  %at = arith.constant 20 : i64
  %fits = arith.cmpi uge, %size, %at : i64
  %from = arith.select %fits, %at, %size : i64
  %left = arith.subi %size, %from : i64
  dsdl.store_scalar %out, %left : !dsdl.ptr<!dsdl.size>, i64
  %view = dsdl.buffer_at %buf[%from] : !dsdl.ptr<const !dsdl.byte> -> !dsdl.ptr<const !dsdl.byte>
  return %view : !dsdl.ptr<const !dsdl.byte>
}

// A scalar getter answers a value and has no size to write back.
// CHECK-LABEL: func.func @get_scalar(
// CHECK-SAME:    %arg0: !dsdl.ptr<const !dsdl.byte>, %arg1: i64) -> i64
func.func @get_scalar(%buf: !dsdl.ptr<const !dsdl.byte>, %size: i64) -> i64 attributes {llvmdsdl.member = "value", llvmdsdl.plan_body = "get"} {
  %at = arith.constant 0 : i64
  %value = dsdl.read_bits %buf[%at], size %size {width = 8 : i64} : !dsdl.ptr<const !dsdl.byte> -> i64
  return %value : i64
}
