// RUN: not %dsdl-opt --dsdl-fold-nested-call-sizes %s 2>&1 | FileCheck %s

// A size local read before the call as well as after it is not a shape the fold recognises. It
// refuses rather than folds, since the target it runs for has no spelling for the call it leaves.

// CHECK: error: 'dsdl.call_serdes' op shares its size local with something other than a read after it
func.func @read_before(%obj: !dsdl.ptr<const !dsdl.object<"vendor.Inner.1.0">>, %buf: !dsdl.ptr<!dsdl.byte>, %available: i64) -> (i8, i64) {
  %slot = dsdl.local %available : i64 -> !dsdl.ptr<!dsdl.size>
  %before = dsdl.load_scalar %slot : !dsdl.ptr<!dsdl.size> -> i64
  %err = dsdl.call_serdes @inner_serialize(%obj, %buf, %slot) {direction = "serialize", member = "inner"} : !dsdl.ptr<const !dsdl.object<"vendor.Inner.1.0">>, !dsdl.ptr<!dsdl.byte>, !dsdl.ptr<!dsdl.size>
  return %err, %before : i8, i64
}
