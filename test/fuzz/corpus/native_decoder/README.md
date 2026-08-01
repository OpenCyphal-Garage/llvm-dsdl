# Native-decoder fuzz corpus

Committed seed + regression corpus for the `llvmdsdl-native-decoder-fuzz` lane
(see [`test/integration/NativeDecoderFuzz.c`](../../../integration/NativeDecoderFuzz.c)
and [`RunNativeDecoderFuzz.cmake`](../../../integration/RunNativeDecoderFuzz.cmake)).

Every file here is a raw libFuzzer input: **the first byte selects the generated
decoder** (`byte % 7` → Heartbeat, Health, Integer8, Frame, ExecuteCommand
Request, ExecuteCommand Response, port.List) and the remainder is the untrusted
payload fed to that type's `deserialize_`.

## Contents

- **Regression reproducers.** When the fuzz lane finds a crash it writes a
  `crash-*` / `leak-*` / `timeout-*` reproducer under the build's `artifacts/`
  directory. After fixing the defect, copy that file here (rename it descriptively,
  e.g. `crash-list-nested-oob.bin`) so the exact input is replayed on every run —
  including on toolchains without libFuzzer, via the harness's ASan/UBSan replay
  mode.
- **Hand-authored edge inputs** that exercise a shape the generated seeds miss.

The harness auto-generates one valid seed per type at run time, so this directory
may legitimately be empty (this README aside) until the first finding lands.
