# Backend Translation

A backend is a translation of MLIR: one pass pipeline turns every serialisation plan into a
serialise and a deserialise function of dialect operations, and a backend spells those functions
in its language. [DESIGN.md](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/DESIGN.md)
states this as the backend contract; `ctest -L backend-contract` accepts a backend against it, and
nothing else does. Every backend meets the contract, and this page is the record of the work.

## Bodies were not derived from the IR

Before this work, every emitter called `collectLoweredFactsFromMlir`, and for five of them that
was the whole of their MLIR consumption. The `LoweredFactsMap` it returns holds, per field, a step
index and the names of helper symbols; per section, a capacity-check helper name, the union tag
width and the alias flag. It holds no operations. The serialise and deserialise bodies of C++,
Rust and Go came from `SerDesStatementPlan` and `NativeEmitterTraversal`; those of TypeScript and
Python from `RuntimeLoweredPlan` and `ScriptedOperationPlan` — planners in `lib/CodeGen` that
walked the semantic module and decided the control flow themselves. C and obj translated the
output of `build-dsdl-plan-bodies`: through EmitC for C source and through the LLVM dialect for
objects.

The gates measure exactly this. Perturb a `dsdl.io` operation's width with the semantic module
held constant and a planned body does not change; perturb the semantic module's cast mode with
the operations held constant and it does. Three times the architecture was asked for and
delivered in that shape, each time passing as the real thing because every emitter consumed the
dialect. Consuming lowered facts is not translating lowered operations.

## The pipeline is one

`lower-dsdl-bodies` is `lower-dsdl-exec`, `dsdl-annotate-aliasability` and
`build-dsdl-plan-bodies`, defined once in `lib/Transforms` as `addLowerDSDLBodiesPipeline` and
registered with `dsdl-opt` under that name. dsdlc runs it once, over the module every backend
receives; `--optimize-lowered-serdes` canonicalises the bodies and their helpers after
`build-dsdl-plan-bodies`, so it acts on every backend alike. The C lane takes each definition's schema, and the functions built for it, from that
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
per language. It takes the function and nothing else: no `SemanticModule`.

Declarations, module layout, manifests, constants, deprecation notices and runtime embedding stay
in each emitter and keep reading the semantic module; a declaration takes the alias verdict and a
union's tag width from the plan operation itself, through
[`include/llvmdsdl/CodeGen/SchemaLookup.h`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/include/llvmdsdl/CodeGen/SchemaLookup.h).
The body half of an emitter — its
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

**Python** followed. `PythonSpelling`, in
[`lib/CodeGen/emitter/Python.cpp`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/lib/CodeGen/emitter/Python.cpp),
spells every integer of the plan as a Python `int`, which holds the value it stands for, so a
constant is that value and the bitwise operators read a negative one as its two's complement. A
body is a method of the dataclass that answers the size it used or the error code, and the
`serialize` and `deserialize` methods wrap it: the one with a buffer of the type's largest size,
the other with a default-constructed object, and each raises `ValueError` with the code's text. A
buffer is a `memoryview`, and a pointer into it is a slice of the view, which the three runtimes
read and write in place. A fixed-length array holds its elements from construction, so a
deserialised object has the storage the plan addresses; a union's option is created when the
plan sets the tag. Python has no empty block, so a block that spelled no statement closes with
`pass`. The C↔Python parity lanes and their variants, the malformed-input and decode-fuzz lanes,
the runtime parity and smoke lanes, and the generation lane accept it.

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

A backend in `LLVMDSDL_BACKEND_CONTRACT_ENFORCED` fails its test on a gap; the others report
it. A gate the
current tree passes proves nothing about a change: each gate above was shown to fail the five
backends before any of them was written.
