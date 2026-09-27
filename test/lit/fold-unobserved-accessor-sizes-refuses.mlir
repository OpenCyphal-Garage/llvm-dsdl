// RUN: not %dsdl-opt --dsdl-fold-unobserved-accessor-sizes %s 2>&1 | FileCheck %s

// A getter that reads back the size it writes is not one the fold recognises. It refuses rather
// than leave a parameter the target's signature does not have.

// CHECK: error: 'dsdl.load_scalar' op reads back the size a getter answering a view writes
func.func @reads_back(%buf: !dsdl.ptr<const !dsdl.byte>, %size: i64, %out: !dsdl.ptr<!dsdl.size>) -> !dsdl.ptr<const !dsdl.byte> attributes {llvmdsdl.member = "nested", llvmdsdl.plan_body = "get"} {
  dsdl.store_scalar %out, %size : !dsdl.ptr<!dsdl.size>, i64
  %back = dsdl.load_scalar %out : !dsdl.ptr<!dsdl.size> -> i64
  %view = dsdl.buffer_at %buf[%back] : !dsdl.ptr<const !dsdl.byte> -> !dsdl.ptr<const !dsdl.byte>
  return %view : !dsdl.ptr<const !dsdl.byte>
}
