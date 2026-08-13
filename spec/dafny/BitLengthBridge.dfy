// SPDX-License-Identifier: MIT
//
// The length-semantics bridge: the bit-length algebra computes EXACTLY the achievable
// serialized lengths, per type constructor.
//
// WHAT THIS ADDS (hardening rung 4): stages 1-3 proved the evaluators correct AGAINST the
// algebra's denotational semantics `Sem`, proved `Sem`'s algebraic shape, and corroborated it
// against pydsdl — but `Sem` itself remained an axiom transcribed from our own header. This
// module makes it a THEOREM: for every type in the grammar the analyzer compiles, the
// algebra expression the analyzer would build (`BlsOf`, transcribing the Analyzer.cpp
// construction sites) denotes precisely the set of serialized bit lengths achievable by
// conforming values (`BitLen`, the length semantics of DSDL serialization):
//
//   BridgeTheorem: L in Sem(BlsOf(t))  <==>  exists v :: Conforms(t, v) && BitLen(t, v) == L
//
// After this, trusting the algebra + the analyzer's construction pattern reduces to trusting
// `BitLen` — five lines of arithmetic, one per serialization rule, each auditable directly
// against the OpenCyphal Specification's serialization chapter:
//   scalar/void   -> its width
//   struct        -> fold of (round the offset up to the field's alignment, add the field),
//                    rounded up to a byte boundary (Analyzer.cpp:1209,
//                    `section.offsetAtEnd = structureOffset.padToAlignment(8)`)
//   tagged union  -> tag width + the chosen alternative, rounded up to a byte boundary
//                    (Analyzer.cpp:884, `(BitLengthSet(tagBits) + payloadSet).padToAlignment(8)`)
//   fixed array   -> sum of exactly n elements
//   variable array-> length-prefix width + sum of 0..capacity elements
//
// RELATION TO CyphalSerdes.dfy: the grammar mirrors its Typ (TScal/TPad -> Scalar,
// TStruct -> Struct, TUnion -> UnionT, TFArr -> FixedArray, TVArr -> VarArray) at BIT
// granularity — the token model deliberately abstracts widths and alignment (its wire is
// token-atomic), so the length projection lives here rather than as WireBits over its
// tokens. A delimited composite (TComp, sealed == false) is representable as
// VarArray(32, Scalar(8), extentBytes): the analyzer models the delimited field's length as
// the FORWARD-COMPAT ENVELOPE — 32-bit delimiter header plus ANY payload of 0..extent bytes
// (the DeCompat tolerance domain, version-skew by design) — not merely the lengths the
// current inner type can write, and that envelope is exactly this VarArray shape
// (Analyzer.cpp: `BitLengthSet(32) + BitLengthSet(8).repeatRange(extent / 8)`). A sealed
// composite field contributes its inner lengths at a byte-aligned offset (align 8, inner).
//
// PROOF SHAPE: mutual structural induction (BridgeTheorem with FieldsBridge / AltsBridge /
// ArrayBridge), each direction constructing explicit witnesses — achievability of every
// algebra value requires BUILDING a conforming value with that length (the independence of
// field/element choices is what licenses Minkowski sums), and conversely every conforming
// value's length must land in the algebra set. Traversals are index-based and termination
// uses uniform (carrier, phase, remaining) measures: sequence MEMBERS rank below their
// sequence, but slices do not, so the helpers walk indices instead of slicing.
//
// NEGATIVE CONTROLS (each mutation must break BridgeTheorem; checked manually when the
// model changes):
//   - drop the prefix Add from VarArray's BlsOf      -> forward direction fails (prefix bits
//                                                       are on the wire; the algebra must
//                                                       count them)
//   - use Repeat instead of RepeatRange for VarArray -> backward fails (short arrays exist)
//   - use Add instead of Union for alternatives      -> fails (one alternative serializes,
//                                                       not all of them)
//   - drop Pad from the struct fold                  -> fails for align > 1 (alignment bits
//                                                       are real)
//   - drop the trailing byte-Pad from Struct/UnionT  -> fails whenever the pre-pad length is
//                                                       not a multiple of 8 (the analyzer pads
//                                                       every section end and every union to a
//                                                       byte boundary: Analyzer.cpp:1209 / :884)
//
// Verified with: dafny verify spec/dafny/BitLengthBridge.dfy   (Dafny 4.11, CI-enforced)

