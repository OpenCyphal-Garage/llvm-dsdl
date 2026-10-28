# Golden record

One row per golden, in the order reviewed. A golden is what a pinned peer generator writes for a
definition, produced by `tools/golden/harvest.py` inside the toolshed image so the bytes are the
same on every machine, or a file written by hand. The project owner decides each row; the
decision and the notes are his words, and he edits the row when the record is wrong.

| # | Definition | Language | Files | Produced by | Date | Decision | Notes |
|---|---|---|---|---|---|---|---|
| 1 | `uavcan.node.Heartbeat.1.0` | C | `nunavut/c/uavcan/node/Heartbeat_1_0.h`, with `Health_1_0.h`, `Mode_1_0.h` and `nunavut/support/serialization.h` beside it | Nunavut 3.0.1b1 (`37095ab`), pydsdl 1.23.0 (`a1506ce`), Python 3.14.4 in `toolshed:ts26.4.3` | 2026-10-17 | deferred | Nunavut has unfinished work; it is fixed first and C comes back after. The candidate is not committed; `tools/golden/harvest.py --language c --definition node/7509.Heartbeat.1.0.dsdl` regenerates it. |
| 2 | `uavcan.node.Heartbeat.1.0` | Rust | `canadensis/rust/uavcan.node.Heartbeat.1.0.rs`, one file holding `uavcan::node::{health_1_0, heartbeat_1_0, mode_1_0}` | `canadensis_codegen_rust` 0.6.3 over a root holding Heartbeat, Health and Mode, `--rustfmt`, rustc 1.93.1 in `toolshed:ts26.4.3` | 2026-10-17 | parked | Not approved. The candidate is committed on branch `golden/rust-heartbeat` so it is in source; the row is decided when that branch is reviewed. |
