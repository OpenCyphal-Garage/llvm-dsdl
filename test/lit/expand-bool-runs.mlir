// RUN: %dsdl-opt --dsdl-expand-bool-runs %s | FileCheck %s --check-prefixes=CHECK,EACH
// RUN: %dsdl-opt '--dsdl-expand-bool-runs=storage=packed-when-fixed' %s | FileCheck %s --check-prefixes=CHECK,FIXED
// RUN: %dsdl-opt '--dsdl-expand-bool-runs=storage=packed' %s | FileCheck %s --check-prefixes=CHECK,PACKED

// The plan moves a bool array as one run between the buffer and the array's packed bytes. For a
// target that stores a bool per element, the run becomes a loop moving one element and one bit per
// turn. A target that packs a fixed array and not a variable one has only the variable run
// expanded; a target that packs both has neither.

// CHECK-LABEL: func.func @write_runs(
// EACH:        scf.for %[[TURN:.*]] =
// EACH-NEXT:     %[[I:.*]] = arith.index_cast %[[TURN]] : index to i64
// EACH-NEXT:     %[[AT:.*]] = arith.addi %arg2, %[[I]] : i64
// EACH-NEXT:     %[[BIT:.*]] = dsdl.load_element %arg0 "flags"[%[[I]]] {storage_bits = 8 : i64, storage_category = "bool"}
// EACH-NEXT:     dsdl.write_bit %arg1[%[[AT]]], %[[BIT]]
// EACH:        scf.for
// EACH:          dsdl.load_element %arg0 "fixed"
// EACH:          dsdl.write_bit
// EACH-NOT:    dsdl.bit_write
// EACH-NOT:    dsdl.element_addr
// FIXED:       scf.for
// FIXED:         dsdl.load_element %arg0 "flags"
// FIXED:       %[[FIXED:.*]] = dsdl.element_addr %arg0 "fixed"
// FIXED-NEXT:  dsdl.bit_write %arg1[%arg2], %{{.*}}, %[[FIXED]][%{{.*}}] :
// PACKED-NOT:  scf.for
// PACKED:      dsdl.bit_write {{.*}} {variable_length}
// PACKED:      dsdl.bit_write
// CHECK:       return
func.func @write_runs(%obj: !dsdl.ptr<const !dsdl.object<"vendor.Flags.1.0">>, %buf: !dsdl.ptr<!dsdl.byte>, %at: i64, %count: i64) -> i8 attributes {llvmdsdl.plan_body = "serialize"} {
  %zero = arith.constant 0 : i64
  %three = arith.constant 3 : i64
  %ok = arith.constant 0 : i8
  %flags = dsdl.element_addr %obj "flags"[%zero] {storage_bits = 8 : i64, storage_category = "bool"} : !dsdl.ptr<const !dsdl.object<"vendor.Flags.1.0">> -> !dsdl.ptr<const !dsdl.byte>
  dsdl.bit_write %buf[%at], %count, %flags[%zero] {variable_length} : !dsdl.ptr<!dsdl.byte>, !dsdl.ptr<const !dsdl.byte>
  %fixed = dsdl.element_addr %obj "fixed"[%zero] {storage_bits = 8 : i64, storage_category = "bool"} : !dsdl.ptr<const !dsdl.object<"vendor.Flags.1.0">> -> !dsdl.ptr<const !dsdl.byte>
  dsdl.bit_write %buf[%at], %three, %fixed[%zero] : !dsdl.ptr<!dsdl.byte>, !dsdl.ptr<const !dsdl.byte>
  return %ok : i8
}

// Reading, each bit read goes to the element the run's turn reaches.
// CHECK-LABEL: func.func @read_runs(
// EACH:        scf.for %[[TURN:.*]] =
// EACH-NEXT:     %[[I:.*]] = arith.index_cast %[[TURN]] : index to i64
// EACH-NEXT:     %[[AT:.*]] = arith.addi %arg3, %[[I]] : i64
// EACH-NEXT:     %[[BIT:.*]] = dsdl.read_bit %arg1[%[[AT]]], size %arg2
// EACH-NEXT:     dsdl.store_element %arg0 "flags"[%[[I]]], %[[BIT]] {storage_bits = 8 : i64, storage_category = "bool"}
// EACH-NOT:    dsdl.bit_read
// FIXED:       dsdl.read_bit
// FIXED:       dsdl.bit_read
// PACKED-NOT:  dsdl.read_bit
// CHECK:       return
func.func @read_runs(%obj: !dsdl.ptr<!dsdl.object<"vendor.Flags.1.0">>, %buf: !dsdl.ptr<const !dsdl.byte>, %size: i64, %at: i64, %count: i64) -> i8 attributes {llvmdsdl.plan_body = "deserialize"} {
  %zero = arith.constant 0 : i64
  %three = arith.constant 3 : i64
  %ok = arith.constant 0 : i8
  %flags = dsdl.element_addr %obj "flags"[%zero] {storage_bits = 8 : i64, storage_category = "bool"} : !dsdl.ptr<!dsdl.object<"vendor.Flags.1.0">> -> !dsdl.ptr<!dsdl.byte>
  dsdl.bit_read %flags, %buf[%at], %count, size %size {variable_length} : !dsdl.ptr<!dsdl.byte>, !dsdl.ptr<const !dsdl.byte>
  %fixed = dsdl.element_addr %obj "fixed"[%zero] {storage_bits = 8 : i64, storage_category = "bool"} : !dsdl.ptr<!dsdl.object<"vendor.Flags.1.0">> -> !dsdl.ptr<!dsdl.byte>
  dsdl.bit_read %fixed, %buf[%at], %three, size %size : !dsdl.ptr<!dsdl.byte>, !dsdl.ptr<const !dsdl.byte>
  return %ok : i8
}

// An initialise body fills a packed array from nothing, and its run is left as it is.
// CHECK-LABEL: func.func @initialise(
// CHECK-NOT:   scf.for
// CHECK:       dsdl.bit_read
func.func @initialise(%obj: !dsdl.ptr<!dsdl.object<"vendor.Flags.1.0">>, %nothing: !dsdl.ptr<!dsdl.byte>) -> i8 attributes {llvmdsdl.plan_body = "initialize"} {
  %zero = arith.constant 0 : i64
  %three = arith.constant 3 : i64
  %ok = arith.constant 0 : i8
  %fixed = dsdl.element_addr %obj "fixed"[%zero] {storage_bits = 8 : i64, storage_category = "bool"} : !dsdl.ptr<!dsdl.object<"vendor.Flags.1.0">> -> !dsdl.ptr<!dsdl.byte>
  dsdl.bit_read %fixed, %nothing[%zero], %three, size %zero : !dsdl.ptr<!dsdl.byte>, !dsdl.ptr<!dsdl.byte>
  return %ok : i8
}
