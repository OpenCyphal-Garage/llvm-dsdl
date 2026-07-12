---------------------------- MODULE CyphalSerdes ----------------------------
(***************************************************************************)
(* Abstract control-flow / round-trip model of Cyphal DSDL serialization   *)
(* and deserialization as a transition system.                             *)
(*                                                                         *)
(* SCOPE (deliberate): this models the SEQUENCE of wire operations and the *)
(* structural invertibility of serialize/deserialize -- NOT the bit-exact  *)
(* byte layout.  Bit-exactness (widths, saturation, endianness, NaN) is    *)
(* verified empirically by the differential-parity harnesses; here the     *)
(* wire is an ordered stream of typed *tokens*, and a scalar's value is an  *)
(* opaque tag that must survive the round trip.                            *)
(*                                                                         *)
(* CHECKED PROPERTIES (see Inv):                                           *)
(*   AllRoundTrip  : De(T, Ser(v).wire) reconstructs v and consumes all    *)
(*                   tokens, for every value v of every test type T.       *)
(*   AllBoundsSafe : De evaluates without reading past the buffer on ANY   *)
(*                   truncation, and accepts iff the buffer is complete     *)
(*                   (no false-accept of a truncated/adversarial stream).  *)
(*   AllSerOrder   : the serialize emit-trace obeys validate-before-write,  *)
(*                   mask-before-write, len-validate-before-len-write, and  *)
(*                   advance-immediately-after-write.                       *)
(*   AllDeOrder    : the deserialize emit-trace obeys read-before-validate  *)
(*                   for tags and lengths.                                  *)
(*                                                                         *)
(* Ser(v).ops and DeOps(v) ARE the emit-order oracle: the accepted op      *)
(* orderings the B1 verifier checks each generated backend against.  The   *)
(* prose spec docs/plans/P2_canonical_emit_order.md is the human-readable   *)
(* projection of this module.                                              *)
(*                                                                         *)
(* NOTE ON ASSURANCE SCOPE: this proves the abstract TRANSITION SYSTEM,     *)
(* not the emitted C++/Rust/Go code.  The link from this model to the       *)
(* generated code is B1 (testing each backend's op-trace against the        *)
(* orderings derived here), not a refinement proof.                        *)
(***************************************************************************)
EXTENDS Naturals, Sequences, FiniteSets

CONSTANTS ScalarVals,   \* opaque scalar value domain, e.g. {0, 1}
          MaxElems      \* max elements considered in a variable-length array

W  == 8   \* a representative scalar bit-width (opaque here)
TB == 8   \* a representative union tag width
PB == 8   \* a representative array length-prefix width

NullVal == [k |-> "null"]   \* sentinel value returned on a failed deserialize

(***************************************************************************)
(* Recursive-operator forward declarations (mutual recursion).             *)
(***************************************************************************)
RECURSIVE Ser(_)
RECURSIVE SerFields(_)
RECURSIVE SerElems(_)
RECURSIVE De(_, _)
RECURSIVE DeFields(_, _)
RECURSIVE DeMany(_, _, _)
RECURSIVE DeOps(_)
RECURSIVE DeOpsFields(_)
RECURSIVE DeOpsElems(_)
RECURSIVE ValuesOf(_)
RECURSIVE FieldTuples(_)

(***************************************************************************)
(* SERIALIZE: value -> [ops : emit-order trace, wire : token stream]       *)
(***************************************************************************)
SerFields(fs) ==
    IF fs = <<>>
    THEN [ops |-> <<>>, wire |-> <<>>]
    ELSE LET r1 == Ser(Head(fs))
             rr == SerFields(Tail(fs))
         IN  [ops  |-> <<"ALIGN">> \o r1.ops \o rr.ops,   \* align before each field
              wire |-> r1.wire \o rr.wire]

SerElems(es) ==
    IF es = <<>>
    THEN [ops |-> <<>>, wire |-> <<>>]
    ELSE LET r1 == Ser(Head(es))
             rr == SerElems(Tail(es))
         IN  [ops |-> r1.ops \o rr.ops, wire |-> r1.wire \o rr.wire]

Ser(v) ==
    CASE v.k = "scal" ->
            [ops  |-> <<"WRITE_SCALAR", "ADVANCE">>,
             wire |-> << [t |-> "scal", w |-> v.w, val |-> v.val] >>]
      [] v.k = "pad" ->
            [ops  |-> <<"PAD", "ADVANCE">>,
             wire |-> << [t |-> "pad", w |-> v.w] >>]
      [] v.k = "struct" ->
            LET r == SerFields(v.fields)
            IN  [ops |-> r.ops \o <<"ALIGN">>,   \* trailing byte-boundary align
                 wire |-> r.wire]
      [] v.k = "farr" ->
            LET r == SerElems(v.elems)
            IN  [ops |-> <<"ELEM_LOOP">> \o r.ops, wire |-> r.wire]
      [] v.k = "varr" ->
            LET r == SerElems(v.elems)
            IN  [ops  |-> <<"LEN_VALIDATE", "LEN_WRITE", "ADVANCE", "ELEM_LOOP">> \o r.ops,
                 wire |-> << [t |-> "len", pb |-> v.pb, val |-> Len(v.elems)] >> \o r.wire]
      [] v.k = "union" ->
            LET r == Ser(v.sel)
            IN  [ops  |-> <<"VALIDATE_TAG", "MASK_TAG", "WRITE_TAG", "ADVANCE",
                            "SWITCH", "CASE", "ALIGN">> \o r.ops \o <<"DEFAULT_BAD_TAG">>,
                 wire |-> << [t |-> "tag", tb |-> v.tb, val |-> v.tag] >> \o r.wire]
      [] v.k = "comp" ->
            LET r == Ser(v.inner)
            IN  IF v.sealed
                THEN [ops |-> <<"COMPOSITE_INLINE">> \o r.ops, wire |-> r.wire]
                ELSE [ops  |-> <<"COMPOSITE_DELIM_HEADER">> \o r.ops,
                      wire |-> << [t |-> "delim", val |-> Len(r.wire)] >> \o r.wire]
      [] OTHER -> [ops |-> <<>>, wire |-> <<>>]

(***************************************************************************)
(* DESERIALIZE: (type, wire) -> [val, rest, ok].  Every token access is    *)
(* guarded by a length check, so De is total on ANY wire (bounds-safe).    *)
(***************************************************************************)
DeFields(fts, w) ==
    IF fts = <<>>
    THEN [vals |-> <<>>, rest |-> w, ok |-> TRUE]
    ELSE LET r == De(Head(fts), w)
         IN  IF ~r.ok
             THEN [vals |-> <<>>, rest |-> r.rest, ok |-> FALSE]
             ELSE LET rr == DeFields(Tail(fts), r.rest)
                  IN  [vals |-> <<r.val>> \o rr.vals, rest |-> rr.rest, ok |-> rr.ok]

DeMany(et, n, w) ==
    IF n = 0
    THEN [vals |-> <<>>, rest |-> w, ok |-> TRUE]
    ELSE LET r == De(et, w)
         IN  IF ~r.ok
             THEN [vals |-> <<>>, rest |-> r.rest, ok |-> FALSE]
             ELSE LET rr == DeMany(et, n - 1, r.rest)
                  IN  [vals |-> <<r.val>> \o rr.vals, rest |-> rr.rest, ok |-> rr.ok]

De(T, w) ==
    CASE T.k = "scal" ->
            IF Len(w) >= 1 /\ w[1].t = "scal"
            THEN [val |-> [k |-> "scal", w |-> T.w, val |-> w[1].val], rest |-> Tail(w), ok |-> TRUE]
            ELSE [val |-> NullVal, rest |-> w, ok |-> FALSE]
      [] T.k = "pad" ->
            IF Len(w) >= 1 /\ w[1].t = "pad"
            THEN [val |-> [k |-> "pad", w |-> T.w], rest |-> Tail(w), ok |-> TRUE]
            ELSE [val |-> NullVal, rest |-> w, ok |-> FALSE]
      [] T.k = "struct" ->
            LET r == DeFields(T.fields, w)
            IN  [val |-> [k |-> "struct", fields |-> r.vals], rest |-> r.rest, ok |-> r.ok]
      [] T.k = "farr" ->
            LET r == DeMany(T.elem, T.cap, w)
            IN  [val |-> [k |-> "farr", elems |-> r.vals], rest |-> r.rest, ok |-> r.ok]
      [] T.k = "varr" ->
            IF Len(w) >= 1 /\ w[1].t = "len"
            THEN LET n == w[1].val
                 IN  IF n <= MaxElems                     \* length-prefix bounds check
                     THEN LET r == DeMany(T.elem, n, Tail(w))
                          IN  [val |-> [k |-> "varr", pb |-> T.pb, elems |-> r.vals],
                               rest |-> r.rest, ok |-> r.ok]
                     ELSE [val |-> NullVal, rest |-> Tail(w), ok |-> FALSE]
            ELSE [val |-> NullVal, rest |-> w, ok |-> FALSE]
      [] T.k = "union" ->
            IF Len(w) >= 1 /\ w[1].t = "tag"
            THEN LET tag == w[1].val
                 IN  IF tag \in 0..(Len(T.opts) - 1)      \* tag validity check
                     THEN LET r == De(T.opts[tag + 1], Tail(w))
                          IN  [val |-> [k |-> "union", tb |-> T.tb, tag |-> tag, sel |-> r.val],
                               rest |-> r.rest, ok |-> r.ok]
                     ELSE [val |-> NullVal, rest |-> Tail(w), ok |-> FALSE]
            ELSE [val |-> NullVal, rest |-> w, ok |-> FALSE]
      [] T.k = "comp" ->
            IF T.sealed
            THEN LET r == De(T.inner, w)
                 IN  [val |-> [k |-> "comp", sealed |-> TRUE, inner |-> r.val],
                      rest |-> r.rest, ok |-> r.ok]
            ELSE IF Len(w) >= 1 /\ w[1].t = "delim"
                 THEN LET sz   == w[1].val
                          body == Tail(w)
                      IN  IF sz <= Len(body)              \* delimiter bounds check
                          THEN LET chunk == SubSeq(body, 1, sz)
                                   after == SubSeq(body, sz + 1, Len(body))
                                   r     == De(T.inner, chunk)
                               IN  IF r.ok /\ r.rest = <<>>
                                   THEN [val |-> [k |-> "comp", sealed |-> FALSE, inner |-> r.val],
                                         rest |-> after, ok |-> TRUE]
                                   ELSE [val |-> NullVal, rest |-> after, ok |-> FALSE]
                          ELSE [val |-> NullVal, rest |-> body, ok |-> FALSE]
                 ELSE [val |-> NullVal, rest |-> w, ok |-> FALSE]
      [] OTHER -> [val |-> NullVal, rest |-> w, ok |-> FALSE]

(***************************************************************************)
(* DESERIALIZE emit-trace (structural, for the read-order invariant).      *)
(***************************************************************************)
DeOpsFields(fs) ==
    IF fs = <<>> THEN <<>>
    ELSE <<"ALIGN">> \o DeOps(Head(fs)) \o DeOpsFields(Tail(fs))

DeOpsElems(es) ==
    IF es = <<>> THEN <<>>
    ELSE DeOps(Head(es)) \o DeOpsElems(Tail(es))

DeOps(v) ==
    CASE v.k = "scal" -> <<"READ_SCALAR", "ADVANCE">>
      [] v.k = "pad"  -> <<"PAD", "ADVANCE">>
      [] v.k = "struct" -> DeOpsFields(v.fields) \o <<"ALIGN">>
      [] v.k = "farr" -> <<"ELEM_LOOP">> \o DeOpsElems(v.elems)
      [] v.k = "varr" -> <<"LEN_READ", "ADVANCE", "LEN_VALIDATE", "ELEM_LOOP">> \o DeOpsElems(v.elems)
      [] v.k = "union" -> <<"READ_TAG", "MASK_TAG", "STORE_TAG", "VALIDATE_TAG",
                            "ADVANCE", "SWITCH", "CASE", "ALIGN">>
                          \o DeOps(v.sel) \o <<"DEFAULT_BAD_TAG">>
      [] v.k = "comp" -> IF v.sealed
                         THEN <<"COMPOSITE_INLINE">> \o DeOps(v.inner)
                         ELSE <<"COMPOSITE_DELIM_HEADER">> \o DeOps(v.inner)
      [] OTHER -> <<>>

(***************************************************************************)
(* Ordering predicates on emit-traces (the equivalence-class constraints). *)
(***************************************************************************)
SerOrderOK(ops) ==
    /\ \A i \in 1..Len(ops) :               \* validate & mask before write tag
          ops[i] = "WRITE_TAG" =>
             (i >= 3 /\ ops[i-1] = "MASK_TAG" /\ ops[i-2] = "VALIDATE_TAG")
    /\ \A i \in 1..Len(ops) :               \* length validated before written
          ops[i] = "LEN_WRITE" => (i >= 2 /\ ops[i-1] = "LEN_VALIDATE")
    /\ \A i \in 1..Len(ops) :               \* advance immediately after any write
          ops[i] \in {"WRITE_TAG", "LEN_WRITE", "WRITE_SCALAR"} =>
             (i < Len(ops) /\ ops[i+1] = "ADVANCE")

DeOrderOK(ops) ==
    /\ \A i \in 1..Len(ops) :               \* tag read (& masked & stored) before validated
          ops[i] = "VALIDATE_TAG" =>
             (i >= 4 /\ ops[i-1] = "STORE_TAG" /\ ops[i-2] = "MASK_TAG" /\ ops[i-3] = "READ_TAG")
    /\ \A i \in 1..Len(ops) :               \* length read (& advanced) before validated
          ops[i] = "LEN_VALIDATE" =>
             (i >= 3 /\ ops[i-1] = "ADVANCE" /\ ops[i-2] = "LEN_READ")

(***************************************************************************)
(* Bounded value generation for a type (the model-checked domain).         *)
(***************************************************************************)
FieldTuples(fts) ==
    IF fts = <<>> THEN { <<>> }
    ELSE { <<hv>> \o tv : hv \in ValuesOf(Head(fts)), tv \in FieldTuples(Tail(fts)) }

ValuesOf(T) ==
    CASE T.k = "scal" -> { [k |-> "scal", w |-> T.w, val |-> x] : x \in ScalarVals }
      [] T.k = "pad"  -> { [k |-> "pad", w |-> T.w] }
      [] T.k = "struct" -> { [k |-> "struct", fields |-> ft] : ft \in FieldTuples(T.fields) }
      [] T.k = "farr" -> { [k |-> "farr", elems |-> es] : es \in [1..T.cap -> ValuesOf(T.elem)] }
      [] T.k = "varr" ->
            { [k |-> "varr", pb |-> T.pb, elems |-> es] :
                 es \in UNION { [1..n -> ValuesOf(T.elem)] : n \in 0..MaxElems } }
      [] T.k = "union" ->
            UNION { { [k |-> "union", tb |-> T.tb, tag |-> (i - 1), sel |-> sv] :
                        sv \in ValuesOf(T.opts[i]) } : i \in 1..Len(T.opts) }
      [] T.k = "comp" ->
            { [k |-> "comp", sealed |-> T.sealed, inner |-> iv] : iv \in ValuesOf(T.inner) }
      [] OTHER -> {}

(***************************************************************************)
(* Test schemas: cover every construct plus key nestings.                  *)
(***************************************************************************)
TScal   == [k |-> "scal", w |-> W]
TPad    == [k |-> "pad", w |-> W]
TStruct == [k |-> "struct", fields |-> <<TScal, TScal>>]
TMixed  == [k |-> "struct", fields |-> <<TScal, TPad, TScal>>]
TFArr   == [k |-> "farr", cap |-> 2, elem |-> TScal]
TVArr   == [k |-> "varr", pb |-> PB, elem |-> TScal]
TUnion  == [k |-> "union", tb |-> TB, opts |-> <<TScal, TVArr>>]
TSealed == [k |-> "comp", sealed |-> TRUE,  inner |-> TStruct]
TDelim  == [k |-> "comp", sealed |-> FALSE, inner |-> TStruct]
TUniInStruct == [k |-> "struct", fields |-> <<TScal, TUnion>>]
TStructInVArr == [k |-> "varr", pb |-> PB, elem |-> TStruct]
TDelimUnion == [k |-> "comp", sealed |-> FALSE, inner |-> TUnion]

TestTypes == { TScal, TPad, TStruct, TMixed, TFArr, TVArr, TUnion,
               TSealed, TDelim, TUniInStruct, TStructInVArr, TDelimUnion }

(***************************************************************************)
(* The checked properties.                                                 *)
(***************************************************************************)
AllRoundTrip ==
    \A T \in TestTypes : \A v \in ValuesOf(T) :
        De(T, Ser(v).wire) = [val |-> v, rest |-> <<>>, ok |-> TRUE]

AllBoundsSafe ==
    \A T \in TestTypes : \A v \in ValuesOf(T) :
        LET w == Ser(v).wire
        IN  \A k \in 0..Len(w) : De(T, SubSeq(w, 1, k)).ok = (k = Len(w))

AllSerOrder == \A T \in TestTypes : \A v \in ValuesOf(T) : SerOrderOK(Ser(v).ops)
AllDeOrder  == \A T \in TestTypes : \A v \in ValuesOf(T) : DeOrderOK(DeOps(v))

(***************************************************************************)
(* Trivial single-transition machine so TLC evaluates the invariant.       *)
(***************************************************************************)
VARIABLE done
Init == done = 0
Next == done' = 1
Spec == Init /\ [][Next]_done

\* The `done \in {0,1}` conjunct ties Inv to the state variable so TLC checks it
\* as a genuine per-state invariant (not a constant-folded formula); it is always true.
Inv == (done \in {0, 1}) /\ AllRoundTrip /\ AllBoundsSafe /\ AllSerOrder /\ AllDeOrder
=============================================================================