include "BitLengthSet.dfy"

module BitLengthBridge {
  import opened BitLengthSetModel

  // ==========================================================================
  // The type grammar the analyzer compiles (bit-granular; see header for the
  // CyphalSerdes.dfy correspondence).
  // ==========================================================================

  // A struct's fields are two parallel sequences (per-field alignment and type) rather than a
  // sequence of records: termination descents into `typs[i]` are then seq<Typ>-member descents,
  // which Dafny's rank ordering handles directly (a record wrapper defeats the automatic
  // rank chain through the destructor).
  datatype Typ =
    | Scalar(width: int)
    | Struct(aligns: seq<int>, typs: seq<Typ>)
    | UnionT(tagBits: int, alts: seq<Typ>)
    | FixedArray(elem: Typ, n: nat)
    | VarArray(prefixBits: int, elem: Typ, cap: nat)

  ghost predicate TWFFields(aligns: seq<int>, typs: seq<Typ>)
    decreases typs, 1, 0
  {
    |aligns| == |typs|
    && (forall j | 0 <= j < |aligns| :: 1 <= aligns[j])
    && forall j | 0 <= j < |typs| :: TWF(typs[j])
  }

  ghost predicate TWF(t: Typ)
    decreases t, 0, 0
  {
    match t
    case Scalar(w) => 0 <= w
    case Struct(aligns, typs) => TWFFields(aligns, typs)
    case UnionT(tb, alts) => 0 <= tb && 1 <= |alts| && forall j | 0 <= j < |alts| :: TWF(alts[j])
    case FixedArray(e, _) => TWF(e)
    case VarArray(p, e, _) => 0 <= p && TWF(e)
  }

  // ==========================================================================
  // Values and conformance. A scalar value carries no payload: the payload does
  // not influence the serialized LENGTH. Conformance is pointwise over indices.
  // ==========================================================================

  datatype Val =
    | VScalar
    | VStruct(vs: seq<Val>)
    | VUnion(tag: nat, inner: Val)
    | VElems(es: seq<Val>)

  // Values vs[i..] conform to fields fs[i..] (entries below i are unconstrained,
  // which is what lets the struct induction thread partially-built witnesses).
  ghost predicate ConformsSuffix(typs: seq<Typ>, vs: seq<Val>, i: nat)
    decreases typs, 1, 0
  {
    |vs| == |typs| && forall j | i <= j < |typs| :: Conforms(typs[j], vs[j])
  }

  ghost predicate ConformsSeq(e: Typ, es: seq<Val>)
    decreases e, 1, 0
  {
    forall j | 0 <= j < |es| :: Conforms(e, es[j])
  }

  ghost predicate Conforms(t: Typ, v: Val)
    decreases t, 0, 0
  {
    match t
    case Scalar(_) => v.VScalar?
    case Struct(_, typs) => v.VStruct? && ConformsSuffix(typs, v.vs, 0)
    case UnionT(_, alts) => v.VUnion? && v.tag < |alts| && Conforms(alts[v.tag], v.inner)
    case FixedArray(e, n) => v.VElems? && |v.es| == n && ConformsSeq(e, v.es)
    case VarArray(_, e, cap) => v.VElems? && |v.es| <= cap && ConformsSeq(e, v.es)
  }

  // ==========================================================================
  // The REQUIREMENT: the serialized bit length of a conforming value — the
  // length semantics of DSDL serialization, one rule per constructor. Struct
  // offsets are relative to the struct's own start (composites begin
  // byte-aligned, so field alignment is start-relative — the same convention
  // as the analyzer's structureOffset accumulation). Struct and union lengths
  // are rounded up to a byte boundary: a serialized composite always occupies
  // a whole number of bytes (the trailing pad the analyzer applies at
  // Analyzer.cpp:1209 and Analyzer.cpp:884).
  // ==========================================================================

