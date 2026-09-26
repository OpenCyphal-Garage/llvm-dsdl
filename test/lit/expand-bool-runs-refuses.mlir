// RUN: not %dsdl-opt --dsdl-expand-bool-runs %s 2>&1 | FileCheck %s

// A run whose storage is not an element of a bool array is not one the expansion recognises. It
// refuses rather than leave a run the target it runs for has no spelling for.

// CHECK: error: 'dsdl.bit_write' op moves a run whose storage is not an element of a bool array
func.func @not_a_bool_array(%obj: !dsdl.ptr<const !dsdl.object<"vendor.Flags.1.0">>, %buf: !dsdl.ptr<!dsdl.byte>, %bytes: !dsdl.ptr<const !dsdl.byte>, %count: i64) -> i8 attributes {llvmdsdl.plan_body = "serialize"} {
  %zero = arith.constant 0 : i64
  %ok = arith.constant 0 : i8
  dsdl.bit_write %buf[%zero], %count, %bytes[%zero] {variable_length} : !dsdl.ptr<!dsdl.byte>, !dsdl.ptr<const !dsdl.byte>
  return %ok : i8
}
