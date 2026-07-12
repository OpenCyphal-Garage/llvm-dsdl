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
//   Bounds safety : De is a TOTAL function with no precondition, so Dafny forbids any
//                   out-of-bounds token access by construction -- there is no wire
//                   (truncated, adversarial, empty) on which De is undefined.
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
  // is bounds-safe on ANY wire by construction.

  function De(t: Typ, w: seq<Token>): Option<(Value, seq<Token>)>
    decreases t, 0
  {
    match t
    case TScal(width) =>
      if |w| >= 1 && w[0].TokScal? then Some((Scal(width, w[0].val), w[1..])) else None
    case TPad(width) =>
      if |w| >= 1 && w[0].TokPad? then Some((Pad(width), w[1..])) else None
    case TStruct(fts) =>
      (match DeSeq(fts, w)
       case None => None
       case Some((vs, rest)) => Some((Struct(vs), rest)))
    case TFArr(cap, et) =>
      (match DeCount(et, cap, w)
       case None => None
       case Some((vs, rest)) => Some((FArr(vs), rest)))
    case TVArr(pb, et) =>
      if |w| >= 1 && w[0].TokLen? then
        (match DeCount(et, w[0].val, w[1..])
         case None => None
         case Some((vs, rest)) => Some((VArr(pb, vs), rest)))
      else None
    case TUnion(tb, opts) =>
      if |w| >= 1 && w[0].TokTag? && w[0].val < |opts| then
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

  // Sanity: the canonical emit traces satisfy the ordering constraints. Proving this
  // for ALL values is a positional-over-concatenation induction with low payoff (B1
  // applies these predicates to real *backend* traces, not the model's own), so we
  // check representative shapes concretely -- Dafny evaluates SerOps/DeOps here and
  // verifies the predicate holds.
  lemma OrderingSanity()
  {
    var u  := Union(8, 0, Scal(8, 0));            // union -> exercises tag validate/mask/write order
    var va := VArr(8, [Scal(8, 0), Scal(8, 0)]);  // variable array -> len validate/write order
    var st := Struct([Scal(8, 0), Scal(8, 0)]);   // struct -> scalar write/advance order
    assert SerOrderOK(SerOps(u));
    assert SerOrderOK(SerOps(va));
    assert SerOrderOK(SerOps(st));
    assert DeOrderOK(DeOps(u));
    assert DeOrderOK(DeOps(va));
    assert DeOrderOK(DeOps(st));
  }
}