  ghost function TailLen(aligns: seq<int>, typs: seq<Typ>, vs: seq<Val>, i: nat, off: int): int
    requires TWFFields(aligns, typs) && i <= |typs| && ConformsSuffix(typs, vs, i)
    decreases typs, 1, |typs| - i
  {
    if i == |typs|
    then off
    else TailLen(aligns, typs, vs, i + 1, RoundUp(off, aligns[i]) + BitLen(typs[i], vs[i]))
  }

  ghost function SeqLenFrom(e: Typ, es: seq<Val>, i: nat): int
    requires TWF(e) && ConformsSeq(e, es) && i <= |es|
    decreases e, 1, |es| - i
  {
    if i == |es| then 0 else BitLen(e, es[i]) + SeqLenFrom(e, es, i + 1)
  }

  ghost function BitLen(t: Typ, v: Val): int
    requires TWF(t) && Conforms(t, v)
    decreases t, 0, 0
  {
    match t
    case Scalar(w) => w
    case Struct(aligns, typs) => RoundUp(TailLen(aligns, typs, v.vs, 0, 0), 8)
    case UnionT(tb, alts) => RoundUp(tb + BitLen(alts[v.tag], v.inner), 8)
    case FixedArray(e, _) => SeqLenFrom(e, v.es, 0)
    case VarArray(p, e, _) => p + SeqLenFrom(e, v.es, 0)
  }

  // TailLen reads only indices >= i, so witnesses may differ below i.
  lemma TailLenAgree(aligns: seq<int>, typs: seq<Typ>, vs1: seq<Val>, vs2: seq<Val>, i: nat, off: int)
    requires TWFFields(aligns, typs) && i <= |typs|
    requires ConformsSuffix(typs, vs1, i) && ConformsSuffix(typs, vs2, i)
    requires forall j | i <= j < |typs| :: vs1[j] == vs2[j]
    ensures TailLen(aligns, typs, vs1, i, off) == TailLen(aligns, typs, vs2, i, off)
    decreases |typs| - i
  {
    if i < |typs| {
      TailLenAgree(aligns, typs, vs1, vs2, i + 1, RoundUp(off, aligns[i]) + BitLen(typs[i], vs1[i]));
    }
  }

  // SeqLenFrom over a cons is the head plus SeqLenFrom over the tail.
  lemma SeqLenShift(e: Typ, v0: Val, rest: seq<Val>, i: nat)
    requires TWF(e) && Conforms(e, v0) && ConformsSeq(e, rest) && i <= |rest|
    requires ConformsSeq(e, [v0] + rest)
    ensures SeqLenFrom(e, [v0] + rest, i + 1) == SeqLenFrom(e, rest, i)
    decreases |rest| - i
  {
    if i < |rest| {
      assert ([v0] + rest)[i + 1] == rest[i];
      SeqLenShift(e, v0, rest, i + 1);
    }
  }

  // ==========================================================================
  // The IMPLEMENTATION map: the algebra expression the analyzer builds for a
  // type — a transcription of the Analyzer.cpp construction sites (see the
  // traceability notes on Expr in BitLengthSet.dfy).
  // ==========================================================================

  // Total (BlsOf carries no precondition): a missing alignment defaults to 1, which TWF makes
  // unreachable — the guard exists so the function is total without threading TWF everywhere.
  ghost function BlsOfFieldsFrom(aligns: seq<int>, typs: seq<Typ>, i: nat, acc: Expr): Expr
    requires i <= |typs|
    decreases typs, 1, |typs| - i
  {
    if i == |typs|
    then acc
    else BlsOfFieldsFrom(aligns, typs, i + 1,
                         Add(Pad(acc, if i < |aligns| then aligns[i] else 1), BlsOf(typs[i])))
  }

  ghost function BlsOfAltsFrom(alts: seq<Typ>, i: nat): Expr
    requires i < |alts|
    decreases alts, 1, |alts| - i
  {
    if i == |alts| - 1 then BlsOf(alts[i]) else Union(BlsOf(alts[i]), BlsOfAltsFrom(alts, i + 1))
  }

