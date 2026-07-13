// SPDX-License-Identifier: MIT
//
// Formal control-flow / round-trip model of Cyphal DSDL serialize/deserialize.
//
// SCOPE (deliberate): models the SEQUENCE of wire operations and the structural
// invertibility of serialize/deserialize -- NOT bit-exact byte layout. The wire is
// an ordered stream of typed *tokens*; a scalar's value is an opaque tag that must
// survive the round trip. Tokens are ATOMIC: effects that split a field mid-way
// (byte-level truncation inside a scalar) are below this abstraction -- exact for
// schema-evolution skew (appended fields start on token boundaries), coarse for
// arbitrary corruption. Bit-exactness (widths, saturation, endianness, NaN) is
// verified empirically by the differential-parity harnesses.
//
// WHAT IS PROVEN (unbounded -- for all conforming values, by structural induction):
//   RoundTrip     : De(t, SerWire(v) + suffix) == Some((v, suffix)) for v conforming
//                   to t. Serialize/deserialize are genuine inverses; the reader
//                   consumes exactly what the writer produced.
//   DeCanonical   : the converse -- every wire De accepts decodes to a conforming
//                   value whose re-serialization is exactly the consumed prefix, so
//                   De accepts EXACTLY SerWire's image. Together the pair pins both
//                   functions: leniency added to De breaks DeCanonical, strictness
//                   breaks RoundTrip.
//   Tolerance     : DeCompat models DSDL's implicit truncation + zero extension at
//                   token granularity. DeCompatConservative: it agrees with De on
//                   every wire De accepts; ZeroExtension: an exhausted wire reads
//                   as the all-zeros value; DeCompatConforms: outputs conform.
//   Version skew  : the extensibility contract. For Evolves(tNew, tOld) -- field
//                   append at DELIMITED boundaries only -- a new reader decodes an
//                   old wire to Upgrade(v) (appended fields read as zeros), and an
//                   old reader decodes a new wire to Downgrade(v) (appended fields
//                   skipped via the delimiter): OldWireNewReader / NewWireOldReader.
//                   Sealed layouts admit no append rule -- frozen by construction.
//   Bounds safety : De is a TOTAL function with no precondition, so Dafny forbids any
//                   out-of-bounds token access by construction -- there is no wire
//                   (truncated, adversarial, empty) on which De is undefined.
//   Order oracle  : CanonicalTracesOrderOK -- SerOps/DeOps satisfy SerOrderOK/DeOrderOK
//                   for ALL values, so the canonical traces can never disagree with
//                   the ordering predicates B1 applies to backend traces.
//
// SerOps(v) / DeOps(v) are the emit-order oracle -- the accepted op orderings the B1
// verifier checks each generated backend against. SerOrderOK / DeOrderOK are those
// ordering constraints; docs/plans/P2_canonical_emit_order.md is the prose projection.
//
// ASSURANCE SCOPE: this proves the abstract model, not the emitted C++/Rust/Go code.
// The link from model to code is B1 (testing each backend's op-trace against the
// orderings here), not a refinement proof.

module CyphalSerdes {

  datatype Option<T> = None | Some(value: T)

  // ---- Data model ----------------------------------------------------------

  // A schema. Deserialize is driven by this (the reader knows the type, not the value).
  datatype Typ =
    | TScal(w: nat)
    | TPad(w: nat)
    | TStruct(fields: seq<Typ>)
    | TFArr(cap: nat, elem: Typ)
    | TVArr(pb: nat, cap: nat, elem: Typ)
    | TUnion(tb: nat, opts: seq<Typ>)
    | TComp(sealed: bool, inner: Typ)

  // A value being (de)serialized. `val` on a scalar is opaque; it only has to survive.
  datatype Value =
    | Scal(w: nat, val: nat)
    | Pad(w: nat)
    | Struct(fields: seq<Value>)
    | FArr(elems: seq<Value>)
    | VArr(pb: nat, elems: seq<Value>)
    | Union(tb: nat, tag: nat, sel: Value)
    | Comp(sealed: bool, inner: Value)

  // One element of the abstract wire.
  datatype Token =
    | TokScal(w: nat, val: nat)
    | TokPad(w: nat)
    | TokTag(tb: nat, val: nat)
    | TokLen(pb: nat, val: nat)
    | TokDelim(len: nat)

  // The emit-order op alphabet (mirrors EmitTrace.h's EmitTraceOp).
  datatype Op =
    | ValidateTag | MaskTag | WriteTag | ReadTag | StoreTag
    | Switch | Case | DefaultBadTag | Align | PadOp
    | WriteScalar | ReadScalar | LenCheck | LenValidate
    | LenWrite | LenRead | ElemLoop | CompositeInline
    | CompositeDelimHeader | Advance

  // ---- Conformance: does a value inhabit a type? ---------------------------

  predicate ConformsTo(v: Value, t: Typ)
    decreases v
  {
    match t
    case TScal(w)        => v.Scal? && v.w == w
    case TPad(w)         => v.Pad? && v.w == w
    case TStruct(fts)    => v.Struct? && |v.fields| == |fts|
                            && (forall i | 0 <= i < |fts| :: ConformsTo(v.fields[i], fts[i]))
    case TFArr(cap, et)  => v.FArr? && |v.elems| == cap
                            && (forall i | 0 <= i < |v.elems| :: ConformsTo(v.elems[i], et))
    case TVArr(pb, cap, et) => v.VArr? && v.pb == pb && |v.elems| <= cap
                            && (forall i | 0 <= i < |v.elems| :: ConformsTo(v.elems[i], et))
    case TUnion(tb, ops) => v.Union? && v.tb == tb && v.tag < |ops|
                            && ConformsTo(v.sel, ops[v.tag])
    case TComp(sl, it)   => v.Comp? && v.sealed == sl && ConformsTo(v.inner, it)
  }

  // ---- Serialize: value -> wire (token stream) -----------------------------

  function SerWire(v: Value): seq<Token>
    decreases v
  {
    match v
    case Scal(w, val)     => [TokScal(w, val)]
    case Pad(w)           => [TokPad(w)]
    case Struct(fs)       => SerWireSeq(fs)
    case FArr(es)         => SerWireSeq(es)
    case VArr(pb, es)     => [TokLen(pb, |es|)] + SerWireSeq(es)
    case Union(tb, tg, s) => [TokTag(tb, tg)] + SerWire(s)
    case Comp(sl, inn)    => if sl then SerWire(inn)
                             else [TokDelim(|SerWire(inn)|)] + SerWire(inn)
  }

  function SerWireSeq(vs: seq<Value>): seq<Token>
    decreases vs
  {
    if |vs| == 0 then []
    else SerWire(vs[0]) + SerWireSeq(vs[1..])
  }

  // ---- Deserialize: (type, wire) -> Option<(value, rest)> ------------------
  // Total (no precondition): Dafny forbids the out-of-bounds token access, so this
  // is bounds-safe on ANY wire by construction. Token kind AND metadata (width /
  // pb / tb) are validated against the schema, so acceptance is canonical -- the
  // guards below are exactly what DeCanonical needs to reconstruct the wire. The
  // variable-array length prefix is additionally validated against capacity
  // (w[0].val <= cap) -- the functional counterpart of the LenValidate op, the
  // same way ValidateTag corresponds to the union tag-range guard.

  function De(t: Typ, w: seq<Token>): Option<(Value, seq<Token>)>
    decreases t, 0
  {
    match t
    case TScal(width) =>
      if |w| >= 1 && w[0].TokScal? && w[0].w == width
      then Some((Scal(width, w[0].val), w[1..])) else None
    case TPad(width) =>
      if |w| >= 1 && w[0].TokPad? && w[0].w == width
      then Some((Pad(width), w[1..])) else None
    case TStruct(fts) =>
      (match DeSeq(fts, w)
       case None => None
       case Some((vs, rest)) => Some((Struct(vs), rest)))
    case TFArr(cap, et) =>
      (match DeCount(et, cap, w)
       case None => None
       case Some((vs, rest)) => Some((FArr(vs), rest)))
    case TVArr(pb, cap, et) =>
      if |w| >= 1 && w[0].TokLen? && w[0].pb == pb && w[0].val <= cap then
        (match DeCount(et, w[0].val, w[1..])
         case None => None
         case Some((vs, rest)) => Some((VArr(pb, vs), rest)))
      else None
    case TUnion(tb, opts) =>
      if |w| >= 1 && w[0].TokTag? && w[0].tb == tb && w[0].val < |opts| then
        (match De(opts[w[0].val], w[1..])
         case None => None
         case Some((sv, rest)) => Some((Union(tb, w[0].val, sv), rest)))
      else None
    case TComp(sl, it) =>
      if sl then
        (match De(it, w)
         case None => None
         case Some((iv, rest)) => Some((Comp(true, iv), rest)))
      else
        if |w| >= 1 && w[0].TokDelim? && w[0].len <= |w[1..]| then
          var body := w[1..];
          var sz := w[0].len;
          (match De(it, body[..sz])
           case Some((iv, r2)) =>
             if r2 == [] then Some((Comp(false, iv), body[sz..])) else None
           case None => None)
        else None
  }

  function DeSeq(fts: seq<Typ>, w: seq<Token>): Option<(seq<Value>, seq<Token>)>
    decreases fts, 0
  {
    if |fts| == 0 then Some(([], w))
    else
      (match De(fts[0], w)
       case None => None
       case Some((v0, rest0)) =>
         (match DeSeq(fts[1..], rest0)
          case None => None
          case Some((vs, rest)) => Some(([v0] + vs, rest))))
  }

  function DeCount(et: Typ, n: nat, w: seq<Token>): Option<(seq<Value>, seq<Token>)>
    decreases et, n
  {
    if n == 0 then Some(([], w))
    else
      (match De(et, w)
       case None => None
       case Some((v0, rest0)) =>
         (match DeCount(et, n - 1, rest0)
          case None => None
          case Some((vs, rest)) => Some(([v0] + vs, rest))))
  }

  // ---- Emit-order traces (the oracle) --------------------------------------

