// RUN: %dsdl-opt --dsdl-mark-unread-arguments %s | FileCheck %s

// Each argument nothing in its function reads is marked `llvmdsdl.unread`; a mark on an argument
// that is read is removed. A declaration has no body to read anything, and is left as it is.

// CHECK-LABEL: func.func @helper(
// CHECK-SAME:    %arg0: i64 {llvmdsdl.unread}, %arg1: i64) -> i64
// CHECK-NOT:   llvmdsdl.unread
func.func @helper(%ignored: i64, %used: i64 {llvmdsdl.unread}) -> i64 {
  return %used : i64
}

// CHECK-LABEL: func.func private @declared(i64) -> i64
func.func private @declared(i64) -> i64
