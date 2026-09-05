# Direct Object Lowering

`--target-language obj`: DSDL dialect to LLVM IR to object code, inside `dsdlc`, with no C
source and no host compiler. Six acceptance gates under `ctest -L direct-lowering-gate`,
described in [Acceptance](#acceptance) below, hold strictly.

## Why the serialisation has to be written first

The obvious reading of "lower to LLVM" is that an LLVM conversion is added beside the existing
EmitC one. That is not the shape of the work, because the serialisation is not in the IR to
convert.

`dsdlc --target-language c` over the UAVCAN corpus emits 362 serialize and deserialize function
bodies, and every one of them is a string literal in an `emitc.verbatim` op — braces, `for`
loops, runtime calls and all.

What is real IR is the helper predicates: `capacity_check` is an `arith.cmpi` and an `scf.if`,
and so are the saturation helpers, `validate_union_tag`, `validate_array_length` and
`array_length_prefix`. The plan predicates are compiled. The plan execution is text.

`DSDL_BitReadOp` and `DSDL_BitWriteOp` are declared in
[`DSDLOps.td`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/include/llvmdsdl/IR/DSDLOps.td) and constructed nowhere in `lib/`. The
vocabulary for representing bit-level serialisation was defined and never used.

So the work is three phases, and they run in the opposite order of the difficulty one would
guess.

## Phase A — the serialisation becomes IR

`lower-dsdl-exec` constructs `dsdl.bit_write`/`dsdl.bit_read` instead of formatting C, and those
lower to `arith`/`scf` over a byte pointer. The C backend consumes the result through the
existing EmitC conversion.

No LLVM code is written in this phase, and that is the point. It carries essentially all of the
risk and effort, and it can be finished and proven before any of the LLVM work starts.

Two properties make it tractable:

- **The oracle already exists.** The parity corpus and the Dafny ordering class in
  [`spec/dafny/CyphalSerdes.dfy`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/spec/dafny/CyphalSerdes.dfy) pin the wire bytes, and no
  golden snapshot pins C source text. The C text being replaced is the specification of the
  behaviour, and the wire bytes must not move; the shape of the generated C is expected to
  change, because EmitC renders it rather than a string formatter.
- **Progress is a number.** The count of `_ir_` function bodies rendered as text goes from 362
  towards 0, counted over `dsdlc --target-language c +uavcan` output. 332 are operations:
  scalars, arrays of scalars and of composites, alignment, void fields, nested composites
  sealed and delimited, and unions of any of those, both directions.

### The dead declaration, resolved

Two causes, both mine, and neither in the loop lowering they appeared to come from.

`emitc.translateToCpp` was being asked to declare variables at the top of each function.
Under that setting it declares every value and then emits some of them inline anyway,
leaving the declaration unread. The C backend now translates without it.

The rest were values an `scf.while` carried and its body never read. MLIR keeps a while
loop's results identical to the values its condition forwards, so a carried induction index
is also a result -- and the canonicaliser does **not** drop an unused one, which was checked
directly rather than assumed. Both element loops are driven by the offset alone now and
recover the index from it, and every loop body reads the error it carries.

Composites and arrays went live as a direct result. All 167 files of the UAVCAN corpus
compile under `-Wall -Wextra -Werror`.

### Error precedence

A step that fails stops the plan, and every later step has to leave its error alone. This has
now been got wrong twice, so `foldError` states it in one place: a later check keeps the
earlier failure if there was one.

The array deserializer overwrote the incoming error with its own length check, so
`uavcan.metatransport.can.Frame.0.2` reported a bad array length where the reference
reported the bad union tag already found in the nested `ArbitrationID` -- Frame being a
union of four composites, one of which holds another union before its payload. The whole
read is guarded now, not just its loop.

The same read also clamped an over-long wire length to the declared capacity where the
reference validates and rejects. A decoder that quietly truncates accepts a message the
sender did not send.

The union tag validation did the same to the capacity check that preceded it, which would
have reported a bad tag for a buffer that was simply too small.

### Nothing renders as text

All 362 serialize and deserialize bodies over the UAVCAN corpus are built as operations, and
all 167 files compile under `-Wall -Wextra -Werror`. A plan the builder cannot express fails
`build-dsdl-plan-bodies`, in every lane, naming the step and the reason.

A `bool` is its own scalar category, and a bool array is not an array of them: the storage is
bitpacked, already in the layout the wire wants, so the whole array moves in one run of bits
rather than a loop. That is what `dsdl.bit_write` and `dsdl.bit_read` are for, and they are
not mirror images -- writing past the end of the buffer is an error, reading past it
zero-extends, so the read takes the buffer's size and the write does not.

A type with no fields encodes nothing, and its body is the prologue and epilogue alone.

A `void` field is a run of reserved bits: written as zeros, and read as nothing, a decoder
having no name to put them under. The writer is the one that pads to an alignment boundary,
given an end offset instead of computing one.

A union's options may be composites or arrays. What one field needs is the same whether it
sits in a union or a struct, so both shapes ask `unsupportedFieldReason`; having the two disagree
is how an option gets accepted that the arm builder cannot emit. A union's step list also
carries alignment steps that are not options, and they are passed over here the same way the
option collection passes over them.

An array of composites is encoded element by element through the nested entry point, so the
stride is whatever each element reports and the loop cannot be driven by the offset. It is
counted, with `scf.for`, whose induction variable is not among its results -- an `scf.while`
would make the index a result nothing reads, which is the dead declaration again. An array of
delimited composites writes a header per element, through the same loop.

A fixed-length array is the variable one without its bookkeeping: its length is in its
declaration, so there is no count member to read, nothing that could be out of range, and
nothing to announce on the wire. The member is the elements rather than a struct holding
them, which is the only thing the element path has to know.

A delimited nested composite is built: its length precedes it so that a reader which does not
know the type can step over it, which is why decoding advances by the length it was told
rather than by what the nested decode consumed. A newer sender may have written fields this
reader has no name for, and skipping only what was understood would leave the cursor inside
them. `llvmdsdl-delimited-forward-compat-skip` decodes a buffer where the two outcomes differ
by a whole byte, and was confirmed to fail when the advance is taken from the consumed count.

A union of scalar options is built: the tag is read, normalised, validated, and written, and
then a chain of tests contributes whichever arm the tag selects while the rest pass the
cursor through untouched. The UAVCAN corpus has no such union -- every one of its unions
holds composites -- so the count does not move, but the fixture suites cover it.

### The pointer type

The serdes signatures take pointers, and the helper predicates that exist today take only `i64`
and `i8`, so the dialect has no pointer to reuse. Add `!dsdl.ptr<T>`, mapped by
`convert-dsdl-to-emitc` to `!emitc.ptr<T>` and by `convert-dsdl-to-llvm` to `!llvm.ptr`.
[`DSDLTypes.td`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/include/llvmdsdl/IR/DSDLTypes.td) already carries parametric `TypeDef`s
to follow.

Building the bodies on `!emitc.ptr` directly would work for the C path and would have to be
redone for phase B, which is the whole failure this design exists to avoid.

`DSDL_BitReadOp` and `DSDL_BitWriteOp` as declared take a width and a saturating flag, with no
operands and no results. They cannot express a bit write and are a redefinition rather than a
starting point.

### What the lowering path already supports

[`CEmitter.cpp`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/lib/CodeGen/CEmitter.cpp) already runs `createSCFToEmitC`,
`createConvertArithToEmitC`, `createConvertFuncToEmitC` and `translateToCpp`, which is how the
helper predicates become C today. Checked against upstream `mlir-opt`/`mlir-translate`, that
path also renders everything the serdes bodies need:

| IR | C |
| --- | --- |
| `func.func` argument of pointer type | `int8_t* v1` |
| `emitc.subscript` + `emitc.load` | `v5 = v3[v4]` |
| `emitc.assign` to a subscript lvalue | `v3[v4] = v7` |
| `emitc.call` to a private declaration | `dsdl_runtime_copy_bits(v2, v4, v8, v1, v4)` |
| `scf.if` yielding a value | `if/else` over a declared variable |

`emitc.apply "*"` does not produce an lvalue and cannot be used to write through the in/out size
pointer; `emitc.subscript` is the form that works.

### The operations the typed path needs

The write is value-based, not a memory-to-memory copy. A typed body reads a struct member,
normalises it through a scalar helper, and writes the resulting value at a bit offset:

```c
const uint64_t _norm_0 = (uint64_t)llvmdsdl_plan_scalar_unsigned__..._ser((int64_t)(obj->foo));
const int8_t _err_0 = dsdl_runtime_set_uxx(buffer, capacity_bytes, offset_bits, _norm_0, 8U);
```

Over the corpus that is `set_bit` 322 times, `set_uxx` 122, `set_f32` 71, `get_u8` 65 and so on
across 19 distinct primitives. `dsdl_runtime_copy_bits` appears three times in 167 types, so a
pointer-to-pointer bit copy is the rare bulk case rather than the shape to build around.

So the operation set is a value write and a value read carrying buffer, capacity, bit offset,
width and an error result, plus addressing for the struct member the value comes from --
`emitc.member_of_ptr` on the C path, and a computed offset into the published struct layout for
object emission.

### What the operations resolve to

Bit operations carry operands rather than naming a call, and that distinction is load-bearing,
because the two targets cannot resolve them the same way.

`runtime/dsdl_runtime.h` is header-only: 25 `static inline` functions and no `.c` file. Internal
linkage means no external symbol, and the typed path reaches 19 of those primitives rather than
one or two. The C path can lower a bit op to a call because the generated
translation unit includes that header and the C compiler inlines it. An object emitted from LLVM
IR cannot: a `call @dsdl_runtime_copy_bits` in the module would be an undefined reference to a
symbol that exists nowhere, and gate 5 would fail at link time.

So phase C picks one of:

- Lower the bit ops to loads, stores, shifts and masks in the IR. No call, no symbol, no runtime
  dependency, and the answer that matches what direct lowering means.
- Define the primitives in the module in IR, so each object carries its own copy.
- Give the runtime external linkage, which changes its contract for every consumer and is the
  reason it is listed last.

Nothing about that choice reaches phase A, which is why phase A can proceed without it.

## Phase B — the LLVM conversion

`convert-dsdl-to-llvm`, then the upstream conversions: `convert-scf-to-cf`,
`convert-cf-to-llvm`, `convert-arith-to-llvm`, `convert-func-to-llvm`.

Bodies are built by `build-dsdl-plan-bodies`, split out of `convert-dsdl-to-emitc` so that a
plan becomes operations before a target is chosen. Its cleanup is scoped to the functions it
built: a `dsdl.serialization_plan` holds a region, has no results and no memory effects, so a
module-wide canonicalisation deletes the plans the next pass has to read.

### Addressing a member

Every `!dsdl.ptr` converts to `!llvm.ptr`, so the spelling `!dsdl.opaque` carries is not
consulted. A member is reached by `llvm.getelementptr` against a struct derived from the
schema, whose `dsdl.io` operations carry the category, width, array kind and capacity the C
struct is built from. Nothing adds up bytes: the member's position indexes the struct and LLVM
computes the offset from its own data layout, which is the one arrangement that cannot drift
from what a C compiler does with the same fields.

That derivation is a second implementation of what `cTypeFromFieldType` already performs, and
it is safe to write twice because the two are held against each other.
`llvmdsdl-member-layout-crosscheck` compiles `offsetof` and `sizeof` over the generated headers
and requires the schema's account of member order and width to match.

The host is only one target, and a variable-length array holds its count in a `size_t`: on a
32-bit target that count is half the width it is here, and every member after it moves.
`llvmdsdl-target-layout-crosscheck` therefore asks the lane what the target spells `size_t` at,
derives the struct at that width, and writes the probe as `_Static_assert`s so it can be compiled
for the target rather than run on it. It runs for the host, `riscv32-unknown-elf` and
`thumbv7m-none-eabi`; a lane that answered with the host's width for every target fails the two
32-bit ones and passes the host, which is what it did before the width was taken from the
target's own data layout.

A position is not a step index. Alignment and padding steps take no member, so a composite
addressed by its position among the steps addresses the wrong one; a union's options are its
first members and its `_tag_` follows them. The LLVM verifier rejects an index past the end of
a struct, which is how each of these was found.

### The names a body is built from

Lowering stamps the unscoped, unversioned spelling of every C name, because it does not know a
backend's naming options and produces one module every backend reads. A backend rewrites them
through `stampCNames`, in [`lib/CodeGen/SchemaNaming.cpp`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/lib/CodeGen/SchemaNaming.cpp),
using the same scopes and renderers it names its own output with.

Two conditions were once one attribute. `llvmdsdl.names_final` says the C names on a module are
a backend's own, and gates body building. `llvmdsdl.headers_available` says the generated header
can be included, and gates the includes the C conversion emits. Only the first bears on an
object lowering, and conflating them made the object lane appear to need headers.

`--target-language mlir` stamps and sets `names_final`, which is what makes the symbols it
prints the ones a generated header declares. It refuses to claim the attribute unless every
schema was stamped, the embedded catalog otherwise contributing schemas the stamp never reached.
`llvmdsdl-schema-symbol-parity` holds those symbols against the headers under both naming modes.

### The target's `size_t`

A variable-length array holds its count in a `size_t`, and every runtime primitive takes the
buffer size and the bit offset in one. Both are the target's, so `convert-dsdl-to-llvm` resolves
the width once -- from the module's data layout, else from its `size-bits` option -- and records
it as `llvmdsdl.size_bits`. The struct it derives and the calls it emits read that one answer;
a count held wider than the target's shifts every member after it.

The value beside them is not a `size_t`. `set_uxx` takes a `uint64_t` whatever the target, so at
32 bits the signature is `(ptr, i32, i32, i64, i8)`.

### The runtime signature

A C function's signature is a property of the function. Taking it from whatever a call site
held gave one declaration per spelling of the same primitive, and a call passing its arguments
somewhere the callee does not read them. `runtimeSignature` states what
[`runtime/dsdl_runtime.h`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/runtime/dsdl_runtime.h) declares, and arguments and results are
converted to it -- a bit travels as `bool`, a narrow field in the holder it fits, a value widened
to the 64-bit carrier `set_uxx` takes.

### Where it reaches

All 362 bodies over the UAVCAN corpus convert with no `emitc` and no `dsdl` operations left.
Through the upstream conversions and `mlir-translate` they become 1025 LLVM IR definitions, and
`llc` assembles them into an object file. Its undefined symbols are 84 nested serdes entry
points, which each type's own object defines as the C lane arranges it, and 20 runtime
primitives.

## Phase C — object emission

### The primitives an object calls

They are `static inline` in [`runtime/dsdl_runtime.h`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/runtime/dsdl_runtime.h): a C
translation unit carries its own copy and no symbol survives compilation, so an object has
nothing to link against. `emit-dsdl-runtime` builds all twenty from the wire format instead,
filling in the declarations the conversion left.

Everything reduces to moving a run of bits between buffers at an arbitrary bit offset, and
`copy_bits` has the header's two paths. When both offsets sit on a byte boundary the run moves a
byte at a time and only a trailing partial byte is merged; otherwise it moves in pieces of up to
eight bits, each piece being whatever is left before the nearer of the two bytes ends. The
integer family is that plus little-endian assembly, and `f32` and `f64` are their bit patterns
written as integers.

The header spells the aligned path with `memmove`. A loop is written here instead, so that an
object carries no undefined symbol.

`binary16` is not `fptrunc`. The header's conversion gives every NaN one quiet pattern and
saturates an overflowing finite value to the largest binary16 rather than to infinity, so the
arithmetic is reproduced rather than replaced by the hardware instruction.

Two implementations of one wire format is a thing to be held together, not trusted:
`llvmdsdl-runtime-parity` calls each primitive twice over randomised inputs -- 40,000 trials
across the integer family and 60,000 across the floats, the latter carrying NaN, the
infinities, subnormals and the binary16 overflow edge -- and compares both the answer and the
buffer.

### Calling a nested type

A generated header publishes `X__serialize_` as a `static inline` that calls the body the plan
was built into. Only that body is a symbol, so a call between objects names it; a nested type
in the same module is called directly rather than declared.

### A member is not its plan's width

A plan carries scalars at the width it computes on and the struct holds them at the width C
declares them, so a load takes the member's own type and widens it -- by its sign where the
field is signed, as C's own conversion does. Reading the plan's width instead takes the
neighbouring members with it, and the value that produces is not obviously wrong: it is large,
so the saturation helper clamps it, and every saturating field comes out at its maximum.

### Where it reaches

The UAVCAN corpus becomes 1045 LLVM IR definitions and an object file with **no undefined
symbols at all**. Linked against a driver in place of the generated C, it round-trips
byte-for-byte with the C lane.

### The lane

`--target-language obj` is the C backend's API with the definitions already assembled: the same
headers, declaring the same symbols. Per definition, the plan becomes operations, converts to the
LLVM dialect, is finished by the upstream conversions, and is handed to `translateModuleToLLVMIR`
and then to `TargetMachine::addPassesToEmitFile`. `--target-triple` names the target; every
backend the build carries is linked in, so emitting for one is a matter of naming it.

A per-definition module holds only its own schema, which is enough for C -- a nested type is
reached by name, and its header supplies that. An object needs the nested type's *layout*, so the
schemas it reaches are cloned in beside it, marked so that neither the body builder nor the helper
lowering touches them. Their serialisation belongs to their own object; a second copy here would
be a duplicate symbol and a second thing to keep right.

The primitives are internal to each object, as `static inline` makes them in C. Exported, no two
objects could be linked together.

### Where it reaches

All six acceptance gates hold, strictly:

| Gate | What it establishes |
|---|---|
| 1 | The IR carries the serialisation; no `emitc` or `dsdl` operations survive |
| 2 | Objects come out with an empty `PATH` and `CC` unset |
| 3 | No `.c` anywhere, no staging tree, `TMPDIR` untouched |
| 4 | 32-bit RISC-V ELF objects on a host with no toolchain for it |
| 5 | The object's wire bytes are the C lane's, transcript for transcript |
| 6 | Every entry point a published header declares is defined in its object |

The UAVCAN corpus passes through in about two seconds: 167 objects, 168 headers, no C. Linked
against a driver in place of the generated sources, it round-trips byte-for-byte.

### Optimisation

A plan is emitted as it is written: a primitive per field, each with its own stack slot and its
own loop. Codegen does not inline across those calls, so the module goes through the ordinary
`-O2` pipeline first -- the same one a C compiler applies to the sources the other lane emits.

The two together are what make the lane fast; neither is sufficient. Round-tripping
`uavcan.primitive.array.Real32` with 64 elements and `uavcan.primitive.array.Bit` with 2048,
20,000 times each:

| | Real32 | Bit |
|---|---|---|
| One bit at a time, no optimisation | 115 ms | 115 ms |
| One bit at a time, optimised | 128 ms | 45 ms |
| Two paths, optimised | 10 ms | 0.3 ms |
| The C lane at `-O2` | 11 ms | 0.3 ms |

Where the build provides a single shared LLVM, that is what the object lane links. The static
component archives beside it put two copies of LLVM in one binary, and an analysis key is a
per-copy static: the pass manager then looks up an analysis registered in the other copy and
follows a null.

## The ABI boundary

Keep the boundary to pointers and scalars: the object in by pointer, the buffer by pointer, the
size by pointer. This is what the published C headers already declare.

Under that restriction the LLVM-dialect struct layout has to agree with the published header's
layout, and nothing more. Layout for aggregates of scalars and arrays under natural alignment is
well-defined and tractable. Widening the boundary to pass or return aggregates by value pulls in
target-specific argument classification — the work Clang's CodeGen does — which is not worth
taking on for a serialisation entry point that has no need of it.

## Acceptance

Six gates, in [`tools/gates/direct_lowering_gates.py`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/tools/gates/direct_lowering_gates.py),
run by `ctest -L direct-lowering-gate`. Each reports `PASS`, `NOT_IMPLEMENTED` or `FAIL`;
`-DLLVMDSDL_DIRECT_LOWERING_STRICT=ON` makes `NOT_IMPLEMENTED` a failure.

| Gate | Asserts | Phase |
| --- | --- | --- |
| 1 `ir-has-no-emitc` | No `emitc` ops survive `--convert-dsdl-to-llvm`, and `llvm` ops are present | B |
| 2 `emits-without-c-compiler` | Objects are produced with an empty `PATH` and `CC` unset | C |
| 3 `writes-no-c-intermediates` | No `.c` reaches the output tree or `TMPDIR`, and no staging tree exists | C |
| 4 `cross-target-without-toolchain` | `riscv32-unknown-elf` yields an ELF with `e_machine` `EM_RISCV`, 32-bit | C |
| 5 `object-matches-c-lane` | The object links and its wire bytes match the C lane | C |
| 6 `object-defines-every-entry-point` | Every `_ir_` prototype a published header declares is defined in its object | C |

Two limits are worth stating, because a gate believed to cover more than it does is worse than
no gate:

- **Gate 5 is satisfied by a C generator**, and that is correct. It measures whether the object
  agrees with the C lane, so an implementation that compiles the C lane agrees trivially. It is
  a correctness gate, not a mechanism gate. Gates 2, 3 and 4 are the mechanism gates; all three
  were validated against the removed lane, rebuilt, and fail it.
- **Gate 1 passes**, strict included: the fixture corpus lowers with no `emitc` and no `dsdl`
  operations remaining. It counts both, a conversion that declined to touch the plan leaving
  `dsdl` behind rather than `emitc`.
- **Gate 6 covers what gate 5 does not.** Gate 5 links one type. An object emitted for a plan
  the builder had declined carried none of its entry points while its header declared them, and
  `dsdlc` exited zero; the link failed later, in the user's build. Gate 6 reads every header's
  prototypes against its object's symbol table, for a target that is ELF on every host.

Gate 5's harness is proven against the C lane on both sides by
`llvmdsdl-direct-lowering-gate-harness-selftest`.

## Consumers

`DSDLCGenerate.cmake` and the `rules_dsdl` Bazel rules link the published objects directly; there
is no archive to wrap. Both showroom recipes build and round-trip against them.