  function SerOps(v: Value): seq<Op>
    decreases v
  {
    match v
    case Scal(w, val)     => [WriteScalar, Advance]
    case Pad(w)           => [PadOp, Advance]
    case Struct(fs)       => SerOpsSeq(fs) + [Align]
    case FArr(es)         => [ElemLoop] + SerOpsSeq(es)
    case VArr(pb, es)     => [LenValidate, LenWrite, Advance, ElemLoop] + SerOpsSeq(es)
    case Union(tb, tg, s) => [ValidateTag, MaskTag, WriteTag, Advance, Switch, Case, Align]
                             + SerOps(s) + [DefaultBadTag]
    case Comp(sl, inn)    => if sl then [CompositeInline] + SerOps(inn)
                             else [CompositeDelimHeader] + SerOps(inn)
  }

  function SerOpsSeq(vs: seq<Value>): seq<Op>
    decreases vs
  {
    if |vs| == 0 then []
    else [Align] + SerOps(vs[0]) + SerOpsSeq(vs[1..])   // align before each struct field
  }

  function DeOps(v: Value): seq<Op>
    decreases v
  {
    match v
    case Scal(w, val)     => [ReadScalar, Advance]
    case Pad(w)           => [PadOp, Advance]
    case Struct(fs)       => DeOpsSeq(fs) + [Align]
    case FArr(es)         => [ElemLoop] + DeOpsSeq(es)
    case VArr(pb, es)     => [LenRead, Advance, LenValidate, ElemLoop] + DeOpsSeq(es)
    case Union(tb, tg, s) => [ReadTag, MaskTag, StoreTag, ValidateTag, Advance, Switch, Case, Align]
                             + DeOps(s) + [DefaultBadTag]
    case Comp(sl, inn)    => if sl then [CompositeInline] + DeOps(inn)
                             else [CompositeDelimHeader] + DeOps(inn)
  }

  function DeOpsSeq(vs: seq<Value>): seq<Op>
    decreases vs
  {
    if |vs| == 0 then []
    else [Align] + DeOps(vs[0]) + DeOpsSeq(vs[1..])
  }

  // ---- Ordering constraints (the equivalence class B1 checks) --------------

  predicate SerOrderOK(ops: seq<Op>) {
    (forall i | 0 <= i < |ops| ::
        ops[i] == WriteTag ==> i >= 2 && ops[i-1] == MaskTag && ops[i-2] == ValidateTag)
    && (forall i | 0 <= i < |ops| ::
        ops[i] == LenWrite ==> i >= 1 && ops[i-1] == LenValidate)
    && (forall i | 0 <= i < |ops| ::
        ops[i] in {WriteTag, LenWrite, WriteScalar} ==> i + 1 < |ops| && ops[i+1] == Advance)
  }

  predicate DeOrderOK(ops: seq<Op>) {
    (forall i | 0 <= i < |ops| ::
        ops[i] == ValidateTag ==>
          i >= 3 && ops[i-1] == StoreTag && ops[i-2] == MaskTag && ops[i-3] == ReadTag)
    && (forall i | 0 <= i < |ops| ::
        ops[i] == LenValidate ==> i >= 2 && ops[i-1] == Advance && ops[i-2] == LenRead)
  }

  // ==========================================================================
  // PROOF: round-trip identity, for ALL conforming values (unbounded).
  //
  // The generalization over an arbitrary `suffix` is what makes the sequential
  // cases (struct fields, array elements, delimited bodies) compose: when a field
  // is deserialized from  field_wire + rest_of_message,  `rest_of_message` is the
  // suffix. RoundTrip is the special case suffix == [].
  // ==========================================================================

  lemma RoundTrip(t: Typ, v: Value, suffix: seq<Token>)
    requires ConformsTo(v, t)
    ensures De(t, SerWire(v) + suffix) == Some((v, suffix))
    decreases v
  {
    match v
    case Scal(w, val) =>
      assert SerWire(v) + suffix == [TokScal(w, val)] + suffix;
    case Pad(w) =>
      assert SerWire(v) + suffix == [TokPad(w)] + suffix;
    case Struct(fs) =>
      assert SerWire(v) + suffix == SerWireSeq(fs) + suffix;
      RoundTripSeq(t.fields, fs, suffix);
    case FArr(es) =>
      assert SerWire(v) + suffix == SerWireSeq(es) + suffix;
      RoundTripCount(t.elem, es, suffix);
    case VArr(pb, es) =>
      assert SerWire(v) + suffix == [TokLen(pb, |es|)] + (SerWireSeq(es) + suffix);
      RoundTripCount(t.elem, es, suffix);
    case Union(tb, tg, s) =>
      assert SerWire(v) + suffix == [TokTag(tb, tg)] + (SerWire(s) + suffix);
      RoundTrip(t.opts[tg], s, suffix);
    case Comp(sl, inn) =>
      if sl {
        RoundTrip(t.inner, inn, suffix);
      } else {
        var iw := SerWire(inn);
        RoundTrip(t.inner, inn, []);          // inner consumes its exact chunk
        assert iw + [] == iw;
        assert SerWire(v) + suffix == [TokDelim(|iw|)] + (iw + suffix);
        assert (iw + suffix)[..|iw|] == iw;
        assert (iw + suffix)[|iw|..] == suffix;
      }
  }

  lemma RoundTripSeq(fts: seq<Typ>, vs: seq<Value>, suffix: seq<Token>)
    requires |fts| == |vs|
    requires forall i | 0 <= i < |vs| :: ConformsTo(vs[i], fts[i])
    ensures DeSeq(fts, SerWireSeq(vs) + suffix) == Some((vs, suffix))
    decreases vs
  {
    if |vs| == 0 {
      assert fts == [];
      assert SerWireSeq(vs) + suffix == suffix;
      assert DeSeq(fts, suffix) == Some((vs, suffix));
    } else {
      assert vs == [vs[0]] + vs[1..];
      assert SerWireSeq(vs) + suffix == SerWire(vs[0]) + (SerWireSeq(vs[1..]) + suffix);
      assert forall i | 0 <= i < |vs[1..]| :: ConformsTo(vs[1..][i], fts[1..][i]);
      RoundTrip(fts[0], vs[0], SerWireSeq(vs[1..]) + suffix);
      RoundTripSeq(fts[1..], vs[1..], suffix);
    }
  }

  lemma RoundTripCount(et: Typ, vs: seq<Value>, suffix: seq<Token>)
    requires forall i | 0 <= i < |vs| :: ConformsTo(vs[i], et)
    ensures DeCount(et, |vs|, SerWireSeq(vs) + suffix) == Some((vs, suffix))
    decreases vs
  {
    if |vs| == 0 {
      assert SerWireSeq(vs) + suffix == suffix;
      assert DeCount(et, 0, suffix) == Some((vs, suffix));
    } else {
      assert vs == [vs[0]] + vs[1..];
      assert SerWireSeq(vs) + suffix == SerWire(vs[0]) + (SerWireSeq(vs[1..]) + suffix);
      RoundTrip(et, vs[0], SerWireSeq(vs[1..]) + suffix);
      RoundTripCount(et, vs[1..], suffix);
    }
  }

  // The headline corollary: full round trip with nothing left over.
  lemma RoundTripFull(t: Typ, v: Value)
    requires ConformsTo(v, t)
    ensures De(t, SerWire(v)) == Some((v, []))
  {
    RoundTrip(t, v, []);
    assert SerWire(v) + [] == SerWire(v);
  }

  // ==========================================================================
  // PROOF: canonical acceptance (the right inverse). Every wire De accepts
  // decodes to a CONFORMING value whose re-serialization is exactly the consumed
  // prefix -- De accepts nothing SerWire cannot produce. Together with RoundTrip
  // this pins De and SerWire completely: any added leniency in De breaks
  // DeCanonical, any added strictness breaks RoundTrip. (It is also why the
  // delimited case demands exact body consumption, r2 == [], and why De checks
  // token metadata: without either, non-canonical wires would be accepted.)
  //
  // This is a MODEL property -- the abstract wire grammar is unambiguous. Real
  // DSDL decoders are deliberately more tolerant (delimited version-skew
  // acceptance); see the README's assurance boundary before citing.
  // ==========================================================================

  lemma DeCanonical(t: Typ, w: seq<Token>)
    requires De(t, w).Some?
    ensures var (v, rest) := De(t, w).value;
            ConformsTo(v, t) && w == SerWire(v) + rest
    decreases t, 0
  {
    match t
    case TScal(width) =>
      assert w == [w[0]] + w[1..];
    case TPad(width) =>
      assert w == [w[0]] + w[1..];
    case TStruct(fts) =>
      DeSeqCanonical(fts, w);
    case TFArr(cap, et) =>
      DeCountCanonical(et, cap, w);
    case TVArr(pb, cap, et) =>
      DeCountCanonical(et, w[0].val, w[1..]);
      assert w == [w[0]] + w[1..];
    case TUnion(tb, opts) =>
      DeCanonical(opts[w[0].val], w[1..]);
      assert w == [w[0]] + w[1..];
    case TComp(sl, it) =>
      if sl {
        DeCanonical(it, w);
      } else {
        var body := w[1..];
        var sz := w[0].len;
        DeCanonical(it, body[..sz]);
        var (iv, r2) := De(it, body[..sz]).value;
        assert r2 == [];                       // exact consumption: else De is None
        assert body[..sz] == SerWire(iv) + r2;
        assert SerWire(iv) + [] == SerWire(iv);
        assert |SerWire(iv)| == sz;            // so the header value is forced
        assert w == [w[0]] + body;
        assert body == body[..sz] + body[sz..];
      }
  }

  lemma DeSeqCanonical(fts: seq<Typ>, w: seq<Token>)
    requires DeSeq(fts, w).Some?
    ensures var (vs, rest) := DeSeq(fts, w).value;
            |vs| == |fts|
            && (forall i | 0 <= i < |vs| :: ConformsTo(vs[i], fts[i]))
            && w == SerWireSeq(vs) + rest
    decreases fts, 0
  {
    if |fts| == 0 {
      assert [] + w == w;
    } else {
      DeCanonical(fts[0], w);
      var (v0, rest0) := De(fts[0], w).value;
      DeSeqCanonical(fts[1..], rest0);
      var (vs1, rest) := DeSeq(fts[1..], rest0).value;
      assert ([v0] + vs1)[1..] == vs1;
      assert SerWireSeq([v0] + vs1) == SerWire(v0) + SerWireSeq(vs1);
      assert w == SerWire(v0) + (SerWireSeq(vs1) + rest);
    }
  }

