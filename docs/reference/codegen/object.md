# Object Backend (`-l obj`)

`--target-language obj` lowers each definition through LLVM IR and assembles it inside `dsdlc`,
publishing the headers `-l c` generates beside one object per definition. No C is written and no
compiler is invoked. `--target-triple` names the target; the host's own is used when it is
omitted, and every target the build's LLVM carries is available.

The design, the ABI boundary and the acceptance gates are in
[Direct Object Lowering](../../development/direct-object-lowering.md). The
[Showroom](../../showroom/index.md) c-cmake recipe links the objects through `dsdlc_generate()`.

## Endianness

The DSDL wire format is little-endian on every target, so `serialize_`/`deserialize_` perform
explicit little-endian bit assembly and are independent of host endianness.