  // Struct and union both end with the trailing byte-pad the analyzer applies:
  //   struct -> Analyzer.cpp:1209 `section.offsetAtEnd = structureOffset.padToAlignment(8)`
  //   union  -> Analyzer.cpp:884  `(BitLengthSet(tagBits) + payloadSet).padToAlignment(8)`
  ghost function BlsOf(t: Typ): Expr
    decreases t, 0, 0
  {
    match t
    case Scalar(w) => Leaf({w})
    case Struct(aligns, typs) => Pad(BlsOfFieldsFrom(aligns, typs, 0, Leaf({0})), 8)
    case UnionT(tb, alts) =>
      if |alts| == 0 then Leaf({0})  // unreachable under TWF; keeps the function total
      else Pad(Add(Leaf({tb}), BlsOfAltsFrom(alts, 0)), 8)
    case FixedArray(e, n) => Repeat(BlsOf(e), n)
    case VarArray(p, e, cap) => Add(Leaf({p}), RepeatRange(BlsOf(e), cap))
  }

  // ==========================================================================
  // Well-formedness of the built expressions.
  // ==========================================================================

  lemma WFBlsFields(aligns: seq<int>, typs: seq<Typ>, i: nat, acc: Expr)
    requires TWFFields(aligns, typs) && i <= |typs| && WF(acc)
    ensures WF(BlsOfFieldsFrom(aligns, typs, i, acc))
    decreases typs, 1, |typs| - i
  {
    if i < |typs| {
      assert i < |aligns| && 1 <= aligns[i];
      WFBls(typs[i]);
      WFBlsFields(aligns, typs, i + 1, Add(Pad(acc, aligns[i]), BlsOf(typs[i])));
    }
  }

  lemma WFBlsAlts(alts: seq<Typ>, i: nat)
    requires (forall j | 0 <= j < |alts| :: TWF(alts[j])) && i < |alts|
    ensures WF(BlsOfAltsFrom(alts, i))
    decreases alts, 1, |alts| - i
  {
    WFBls(alts[i]);
    if i < |alts| - 1 {
      WFBlsAlts(alts, i + 1);
    }
  }

  lemma WFBls(t: Typ)
    requires TWF(t)
    ensures WF(BlsOf(t))
    decreases t, 0, 0
  {
    match t
    case Scalar(w) =>
    case Struct(aligns, typs) =>
      WFBlsFields(aligns, typs, 0, Leaf({0}));
    case UnionT(tb, alts) =>
      WFBlsAlts(alts, 0);
    case FixedArray(e, n) =>
      WFBls(e);
    case VarArray(p, e, cap) =>
      WFBls(e);
  }

  // ==========================================================================
  // The bridge theorem and its mutual-induction helpers. Lemma decreases
  // triples: (carrier, phase, remaining) — helpers run at phase 2 above
  // BridgeTheorem's phase 0, so a helper may call the theorem on the SAME
  // carrier (ArrayBridge on its element type) and the theorem may call helpers
  // on strictly smaller carriers (constructor arguments / sequence members).
  // ==========================================================================