  lemma DeCountCanonical(et: Typ, n: nat, w: seq<Token>)
    requires DeCount(et, n, w).Some?
    ensures var (vs, rest) := DeCount(et, n, w).value;
            |vs| == n
            && (forall i | 0 <= i < |vs| :: ConformsTo(vs[i], et))
            && w == SerWireSeq(vs) + rest
    decreases et, n
  {
    if n == 0 {
      assert [] + w == w;
    } else {
      DeCanonical(et, w);
      var (v0, rest0) := De(et, w).value;
      DeCountCanonical(et, n - 1, rest0);
      var (vs1, rest) := DeCount(et, n - 1, rest0).value;
      assert ([v0] + vs1)[1..] == vs1;
      assert SerWireSeq([v0] + vs1) == SerWire(v0) + SerWireSeq(vs1);
      assert w == SerWire(v0) + (SerWireSeq(vs1) + rest);
    }
  }

  // The characterization corollary: De succeeds on EXACTLY the canonical wires.
  lemma DeAcceptanceCharacterization(t: Typ, w: seq<Token>)
    ensures De(t, w).Some? <==>
            exists v: Value, rest: seq<Token> ::
              ConformsTo(v, t) && w == SerWire(v) + rest
  {
    if De(t, w).Some? {
      DeCanonical(t, w);
      var (v, rest) := De(t, w).value;
      assert ConformsTo(v, t) && w == SerWire(v) + rest;
    }
    forall v: Value, rest: seq<Token> | ConformsTo(v, t) && w == SerWire(v) + rest
      ensures De(t, w).Some?
    {
      RoundTrip(t, v, rest);
    }
  }

  // ==========================================================================
  // TOLERANT DECODER (DSDL implicit truncation + zero extension) -- Phase 1.
  // DeCompat is the version-skew-tolerant reader; De stays the canonical one.
  //   Zero extension : an exhausted wire denotes an all-zeros remainder, so any
  //                    read past the end yields the zero value of the requested
  //                    type (the |w| == 0 shortcut at the top of DeCompat).
  //   Truncation     : a delimited section is consumed to exactly its declared
  //                    length; nested leftover inside the section is skipped. A
  //                    header overrunning the physical wire clamps to what is
  //                    available (the rest is virtual zeros).
  // Present-but-WRONG data is still an error: bad union tag, over-capacity
  // length, or metadata mismatch return None. Tolerance is only about MISSING
  // data. Tokens are atomic; see SCOPE at the top of the file.
  // Envelope: DeCompatConservative (agrees with De wherever De succeeds, so
  // tolerance never changes the meaning of a canonical wire), DeCompatRoundTrip,
  // ZeroExtension, DeCompatConforms, and the concrete CompatTolerates /
  // CompatStillRejects checks. DeCompat deliberately has NO canonicity theorem:
  // its acceptance is a strict superset of the canonical wires. Cross-version
  // skew theorems (the Evolves relation) are Phase 2.
  // ==========================================================================

  // Well-formed schema: unions are non-empty, so option 0 exists for the zero
  // value. (Real DSDL requires >= 2 options; the model only needs >= 1.)
  predicate WFTyp(t: Typ)
    decreases t
  {
    match t
    case TScal(w)           => true
    case TPad(w)            => true
    case TStruct(fts)       => forall i | 0 <= i < |fts| :: WFTyp(fts[i])
    case TFArr(cap, et)     => WFTyp(et)
    case TVArr(pb, cap, et) => WFTyp(et)
    case TUnion(tb, opts)   => |opts| >= 1 && forall i | 0 <= i < |opts| :: WFTyp(opts[i])
    case TComp(sl, it)      => WFTyp(it)
  }

  // The all-zeros value of a type: what a reader obtains from missing bits.
  function ZeroValue(t: Typ): Value
    requires WFTyp(t)
    decreases t, 0
  {
    match t
    case TScal(w)           => Scal(w, 0)
    case TPad(w)            => Pad(w)
    case TStruct(fts)       => Struct(ZeroValueSeq(fts))
    case TFArr(cap, et)     => FArr(ZeroValueRep(et, cap))
    case TVArr(pb, cap, et) => VArr(pb, [])
    case TUnion(tb, opts)   => Union(tb, 0, ZeroValue(opts[0]))
    case TComp(sl, it)      => Comp(sl, ZeroValue(it))
  }

  function ZeroValueSeq(fts: seq<Typ>): seq<Value>
    requires forall i | 0 <= i < |fts| :: WFTyp(fts[i])
    decreases fts, 0
  {
    if |fts| == 0 then [] else [ZeroValue(fts[0])] + ZeroValueSeq(fts[1..])
  }

  function ZeroValueRep(et: Typ, n: nat): seq<Value>
    requires WFTyp(et)
    decreases et, n
  {
    if n == 0 then [] else [ZeroValue(et)] + ZeroValueRep(et, n - 1)
  }

  // The tolerant reader. Total on wires (the precondition is on the SCHEMA);
  // same guards as De for present data, zeros for missing data, and delimited
  // sections consumed to their declared length.
  function DeCompat(t: Typ, w: seq<Token>): Option<(Value, seq<Token>)>
    requires WFTyp(t)
    decreases t, 0
  {
    if |w| == 0 then Some((ZeroValue(t), []))
    else
      match t
      case TScal(width) =>
        if w[0].TokScal? && w[0].w == width
        then Some((Scal(width, w[0].val), w[1..])) else None
      case TPad(width) =>
        if w[0].TokPad? && w[0].w == width
        then Some((Pad(width), w[1..])) else None
      case TStruct(fts) =>
        (match DeCompatSeq(fts, w)
         case None => None
         case Some((vs, rest)) => Some((Struct(vs), rest)))
      case TFArr(cap, et) =>
        (match DeCompatCount(et, cap, w)
         case None => None
         case Some((vs, rest)) => Some((FArr(vs), rest)))
      case TVArr(pb, cap, et) =>
        if w[0].TokLen? && w[0].pb == pb && w[0].val <= cap then
          (match DeCompatCount(et, w[0].val, w[1..])
           case None => None
           case Some((vs, rest)) => Some((VArr(pb, vs), rest)))
        else None
      case TUnion(tb, opts) =>
        if w[0].TokTag? && w[0].tb == tb && w[0].val < |opts| then
          (match DeCompat(opts[w[0].val], w[1..])
           case None => None
           case Some((sv, rest)) => Some((Union(tb, w[0].val, sv), rest)))
        else None
      case TComp(sl, it) =>
        if sl then
          (match DeCompat(it, w)
           case None => None
           case Some((iv, rest)) => Some((Comp(true, iv), rest)))
        else
          if w[0].TokDelim? then
            var body := w[1..];
            var sz := w[0].len;
            var take := if sz <= |body| then sz else |body|;
            (match DeCompat(it, body[..take])
             case Some((iv, r2)) => Some((Comp(false, iv), body[take..]))  // skip leftover
             case None => None)
          else None
  }

  function DeCompatSeq(fts: seq<Typ>, w: seq<Token>): Option<(seq<Value>, seq<Token>)>
    requires forall i | 0 <= i < |fts| :: WFTyp(fts[i])
    decreases fts, 0
  {
    if |fts| == 0 then Some(([], w))
    else
      (match DeCompat(fts[0], w)
       case None => None
       case Some((v0, rest0)) =>
         (match DeCompatSeq(fts[1..], rest0)
          case None => None
          case Some((vs, rest)) => Some(([v0] + vs, rest))))
  }

  function DeCompatCount(et: Typ, n: nat, w: seq<Token>): Option<(seq<Value>, seq<Token>)>
    requires WFTyp(et)
    decreases et, n
  {
    if n == 0 then Some(([], w))
    else
      (match DeCompat(et, w)
       case None => None
       case Some((v0, rest0)) =>
         (match DeCompatCount(et, n - 1, rest0)
          case None => None
          case Some((vs, rest)) => Some(([v0] + vs, rest))))
  }

  // Zero extension, stated as the headline theorem (definitional by design:
  // the |w| == 0 rule appears exactly once, at the top of DeCompat).
  lemma ZeroExtension(t: Typ)
    requires WFTyp(t)
    ensures DeCompat(t, []) == Some((ZeroValue(t), []))
  {}

  // The zero value inhabits its type, so zero extension yields conforming values.
  lemma ZeroValueConforms(t: Typ)
    requires WFTyp(t)
    ensures ConformsTo(ZeroValue(t), t)
    decreases t, 0
  {
    match t
    case TStruct(fts)       => ZeroValueSeqConforms(fts);
    case TFArr(cap, et)     => ZeroValueRepConforms(et, cap);
    case TUnion(tb, opts)   => ZeroValueConforms(opts[0]);
    case TComp(sl, it)      => ZeroValueConforms(it);
    case TScal(_)           =>
    case TPad(_)            =>
    case TVArr(_, _, _)     =>
  }

  lemma ZeroValueSeqConforms(fts: seq<Typ>)
    requires forall i | 0 <= i < |fts| :: WFTyp(fts[i])
    ensures |ZeroValueSeq(fts)| == |fts|
    ensures forall i | 0 <= i < |fts| :: ConformsTo(ZeroValueSeq(fts)[i], fts[i])
    decreases fts, 0
  {
    if |fts| == 0 {
    } else {
      ZeroValueConforms(fts[0]);
      ZeroValueSeqConforms(fts[1..]);
      assert ZeroValueSeq(fts) == [ZeroValue(fts[0])] + ZeroValueSeq(fts[1..]);
    }
  }

  lemma ZeroValueRepConforms(et: Typ, n: nat)
    requires WFTyp(et)
    ensures |ZeroValueRep(et, n)| == n
    ensures forall i | 0 <= i < n :: ConformsTo(ZeroValueRep(et, n)[i], et)
    decreases et, n
  {
    if n == 0 {
    } else {
      ZeroValueConforms(et);
      ZeroValueRepConforms(et, n - 1);
      assert ZeroValueRep(et, n) == [ZeroValue(et)] + ZeroValueRep(et, n - 1);
    }
  }

