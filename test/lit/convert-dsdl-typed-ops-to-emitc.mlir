// RUN: %dsdl-opt --convert-dsdl-to-emitc %s | FileCheck %s

// The operations a typed serialisation plan is made of. The runtime spells one primitive per
// value shape rather than one generic call, so the conversion selects on value type, width and
// signedness together, and these cases pin that selection.

// An unsigned field of a non-standard width goes through the width-carrying primitive, and the
// width travels as an argument.
// CHECK-LABEL: func.func @write_unsigned
func.func @write_unsigned(%buf: !dsdl.ptr<i8>, %cap: i64, %off: i64, %v: i64) -> i8 {
  // CHECK: %[[W:.*]] = "emitc.constant"() <{value = 13 : i8}>
  // CHECK: emitc.call_opaque "dsdl_runtime_set_uxx"(%{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %[[W]])
  %e = dsdl.write_bits %buf[%off], %v, size %cap {width = 13 : i64} : !dsdl.ptr<i8>, i64
  return %e : i8
}

// Signedness is an attribute because MLIR integers are signless and the wire encoding is not.
// CHECK-LABEL: func.func @write_signed
func.func @write_signed(%buf: !dsdl.ptr<i8>, %cap: i64, %off: i64, %v: i64) -> i8 {
  // CHECK: emitc.call_opaque "dsdl_runtime_set_ixx"
  %e = dsdl.write_bits %buf[%off], %v, size %cap {width = 9 : i64, is_signed} : !dsdl.ptr<i8>, i64
  return %e : i8
}

// A single unsigned bit has its own primitive, which takes no width.
// CHECK-LABEL: func.func @write_bit
func.func @write_bit(%buf: !dsdl.ptr<i8>, %cap: i64, %off: i64, %v: i1) -> i8 {
  // CHECK: emitc.call_opaque "dsdl_runtime_set_bit"(%{{.*}}, %{{.*}}, %{{.*}})
  // CHECK-NOT: dsdl_runtime_set_uxx
  %e = dsdl.write_bits %buf[%off], %v, size %cap {width = 1 : i64} : !dsdl.ptr<i8>, i1
  return %e : i8
}

// Floats are selected by their own bit width, not by the field width.
// CHECK-LABEL: func.func @write_float
func.func @write_float(%buf: !dsdl.ptr<i8>, %cap: i64, %off: i64, %v: f32) -> i8 {
  // CHECK: emitc.call_opaque "dsdl_runtime_set_f32"
  %e = dsdl.write_bits %buf[%off], %v, size %cap {width = 32 : i64} : !dsdl.ptr<i8>, f32
  return %e : i8
}

// A read answers in a concrete width, so the primitive is the smallest standard integer that
// holds the field rather than the field width itself.
// CHECK-LABEL: func.func @read_narrow
func.func @read_narrow(%buf: !dsdl.ptr<i8>, %cap: i64, %off: i64) -> i16 {
  // CHECK: emitc.call_opaque "dsdl_runtime_get_u16"
  %v = dsdl.read_bits %buf[%off], size %cap {width = 12 : i64} : !dsdl.ptr<i8> -> i16
  return %v : i16
}

// CHECK-LABEL: func.func @read_float
func.func @read_float(%buf: !dsdl.ptr<i8>, %cap: i64, %off: i64) -> f64 {
  // CHECK: emitc.call_opaque "dsdl_runtime_get_f64"
  %v = dsdl.read_bits %buf[%off], size %cap {width = 64 : i64} : !dsdl.ptr<i8> -> f64
  return %v : f64
}
