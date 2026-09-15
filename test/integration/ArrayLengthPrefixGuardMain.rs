//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//! Runs the array-length prefix guard cases on the host: one line per case and a summary
//! line the lane parses.

use std::process::ExitCode;

use llvmdsdl_array_length_prefix_guard::{run_all, Verdict};

fn main() -> ExitCode {
    let outcomes = run_all();
    let mut passed = 0usize;
    let mut skipped = 0usize;
    let mut failed = 0usize;
    for outcome in &outcomes {
        match &outcome.verdict {
            Verdict::Passed => {
                passed += 1;
                println!("PASS {} prefix={}", outcome.case, outcome.prefix);
            }
            Verdict::Skipped(reason) => {
                skipped += 1;
                println!("SKIP {} prefix={} {reason}", outcome.case, outcome.prefix);
            }
            Verdict::Failed(detail) => {
                failed += 1;
                println!("FAIL {} prefix={}: {detail}", outcome.case, outcome.prefix);
            }
        }
    }
    let status = if failed == 0 { "PASS" } else { "FAIL" };
    println!(
        "{status} rust-array-length-prefix-guard cases={} passed={passed} skipped={skipped} failed={failed}",
        outcomes.len()
    );
    if failed == 0 {
        ExitCode::SUCCESS
    } else {
        ExitCode::FAILURE
    }
}
