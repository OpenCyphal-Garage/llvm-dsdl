// SPDX-License-Identifier: MIT
//
// Formal control-flow / round-trip model of Cyphal DSDL serialize/deserialize.
//
// SCOPE (deliberate): models the SEQUENCE of wire operations and the structural
// invertibility of serialize/deserialize -- NOT bit-exact byte layout. The wire is
// an ordered stream of typed *tokens*; a scalar's value is an opaque tag that must
// survive the round trip. Bit-exactness (widths, saturation, endianness, NaN) is
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
    | TVArr(pb: nat, elem: Typ)
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
    case TVArr(pb, et)   => v.VArr? && v.pb == pb
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
  // guards below are exactly what DeCanonical needs to reconstruct the wire.

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
    case TVArr(pb, et) =>
      if |w| >= 1 && w[0].TokLen? && w[0].pb == pb then
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
    case TVArr(pb, et) =>
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
