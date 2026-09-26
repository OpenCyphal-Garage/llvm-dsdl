// RUN: not %dsdl-opt --split-input-file --dsdl-fold-body-sizes %s 2>&1 | FileCheck %s

// A body that writes its size back other than once, or from inside a loop, is not one the fold
// recognises. It refuses rather than leave a body its target has no spelling for.

// CHECK: error: 'dsdl.store_scalar' op handles the size its body is handed other than by reading it and writing it back once
func.func @written_twice(%obj: !dsdl.ptr<const !dsdl.object<"vendor.Msg.1.0">>, %buf: !dsdl.ptr<!dsdl.byte>, %size: !dsdl.ptr<!dsdl.size>) -> i8 attributes {llvmdsdl.plan_body = "serialize"} {
  %zero = arith.constant 0 : i64
  %ok = arith.constant 0 : i8
  dsdl.store_scalar %size, %zero : !dsdl.ptr<!dsdl.size>, i64
  dsdl.store_scalar %size, %zero : !dsdl.ptr<!dsdl.size>, i64
  return %ok : i8
}

// -----

// CHECK: error: 'scf.for' op holds the write of its body's size, which only an scf.if may
func.func @written_in_a_loop(%obj: !dsdl.ptr<const !dsdl.object<"vendor.Msg.1.0">>, %buf: !dsdl.ptr<!dsdl.byte>, %size: !dsdl.ptr<!dsdl.size>) -> i8 attributes {llvmdsdl.plan_body = "serialize"} {
  %ok = arith.constant 0 : i8
  %from = arith.constant 0 : index
  %to = arith.constant 2 : index
  %step = arith.constant 1 : index
  %call = func.call @measure() : () -> i64
  scf.for %i = %from to %to step %step {
    dsdl.store_scalar %size, %call : !dsdl.ptr<!dsdl.size>, i64
  }
  return %ok : i8
}

func.func private @measure() -> i64
