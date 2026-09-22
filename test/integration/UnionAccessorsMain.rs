//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// The C probe's reading of an equal-length union, in Rust.
//
//===----------------------------------------------------------------------===//

use llvmdsdl_generated::fixtures_union::vendor::choice_1_0::Choice as Choice;
fn main() { let mut wire = [1u8, 0, 0, 0, 0]; wire[1..5].copy_from_slice(&5.5f32.to_le_bytes());
  let ok = Choice::get_tag_(&wire) == 1 && Choice::get_real(&wire) == 5.5 && Choice::get_quad(&wire).len() == 4 && Choice::set_tag_(&mut wire, 0) == Ok(());
  println!("union-accessors Rust: {}", if ok { "ok" } else { "FAILED" }); std::process::exit(if ok { 0 } else { 1 }); }