  // Fixed count: L is a sum of exactly n independent element lengths iff L is
  // in the n-fold Minkowski power of the element set.
  lemma {:isolate_assertions} ArrayBridge(e: Typ, n: nat, L: int)
    requires TWF(e)
    ensures WF(BlsOf(e))
    ensures (exists es :: |es| == n && ConformsSeq(e, es) && SeqLenFrom(e, es, 0) == L)
            <==> L in RepeatSem(Sem(BlsOf(e)), n)
    decreases e, 2, n
  {
    WFBls(e);
    if n == 0 {
      if L == 0 {
        var empty: seq<Val> := [];
        assert ConformsSeq(e, empty) && SeqLenFrom(e, empty, 0) == 0;
      }
    } else {
      if exists es :: |es| == n && ConformsSeq(e, es) && SeqLenFrom(e, es, 0) == L {
        var es :| |es| == n && ConformsSeq(e, es) && SeqLenFrom(e, es, 0) == L;
        var v0 := es[0];
        var rest := es[1..];
        assert es == [v0] + rest;
        assert ConformsSeq(e, rest);
        SeqLenShift(e, v0, rest, 0);
        var head := BitLen(e, v0);
        var tailLen := SeqLenFrom(e, rest, 0);
        assert L == head + tailLen;
        BridgeTheorem(e, head);
        assert Conforms(e, v0) && BitLen(e, v0) == head;
        assert exists w :: Conforms(e, w) && BitLen(e, w) == head;
        assert head in Sem(BlsOf(e));
        ArrayBridge(e, n - 1, tailLen);
        assert |rest| == n - 1 && ConformsSeq(e, rest) && SeqLenFrom(e, rest, 0) == tailLen;
        assert tailLen in RepeatSem(Sem(BlsOf(e)), n - 1);
        assert L == tailLen + head;
        assert L in RepeatSem(Sem(BlsOf(e)), n);
      }
      if L in RepeatSem(Sem(BlsOf(e)), n) {
        var x, y :| x in RepeatSem(Sem(BlsOf(e)), n - 1) && y in Sem(BlsOf(e)) && L == x + y;
        ArrayBridge(e, n - 1, x);
        var rest :| |rest| == n - 1 && ConformsSeq(e, rest) && SeqLenFrom(e, rest, 0) == x;
        BridgeTheorem(e, y);
        var v0 :| Conforms(e, v0) && BitLen(e, v0) == y;
        var es := [v0] + rest;
        assert |es| == n;
        assert forall j | 0 <= j < |es| :: Conforms(e, es[j]);
        SeqLenShift(e, v0, rest, 0);
        assert SeqLenFrom(e, es, 0) == y + x == L;
      }
    }
  }

  // Alternatives: L is achievable by SOME alternative in alts[i..] iff L is in
  // their union.
  lemma AltsBridge(alts: seq<Typ>, i: nat, L: int)
    requires (forall j | 0 <= j < |alts| :: TWF(alts[j])) && i < |alts|
    ensures WF(BlsOfAltsFrom(alts, i))
    ensures (exists j :: i <= j < |alts| && WF(BlsOf(alts[j])) && L in Sem(BlsOf(alts[j])))
            <==> L in Sem(BlsOfAltsFrom(alts, i))
    decreases alts, 2, |alts| - i
  {
    WFBlsAlts(alts, i);
    WFBls(alts[i]);
    if i == |alts| - 1 {
      // BlsOfAltsFrom is BlsOf(alts[i]); the only candidate witness is j == i.
      if exists j :: i <= j < |alts| && WF(BlsOf(alts[j])) && L in Sem(BlsOf(alts[j])) {
        var j :| i <= j < |alts| && WF(BlsOf(alts[j])) && L in Sem(BlsOf(alts[j]));
        assert j == i;
      }
    } else {
      AltsBridge(alts, i + 1, L);
      if exists j :: i <= j < |alts| && WF(BlsOf(alts[j])) && L in Sem(BlsOf(alts[j])) {
        var j :| i <= j < |alts| && WF(BlsOf(alts[j])) && L in Sem(BlsOf(alts[j]));
        if j == i {
          assert L in Sem(BlsOfAltsFrom(alts, i));
        } else {
          assert i + 1 <= j < |alts| && WF(BlsOf(alts[j])) && L in Sem(BlsOf(alts[j]));
          assert L in Sem(BlsOfAltsFrom(alts, i + 1));
        }
      }
      if L in Sem(BlsOfAltsFrom(alts, i)) {
        if L in Sem(BlsOf(alts[i])) {
          assert i <= i < |alts| && WF(BlsOf(alts[i])) && L in Sem(BlsOf(alts[i]));
        } else {
          assert L in Sem(BlsOfAltsFrom(alts, i + 1));
          var j :| i + 1 <= j < |alts| && WF(BlsOf(alts[j])) && L in Sem(BlsOf(alts[j]));
          assert i <= j < |alts| && WF(BlsOf(alts[j])) && L in Sem(BlsOf(alts[j]));
        }
      }
    }
  }

