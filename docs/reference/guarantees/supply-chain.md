# Supply chain: the LLVM version lock

**Policy: the LLVM/MLIR major version is locked to 22.**

## Rationale

The emitted bytes are downstream of MLIR and LLVM: MLIR's canonicaliser runs over every
serialisation body, and LLVM's code generator settles the object lane's bytes outright. The same
DSDL compiled against two MLIR majors may therefore produce output that differs while remaining
semantically equivalent, which makes the LLVM major a semantic input to the output rather than a
build detail.

## Enforcement

The lock is enforced **at configure time** by the build system and **again** by CI.

- **Build system:** after `find_package(LLVM/MLIR)`, CMake asserts `LLVM_VERSION_MAJOR` equals
  `LLVMDSDL_REQUIRED_LLVM_MAJOR` (`22`) and fails configuration otherwise.
  `-DLLVMDSDL_ALLOW_LLVM_MAJOR_MISMATCH=ON` downgrades the failure to a warning. MLIR ships with LLVM
  at the same version, so asserting the LLVM major covers MLIR too.
- **Everywhere:** CI and the release link the toolchain this project builds for itself, pinned by
  revision in `packaging/toolchain/llvm.pin` rather than by major alone. Every lane therefore links
  one exact LLVM, not merely one major of it.
- **Hashed-container iteration** is excluded at the source rather than detected afterwards.
  `std::unordered_*` iteration order is a property of the standard library, so a loop over one could
  emit different bytes when built against a different one. `tools/determinism/check_unordered_iteration.py`
  (ctest `llvmdsdl-determinism-unordered-iteration`) rejects any such iteration in the
  generated-output path unless it carries a stated reason why the order cannot reach emitted text.

## Consequences

- Build against the locked major to reproduce released artifacts byte-for-byte.
- To raise the lock: bump `LLVMDSDL_REQUIRED_LLVM_MAJOR` and `packaging/toolchain/llvm.pin` together,
  and let the Toolchain workflow republish.
