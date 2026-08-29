# Object Backend (`-l obj`)

`--target-language obj` is recognised and exits with `not implemented`.

The design for lowering the DSDL dialect through LLVM IR to object code — the three phases, the
ABI boundary, and the five acceptance gates that define done — is in
[Direct Object Lowering](../../development/direct-object-lowering.md).

To compile generated sources into objects today, generate them with `-l c` or `-l cpp` and build
them with your own toolchain. `dsdlc --list-outputs` reports the source set, and
the [Showroom](../../showroom/index.md) recipes cover wiring that into a build.

## Endianness

The DSDL wire format is little-endian on every target, so `serialize_`/`deserialize_` perform
explicit little-endian bit assembly and are independent of host endianness.