  // Struct fold: the offsets reachable after fields fs[..i] are Sem(acc); the
  // fold threads pad-then-add exactly as the analyzer accumulates
  // structureOffset.
  lemma {:isolate_assertions} FieldsBridge(aligns: seq<int>, typs: seq<Typ>, i: nat, acc: Expr, L: int)
    requires TWFFields(aligns, typs) && i <= |typs| && WF(acc)
    ensures WF(BlsOfFieldsFrom(aligns, typs, i, acc))
    ensures (exists off, vs :: off in Sem(acc) && ConformsSuffix(typs, vs, i)
                            && TailLen(aligns, typs, vs, i, off) == L)
            <==> L in Sem(BlsOfFieldsFrom(aligns, typs, i, acc))
    decreases typs, 2, |typs| - i
  {
    WFBlsFields(aligns, typs, i, acc);
    if i == |typs| {
      if exists off, vs :: off in Sem(acc) && ConformsSuffix(typs, vs, i)
                        && TailLen(aligns, typs, vs, i, off) == L {
        var off, vs :| off in Sem(acc) && ConformsSuffix(typs, vs, i)
                    && TailLen(aligns, typs, vs, i, off) == L;
        assert L == off;
      }
      if L in Sem(acc) {
        var vs := seq(|typs|, _ => VScalar);
        assert ConformsSuffix(typs, vs, i) && TailLen(aligns, typs, vs, i, L) == L;
      }
    } else {
      var a0 := aligns[i];
      var t0 := typs[i];
      assert i < |aligns| && 1 <= a0;
      var step := Add(Pad(acc, a0), BlsOf(t0));
      WFBls(t0);
      assert WF(step);
      assert BlsOfFieldsFrom(aligns, typs, i, acc) == BlsOfFieldsFrom(aligns, typs, i + 1, step);
      FieldsBridge(aligns, typs, i + 1, step, L);

      if exists off, vs :: off in Sem(acc) && ConformsSuffix(typs, vs, i)
                        && TailLen(aligns, typs, vs, i, off) == L {
        var off, vs :| off in Sem(acc) && ConformsSuffix(typs, vs, i)
                    && TailLen(aligns, typs, vs, i, off) == L;
        var off1 := RoundUp(off, a0) + BitLen(t0, vs[i]);
        assert RoundUp(off, a0) in PadSet(Sem(acc), a0);
        BridgeTheorem(t0, BitLen(t0, vs[i]));
        assert BitLen(t0, vs[i]) in Sem(BlsOf(t0));
        assert off1 in Sem(step);
        assert ConformsSuffix(typs, vs, i + 1);
        assert TailLen(aligns, typs, vs, i + 1, off1) == L;
        assert exists off2, vs2 {:trigger ConformsSuffix(typs, vs2, i + 1), off2 in Sem(step)} ::
            off2 in Sem(step) && ConformsSuffix(typs, vs2, i + 1)
            && TailLen(aligns, typs, vs2, i + 1, off2) == L;
        assert L in Sem(BlsOfFieldsFrom(aligns, typs, i + 1, step));
        assert L in Sem(BlsOfFieldsFrom(aligns, typs, i, acc));
      }
      if L in Sem(BlsOfFieldsFrom(aligns, typs, i, acc)) {
        assert L in Sem(BlsOfFieldsFrom(aligns, typs, i + 1, step));
        var off1, vs1 :| off1 in Sem(step) && ConformsSuffix(typs, vs1, i + 1)
                      && TailLen(aligns, typs, vs1, i + 1, off1) == L;
        var p, b :| p in PadSet(Sem(acc), a0) && b in Sem(BlsOf(t0)) && off1 == p + b;
        var off :| off in Sem(acc) && p == RoundUp(off, a0);
        BridgeTheorem(t0, b);
        var v0 :| Conforms(t0, v0) && BitLen(t0, v0) == b;
        var vs := vs1[i := v0];
        assert ConformsSuffix(typs, vs, i);
        assert forall j | i + 1 <= j < |typs| :: vs[j] == vs1[j];
        assert ConformsSuffix(typs, vs, i + 1);
        TailLenAgree(aligns, typs, vs, vs1, i + 1, off1);
        assert TailLen(aligns, typs, vs, i + 1, off1) == L;
        assert RoundUp(off, a0) + BitLen(t0, vs[i]) == off1;
        assert TailLen(aligns, typs, vs, i, off) == L;
        assert exists off2, vs2 :: off2 in Sem(acc) && ConformsSuffix(typs, vs2, i)
                                && TailLen(aligns, typs, vs2, i, off2) == L;
      }
    }
  }

