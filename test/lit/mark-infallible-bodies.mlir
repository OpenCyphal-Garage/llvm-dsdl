// RUN: %dsdl-opt --dsdl-mark-infallible-bodies %s | FileCheck %s

// A body whose every return answers an error of zero has no error to report, and is marked so a
// backend whose idiom reports an error apart from the result reads that rather than deriving it again.

// CHECK-LABEL: func.func @serialize_always_succeeds(
// CHECK-SAME:  llvmdsdl.infallible
func.func @serialize_always_succeeds() -> i8 attributes {llvmdsdl.plan_body = "serialize"} {
  %zero = arith.constant 0 : i8
  return %zero : i8
}

// CHECK-LABEL: func.func @setter_always_succeeds(
// CHECK-SAME:  llvmdsdl.infallible
func.func @setter_always_succeeds() -> i8 attributes {llvmdsdl.plan_body = "set"} {
  %zero = arith.constant 0 : i8
  return %zero : i8
}

// A body that answers the size it used answers its error first.
// CHECK-LABEL: func.func @sized_always_succeeds(
// CHECK-SAME:  llvmdsdl.infallible
func.func @sized_always_succeeds() -> (i8, index) attributes {llvmdsdl.plan_body = "serialize"} {
  %zero = arith.constant 0 : i8
  %used = arith.constant 7 : index
  return %zero, %used : i8, index
}

// A body that can answer anything else can fail.
// CHECK-LABEL: func.func @deserialize_may_fail(
// CHECK-NOT:   llvmdsdl.infallible
// CHECK:       return
func.func @deserialize_may_fail(%rejected: i1) -> i8 attributes {llvmdsdl.plan_body = "deserialize"} {
  %invalid = arith.constant -2 : i8
  %zero = arith.constant 0 : i8
  %err = arith.select %rejected, %invalid, %zero : i8
  return %err : i8
}

// A getter answers a value, and a zero it answers is the member's value rather than success.
// CHECK-LABEL: func.func @getter_answers_a_value(
// CHECK-NOT:   llvmdsdl.infallible
// CHECK:       return
func.func @getter_answers_a_value() -> i8 attributes {llvmdsdl.plan_body = "get"} {
  %zero = arith.constant 0 : i8
  return %zero : i8
}

// A helper is not a body; what it answers is its caller's to interpret.
// CHECK-LABEL: func.func @helper(
// CHECK-NOT:   llvmdsdl.infallible
// CHECK:       return
func.func @helper() -> i8 {
  %zero = arith.constant 0 : i8
  return %zero : i8
}

// A mark the body no longer earns is taken away, so the pass can run again after a change.
// CHECK-LABEL: func.func @initialize_no_longer_infallible(
// CHECK-NOT:   llvmdsdl.infallible
// CHECK:       return
func.func @initialize_no_longer_infallible() -> i8 attributes {llvmdsdl.infallible, llvmdsdl.plan_body = "initialize"} {
  %too_short = arith.constant -3 : i8
  return %too_short : i8
}