  // If the STRICT reader succeeds on the empty wire (only zero-footprint types
  // can), it returns exactly the zero value -- needed so DeCompat's |w| == 0
  // shortcut agrees with De wherever both are defined.
  lemma DeOnEmptyIsZero(t: Typ)
    requires WFTyp(t)
    requires De(t, []).Some?
    ensures De(t, []) == Some((ZeroValue(t), []))
    decreases t, 0
  {
    match t
    case TStruct(fts)       => DeSeqOnEmptyIsZero(fts);
    case TFArr(cap, et)     => DeCountOnEmptyIsZero(et, cap);
    case TComp(sl, it)      => if sl { DeOnEmptyIsZero(it); }
    case TScal(_)           =>  // vacuous: De is None on the empty wire
    case TPad(_)            =>
    case TVArr(_, _, _)     =>
    case TUnion(_, _)       =>
  }

  lemma DeSeqOnEmptyIsZero(fts: seq<Typ>)
    requires forall i | 0 <= i < |fts| :: WFTyp(fts[i])
    requires DeSeq(fts, []).Some?
    ensures DeSeq(fts, []) == Some((ZeroValueSeq(fts), []))
    decreases fts, 0
  {
    if |fts| == 0 {
    } else {
      DeOnEmptyIsZero(fts[0]);
      DeSeqOnEmptyIsZero(fts[1..]);
    }
  }

  lemma DeCountOnEmptyIsZero(et: Typ, n: nat)
    requires WFTyp(et)
    requires DeCount(et, n, []).Some?
    ensures DeCount(et, n, []) == Some((ZeroValueRep(et, n), []))
    decreases et, n
  {
    if n == 0 {
    } else {
      DeOnEmptyIsZero(et);
      DeCountOnEmptyIsZero(et, n - 1);
    }
  }

  // Conservative extension: tolerance never changes the meaning of a wire the
  // strict reader accepts. DeCompat only ADDS accepted wires.
  lemma DeCompatConservative(t: Typ, w: seq<Token>)
    requires WFTyp(t)
    requires De(t, w).Some?
    ensures DeCompat(t, w) == De(t, w)
    decreases t, 0
  {
    if |w| == 0 {
      DeOnEmptyIsZero(t);
    } else {
      match t
      case TStruct(fts) =>
        DeCompatSeqConservative(fts, w);
      case TFArr(cap, et) =>
        DeCompatCountConservative(et, cap, w);
      case TVArr(pb, cap, et) =>
        DeCompatCountConservative(et, w[0].val, w[1..]);
      case TUnion(tb, opts) =>
        DeCompatConservative(opts[w[0].val], w[1..]);
      case TComp(sl, it) =>
        if sl {
          DeCompatConservative(it, w);
        } else {
          var body := w[1..];
          var sz := w[0].len;
          DeCompatConservative(it, body[..sz]);
        }
      case TScal(_) =>
      case TPad(_)  =>
    }
  }

  lemma DeCompatSeqConservative(fts: seq<Typ>, w: seq<Token>)
    requires forall i | 0 <= i < |fts| :: WFTyp(fts[i])
    requires DeSeq(fts, w).Some?
    ensures DeCompatSeq(fts, w) == DeSeq(fts, w)
    decreases fts, 0
  {
    if |fts| == 0 {
    } else {
      DeCompatConservative(fts[0], w);
      var (v0, rest0) := De(fts[0], w).value;
      DeCompatSeqConservative(fts[1..], rest0);
    }
  }

  lemma DeCompatCountConservative(et: Typ, n: nat, w: seq<Token>)
    requires WFTyp(et)
    requires DeCount(et, n, w).Some?
    ensures DeCompatCount(et, n, w) == DeCount(et, n, w)
    decreases et, n
  {
    if n == 0 {
    } else {
      DeCompatConservative(et, w);
      var (v0, rest0) := De(et, w).value;
      DeCompatCountConservative(et, n - 1, rest0);
    }
  }

  // Corollary: the tolerant reader round-trips every canonical wire.
  lemma DeCompatRoundTrip(t: Typ, v: Value, suffix: seq<Token>)
    requires WFTyp(t)
    requires ConformsTo(v, t)
    ensures DeCompat(t, SerWire(v) + suffix) == Some((v, suffix))
  {
    RoundTrip(t, v, suffix);
    DeCompatConservative(t, SerWire(v) + suffix);
  }

  // Type soundness of the tolerant reader: whatever it accepts -- canonical,
  // truncated, or zero-extended -- the decoded value conforms to the schema.
  lemma DeCompatConforms(t: Typ, w: seq<Token>)
    requires WFTyp(t)
    requires DeCompat(t, w).Some?
    ensures ConformsTo(DeCompat(t, w).value.0, t)
    decreases t, 0
  {
    if |w| == 0 {
      ZeroValueConforms(t);
    } else {
      match t
      case TStruct(fts) =>
        DeCompatSeqConforms(fts, w);
      case TFArr(cap, et) =>
        DeCompatCountConforms(et, cap, w);
      case TVArr(pb, cap, et) =>
        DeCompatCountConforms(et, w[0].val, w[1..]);
      case TUnion(tb, opts) =>
        DeCompatConforms(opts[w[0].val], w[1..]);
      case TComp(sl, it) =>
        if sl {
          DeCompatConforms(it, w);
        } else {
          var body := w[1..];
          var sz := w[0].len;
          var take := if sz <= |body| then sz else |body|;
          DeCompatConforms(it, body[..take]);
        }
      case TScal(_) =>
      case TPad(_)  =>
    }
  }

  lemma DeCompatSeqConforms(fts: seq<Typ>, w: seq<Token>)
    requires forall i | 0 <= i < |fts| :: WFTyp(fts[i])
    requires DeCompatSeq(fts, w).Some?
    ensures var (vs, rest) := DeCompatSeq(fts, w).value;
            |vs| == |fts|
            && (forall i | 0 <= i < |vs| :: ConformsTo(vs[i], fts[i]))
    decreases fts, 0
  {
    if |fts| == 0 {
    } else {
      DeCompatConforms(fts[0], w);
      var (v0, rest0) := DeCompat(fts[0], w).value;
      DeCompatSeqConforms(fts[1..], rest0);
      var (vs1, rest) := DeCompatSeq(fts[1..], rest0).value;
      assert ([v0] + vs1)[1..] == vs1;
    }
  }

  lemma DeCompatCountConforms(et: Typ, n: nat, w: seq<Token>)
    requires WFTyp(et)
    requires DeCompatCount(et, n, w).Some?
    ensures var (vs, rest) := DeCompatCount(et, n, w).value;
            |vs| == n
            && (forall i | 0 <= i < |vs| :: ConformsTo(vs[i], et))
    decreases et, n
  {
    if n == 0 {
    } else {
      DeCompatConforms(et, w);
      var (v0, rest0) := DeCompat(et, w).value;
      DeCompatCountConforms(et, n - 1, rest0);
      var (vs1, rest) := DeCompatCount(et, n - 1, rest0).value;
      assert ([v0] + vs1)[1..] == vs1;
    }
  }

  // Concrete tolerance bounds, CI-enforced. Positive direction: the exact
  // shapes the strict reader rejects and the tolerant reader must accept.
  lemma CompatTolerates()
    // delimited section longer than its payload: leftover inside the section is
    // skipped (the outer cursor advances by the declared length)
    ensures De(TComp(false, TScal(8)), [TokDelim(2), TokScal(8, 1), TokScal(8, 9)]) == None
    ensures DeCompat(TComp(false, TScal(8)), [TokDelim(2), TokScal(8, 1), TokScal(8, 9)])
         == Some((Comp(false, Scal(8, 1)), []))
    // section shorter than the nested schema: missing fields read as zeros
    ensures DeCompat(TComp(false, TStruct([TScal(8), TScal(8)])), [TokDelim(1), TokScal(8, 7)])
         == Some((Comp(false, Struct([Scal(8, 7), Scal(8, 0)])), []))
    // header overruns the physical wire: what is available plus virtual zeros
    ensures DeCompat(TComp(false, TScal(8)), [TokDelim(5)])
         == Some((Comp(false, Scal(8, 0)), []))
    // flat wire truncated mid-struct: the missing trailing field reads as zero
    ensures DeCompat(TStruct([TScal(8), TScal(8)]), [TokScal(8, 7)])
         == Some((Struct([Scal(8, 7), Scal(8, 0)]), []))
    // sealed composites have no section boundary: nothing is skipped
    ensures DeCompat(TComp(true, TScal(8)), [TokScal(8, 1), TokScal(8, 2)])
         == Some((Comp(true, Scal(8, 1)), [TokScal(8, 2)]))
  {
    // evaluation stepping stones (bounded unfolding)
    assert De(TScal(8), [TokScal(8, 1), TokScal(8, 9)])
        == Some((Scal(8, 1), [TokScal(8, 9)]));
    assert [TokScal(8, 1), TokScal(8, 9)][..2] == [TokScal(8, 1), TokScal(8, 9)];
    assert DeCompat(TScal(8), [TokScal(8, 1), TokScal(8, 9)])
        == Some((Scal(8, 1), [TokScal(8, 9)]));
    assert DeCompat(TScal(8), []) == Some((Scal(8, 0), []));
    var e: seq<Token> := [];
    assert [TScal(8)][1..] == [];
    assert DeCompatSeq([], e) == Some(([], e));
    assert [Scal(8, 0)] + [] == [Scal(8, 0)];
    assert DeCompatSeq([TScal(8)], []) == Some(([Scal(8, 0)], []));
    assert DeCompat(TScal(8), [TokScal(8, 7)]) == Some((Scal(8, 7), []));
    assert [TScal(8), TScal(8)][1..] == [TScal(8)];
    assert [Scal(8, 7)] + [Scal(8, 0)] == [Scal(8, 7), Scal(8, 0)];
    assert DeCompatSeq([TScal(8), TScal(8)], [TokScal(8, 7)])
        == Some(([Scal(8, 7), Scal(8, 0)], []));
    assert DeCompat(TStruct([TScal(8), TScal(8)]), [TokScal(8, 7)])
        == Some((Struct([Scal(8, 7), Scal(8, 0)]), []));
    assert [TokScal(8, 7)][..1] == [TokScal(8, 7)];
  }

