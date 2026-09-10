# Backend Translation

A backend is a translation of MLIR: one pass pipeline turns every serialisation plan into a
serialise and a deserialise function of dialect operations, and a backend spells those functions
in its language. [DESIGN.md](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/DESIGN.md)
states this as the backend contract; `ctest -L backend-contract` accepts a backend against it, and
nothing else does. C, obj, C++, Rust, Go and TypeScript meet the contract. Python does not,
and this page is the record of making it.

## Bodies are not derived from the IR

Every emitter calls `collectLoweredFactsFromMlir`, and for one of them that is the whole of
their MLIR consumption. The `LoweredFactsMap` it returns holds, per field, a step index and the
names of helper symbols; per section, a capacity-check helper name, the union tag width and the
alias flag. It holds no operations. The serialise and deserialise bodies of Python come from
`RuntimeLoweredPlan` and `ScriptedOperationPlan` — planners in `lib/CodeGen` that walk the
semantic module and decide the control flow themselves. C, obj, C++, Rust, Go and TypeScript
translate the output of `build-dsdl-plan-bodies`: through EmitC for C source, through the LLVM
dialect for objects, and through the translator below for C++, Rust, Go and TypeScript.

The gates measure exactly this. Perturb an `dsdl.io` operation's width with the semantic module
held constant and Python's bodies do not change; perturb the semantic module's cast
mode with the operations held constant and they do. Three times the architecture was asked for
and delivered in that shape, each time passing as the real thing because every emitter consumed
the dialect. Consuming lowered facts is not translating lowered operations.

## The pipeline is one

`lower-dsdl-bodies` is `lower-dsdl-exec`, `dsdl-annotate-aliasability` and
`build-dsdl-plan-bodies`, defined once in `lib/Transforms` as `addLowerDSDLBodiesPipeline` and
registered with `dsdl-opt` under that name. dsdlc runs it once, over the module every backend
receives. The C lane takes each definition's schema, and the functions built for it, from that
module; stamps C names on the schema; and converts them.

## The body IR is target-neutral

A body spells nothing the way C does:

| in the body | the C conversions spell it as |
|---|---|
| `!dsdl.ptr<!dsdl.object<"vendor.Msg.1.0">>`, `!dsdl.ptr<!dsdl.byte>`, `!dsdl.ptr<!dsdl.size>`, `const` carried on the pointer | `struct vendor__Msg*`, `uint8_t*`, `size_t*` |
| `dsdl.load_member %obj "field"`, and the store, address, element and length forms, each carrying the DSDL member name | the member's `c_name` from the schema |
| `dsdl.array_length`, `dsdl.set_array_length` | `.count` |
| `dsdl.element_addr`, `dsdl.load_element`, `dsdl.store_element`, carrying the element's storage category and width | `.elements[i]`, or `.bitpacked` for a `bool` array |
| `dsdl.union_tag`, `dsdl.set_union_tag` | `._tag_` |
| `dsdl.call_serdes @vendor_Inner_1_0__serialize_ir_`, carrying the member and the direction | `vendor__Inner__serialize_` |

`convert-dsdl-to-emitc` and `convert-dsdl-to-llvm` take the C spelling from the stamped schema
when they run. `test/lit/lower-dsdl-bodies-neutral.txt` holds that a module after
`lower-dsdl-bodies` carries none of it, and the gate fixture has a variable array, a union and a
nested composite, with a row for each.

The union operations are designed against Rust's enum, the object model furthest from a C
struct, and validated on C, where the answer is known.

## One translator, one spelling per language

The body vocabulary is fixed: the `dsdl` operations above, of which ten map onto runtime
primitives every language runtime already provides (`set_uxx`, `get_u8` to `get_u64`, `set_f16`,
`copy_bits` and their kin); a dozen `arith` operations; `scf.if`, `scf.while` and `scf.for`;
`func.call` and `func.return`; and the helper functions lowering synthesises, which are `arith`
and `scf` and translate like any other function.

`translateFunction`, in [`lib/CodeGen/BodyTranslator.cpp`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/lib/CodeGen/BodyTranslator.cpp),
has the shape of `mlir::emitc::translateToCpp`: it walks a function, names its values, spells
`scf.if`, `scf.while` and `scf.for` as the language's structured statements, and dispatches each
operation through a `BodySpelling` — types and literals, operators with width and sign, runtime
primitive calls, member, element, array and union access, signatures. One skeleton; one spelling
per language. It takes the function and nothing else: no `SemanticModule` and no
`LoweredFactsMap`.

Declarations, module layout, manifests, constants, deprecation notices and runtime embedding stay
in each emitter and keep reading the semantic module. The body half of an emitter — its
function-body emitters, helper-binding spellings, alignment, padding and composite renderers — is
replaced by a call into the translator.

## Each backend, in order

Each step is one change. A backend's gate turns green and it joins
`LLVMDSDL_BACKEND_CONTRACT_ENFORCED` in that same change; its body renderers are deleted in that
same change.

**C++** went first because its object model is C's. `CppSpelling`, in
[`lib/CodeGen/emitter/Cpp.cpp`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/lib/CodeGen/emitter/Cpp.cpp),
names members from the scope the struct declaration names them in, built from the schema's own
fields and constants; names a nested type from the schema its callee belongs to; and emits the
helpers as inline functions ahead of the sections that call them. The plan's `i64` is spelled
unsigned, which is what the wire arithmetic and the runtime primitives take, and the few signed
comparisons cast for the comparison alone. A fixed bool array is packed bytes and copies as a
run; a variable-length one is the profile's container of `bool` and copies an element at a time.
A variable-length array is sized within its capacity before the plan validates the count, so a
malformed count never sizes a container past it. The `std`, `pmr` and `autosar` profiles differ
in how those containers are spelled and, under `pmr`, in the memory resource a nested call is
handed. The C↔C++ parity lanes and the generation lane accept it.

