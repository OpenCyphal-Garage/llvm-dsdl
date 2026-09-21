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
output of `build-dsdl-plan-bodies` rather than planning their own.

The gates measure exactly this. Perturb a `dsdl.io` operation's width with the semantic module
held constant and a planned body does not change; perturb the semantic module's cast mode with
the operations held constant and it does. Three times the architecture was asked for and
delivered in that shape, each time passing as the real thing because every emitter consumed the
dialect. Consuming lowered facts is not translating lowered operations.

## The pipeline is one

`lower-dsdl-bodies` is `lower-dsdl-exec`, `dsdl-verify-alias-layout` and
`build-dsdl-plan-bodies`, defined once in `lib/Transforms` as `addLowerDSDLBodiesPipeline` and
registered with `dsdl-opt` under that name. dsdlc runs it once, over the module every backend
receives; each plan yields three bodies, serialise, deserialise and initialise; `--optimize-lowered-serdes` canonicalises the bodies and their helpers after
`build-dsdl-plan-bodies`, so it acts on every backend alike. The C and object lanes take each definition's schema, and the functions built for it, from that
module, and stamp C names on the schema before spelling or converting them.

## The body IR is target-neutral

A body spells nothing the way C does:

| in the body | in C |
|---|---|
| `!dsdl.ptr<!dsdl.object<"vendor.Msg.1.0">>`, `!dsdl.ptr<!dsdl.byte>`, `!dsdl.ptr<!dsdl.size>`, `const` carried on the pointer | `struct vendor__Msg*`, `uint8_t*`, `size_t*` |
| `dsdl.load_member %obj "field"`, and the store, address, element and length forms, each carrying the DSDL member name | the member's `c_name` from the schema |
| `dsdl.array_length`, `dsdl.set_array_length` | `.count` |
| `dsdl.element_addr`, `dsdl.load_element`, `dsdl.store_element`, carrying the element's storage category and width | `.elements[i]`, or `.bitpacked` for a `bool` array |
| `dsdl.union_tag`, `dsdl.set_union_tag` | `._tag_` |
| `dsdl.call_serdes @vendor_Inner_1_0__serialize_ir_`, carrying the member and the direction | `vendor__Inner__serialize_` |
| `dsdl.call_initialize @vendor_Inner_1_0__initialize_ir_`, carrying the member | `vendor__Inner__initialize_` |
| `dsdl.store_view`, `dsdl.clear_view`, `dsdl.load_view`, carrying the member held as a view and, in an array of views, the element's index | `.bytes` and `.size_bytes` of the member's `dsdl_runtime_view_t`, or of the element's; `dsdl_runtime_clear_views` over a fixed array |
| `dsdl.copy_bytes`, carrying the field's width | `dsdl_runtime_copy_bytes` |

`CSpelling` and `convert-dsdl-to-llvm` take the C spelling from the stamped schema when they
run. `test/lit/lower-dsdl-bodies-neutral.txt` holds that a module after
`lower-dsdl-bodies` carries none of it, and the gate fixture has a variable array, a union and a
nested composite, with a row for each.

The union operations are designed against Rust's enum, the object model furthest from a C
struct, and validated on C, where the answer is known.

## One translator, one spelling per language

The body vocabulary is fixed: the `dsdl` operations above, of which ten map onto runtime
primitives every language runtime already provides (`set_uxx`, `get_u8` to `get_u64`, `set_f16`,
`copy_bits` and their kin); `dsdl.index_holds`, which answers whether the target's index type
holds a count and so belongs to the target rather than to a cast a pass may fold; a dozen `arith`
operations; `scf.if`, `scf.while` and `scf.for`; `func.call` and `func.return`; and the helper
functions lowering synthesises, which translate like any other function.

An array length read off the wire is validated by its helper before anything is sized by it:
past the declared capacity, or not held by the target's index, and the message is rejected with
the array untouched. `dsdl.set_array_length` therefore receives a validated count, and a spelling
narrows it without bounding it.

`translateFunction`, in [`lib/CodeGen/BodyTranslator.cpp`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/lib/CodeGen/BodyTranslator.cpp),
has the shape of `mlir::emitc::translateToCpp`: it walks a function, names its values, spells
`scf.if`, `scf.while` and `scf.for` as the language's structured statements, and dispatches each
operation through a `BodySpelling` — types and literals, operators with width and sign, runtime
primitive calls, member, element, array and union access, signatures. One skeleton; one spelling
per language. It takes the function and nothing else: no `SemanticModule`.

## A value is named by the operation that defines it

A `dsdl` operation states what its result is, and several name the DSDL member it belongs to:
`dsdl.member_addr` and `dsdl.element_addr` carry a member, `dsdl.call_serdes` carries the member
it calls, `dsdl.array_length` the array it counts, and lowering marks each helper it synthesises
with what that helper answers. The translator reads a `ValueRole` from the operation alone, so it
is the same role in every language, and asks the spelling for an identifier through
`BodySpelling::valueName`. `snakeValueName` and `camelValueName` render the two styles the
backends use: C++, Rust and Python take the first, Go and TypeScript the second.

A name is claimed only where the translator declares a value. An operation a spelling answers
`spellsInline` for is spelled at each use instead and claims nothing, which is why a row below
reaches one language and not another: C++ forms every address inline and declares its null tests,
the others form a member and an element address inline and declare the buffer's, and TypeScript
declares a deserialiser's member address because that is where the storage is created.

