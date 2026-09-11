// RUN: %dsdl-opt --convert-dsdl-to-emitc %s | FileCheck %s

// The bulk bit moves, which a bool array uses: it is stored bitpacked rather than as
// elements, so the whole array travels in one run whose length is its count.
//
// The two directions are not mirror images. Writing past the end of the buffer is an error;
// reading past it zero-extends, which is the tolerance a deserialiser is required to have. So
// the read takes the buffer's size and the write does not, and they lower to different
// primitives.

// CHECK-LABEL: func.func @pack
// CHECK-SAME: (%[[OBJ:.*]]: !emitc.ptr<!emitc.opaque<"const uint8_t">>, %[[BUF:.*]]: !emitc.ptr<!emitc.opaque<"uint8_t">>
func.func @pack(%obj: !dsdl.ptr<const !dsdl.byte>,
                %buf: !dsdl.ptr<!dsdl.byte>,
                %off: i64, %count: i64) {
  %zero = arith.constant 0 : i64
  // Destination first, as an assignment reads.
  // CHECK: emitc.call_opaque "dsdl_runtime_copy_bits"(%[[BUF]], %{{.*}}, %{{.*}}, %[[OBJ]], %{{.*}})
  dsdl.bit_write %buf[%off], %count, %obj[%zero] : !dsdl.ptr<!dsdl.byte>, !dsdl.ptr<const !dsdl.byte>
  return
}

// CHECK-LABEL: func.func @unpack
// CHECK-SAME: (%[[OUT:.*]]: !emitc.ptr<!emitc.opaque<"uint8_t">>, %[[SRC:.*]]: !emitc.ptr<!emitc.opaque<"const uint8_t">>
func.func @unpack(%out: !dsdl.ptr<!dsdl.byte>,
                  %buf: !dsdl.ptr<const !dsdl.byte>,
                  %cap: i64, %off: i64, %count: i64) {
  // The capacity travels with the buffer: the primitive needs it to know where the wire ends
  // and the zero-extension begins.
  // CHECK: emitc.call_opaque "dsdl_runtime_get_bits"(%[[OUT]], %[[SRC]], %{{.*}}, %{{.*}}, %{{.*}})
  // CHECK-NOT: dsdl_runtime_copy_bits
  dsdl.bit_read %out, %buf[%off], %count, size %cap : !dsdl.ptr<!dsdl.byte>, !dsdl.ptr<const !dsdl.byte>
  return
}