  // Negative direction: tolerance is only about MISSING data. Present-but-wrong
  // data must still fail, exactly like the strict reader.
  lemma CompatStillRejects()
    ensures DeCompat(TUnion(8, [TScal(8)]), [TokTag(8, 5), TokScal(8, 0)]) == None   // bad tag
    ensures DeCompat(TVArr(8, 1, TScal(8)),
                     [TokLen(8, 2), TokScal(8, 0), TokScal(8, 0)]) == None            // over capacity
    ensures DeCompat(TScal(8), [TokScal(16, 0)]) == None                              // metadata mismatch
  {}

  // ==========================================================================
  // VERSION SKEW (Phase 2): schema evolution at delimited boundaries.
  // Evolves(tN, tO) -- tN extends tO by APPENDING fields to the struct directly
  // inside a delimited composite (recursively; congruent everywhere else).
  // There is NO append rule at sealed boundaries: sealed layouts are frozen by
  // construction -- only their nested delimited members may evolve.
  // Upgrade / Downgrade are the value projections across an Evolves pair.
  //
  // THE EXTENSIBILITY CONTRACT, machine-checked, both directions, unbounded:
  //   OldWireNewReader : a new reader decodes an old wire to Upgrade(v) -- the
  //                      old fields plus zeros for the appended ones.
  //   NewWireOldReader : an old reader decodes a new wire to Downgrade(v) --
  //                      the appended fields are skipped via the delimiter.
  // Both hold ONLY because the section length travels on the wire (TokDelim),
  // making consumption schema-independent at delimited boundaries. That is the
  // machine-checked reason DSDL restricts evolution to delimited types.
  // ==========================================================================

  predicate Evolves(tN: Typ, tO: Typ)
    decreases tN
  {
    match tN
    case TScal(_)            => tO == tN
    case TPad(_)             => tO == tN
    case TStruct(ftsN)       => tO.TStruct? && |tO.fields| == |ftsN|
                                && forall i | 0 <= i < |ftsN| :: Evolves(ftsN[i], tO.fields[i])
    case TFArr(cap, etN)     => tO.TFArr? && tO.cap == cap && Evolves(etN, tO.elem)
    case TVArr(pb, cap, etN) => tO.TVArr? && tO.pb == pb && tO.cap == cap
                                && Evolves(etN, tO.elem)
    case TUnion(tb, optsN)   => tO.TUnion? && tO.tb == tb && |tO.opts| == |optsN|
                                && forall i | 0 <= i < |optsN| :: Evolves(optsN[i], tO.opts[i])
    case TComp(sl, itN)      =>
      tO.TComp? && tO.sealed == sl
      && (if sl then Evolves(itN, tO.inner)   // sealed: congruence only, layout frozen
          else if itN.TStruct? && tO.inner.TStruct? then
            // THE evolution rule: append fields at a delimited boundary
            |tO.inner.fields| <= |itN.fields|
            && forall i | 0 <= i < |tO.inner.fields| :: Evolves(itN.fields[i], tO.inner.fields[i])
          else Evolves(itN, tO.inner))
  }

  // Old value -> new schema: keep the old fields (recursively upgraded), fill
  // the appended ones with zeros.
  function Upgrade(v: Value, tO: Typ, tN: Typ): Value
    requires WFTyp(tN)
    requires Evolves(tN, tO)
    requires ConformsTo(v, tO)
    decreases v
  {
    match tN
    case TScal(_)          => v
    case TPad(_)           => v
    case TStruct(ftsN)     => Struct(UpgradeSeq(v.fields, tO.fields, ftsN))
    case TFArr(_, etN)     => FArr(UpgradeElems(v.elems, tO.elem, etN))
    case TVArr(pb, _, etN) => VArr(pb, UpgradeElems(v.elems, tO.elem, etN))
    case TUnion(tb, optsN) => Union(tb, v.tag, Upgrade(v.sel, tO.opts[v.tag], optsN[v.tag]))
    case TComp(sl, itN)    =>
      if sl then Comp(true, Upgrade(v.inner, tO.inner, itN))
      else if itN.TStruct? && tO.inner.TStruct? then
        assert ConformsTo(v.inner, tO.inner) && v.inner.Struct?;
        assert WFTyp(itN);
        assert |tO.inner.fields| <= |itN.fields|
            && forall i | 0 <= i < |tO.inner.fields| :: Evolves(itN.fields[i], tO.inner.fields[i]);
        Comp(false, Struct(UpgradeSeq(v.inner.fields, tO.inner.fields, itN.fields)))
      else
        Comp(false, Upgrade(v.inner, tO.inner, itN))
  }

  function UpgradeSeq(vs: seq<Value>, ftsO: seq<Typ>, ftsN: seq<Typ>): seq<Value>
    requires |vs| == |ftsO| <= |ftsN|
    requires forall i | 0 <= i < |ftsN| :: WFTyp(ftsN[i])
    requires forall i | 0 <= i < |ftsO| :: Evolves(ftsN[i], ftsO[i])
    requires forall i | 0 <= i < |vs| :: ConformsTo(vs[i], ftsO[i])
    decreases vs
  {
    if |vs| == 0 then ZeroValueSeq(ftsN)   // everything left is appended: zeros
    else [Upgrade(vs[0], ftsO[0], ftsN[0])] + UpgradeSeq(vs[1..], ftsO[1..], ftsN[1..])
  }

  function UpgradeElems(vs: seq<Value>, etO: Typ, etN: Typ): seq<Value>
    requires WFTyp(etN)
    requires Evolves(etN, etO)
    requires forall i | 0 <= i < |vs| :: ConformsTo(vs[i], etO)
    decreases vs
  {
    if |vs| == 0 then [] else [Upgrade(vs[0], etO, etN)] + UpgradeElems(vs[1..], etO, etN)
  }

  // New value -> old schema: keep the old prefix (recursively downgraded), drop
  // the appended fields.
  function Downgrade(v: Value, tN: Typ, tO: Typ): Value
    requires Evolves(tN, tO)
    requires ConformsTo(v, tN)
    decreases v
  {
    match tO
    case TScal(_)          => v
    case TPad(_)           => v
    case TStruct(ftsO)     => Struct(DowngradeSeq(v.fields, tN.fields, ftsO))
    case TFArr(_, etO)     => FArr(DowngradeElems(v.elems, tN.elem, etO))
    case TVArr(pb, _, etO) => VArr(pb, DowngradeElems(v.elems, tN.elem, etO))
    case TUnion(tb, optsO) => Union(tb, v.tag, Downgrade(v.sel, tN.opts[v.tag], optsO[v.tag]))
    case TComp(sl, itO)    =>
      if sl then Comp(true, Downgrade(v.inner, tN.inner, itO))
      else if itO.TStruct? && tN.inner.TStruct? then
        assert tN.TComp? && ConformsTo(v.inner, tN.inner) && v.inner.Struct?;
        assert |itO.fields| <= |tN.inner.fields|
            && forall i | 0 <= i < |itO.fields| :: Evolves(tN.inner.fields[i], itO.fields[i]);
        Comp(false, Struct(DowngradeSeq(v.inner.fields, tN.inner.fields, itO.fields)))
      else
        Comp(false, Downgrade(v.inner, tN.inner, itO))
  }

  function DowngradeSeq(vs: seq<Value>, ftsN: seq<Typ>, ftsO: seq<Typ>): seq<Value>
    requires |vs| == |ftsN| && |ftsO| <= |ftsN|
    requires forall i | 0 <= i < |ftsO| :: Evolves(ftsN[i], ftsO[i])
    requires forall i | 0 <= i < |vs| :: ConformsTo(vs[i], ftsN[i])
    decreases vs
  {
    if |ftsO| == 0 then []
    else [Downgrade(vs[0], ftsN[0], ftsO[0])] + DowngradeSeq(vs[1..], ftsN[1..], ftsO[1..])
  }

  function DowngradeElems(vs: seq<Value>, etN: Typ, etO: Typ): seq<Value>
    requires Evolves(etN, etO)
    requires forall i | 0 <= i < |vs| :: ConformsTo(vs[i], etN)
    decreases vs
  {
    if |vs| == 0 then [] else [Downgrade(vs[0], etN, etO)] + DowngradeElems(vs[1..], etN, etO)
  }

  // ---- Support: zero-fill and zero-footprint lemmas ------------------------
  // A zero-footprint value (SerWire == []) exists only for empty structs,
  // zero-capacity fixed arrays, and sealed wrappers thereof. Where a section is
  // empty, DeCompat's |w| == 0 shortcut answers ZeroValue instead of running
  // DeCompatSeq, so the skew proofs need: projecting a zero-footprint value
  // across an Evolves pair yields exactly the target's zero value.

  lemma DeCompatSeqOnEmpty(fts: seq<Typ>)
    requires forall i | 0 <= i < |fts| :: WFTyp(fts[i])
    ensures DeCompatSeq(fts, []) == Some((ZeroValueSeq(fts), []))
    decreases fts
  {
    if |fts| == 0 {
    } else {
      DeCompatSeqOnEmpty(fts[1..]);
    }
  }

  lemma UpgradeOfZeroFootprint(tN: Typ, tO: Typ, v: Value)
    requires WFTyp(tN)
    requires Evolves(tN, tO)
    requires ConformsTo(v, tO)
    requires SerWire(v) == []
    ensures Upgrade(v, tO, tN) == ZeroValue(tN)
    decreases v
  {
    match v
    case Struct(fs) =>
      UpgradeSeqOfZeroFootprint(tN.fields, tO.fields, fs);
    case FArr(es) =>
      UpgradeElemsOfZeroFootprint(tN.elem, tO.elem, es);
    case Comp(sl, inn) =>
      if sl {
        UpgradeOfZeroFootprint(tN.inner, tO.inner, inn);
      }  // !sl: SerWire starts with TokDelim -- contradicts SerWire(v) == []
    case Scal(_, _)   =>  // vacuous: SerWire is a single token
    case Pad(_)       =>
    case VArr(_, _)   =>
    case Union(_, _, _) =>
  }

