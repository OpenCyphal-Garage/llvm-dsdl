// RUN: %dsdl-opt --convert-dsdl-to-emitc %s | FileCheck %s

// The operations a typed serialization plan is made of. The runtime spells one primitive per
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

// Member access keeps the published signature: the composite carries the spelling the header
// already declares, so the emitted definition matches the emitted declaration. `member_of_ptr`
// needs an lvalue holding the pointer, which a function parameter is not, hence the slot.
// CHECK-LABEL: func.func @member_round_trip
// CHECK-SAME: (%[[OBJ:.*]]: !emitc.ptr<!emitc.opaque<"vendor__Widget">>
func.func @member_round_trip(%obj: !dsdl.ptr<!dsdl.opaque<"vendor__Widget">>, %v: i64) -> i64 {
  // CHECK: %[[SLOT:.*]] = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.ptr<!emitc.opaque<"vendor__Widget">>>
  // CHECK: emitc.assign %[[OBJ]] : {{.*}} to %[[SLOT]]
  // CHECK: %[[M:.*]] = "emitc.member_of_ptr"(%[[SLOT]]) <{member = "foo"}>
  // CHECK: emitc.load %[[M]]
  %got = dsdl.load_member %obj ["foo"] {indices = array<i64: 0>} : !dsdl.ptr<!dsdl.opaque<"vendor__Widget">> -> i64
  // CHECK: "emitc.member_of_ptr"({{.*}}) <{member = "bar"}>
  // CHECK: emitc.assign
  dsdl.store_member %obj ["bar"], %v {indices = array<i64: 1>} : !dsdl.ptr<!dsdl.opaque<"vendor__Widget">>, i64
  return %got : i64
}