  // The bridge: the algebra expression the analyzer builds for `t` denotes
  // EXACTLY the achievable serialized bit lengths of values conforming to `t`.
  lemma BridgeTheorem(t: Typ, L: int)
    requires TWF(t)
    ensures WF(BlsOf(t))
    ensures (exists v :: Conforms(t, v) && BitLen(t, v) == L) <==> L in Sem(BlsOf(t))
    decreases t, 0, 0
  {
    WFBls(t);
    match t
    case Scalar(w) =>
      assert Sem(BlsOf(t)) == {w};
      if L == w {
        assert Conforms(t, VScalar) && BitLen(t, VScalar) == w;
        assert exists v :: Conforms(t, v) && BitLen(t, v) == L;
      } else {
        if exists v :: Conforms(t, v) && BitLen(t, v) == L {
          var v :| Conforms(t, v) && BitLen(t, v) == L;
          assert v.VScalar? && BitLen(t, v) == w;
          assert false;
        }
      }
    case Struct(aligns, typs) =>
      // BlsOf(t) == Pad(fold, 8): bridge the fold at the PRE-PAD length L0, then carry
      // membership through PadSet (BitLen applies the same RoundUp(_, 8)).
      var fold := BlsOfFieldsFrom(aligns, typs, 0, Leaf({0}));
      if exists v :: Conforms(t, v) && BitLen(t, v) == L {
        var v :| Conforms(t, v) && BitLen(t, v) == L;
        var L0 := TailLen(aligns, typs, v.vs, 0, 0);
        FieldsBridge(aligns, typs, 0, Leaf({0}), L0);
        assert 0 in Sem(Leaf({0}));
        assert ConformsSuffix(typs, v.vs, 0) && TailLen(aligns, typs, v.vs, 0, 0) == L0;
        assert exists off, vs :: off in Sem(Leaf({0})) && ConformsSuffix(typs, vs, 0)
                              && TailLen(aligns, typs, vs, 0, off) == L0;
        assert L0 in Sem(fold);
        assert L == RoundUp(L0, 8);
        assert L in PadSet(Sem(fold), 8);
        assert L in Sem(BlsOf(t));
      }
      if L in Sem(BlsOf(t)) {
        assert L in PadSet(Sem(fold), 8);
        var L0 :| L0 in Sem(fold) && L == RoundUp(L0, 8);
        FieldsBridge(aligns, typs, 0, Leaf({0}), L0);
        var off, vs :| off in Sem(Leaf({0})) && ConformsSuffix(typs, vs, 0)
                    && TailLen(aligns, typs, vs, 0, off) == L0;
        assert off == 0;
        assert Conforms(t, VStruct(vs)) && BitLen(t, VStruct(vs)) == RoundUp(L0, 8) == L;
        assert exists v :: Conforms(t, v) && BitLen(t, v) == L;
      }
    case UnionT(tb, alts) =>
      forall j | 0 <= j < |alts|
        ensures WF(BlsOf(alts[j]))
      {
        WFBls(alts[j]);
      }
      // BlsOf(t) == Pad(Add(tag, alts), 8): bridge tag + alternative at the PRE-PAD length
      // L0, then carry membership through PadSet (BitLen applies the same RoundUp(_, 8)).
      var core := Add(Leaf({tb}), BlsOfAltsFrom(alts, 0));
      if exists v :: Conforms(t, v) && BitLen(t, v) == L {
        var v :| Conforms(t, v) && BitLen(t, v) == L;
        var y := BitLen(alts[v.tag], v.inner);
        AltsBridge(alts, 0, y);
        BridgeTheorem(alts[v.tag], y);
        assert Conforms(alts[v.tag], v.inner) && BitLen(alts[v.tag], v.inner) == y;
        assert exists w :: Conforms(alts[v.tag], w) && BitLen(alts[v.tag], w) == y;
        assert y in Sem(BlsOf(alts[v.tag]));
        assert 0 <= v.tag < |alts| && WF(BlsOf(alts[v.tag])) && y in Sem(BlsOf(alts[v.tag]));
        assert exists j :: 0 <= j < |alts| && WF(BlsOf(alts[j])) && y in Sem(BlsOf(alts[j]));
        assert y in Sem(BlsOfAltsFrom(alts, 0));
        assert tb in Sem(Leaf({tb}));
        assert tb + y in Sem(core);
        assert L == RoundUp(tb + y, 8);
        assert L in PadSet(Sem(core), 8);
        assert L in Sem(BlsOf(t));
      }
      if L in Sem(BlsOf(t)) {
        assert L in PadSet(Sem(core), 8);
        var L0 :| L0 in Sem(core) && L == RoundUp(L0, 8);
        var x, y :| x in Sem(Leaf({tb})) && y in Sem(BlsOfAltsFrom(alts, 0)) && L0 == x + y;
        assert x == tb;
        assert y == L0 - tb;
        AltsBridge(alts, 0, y);
        var j :| 0 <= j < |alts| && WF(BlsOf(alts[j])) && y in Sem(BlsOf(alts[j]));
        BridgeTheorem(alts[j], y);
        var inner :| Conforms(alts[j], inner) && BitLen(alts[j], inner) == y;
        assert Conforms(t, VUnion(j, inner))
            && BitLen(t, VUnion(j, inner)) == RoundUp(tb + y, 8) == L;
        assert exists v :: Conforms(t, v) && BitLen(t, v) == L;
      }
    case FixedArray(e, n) =>
      ArrayBridge(e, n, L);
      if exists v :: Conforms(t, v) && BitLen(t, v) == L {
        var v :| Conforms(t, v) && BitLen(t, v) == L;
        assert |v.es| == n && ConformsSeq(e, v.es) && SeqLenFrom(e, v.es, 0) == L;
        assert exists es :: |es| == n && ConformsSeq(e, es) && SeqLenFrom(e, es, 0) == L;
        assert L in RepeatSem(Sem(BlsOf(e)), n);
        assert L in Sem(BlsOf(t));
      }
      if L in Sem(BlsOf(t)) {
        assert L in RepeatSem(Sem(BlsOf(e)), n);
        var es :| |es| == n && ConformsSeq(e, es) && SeqLenFrom(e, es, 0) == L;
        assert Conforms(t, VElems(es)) && BitLen(t, VElems(es)) == L;
        assert exists v :: Conforms(t, v) && BitLen(t, v) == L;
      }
    case VarArray(p, e, cap) =>
      WFBls(e);
      if exists v :: Conforms(t, v) && BitLen(t, v) == L {
        var v :| Conforms(t, v) && BitLen(t, v) == L;
        var k := |v.es|;
        ArrayBridge(e, k, L - p);
        assert SeqLenFrom(e, v.es, 0) == L - p;
        assert |v.es| == k && ConformsSeq(e, v.es) && SeqLenFrom(e, v.es, 0) == L - p;
        assert exists es :: |es| == k && ConformsSeq(e, es) && SeqLenFrom(e, es, 0) == L - p;
        assert L - p in RepeatSem(Sem(BlsOf(e)), k);
        RepeatRangeCharacterization(Sem(BlsOf(e)), cap, L - p);
        assert L - p in RepeatRangeSem(Sem(BlsOf(e)), cap);
        assert p in Sem(Leaf({p}));
        assert L == p + (L - p);
        assert L in Sem(BlsOf(t));
      }
      if L in Sem(BlsOf(t)) {
        var x, y :| x in Sem(Leaf({p})) && y in Sem(RepeatRange(BlsOf(e), cap)) && L == x + y;
        assert x == p;
        RepeatRangeCharacterization(Sem(BlsOf(e)), cap, y);
        var k: nat :| k <= cap && y in RepeatSem(Sem(BlsOf(e)), k);
        ArrayBridge(e, k, y);
        var es :| |es| == k && ConformsSeq(e, es) && SeqLenFrom(e, es, 0) == y;
        assert Conforms(t, VElems(es)) && BitLen(t, VElems(es)) == p + y == L;
        assert exists v :: Conforms(t, v) && BitLen(t, v) == L;
      }
  }
}