  lemma UpgradeSeqOfZeroFootprint(ftsN: seq<Typ>, ftsO: seq<Typ>, vs: seq<Value>)
    requires |vs| == |ftsO| <= |ftsN|
    requires forall i | 0 <= i < |ftsN| :: WFTyp(ftsN[i])
    requires forall i | 0 <= i < |ftsO| :: Evolves(ftsN[i], ftsO[i])
    requires forall i | 0 <= i < |vs| :: ConformsTo(vs[i], ftsO[i])
    requires SerWireSeq(vs) == []
    ensures UpgradeSeq(vs, ftsO, ftsN) == ZeroValueSeq(ftsN)
    decreases vs
  {
    if |vs| == 0 {
    } else {
      assert SerWireSeq(vs) == SerWire(vs[0]) + SerWireSeq(vs[1..]);
      assert SerWire(vs[0]) == [] && SerWireSeq(vs[1..]) == [];
      UpgradeOfZeroFootprint(ftsN[0], ftsO[0], vs[0]);
      UpgradeSeqOfZeroFootprint(ftsN[1..], ftsO[1..], vs[1..]);
      assert ZeroValueSeq(ftsN) == [ZeroValue(ftsN[0])] + ZeroValueSeq(ftsN[1..]);
    }
  }

  lemma UpgradeElemsOfZeroFootprint(etN: Typ, etO: Typ, vs: seq<Value>)
    requires WFTyp(etN)
    requires Evolves(etN, etO)
    requires forall i | 0 <= i < |vs| :: ConformsTo(vs[i], etO)
    requires SerWireSeq(vs) == []
    ensures UpgradeElems(vs, etO, etN) == ZeroValueRep(etN, |vs|)
    decreases vs
  {
    if |vs| == 0 {
    } else {
      assert SerWireSeq(vs) == SerWire(vs[0]) + SerWireSeq(vs[1..]);
      assert SerWire(vs[0]) == [] && SerWireSeq(vs[1..]) == [];
      UpgradeOfZeroFootprint(etN, etO, vs[0]);
      UpgradeElemsOfZeroFootprint(etN, etO, vs[1..]);
    }
  }

  lemma DowngradeOfZeroFootprint(tN: Typ, tO: Typ, v: Value)
    requires WFTyp(tO)
    requires Evolves(tN, tO)
    requires ConformsTo(v, tN)
    requires SerWire(v) == []
    ensures Downgrade(v, tN, tO) == ZeroValue(tO)
    decreases v
  {
    match v
    case Struct(fs) =>
      DowngradeSeqOfZeroFootprint(tN.fields, tO.fields, fs);
    case FArr(es) =>
      DowngradeElemsOfZeroFootprint(tN.elem, tO.elem, es);
    case Comp(sl, inn) =>
      if sl {
        DowngradeOfZeroFootprint(tN.inner, tO.inner, inn);
      }
    case Scal(_, _)   =>
    case Pad(_)       =>
    case VArr(_, _)   =>
    case Union(_, _, _) =>
  }

  lemma DowngradeSeqOfZeroFootprint(ftsN: seq<Typ>, ftsO: seq<Typ>, vs: seq<Value>)
    requires |vs| == |ftsN| && |ftsO| <= |ftsN|
    requires forall i | 0 <= i < |ftsO| :: WFTyp(ftsO[i])
    requires forall i | 0 <= i < |ftsO| :: Evolves(ftsN[i], ftsO[i])
    requires forall i | 0 <= i < |vs| :: ConformsTo(vs[i], ftsN[i])
    requires SerWireSeq(vs) == []
    ensures DowngradeSeq(vs, ftsN, ftsO) == ZeroValueSeq(ftsO)
    decreases vs
  {
    if |ftsO| == 0 {
    } else {
      assert SerWireSeq(vs) == SerWire(vs[0]) + SerWireSeq(vs[1..]);
      assert SerWire(vs[0]) == [] && SerWireSeq(vs[1..]) == [];
      DowngradeOfZeroFootprint(ftsN[0], ftsO[0], vs[0]);
      DowngradeSeqOfZeroFootprint(ftsN[1..], ftsO[1..], vs[1..]);
      assert ZeroValueSeq(ftsO) == [ZeroValue(ftsO[0])] + ZeroValueSeq(ftsO[1..]);
    }
  }

  lemma DowngradeElemsOfZeroFootprint(etN: Typ, etO: Typ, vs: seq<Value>)
    requires WFTyp(etO)
    requires Evolves(etN, etO)
    requires forall i | 0 <= i < |vs| :: ConformsTo(vs[i], etN)
    requires SerWireSeq(vs) == []
    ensures DowngradeElems(vs, etN, etO) == ZeroValueRep(etO, |vs|)
    decreases vs
  {
    if |vs| == 0 {
    } else {
      assert SerWireSeq(vs) == SerWire(vs[0]) + SerWireSeq(vs[1..]);
      assert SerWire(vs[0]) == [] && SerWireSeq(vs[1..]) == [];
      DowngradeOfZeroFootprint(etN, etO, vs[0]);
      DowngradeElemsOfZeroFootprint(etN, etO, vs[1..]);
    }
  }

  // ---- Skew theorem 1: old wire, new reader (zero extension) ---------------

  lemma OldWireNewReader(tN: Typ, tO: Typ, v: Value, suffix: seq<Token>)
    requires WFTyp(tN)
    requires Evolves(tN, tO)
    requires ConformsTo(v, tO)
    ensures DeCompat(tN, SerWire(v) + suffix) == Some((Upgrade(v, tO, tN), suffix))
    decreases v
  {
    var w := SerWire(v) + suffix;
    if |w| == 0 {
      assert SerWire(v) == [] && suffix == [];
      UpgradeOfZeroFootprint(tN, tO, v);
    } else {
      match v
      case Scal(sw, val) =>
        assert SerWire(v) + suffix == [TokScal(sw, val)] + suffix;
      case Pad(pw) =>
        assert SerWire(v) + suffix == [TokPad(pw)] + suffix;
      case Struct(fs) =>
        assert SerWire(v) + suffix == SerWireSeq(fs) + suffix;
        assert forall i | 0 <= i < |tN.fields| :: WFTyp(tN.fields[i]);
        OldNewSeq(tN.fields, tO.fields, fs, suffix);
      case FArr(es) =>
        OldNewElems(tN.elem, tO.elem, es, suffix);
      case VArr(pb, es) =>
        assert SerWire(v) + suffix == [TokLen(pb, |es|)] + (SerWireSeq(es) + suffix);
        OldNewElems(tN.elem, tO.elem, es, suffix);
      case Union(tb, tg, s) =>
        assert SerWire(v) + suffix == [TokTag(tb, tg)] + (SerWire(s) + suffix);
        OldWireNewReader(tN.opts[tg], tO.opts[tg], s, suffix);
      case Comp(sl, inn) =>
        if sl {
          OldWireNewReader(tN.inner, tO.inner, inn, suffix);
        } else {
          var iw := SerWire(inn);
          assert SerWire(v) + suffix == [TokDelim(|iw|)] + (iw + suffix);
          assert (iw + suffix)[..|iw|] == iw;
          assert (iw + suffix)[|iw|..] == suffix;
          if tN.inner.TStruct? && tO.inner.TStruct? {
            var fs := inn.fields;
            assert iw == SerWireSeq(fs);
            assert forall i | 0 <= i < |tN.inner.fields| :: WFTyp(tN.inner.fields[i]);
            OldNewStructBody(tN.inner.fields, tO.inner.fields, fs);
          } else {
            OldWireNewReader(tN.inner, tO.inner, inn, []);
            assert iw + [] == iw;
          }
        }
    }
  }

  lemma OldNewSeq(ftsN: seq<Typ>, ftsO: seq<Typ>, vs: seq<Value>, suffix: seq<Token>)
    requires forall i | 0 <= i < |ftsN| :: WFTyp(ftsN[i])
    requires |vs| == |ftsO| <= |ftsN|
    requires |ftsN| == |ftsO| || suffix == []   // appended fields need the section boundary
    requires forall i | 0 <= i < |ftsO| :: Evolves(ftsN[i], ftsO[i])
    requires forall i | 0 <= i < |vs| :: ConformsTo(vs[i], ftsO[i])
    ensures DeCompatSeq(ftsN, SerWireSeq(vs) + suffix)
         == Some((UpgradeSeq(vs, ftsO, ftsN), suffix))
    decreases vs, 0
  {
    if |vs| == 0 {
      assert SerWireSeq(vs) + suffix == suffix;
      if |ftsN| == 0 {
      } else {
        // suffix == []: the remaining (appended) fields zero-fill
        DeCompatSeqOnEmpty(ftsN);
      }
    } else {
      assert vs == [vs[0]] + vs[1..];
      assert SerWireSeq(vs) + suffix == SerWire(vs[0]) + (SerWireSeq(vs[1..]) + suffix);
      assert forall i | 0 <= i < |ftsN[1..]| :: WFTyp(ftsN[1..][i]);
      assert forall i | 0 <= i < |ftsO[1..]| :: Evolves(ftsN[1..][i], ftsO[1..][i]);
      assert forall i | 0 <= i < |vs[1..]| :: ConformsTo(vs[1..][i], ftsO[1..][i]);
      OldWireNewReader(ftsN[0], ftsO[0], vs[0], SerWireSeq(vs[1..]) + suffix);
      OldNewSeq(ftsN[1..], ftsO[1..], vs[1..], suffix);
      assert UpgradeSeq(vs, ftsO, ftsN)
          == [Upgrade(vs[0], ftsO[0], ftsN[0])] + UpgradeSeq(vs[1..], ftsO[1..], ftsN[1..]);
    }
  }

  lemma OldNewElems(etN: Typ, etO: Typ, vs: seq<Value>, suffix: seq<Token>)
    requires WFTyp(etN)
    requires Evolves(etN, etO)
    requires forall i | 0 <= i < |vs| :: ConformsTo(vs[i], etO)
    ensures DeCompatCount(etN, |vs|, SerWireSeq(vs) + suffix)
         == Some((UpgradeElems(vs, etO, etN), suffix))
    decreases vs, 0
  {
    if |vs| == 0 {
      assert SerWireSeq(vs) + suffix == suffix;
    } else {
      assert vs == [vs[0]] + vs[1..];
      assert SerWireSeq(vs) + suffix == SerWire(vs[0]) + (SerWireSeq(vs[1..]) + suffix);
      OldWireNewReader(etN, etO, vs[0], SerWireSeq(vs[1..]) + suffix);
      OldNewElems(etN, etO, vs[1..], suffix);
    }
  }

