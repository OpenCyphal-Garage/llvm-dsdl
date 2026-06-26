# Backend Overview

`dsdlc` shares one frontend/semantic pipeline and dispatches to multiple backend emitters.

## Targets

- `c`: C headers + implementation
- `cpp`: C++ model/runtime outputs (`std`, `pmr`, `autosar`)
- `rust`: crate/module output with profile/runtime controls
- `go`: module/package output
- `ts`: typed model + runtime helpers
- `python`: package + runtime loader/specialization
- `obj`: compiled `.o` + optional `.a`

## Shared principles

- One semantic interpretation per DSDL source set
- Contract-validated lowering boundaries
- Deterministic file planning
- Backend parity coverage in CI

## Object code path

The `obj` lane runs executable lowering and emits compiled artifacts via host compiler toolchains.

See [Object Backend](object.md) for endianness, ABI, and artifact details.
