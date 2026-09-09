# Backend Translation

A backend is a translation of MLIR: one pass pipeline turns every serialisation plan into a
serialise and a deserialise function of dialect operations, and a backend spells those functions
in its language. [DESIGN.md](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/DESIGN.md)
states this as the backend contract; `ctest -L backend-contract` accepts a backend against it, and
nothing else does. C and obj meet the contract. C++, Rust, Go, TypeScript and Python do not, and
this page is the record of making them.

## Bodies are not derived from the IR

Every emitter calls `collectLoweredFactsFromMlir`, and for five of them that is the whole of their
MLIR consumption. The `LoweredFactsMap` it returns holds, per field, a step index and the names
of helper symbols; per section, a capacity-check helper name, the union tag width and the alias
flag. It holds no operations. The serialise and deserialise bodies of C++, Rust and Go come from
`SerDesStatementPlan` and `NativeEmitterTraversal`; those of TypeScript and Python from
`RuntimeLoweredPlan` and `ScriptedOperationPlan` — planners in `lib/CodeGen` that walk the
semantic module and decide the control flow themselves. Only the C lane runs
`build-dsdl-plan-bodies` and translates its output, through EmitC for C source and through the
LLVM dialect for objects.

The gates measure exactly this. Perturb an `dsdl.io` operation's width with the semantic module
held constant and the five backends' bodies do not change; perturb the semantic module's cast
mode with the operations held constant and they do. Three times the architecture was asked for
and delivered in that shape, each time passing as the real thing because every emitter consumed
the dialect. Consuming lowered facts is not translating lowered operations.

## The pipeline is one

`lower-dsdl-bodies` is `lower-dsdl-exec`, `dsdl-annotate-aliasability` and
`build-dsdl-plan-bodies`, defined once in `lib/Transforms` as `addLowerDSDLBodiesPipeline` and
registered with `dsdl-opt` under that name. The C lane runs it for both of its artefacts.

The C lane runs it per definition, after stamping C names on the schema, and translates each
result.

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

## One translator, five spellings

The body vocabulary is fixed: the `dsdl` operations above, of which ten map onto runtime
primitives every language runtime already provides (`set_uxx`, `get_u8` to `get_u64`, `set_f16`,
`copy_bits` and their kin); a dozen `arith` operations; `scf.if`, `scf.while` and `scf.for`;
`func.call` and `func.return`; and the helper functions lowering synthesises, which are `arith`
and `scf` and translate like any other function.

The translator has the shape of `mlir::emitc::translateToCpp`: walk a function, name its values,
emit structured statements, and dispatch each operation through a `LanguageSpelling` — types,
operators with width and sign, runtime primitive calls, member, element, array and union access,
signatures. One skeleton; one spelling per language. Its signature is
`translateBodies(mlir::ModuleOp bodies, const LanguageSpelling&, SourceWriter&)`: it takes no
`SemanticModule` and no `LoweredFactsMap`.

Declarations, module layout, manifests, constants, deprecation notices and runtime embedding stay
in each emitter and keep reading the semantic module. The body half of each emitter — the
function-body emitters, the helper-binding spellings, the alignment, padding and composite
renderers — is replaced by a call into the translator.

## Each backend, in order

Each step is one change. A backend's gate turns green and it joins
`LLVMDSDL_BACKEND_CONTRACT_ENFORCED` in that same change; its body renderers are deleted in that
same change.

1. **C++** — first because its object model is C's; the `std`, `pmr` and `autosar` profiles
   differ only in how `element_addr` and `set_array_length` spell containers. Oracle: the C↔C++
   parity lanes.
2. **Rust** — enum unions built from `set_union_tag` and the store to the selected option, `Vec` and bounded arrays
   behind `set_array_length`; `std` and `no-std-alloc`, both runtime specialisations, both memory
   modes. Oracle: the C↔Rust parity lanes and their variants.
3. **Go** — slices and structs. Oracle: the C↔Go parity lanes.
4. **TypeScript** — the integer model is the work: `arith` on `i64` needs `bigint`, narrower
   arithmetic needs explicit truncation. Oracle: the `c-ts-*` fixtures, `bigint-parity` and
   `truncated-decode-parity` among them.
5. **Python** — arbitrary-precision integers, so wrap semantics are explicit masks;
   `auto|pure|accel` is a primitive-table choice. Oracle: the `c-python-*` fixtures and the
   runtime-execution lanes.

## What is removed

With the last backend converted: the planners — `SerDesStatementPlan`, `NativeEmitterTraversal`,
`RuntimeLoweredPlan`, `ScriptedOperationPlan`, `ScriptedBodyPlan`, `LoweredBodyPlan`,
`LoweredRenderIR`, `SectionHelperBodies`, `RuntimeHelperBindings`, `NativeHelperContract`,
`SectionHelperBindingPlan`, `LoweredFactsLookup` — and `MlirLoweredFacts` with them. The
convergence report and its scorecard page, which grade an emitter by the presence of
`collectLoweredFactsFromMlir(` in its source. The five per-language emit-order traces, replaced
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