  // The delimited-section body against the NEW struct: encapsulates the
  // empty-section corner (DeCompat's shortcut) next to the DeCompatSeq path.
  lemma OldNewStructBody(ftsN: seq<Typ>, ftsO: seq<Typ>, vs: seq<Value>)
    requires forall i | 0 <= i < |ftsN| :: WFTyp(ftsN[i])
    requires |vs| == |ftsO| <= |ftsN|
    requires forall i | 0 <= i < |ftsO| :: Evolves(ftsN[i], ftsO[i])
    requires forall i | 0 <= i < |vs| :: ConformsTo(vs[i], ftsO[i])
    ensures DeCompat(TStruct(ftsN), SerWireSeq(vs))
         == Some((Struct(UpgradeSeq(vs, ftsO, ftsN)), []))
    decreases vs, 1
  {
    assert WFTyp(TStruct(ftsN));
    if |SerWireSeq(vs)| == 0 {
      UpgradeSeqOfZeroFootprint(ftsN, ftsO, vs);
    } else {
      OldNewSeq(ftsN, ftsO, vs, []);
      assert SerWireSeq(vs) + [] == SerWireSeq(vs);
    }
  }

  // ---- Skew theorem 2: new wire, old reader (truncation) -------------------

  lemma NewWireOldReader(tN: Typ, tO: Typ, v: Value, suffix: seq<Token>)
    requires WFTyp(tO)
    requires Evolves(tN, tO)
    requires ConformsTo(v, tN)
    ensures DeCompat(tO, SerWire(v) + suffix) == Some((Downgrade(v, tN, tO), suffix))
    decreases v
  {
    var w := SerWire(v) + suffix;
    if |w| == 0 {
      assert SerWire(v) == [] && suffix == [];
      DowngradeOfZeroFootprint(tN, tO, v);
    } else {
      match v
      case Scal(sw, val) =>
        assert SerWire(v) + suffix == [TokScal(sw, val)] + suffix;
      case Pad(pw) =>
        assert SerWire(v) + suffix == [TokPad(pw)] + suffix;
      case Struct(fs) =>
        assert SerWire(v) + suffix == SerWireSeq(fs) + suffix;
        assert forall i | 0 <= i < |tO.fields| :: WFTyp(tO.fields[i]);
        NewOldSeq(tN.fields, tO.fields, fs, suffix);
        assert fs[|tO.fields|..] == [];
        assert SerWireSeq([]) + suffix == suffix;
      case FArr(es) =>
        NewOldElems(tN.elem, tO.elem, es, suffix);
      case VArr(pb, es) =>
        assert SerWire(v) + suffix == [TokLen(pb, |es|)] + (SerWireSeq(es) + suffix);
        NewOldElems(tN.elem, tO.elem, es, suffix);
      case Union(tb, tg, s) =>
        assert SerWire(v) + suffix == [TokTag(tb, tg)] + (SerWire(s) + suffix);
        NewWireOldReader(tN.opts[tg], tO.opts[tg], s, suffix);
      case Comp(sl, inn) =>
        if sl {
          NewWireOldReader(tN.inner, tO.inner, inn, suffix);
        } else {
          var iw := SerWire(inn);
          assert SerWire(v) + suffix == [TokDelim(|iw|)] + (iw + suffix);
          assert (iw + suffix)[..|iw|] == iw;
          assert (iw + suffix)[|iw|..] == suffix;
          if tN.inner.TStruct? && tO.inner.TStruct? {
            var fs := inn.fields;
            assert iw == SerWireSeq(fs);
            assert forall i | 0 <= i < |tO.inner.fields| :: WFTyp(tO.inner.fields[i]);
            NewOldStructBody(tN.inner.fields, tO.inner.fields, fs);
          } else {
            NewWireOldReader(tN.inner, tO.inner, inn, []);
            assert iw + [] == iw;
          }
        }
    }
  }

  lemma NewOldSeq(ftsN: seq<Typ>, ftsO: seq<Typ>, vs: seq<Value>, suffix: seq<Token>)
    requires forall i | 0 <= i < |ftsO| :: WFTyp(ftsO[i])
    requires |vs| == |ftsN| && |ftsO| <= |ftsN|
    requires forall i | 0 <= i < |ftsO| :: Evolves(ftsN[i], ftsO[i])
    requires forall i | 0 <= i < |vs| :: ConformsTo(vs[i], ftsN[i])
    ensures DeCompatSeq(ftsO, SerWireSeq(vs) + suffix)
         == Some((DowngradeSeq(vs, ftsN, ftsO), SerWireSeq(vs[|ftsO|..]) + suffix))
    decreases vs, 0
  {
    if |ftsO| == 0 {
      assert vs[0..] == vs;
    } else {
      assert vs == [vs[0]] + vs[1..];
      assert SerWireSeq(vs) + suffix == SerWire(vs[0]) + (SerWireSeq(vs[1..]) + suffix);
      assert forall i | 0 <= i < |ftsO[1..]| :: WFTyp(ftsO[1..][i]);
      assert forall i | 0 <= i < |ftsO[1..]| :: Evolves(ftsN[1..][i], ftsO[1..][i]);
      assert forall i | 0 <= i < |vs[1..]| :: ConformsTo(vs[1..][i], ftsN[1..][i]);
      NewWireOldReader(ftsN[0], ftsO[0], vs[0], SerWireSeq(vs[1..]) + suffix);
      NewOldSeq(ftsN[1..], ftsO[1..], vs[1..], suffix);
      assert vs[1..][|ftsO[1..]|..] == vs[|ftsO|..];
      assert DowngradeSeq(vs, ftsN, ftsO)
          == [Downgrade(vs[0], ftsN[0], ftsO[0])] + DowngradeSeq(vs[1..], ftsN[1..], ftsO[1..]);
    }
  }

  lemma NewOldElems(etN: Typ, etO: Typ, vs: seq<Value>, suffix: seq<Token>)
    requires WFTyp(etO)
    requires Evolves(etN, etO)
    requires forall i | 0 <= i < |vs| :: ConformsTo(vs[i], etN)
    ensures DeCompatCount(etO, |vs|, SerWireSeq(vs) + suffix)
         == Some((DowngradeElems(vs, etN, etO), suffix))
    decreases vs, 0
  {
    if |vs| == 0 {
      assert SerWireSeq(vs) + suffix == suffix;
    } else {
      assert vs == [vs[0]] + vs[1..];
      assert SerWireSeq(vs) + suffix == SerWire(vs[0]) + (SerWireSeq(vs[1..]) + suffix);
      NewWireOldReader(etN, etO, vs[0], SerWireSeq(vs[1..]) + suffix);
      NewOldElems(etN, etO, vs[1..], suffix);
    }
  }

  // The delimited-section body against the OLD struct. The rest of the parse
  // (the appended fields' wire) is irrelevant: the delimiter makes the outer
  // reader skip it, so only the decoded value is stated.
  lemma NewOldStructBody(ftsN: seq<Typ>, ftsO: seq<Typ>, vs: seq<Value>)
    requires forall i | 0 <= i < |ftsO| :: WFTyp(ftsO[i])
    requires |vs| == |ftsN| && |ftsO| <= |ftsN|
    requires forall i | 0 <= i < |ftsO| :: Evolves(ftsN[i], ftsO[i])
    requires forall i | 0 <= i < |vs| :: ConformsTo(vs[i], ftsN[i])
    ensures DeCompat(TStruct(ftsO), SerWireSeq(vs)).Some?
    ensures DeCompat(TStruct(ftsO), SerWireSeq(vs)).value.0
         == Struct(DowngradeSeq(vs, ftsN, ftsO))
    decreases vs, 1
  {
    assert WFTyp(TStruct(ftsO));
    if |SerWireSeq(vs)| == 0 {
      DowngradeSeqOfZeroFootprint(ftsN, ftsO, vs);
    } else {
      NewOldSeq(ftsN, ftsO, vs, []);
      assert SerWireSeq(vs) + [] == SerWireSeq(vs);
    }
  }

  // Corollaries: the projections are type-sound (via the skew theorems and
  // DeCompatConforms -- no separate induction needed).
  lemma UpgradeConforms(tN: Typ, tO: Typ, v: Value)
    requires WFTyp(tN)
    requires Evolves(tN, tO)
    requires ConformsTo(v, tO)
    ensures ConformsTo(Upgrade(v, tO, tN), tN)
  {
    OldWireNewReader(tN, tO, v, []);
    assert SerWire(v) + [] == SerWire(v);
    DeCompatConforms(tN, SerWire(v));
  }

  lemma DowngradeConforms(tN: Typ, tO: Typ, v: Value)
    requires WFTyp(tO)
    requires Evolves(tN, tO)
    requires ConformsTo(v, tN)
    ensures ConformsTo(Downgrade(v, tN, tO), tO)
  {
    NewWireOldReader(tN, tO, v, []);
    assert SerWire(v) + [] == SerWire(v);
    DeCompatConforms(tO, SerWire(v));
  }