| the operation, where its value is declared | the value is spelled |
|---|---|
| `dsdl.member_addr %obj "frequency"` | `frequency_addr`, `frequencyAddr` |
| `dsdl.call_serdes @… {member = "frequency"}` | `frequency_err`, `frequencyErr` |
| `dsdl.local`, and the `dsdl.load_scalar` that reads it back | `frequency_size`, `frequencySize` |
| `dsdl.array_length %obj "name"` | `name_count`, `nameCount` |
| `dsdl.buffer_at`, named by the nested call it addresses for | `frequency_buf`, `frequencyBuf` |
| `dsdl.load_view %obj "pose"`, its bytes and their count | `pose_buf` and `pose_size`, `poseBuf` and `poseSize` |
| `dsdl.is_null`, `dsdl.union_tag`, `dsdl.buffer_or_empty` | `is_null`, `tag`, `buf` |
| `dsdl.index_holds`, in an array length's validation helper | `index_holds`, `indexHolds` |
| a helper call, by the marker lowering left on the helper | `err`, `value`, `count`, `tag` |

An `scf` result takes the role the values yielded into it share, which is how the error a plan
threads through its fields keeps the name at every step. Two arms agreeing on the role but not on
the member give the role alone, and two stating different roles give none. An arm whose value no
operation describes states nothing rather than states no role, so it neither carries the answer
nor takes it away. An operation that states nothing about its result takes the
translator's own `v<N>`, and so does an `arith` result a body declares; a constant is spelled where
it is used and claims no name at all.

A plan's own structure states what no operation in it does. The bit offset threaded from step to
step, and the error carried beside it, reach a body as `scf` results, and the operations that build
those results say nothing about either -- an error has a role above only where an operation answers
with one. `guarded`, the union arm, the element loops and the epilogue each build the pair knowing
which is which.
`build-dsdl-plan-bodies` writes that down as `llvmdsdl.result_roles`, one name per result:

```mlir
%10:2 = scf.if %9 -> (i64, i8) {
  ...
} {llvmdsdl.result_roles = ["offset", "error"]}
```

`test/lit/plan-cursor-result-roles.txt` holds the stamp on the guard, the loops and the epilogue.
The translator takes a stamp only while its length still equals the operation's result count:
canonicalisation drops a result nothing reads and carries the attribute onto the operation it
rebuilds, so a stamp that has outlived its results names none of them, and the yielded values are
asked instead.

A name is asked for from ordinal zero upwards until the function has not used the answer, so a
repeat is distinguished in the spelling's own style — `err_2` or `err2`. The ordinal counts
candidates rather than values: a role whose plain form a parameter or a reserved local has claimed
takes its first value from an ordinal above zero. The joined identifier collapses
its underscore runs: a member that already trails one, which is how a target escapes a reserved
word, would otherwise reach C++ as the `__` it reserves.

## What a local must not capture

The pool a name is claimed from holds the function's parameters and `BodySpelling::reservedLocals`
— the identifiers a spelling's bodies use without qualifying them. Go's bodies convert with the
predeclared type names and measure with `len`; Python's call `len`, `min` and `memoryview`;
TypeScript's construct through `BigInt`, `Number` and the typed arrays. A local of any of those
names captures the call, and the capture is legal code that means something else: Go stops
compiling, and Python makes the name local for the whole function so the builtin is gone from its
first line. Rust and C++ answer with nothing — Rust resolves a conversion in the type namespace
and reaches the runtime by path, C++ qualifies every standard-library call and prefixes every
runtime one.

Stropping does not cover this. `codegenIsKeyword` holds keywords, and `len` is not one in either
language; shadowing a predeclared identifier is permitted, so there is no illegal spelling for an
escape to fix. It is a question about the scope, which is where it is answered.

A role word is therefore chosen for how it reads. `count` rather than `len` is still the better
word — `len` is claimed, so every array count would come back as `len2`.

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

**C** came last, having already translated the bodies by another route: through
`convert-dsdl-to-emitc` and MLIR's `translateToCpp`, which names every value `v<N>` and, having
no spelling for the plan's signed arithmetic, wrote each operation as four statements — cast to
unsigned, cast, operate, cast back — which CSE could not merge, `emitc.cast` carrying no `Pure`
trait. `CSpelling`, in
[`lib/CodeGen/emitter/C.cpp`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/lib/CodeGen/emitter/C.cpp),
spells the plan's `i64` unsigned, which is the wire arithmetic and the runtime's argument, so
each operation is one statement. A member or element address is spelled where it is used: a
declaration would sit in the block the plan formed it in, and C ends that scope before the value
is read again. A local holds what its address points at, so the slot a nested call takes the size
by is a `size_t`. An accessor's signature is the header's, which declares the offsets signed, and
the body takes each into the type it operates in once rather than at every use. The file opens
with the includes its bodies reach for, and a nested type's C name comes from the field that
refers to it, its schema not being cloned into a source build's module. C is the first backend to
translate an initialise body as a function, which `BodySpelling::declareCallInitialize` is the
hook for. Over the regulated corpus the generated C is 38% smaller, 67,835 lines to 42,039, and
570 anonymous values fall to 115. The parity lanes against every other language accept it, as do the
sanitizer, fuzz and generation lanes, and the object lane, which compares its wire bytes against
this one, transcript for transcript.

Two dead comparisons surfaced in the port, both of them `-Werror=type-limits` under GCC and both
in the plan rather than in a spelling. A plan whose type needs no bits fits whatever buffer it is
given, and the capacity helper synthesised for it compared a constant against an unsigned
quantity. A plan states its need in `max_bits`; where that is nought the helper is not built, the
attribute naming it is not set, and a body begins from success. A composite field
the buffer starts with is within whatever buffer there is, and its accessor's guard is the answer
it gives, so at offset nought the comparison is not built. The generated Rust and Go had carried
the first of the two, ten of each, their compilers not treating it as a defect.

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