**Rust** followed. `RustSpelling`, in
[`lib/CodeGen/emitter/Rust.cpp`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/lib/CodeGen/emitter/Rust.cpp),
names members from the scope the struct declaration names them in, and never names a nested type:
a nested call is a method call on the member. The plan's `i64` is `u64` with wrapping arithmetic,
which is the plan's arithmetic and cannot panic; the size a plan is handed by pointer is a local
`usize`, and a nested call's answer is written back to it. A buffer is a slice, and a pointer
into it is a sub-slice clamped to the buffer's end. A fixed-length array is `[T; N]`, which is the
object the plan addresses without a count; it was a growable container with a length check the
plan does not state. A variable-length array is sized within its capacity, under the section's
memory contract, before the plan validates the count. The helpers are functions of the module.
Both profiles, both runtime specialisations and both memory modes are one spelling. The C↔Rust
parity lanes and their variants, the cargo-check lanes and the generation lane accept it.

**Go** followed. `GoSpelling`, in
[`lib/CodeGen/emitter/Go.cpp`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/lib/CodeGen/emitter/Go.cpp),
names members from the scope the struct declaration names them in, and never names a nested type:
a nested call is a method call on the member, and its two answers, the code and the size it used,
make it a statement rather than a value. The output is gofmt-clean by construction: each operation
is one statement, so no operator sits inside a call argument or an index, and a conditional value
is an `if` that assigns a declared variable. Go's fixed-width arithmetic wraps, which is the
plan's arithmetic. A negative literal is spelled in the signed type it is compared in, since a
constant conversion that overflows is a compile error. A `bool` member read as an integer goes
through the runtime's `BoolToUint64`; a variable-length array is sized within its capacity by the
runtime's `Resize`, which names no element type. A buffer is a slice, and a pointer into it is a
sub-slice clamped to the buffer's end. The helpers are functions of the package. The C↔Go parity
lanes, the decoder fuzz and forward-compatibility lanes, the go-build lane and the generation
lane, which runs gofmt, accept it.

**TypeScript** followed. `TsSpelling`, in
[`lib/CodeGen/emitter/Ts.cpp`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/lib/CodeGen/emitter/Ts.cpp),
spells the plan's `i64` as `bigint`, so a 64-bit field and the plan's arithmetic on it are exact;
an index and an error code are `number`. A constant is the value it stands for, since a bigint's
bitwise operators read a negative value as its two's complement, and an unsigned comparison
against a negative literal reads both sides as unsigned 64-bit values. A body is an exported
function of the object and the buffer that answers the size it used or the error code, and the
`serialize` and `deserialize` entry points a consumer calls wrap it: the one with a buffer of the
type's largest size, the other with an empty object, and each throws on a code. A deserialise body
is handed that empty object, so the storage a member address names is created where it is first
addressed; a union is one of its option objects, reached through the object cast to the option's
shape. A buffer is a `Uint8Array`, and a pointer into it is a subarray clamped to the buffer's
end. The runtime's write functions answer the plan's code. Both runtime specialisations are one
spelling. The C↔TypeScript parity lanes and their variants, the decoder fuzz lane, the runtime
smoke lanes, the type-check lanes and the generation lane accept it.

**Python** remains: arbitrary-precision integers, so wrap semantics are explicit masks;
`auto|pure|accel` is a primitive-table choice. Oracle: the `c-python-*` fixtures and the
runtime-execution lanes.

## What is removed

With the last backend converted: the planners — `SerDesStatementPlan`, `NativeEmitterTraversal`,
`RuntimeLoweredPlan`, `ScriptedOperationPlan`, `ScriptedBodyPlan`, `LoweredBodyPlan`,
`LoweredRenderIR`, `SectionHelperBodies`, `RuntimeHelperBindings`, `NativeHelperContract`,
`SectionHelperBindingPlan`, `LoweredFactsLookup` — and `MlirLoweredFacts` with them. The
convergence report and its scorecard page, which grade an emitter by the presence of
`collectLoweredFactsFromMlir(` in its source. The Python emit-order trace, replaced
by one check of step order on the plan-body IR, downstream of which order is by construction.
The facts-channel check in the gate tool, which has no channel left to check.
`--optimize-lowered-serdes` then sits after `build-dsdl-plan-bodies` in the shared pipeline and
acts on every backend alike, or is removed with its `-optimized` lanes.

## Acceptance

`ctest -L backend-contract` runs
[`test/integration/BackendContractTool.cpp`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/test/integration/BackendContractTool.cpp)
once per backend over
[`test/integration/backend_contract`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/test/integration/backend_contract).

| gate | perturbed | held | bodies must |
|---|---|---|---|
| operation reflection | one `dsdl.io` class per row — unsigned, signed and float widths, alignment, an array element's width, a union option's width | the semantic module | change |
| model independence | the semantic module's cast mode; its field widths | the MLIR module | be identical |
| determinism | nothing; the baseline is generated twice | everything | be identical |

A row visible through `LoweredFactsMap` is reported and not scored. A backend in
`LLVMDSDL_BACKEND_CONTRACT_ENFORCED` fails its test on a gap; the others report it. A gate the
current tree passes proves nothing about a change: each gate above was shown to fail the five
backends before any of them was written.