  // Concrete bookend, CI-enforced: a v1.0 -> v1.1 field append, both skew
  // directions evaluated end to end -- and the sealed counterpart is NOT an
  // evolution (the append rule exists only at delimited boundaries).
  lemma VersionSkewExample()
  {
    var tOld := TComp(false, TStruct([TScal(8)]));
    var tNew := TComp(false, TStruct([TScal(8), TScal(16)]));
    assert WFTyp(tOld) && WFTyp(tNew);
    assert Evolves(tNew, tOld);

    var vOld := Comp(false, Struct([Scal(8, 42)]));
    var vNew := Comp(false, Struct([Scal(8, 42), Scal(16, 7)]));
    assert ConformsTo(vOld, tOld) && ConformsTo(vNew, tNew);

    // old wire, new reader: the appended field decodes as zero
    OldWireNewReader(tNew, tOld, vOld, []);
    assert SerWire(vOld) + [] == SerWire(vOld);
    assert [TScal(8), TScal(16)][1..] == [TScal(16)];
    assert UpgradeSeq([], [], [TScal(16)]) == ZeroValueSeq([TScal(16)]);
    assert ZeroValueSeq([TScal(16)]) == [Scal(16, 0)];
    assert UpgradeSeq([Scal(8, 42)], [TScal(8)], [TScal(8), TScal(16)])
        == [Scal(8, 42), Scal(16, 0)];
    assert DeCompat(tNew, SerWire(vOld))
        == Some((Comp(false, Struct([Scal(8, 42), Scal(16, 0)])), []));

    // new wire, old reader: the appended field is skipped via the delimiter
    NewWireOldReader(tNew, tOld, vNew, []);
    assert SerWire(vNew) + [] == SerWire(vNew);
    assert DowngradeSeq([Scal(8, 42), Scal(16, 7)], [TScal(8), TScal(16)], [TScal(8)])
        == [Scal(8, 42)];
    assert DeCompat(tOld, SerWire(vNew)) == Some((vOld, []));

    // sealed layouts are frozen: the same append is NOT an evolution
    assert !Evolves(TComp(true, TStruct([TScal(8), TScal(16)])),
                    TComp(true, TStruct([TScal(8)])));
  }

  // ==========================================================================
  // PROOF: the canonical traces satisfy the ordering predicates, for ALL values
  // (unbounded). B1 applies SerOrderOK/DeOrderOK to real *backend* traces; these
  // lemmas guarantee the model's own SerOps/DeOps can never disagree with those
  // predicates, so the two halves of the oracle stay consistent by machine check
  // rather than by sampling representative shapes.
  //
  // Shape of the proof: the predicates only ever constrain an op against
  // neighbours at fixed offsets, and every constrained op is emitted inside one
  // contiguous constructor-level block that carries its full context (e.g.
  // WriteTag only ever appears as ...ValidateTag, MaskTag, WriteTag, Advance...).
  // Hence the predicates are closed under concatenation (the two Concat lemmas),
  // and the universal statement follows by structural induction over Value: each
  // match arm checks its head block concretely, then stitches in the recursive
  // traces with the concat lemma.
  // ==========================================================================

  lemma SerOrderOKConcat(a: seq<Op>, b: seq<Op>)
    requires SerOrderOK(a)
    requires SerOrderOK(b)
    ensures SerOrderOK(a + b)
  {
    var c := a + b;
    forall i | 0 <= i < |c| && c[i] == WriteTag
      ensures i >= 2 && c[i-1] == MaskTag && c[i-2] == ValidateTag
    {
      if i < |a| {
        assert a[i] == WriteTag;
      } else {
        var j := i - |a|;
        assert b[j] == WriteTag;
        assert j >= 2 && b[j-1] == MaskTag && b[j-2] == ValidateTag;
      }
    }
    forall i | 0 <= i < |c| && c[i] == LenWrite
      ensures i >= 1 && c[i-1] == LenValidate
    {
      if i < |a| {
        assert a[i] == LenWrite;
      } else {
        var j := i - |a|;
        assert b[j] == LenWrite;
      }
    }
    forall i | 0 <= i < |c| && c[i] in {WriteTag, LenWrite, WriteScalar}
      ensures i + 1 < |c| && c[i+1] == Advance
    {
      if i < |a| {
        assert a[i] in {WriteTag, LenWrite, WriteScalar};
        assert i + 1 < |a| && a[i+1] == Advance;
      } else {
        var j := i - |a|;
        assert b[j] in {WriteTag, LenWrite, WriteScalar};
        assert j + 1 < |b| && b[j+1] == Advance;
      }
    }
  }

  lemma DeOrderOKConcat(a: seq<Op>, b: seq<Op>)
    requires DeOrderOK(a)
    requires DeOrderOK(b)
    ensures DeOrderOK(a + b)
  {
    var c := a + b;
    forall i | 0 <= i < |c| && c[i] == ValidateTag
      ensures i >= 3 && c[i-1] == StoreTag && c[i-2] == MaskTag && c[i-3] == ReadTag
    {
      if i < |a| {
        assert a[i] == ValidateTag;
      } else {
        var j := i - |a|;
        assert b[j] == ValidateTag;
        assert j >= 3 && b[j-1] == StoreTag && b[j-2] == MaskTag && b[j-3] == ReadTag;
      }
    }
    forall i | 0 <= i < |c| && c[i] == LenValidate
      ensures i >= 2 && c[i-1] == Advance && c[i-2] == LenRead
    {
      if i < |a| {
        assert a[i] == LenValidate;
      } else {
        var j := i - |a|;
        assert b[j] == LenValidate;
        assert j >= 2 && b[j-1] == Advance && b[j-2] == LenRead;
      }
    }
  }

  lemma SerOpsOrderOK(v: Value)
    ensures SerOrderOK(SerOps(v))
    decreases v
  {
    match v
    case Scal(w, val) =>
      assert SerOps(v) == [WriteScalar, Advance];
    case Pad(w) =>
      assert SerOps(v) == [PadOp, Advance];
    case Struct(fs) =>
      SerOpsSeqOrderOK(fs);
      assert SerOrderOK([Align]);
      SerOrderOKConcat(SerOpsSeq(fs), [Align]);
    case FArr(es) =>
      SerOpsSeqOrderOK(es);
      assert SerOrderOK([ElemLoop]);
      SerOrderOKConcat([ElemLoop], SerOpsSeq(es));
    case VArr(pb, es) =>
      SerOpsSeqOrderOK(es);
      assert SerOrderOK([LenValidate, LenWrite, Advance, ElemLoop]);
      SerOrderOKConcat([LenValidate, LenWrite, Advance, ElemLoop], SerOpsSeq(es));
    case Union(tb, tg, s) =>
      SerOpsOrderOK(s);
      var hd := [ValidateTag, MaskTag, WriteTag, Advance, Switch, Case, Align];
      assert SerOrderOK(hd);    // the negative control: reordering the union head block fails here
      assert SerOrderOK([DefaultBadTag]);
      SerOrderOKConcat(hd, SerOps(s));
      SerOrderOKConcat(hd + SerOps(s), [DefaultBadTag]);
    case Comp(sl, inn) =>
      SerOpsOrderOK(inn);
      if sl {
        assert SerOrderOK([CompositeInline]);
        SerOrderOKConcat([CompositeInline], SerOps(inn));
      } else {
        assert SerOrderOK([CompositeDelimHeader]);
        SerOrderOKConcat([CompositeDelimHeader], SerOps(inn));
      }
  }

  lemma SerOpsSeqOrderOK(vs: seq<Value>)
    ensures SerOrderOK(SerOpsSeq(vs))
    decreases vs
  {
    if |vs| == 0 {
      assert SerOpsSeq(vs) == [];
    } else {
      SerOpsOrderOK(vs[0]);
      SerOpsSeqOrderOK(vs[1..]);
      assert SerOrderOK([Align]);
      SerOrderOKConcat([Align], SerOps(vs[0]));
      SerOrderOKConcat([Align] + SerOps(vs[0]), SerOpsSeq(vs[1..]));
      assert SerOpsSeq(vs) == ([Align] + SerOps(vs[0])) + SerOpsSeq(vs[1..]);
    }
  }

  lemma DeOpsOrderOK(v: Value)
    ensures DeOrderOK(DeOps(v))
    decreases v
  {
    match v
    case Scal(w, val) =>
      assert DeOps(v) == [ReadScalar, Advance];
    case Pad(w) =>
      assert DeOps(v) == [PadOp, Advance];
    case Struct(fs) =>
      DeOpsSeqOrderOK(fs);
      assert DeOrderOK([Align]);
      DeOrderOKConcat(DeOpsSeq(fs), [Align]);
    case FArr(es) =>
      DeOpsSeqOrderOK(es);
      assert DeOrderOK([ElemLoop]);
      DeOrderOKConcat([ElemLoop], DeOpsSeq(es));
    case VArr(pb, es) =>
      DeOpsSeqOrderOK(es);
      assert DeOrderOK([LenRead, Advance, LenValidate, ElemLoop]);
      DeOrderOKConcat([LenRead, Advance, LenValidate, ElemLoop], DeOpsSeq(es));
    case Union(tb, tg, s) =>
      DeOpsOrderOK(s);
      var hd := [ReadTag, MaskTag, StoreTag, ValidateTag, Advance, Switch, Case, Align];
      assert DeOrderOK(hd);
      assert DeOrderOK([DefaultBadTag]);
      DeOrderOKConcat(hd, DeOps(s));
      DeOrderOKConcat(hd + DeOps(s), [DefaultBadTag]);
    case Comp(sl, inn) =>
      DeOpsOrderOK(inn);
      if sl {
        assert DeOrderOK([CompositeInline]);
        DeOrderOKConcat([CompositeInline], DeOps(inn));
      } else {
        assert DeOrderOK([CompositeDelimHeader]);
        DeOrderOKConcat([CompositeDelimHeader], DeOps(inn));
      }
  }

  lemma DeOpsSeqOrderOK(vs: seq<Value>)
    ensures DeOrderOK(DeOpsSeq(vs))
    decreases vs
  {
    if |vs| == 0 {
      assert DeOpsSeq(vs) == [];
    } else {
      DeOpsOrderOK(vs[0]);
      DeOpsSeqOrderOK(vs[1..]);
      assert DeOrderOK([Align]);
      DeOrderOKConcat([Align], DeOps(vs[0]));
      DeOrderOKConcat([Align] + DeOps(vs[0]), DeOpsSeq(vs[1..]));
      assert DeOpsSeq(vs) == ([Align] + DeOps(vs[0])) + DeOpsSeq(vs[1..]);
    }
  }

  // The headline corollary: the two halves of the emit-order oracle -- the
  // canonical traces and the ordering predicates -- agree for EVERY value.
  lemma CanonicalTracesOrderOK(v: Value)
    ensures SerOrderOK(SerOps(v))
    ensures DeOrderOK(DeOps(v))
  {
    SerOpsOrderOK(v);
    DeOpsOrderOK(v);
  }
}
