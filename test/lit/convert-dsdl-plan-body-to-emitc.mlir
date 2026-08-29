// RUN: %dsdl-opt --convert-dsdl-to-emitc %s | FileCheck %s

// A whole serialization plan body carried as operations rather than as C text, to pin the
// shape the producer has to build: the published signature spelled exactly, the argument
// checks, the size read through a pointer, and the scalar write.
//
// Control flow is structured because it has to be. There is no cf-to-emitc conversion, so the
// C path cannot take a branch graph, and the early returns of the hand-written text become
// nested `scf.if` yielding the error code.

// CHECK-LABEL: func.func @widget_serialize
// The signature survives the conversion, which is what lets the definition match the
// declaration the header already publishes.
// CHECK-SAME: !emitc.ptr<!emitc.opaque<"const fixtures__vendor__Widget">>
// CHECK-SAME: !emitc.ptr<!emitc.opaque<"uint8_t">>
// CHECK-SAME: !emitc.ptr<!emitc.opaque<"size_t">>

// CHECK: %[[NULL:.*]] = "emitc.constant"() <{value = #emitc.opaque<"NULL">}>
// CHECK: emitc.cmp eq
// The size arrives by pointer and is read through a zero subscript, then cast from the
// target's spelling into the width the plan works in.
// CHECK: emitc.subscript
// CHECK: emitc.load
// CHECK: emitc.cast
// CHECK: "emitc.member_of_ptr"({{.*}}) <{member = "foo"}>
// CHECK: emitc.call_opaque "dsdl_runtime_set_uxx"
// Writing the consumed size back casts in the other direction.
// CHECK: emitc.cast
// CHECK: emitc.assign
// CHECK-NOT: dsdl.

module {
  func.func private @capacity_check(i64) -> i8
  func.func private @scalar_ser_0(i64) -> i64
  func.func @widget_serialize(
      %obj: !dsdl.ptr<!dsdl.opaque<"const fixtures__vendor__Widget">>,
      %buf: !dsdl.ptr<!dsdl.opaque<"uint8_t">>,
      %sz:  !dsdl.ptr<!dsdl.opaque<"size_t">>) -> i8 {
    %c0   = arith.constant 0 : i64
    %c8   = arith.constant 8 : i64
    %ok   = arith.constant 0 : i8
    %inval = arith.constant -2 : i8

    %n1 = dsdl.is_null %obj : !dsdl.ptr<!dsdl.opaque<"const fixtures__vendor__Widget">>
    %n2 = dsdl.is_null %buf : !dsdl.ptr<!dsdl.opaque<"uint8_t">>
    %n3 = dsdl.is_null %sz  : !dsdl.ptr<!dsdl.opaque<"size_t">>
    %a  = arith.ori %n1, %n2 : i1
    %anynull = arith.ori %a, %n3 : i1

    %res = scf.if %anynull -> (i8) {
      scf.yield %inval : i8
    } else {
      %cap = dsdl.load_scalar %sz : !dsdl.ptr<!dsdl.opaque<"size_t">> -> i64
      %capbits = arith.muli %cap, %c8 : i64
      %cerr = func.call @capacity_check(%capbits) : (i64) -> i8
      %bad = arith.cmpi slt, %cerr, %ok : i8
      %r = scf.if %bad -> (i8) {
        scf.yield %cerr : i8
      } else {
        %foo = dsdl.load_member %obj ["foo"] {indices = array<i64: 0>} : !dsdl.ptr<!dsdl.opaque<"const fixtures__vendor__Widget">> -> i64
        %norm = func.call @scalar_ser_0(%foo) : (i64) -> i64
        %werr = dsdl.write_bits %buf[%c0], %norm, size %cap {width = 8 : i64} : !dsdl.ptr<!dsdl.opaque<"uint8_t">>, i64
        %wbad = arith.cmpi slt, %werr, %ok : i8
        %r2 = scf.if %wbad -> (i8) {
          scf.yield %werr : i8
        } else {
          %consumed = arith.constant 1 : i64
          dsdl.store_scalar %sz, %consumed : !dsdl.ptr<!dsdl.opaque<"size_t">>, i64
          scf.yield %ok : i8
        }
        scf.yield %r2 : i8
      }
      scf.yield %r : i8
    }
    return %res : i8
  }
}
