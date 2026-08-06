# Security policy

## Reporting a vulnerability

Report privately through
[GitHub Security Advisories](https://github.com/OpenCyphal-Garage/llvm-dsdl/security/advisories/new).
That opens a channel visible only to you and the maintainers.

Please do not open a public issue for a suspected vulnerability. A public report
on a compiler is a public report on everything it has compiled.

Include what you would want if you were fixing it: the DSDL or the input that
triggers it, the tool and version (`dsdlc --version`), the platform, and what
you observed. A reproducer beats a description.

This is a pre-1.0 project maintained by volunteers, so expect an acknowledgement
within a week rather than within a day. If a report goes unacknowledged for two
weeks, escalate on the
[OpenCyphal forum](https://forum.opencyphal.org/) without describing the issue,
and someone will find the report.

## Supported versions

The latest release. Fixes land on `main` and go out in the next release; there
are no maintenance branches to back-port to.

## Scope

The interesting surface of a DSDL compiler is that it reads files it did not
write and emits code somebody else will run.

**In scope**

- **The frontend reading untrusted DSDL.** A definition that causes memory
  corruption, an unbounded allocation, or a crash that is not a diagnostic.
  Malformed input must produce an error, not a signal.
- **Generated serialisation code.** A decoder that reads outside its buffer, or
  accepts a frame it should reject, on malformed or hostile wire data. This is
  the highest-severity class here: the defect ships to every consumer of the
  generated code rather than staying in the compiler.
- **`dsdld`.** It reads LSP over stdio, opens files by client-supplied path, and
  loads lint plugins with `dlopen`. Path traversal outside the configured roots,
  or plugin loading that can be steered by a document, is in scope.
- **The release artifacts.** A `.deb` or tarball whose contents do not match
  what the workflow built, or a build step that could be made to embed something
  the source does not contain.

**Out of scope**

- Defects in the DSDL corpus under `submodules/`, which is upstream — report
  those to the project that owns it.
- Defects in the CI toolshed image, likewise upstream.
- Running `dsdlc` on DSDL you do not trust and getting code you do not trust.
  Generating code is executing the definitions' intent; a definition that
  declares a 64 KiB array gets one. Trust boundaries belong around the
  definitions you compile.
- Resource exhaustion from deliberately pathological input, unless it escapes
  the bounds the compiler states — nesting depth is capped at 128, and a
  definition exceeding it is a diagnostic.

## What you can verify yourself

Release artifacts carry a provenance attestation and a CycloneDX SBOM recording
the exact source commit and the LLVM revision linked. The LLVM the tools link is
built by this repository from a revision pinned in
`packaging/toolchain/llvm.pin`, rather than taken from a distribution, so what
went into a binary is answerable from the tree that produced it. See
[docs/reference/guarantees/supply-chain.md](docs/reference/guarantees/supply-chain.md).
