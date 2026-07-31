# Showroom

A namespace of plausible vendor-specific datatypes for an aerial drone system, generated into every
language and profile dsdlc supports.

It exists so that you can read what the compiler produces for definitions shaped like the ones you
are about to write, before you write them. It is not a test fixture and nothing here is regulated:
`lanyard` is a fictional vendor, and these are the sort of types a drone programme adds alongside the
standard `uavcan` namespace. UDRAL is deliberately not used -- the point is to show what a vendor
writes from scratch, leaning on the standard types where standard types exist.

## Build it

```bash
cmake --build <build-dir> --target showroom
```

Output lands in `<build-dir>/showroom/<variant>/`:

| Variant | Command |
|---|---|
| `c` | `--target-language c` |
| `cpp-std`, `cpp-pmr`, `cpp-autosar` | `--target-language cpp --cpp-profile <profile>` |
| `rust-std`, `rust-no-std-alloc` | `--target-language rust --rust-profile <profile>` |
| `go` | `--target-language go` |
| `ts` | `--target-language ts` |
| `python` | `--target-language python` |
| `mlir` | `--target-language mlir` (the intermediate form, one file) |

Nothing is compiled. The showroom generates and stops; correctness of the generated code is what
`test/lit` and the integration suites are for.

Individual variants build on their own: `cmake --build <build-dir> --target showroom-rust-std`.

There is no submodule dependency -- `lanyard` refers only to `uavcan` types, which dsdlc carries in
its embedded catalog.

## Browse it

<!-- showroom-docs: skip -->

`cmake --build <build-dir> --target showroom-docs` generates `docs/showroom/`, which pairs each
definition with its wire-layout facts and a declaration excerpt in each language. Those pages are the
compiler's own output and are gitignored: the documentation workflow runs inside the toolshed
container, builds dsdlc, and produces them at publish time. Run the target before previewing the site
locally -- mkdocs has a nav entry for the showroom and `--strict` fails without a page behind it.

This file is also the source of `docs/showroom/index.md`. The target renders the README into that
page, expanding `<!-- showroom-docs: type-table -->` into the generated table of types and dropping
any section whose body opens with `<!-- showroom-docs: skip -->` -- as this one does, since it is
addressed to someone reading the repository rather than the site. Edit the README; `index.md` is
overwritten on every run.

## What is in it

Twenty-four definitions across six sub-namespaces, chosen so that between them they exercise the
language and the three LEAST SUPPORTED TRANSPORTs a real vehicle spans.

<!-- showroom-docs: type-table -->

## Least Supported Transport

Every definition states a minimally capable transport it was sized for, and most of them assert that
budget with `@assert _offset_.max <= ...` so that a layout change breaks the build rather than quietly
spilling into a multi-frame transfer. Cyphal does not limit DSDL by transport but type authors often
make different design choices based on the limitations of certain transports. In some cases these design
choices are not clear unless these limitations are called out in type comments or by using DSDL assert
statements.

| Transport | Budget | Examples |
|---|---|---|
| Classic CAN | 7 payload bytes, one frame | `propulsion.EscStatus.1.0`, hand-packed to exactly 56 bits |
| CAN FD | 63 payload bytes, one frame | `nav.GlobalPosition.1.0`, `link.RcInput.1.0`, `payload.GimbalStatus.1.0` |
| Cyphal/UDP | a datagram, kilobytes | `nav.MissionPlan.1.0`, `payload.CameraFrameMetadata.1.0` |

`EscStatus.1.0` and `MissionPlan.1.0` are the two ends of that range on purpose: same compiler, same
language, one sized for a 7-byte frame and the other for a 24 KiB datagram.

### Versioning

Five migrations, each answering a different question about when a change forces a major bump.

| Transition | Breaking | What it shows |
|---|---|---|
| `ThrottleCommand` 0.1 → 1.0 | n/a | A `0.x` definition promises nothing; promotion to 1.0 is where the promise begins |
| `VehicleState` 1.0 → 1.1 | no | A field appended inside an unchanged `@extent`; both share port 6210 |
| `VehicleState` 1.1 → 2.0 | yes | A field retyped, a field replaced, the extent grown; new port 6211 |
| `EscStatus` 1.0 → 2.0 | yes | `@sealed` is a one-way door: extensibility costs a major version |
| `LegacyBatteryPoll` 1.0 → `BatteryStatus` 2.0 | yes | `@deprecated` marking a superseded service, both halves kept |

