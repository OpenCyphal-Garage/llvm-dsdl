// RUN: %dsdl-opt --convert-dsdl-to-emitc %s | FileCheck %s

// Array access, which the production predicate currently holds back but the conversion still
// has to get right. A variable-length array is a member holding a count and element storage,
// so both are reached by a path rather than by a single name.

// CHECK-LABEL: func.func @read_count
func.func @read_count(%obj: !dsdl.ptr<!dsdl.opaque<"const vendor__Msg">>) -> i64 {
  // Two hops: the first leaves a pointer, the rest are within a value.
  // CHECK: "emitc.member_of_ptr"({{.*}}) <{member = "path"}>
  // CHECK: "emitc.member"({{.*}}) <{member = "count"}>
  // CHECK: emitc.load
  %n = dsdl.load_member %obj ["path", "count"] {indices = array<i64: 0, 0>} : !dsdl.ptr<!dsdl.opaque<"const vendor__Msg">> -> i64
  return %n : i64
}

// CHECK-LABEL: func.func @read_element
func.func @read_element(%obj: !dsdl.ptr<!dsdl.opaque<"const vendor__Msg">>, %i: i64) -> i64 {
  // The storage is reached at the qualification it is declared with -- a serializer holds the
  // object by pointer-to-const -- and then taken unqualified, because the element read out of
  // it is assigned to a variable that a const declaration would not survive.
  // CHECK: "emitc.member"({{.*}}) <{member = "elements"}>
  // CHECK: emitc.cast %{{.*}} : !emitc.ptr<!emitc.opaque<"const uint8_t">> to !emitc.ptr<!emitc.opaque<"uint8_t">>
  // CHECK: emitc.subscript
  // The element is addressed at its own width, not the width the plan works in: striding an
  // array of uint8_t by 64 bits would read the wrong bytes entirely.
  // CHECK: emitc.load %{{.*}} : <!emitc.opaque<"uint8_t">>
  // CHECK: emitc.cast %{{.*}} : !emitc.opaque<"uint8_t"> to i64
  %v = dsdl.load_element %obj ["path", "elements"] [%i] {indices = array<i64: 0, 0>, element_type = "const uint8_t"} : !dsdl.ptr<!dsdl.opaque<"const vendor__Msg">> -> i64
  return %v : i64
}

// CHECK-LABEL: func.func @write_element
func.func @write_element(%obj: !dsdl.ptr<!dsdl.opaque<"vendor__Msg">>, %i: i64, %v: i64) {
  // CHECK: emitc.subscript
  // CHECK: emitc.cast %{{.*}} : i64 to !emitc.opaque<"uint8_t">
  // CHECK: emitc.assign
  dsdl.store_element %obj ["path", "elements"] [%i], %v {indices = array<i64: 0, 0>, element_type = "uint8_t"} : !dsdl.ptr<!dsdl.opaque<"vendor__Msg">>, i64
  return
}

// A wider element keeps its own spelling, which is what the stride depends on.
// CHECK-LABEL: func.func @read_wide_element
func.func @read_wide_element(%obj: !dsdl.ptr<!dsdl.opaque<"const vendor__Wide">>, %i: i64) -> i64 {
  // CHECK: emitc.load %{{.*}} : <!emitc.opaque<"int16_t">>
  %v = dsdl.load_element %obj ["value", "elements"] [%i] {indices = array<i64: 0, 0>, element_type = "const int16_t"} : !dsdl.ptr<!dsdl.opaque<"const vendor__Wide">> -> i64
  return %v : i64
}