### Language features

| Feature | Where |
|---|---|
| `@sealed` | `EscStatus.1.0`, `RcInput.1.0`, `SubsystemReport.1.0` |
| `@extent` | `VehicleState.1.0`, `MissionPlan.1.0`, `Waypoint.1.0` |
| `@union` | `ControlSurfaces.1.0` |
| `@assert` | `EscStatus.1.0`, `GlobalPosition.1.0`, `CapturePhoto.1.0` |
| `@print` | `GlobalPosition.1.0` |
| `@deprecated` | `LegacyBatteryPoll.1.0` |
| Service request/response sections | `UploadMission.1.0`, `CapturePhoto.1.0` |
| Non-byte-aligned scalars | `EscStatus.1.0` (`uint14`, `int12`, `int9`, `uint4`) |
| `void` padding | `EscStatus.1.0`, `GlobalPosition.1.0`, `SubsystemReport.1.0` |
| Fixed-size arrays | `GlobalPosition.1.0` (`float16[9]`), `RcInput.1.0` (`uint11[16]`) |
| Variable-length arrays | `ThrottleCommand.1.0`, `MissionPlan.1.0`, `BatteryStatus.2.0` |
| Arrays of composites | `MissionPlan.1.0` (delimited elements), `SystemHealth.1.0` (sealed elements) |
| Cast modes | `CameraFrameMetadata.1.0` (`truncated` against `saturated`) |
| Constants as enumerations | `FlightMode.1.0`, `Waypoint.1.0`, `GlobalPosition.1.0` |
| Reuse of standard `uavcan` types | throughout; `SubsystemReport.1.0` is the clearest case |

## Documentation

Every comment block reaches the generated source in all six languages, attached to the type, field,
or constant it documents. That is why the definitions are commented as heavily as they are: the DSDL
is the only place the documentation is written, and the generated code is where most people read it.

**Wrap comment lines at 72 columns.** The documentation site gives a code block about eighty
monospace characters before it scrolls horizontally, and that width does not grow with the window —
extra width goes to the sidebars. The widest comment prefix any backend adds is eight characters
(`  /* … */` in C, `    /// ` in Rust), so 72 columns of DSDL is what survives the trip. Generated
*code* may exceed it and scroll — some serializer signatures are unavoidably long — but a comment
that scrolls is a defect in the definition, since its width is ours to choose. `showroom-docs`
enforces this and fails the build on a violation.

Comment placement follows the OpenCyphal convention used by the regulated namespace: **the block goes
after the attribute it documents, followed by a blank line.** A block placed *before* a field
attaches to whatever precedes it instead, or is absorbed into the type's own documentation if the
field is the first one.

`@deprecated` rides along with it. Every backend appends a `Deprecated: …` notice to the type's
documentation and emits an `IS_DEPRECATED` constant, so the marking is visible to a developer who
never opens the DSDL. Go treats the notice as a real deprecation — its toolchain keys on a
`Deprecated: ` doc paragraph — and TypeScript additionally gets a `/** @deprecated */` JSDoc block,
which is what `tsc` and editors read.

C, C++, and Rust get compile-time enforcement on top of that, by default: `__attribute__((deprecated))`,
`[[deprecated]]`, and `#[deprecated]`. Only code that names a deprecated type is diagnosed — each
generated file suppresses the diagnostic across its own body, so including the headers stays clean
under `-Werror`. `dsdlc --no-deprecation-attributes` drops the attributes for a `-Werror` build that
depends on a deprecated definition with no migration target yet. See
`health/258.LegacyBatteryPoll.1.0.dsdl`.

## Port identifiers

`lanyard` is not a standard root namespace, so its fixed port identifiers come from the unregulated
ranges: **6144–7167 for messages** and **256–383 for services**. Staying inside them is what lets the
showroom build without `--allow-unregulated-fixed-port-id`.

A fixed port identifier belongs to one major version. Minor versions share it (`VehicleState.1.0` and
`1.1` are both on 6210); a major bump takes a new one (`VehicleState.2.0` moves to 6211).
