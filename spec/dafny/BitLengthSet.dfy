// SPDX-License-Identifier: MIT
//
// Formal model of the BitLengthSet expression algebra and its evaluators.
//
// SCOPE: the expression DAG's mathematical denotation and interval evaluators before int64
// saturation. `FitsInt64` is the explicit precondition under which ordinary modular
// homomorphisms describe the C++ denotation. The C++ evaluator computes the same condition
// structurally before using symbolic residues; saturated boundary behaviour is checked against
// the arbitrary-precision oracle in test/integration/bls_int64_differential.py.
//
// WHAT IS PROVEN (unbounded — for all well-formed expressions, by structural induction):
//   SemNonEmpty          : I1 — the denoted set is never empty.
//   SemNonNeg            : the value-domain invariant — every denoted value is >= 0. The C++
//                          constructors ESTABLISH this by clamping negatives to 0; WF is the
//                          model-side record of that guarantee.
//   MinExact / MaxExact  : the per-node interval evaluators (the C++ min()/max() derivations,
//                          transcribed) return true elements of S that bound S — exact at any
//                          cardinality, with no refusal path. These queries must be TOTAL:
//                          layout classification and buffer sizing depend on them
//                          unconditionally.
//   FixedCharacterization: fixed() == (min == max) decides |S| == 1 exactly.
//   Algebraic laws       : the header's law table as theorems over Sem ALONE — Minkowski
//                          commutativity/associativity/identity, + distributes over |,
//                          repeat additivity (RepeatSplit — the correctness backbone of
//                          RunSet::repeated()), pad idempotence/identity/multiples/soundness,
//                          and the repeatRange union characterization. These constrain the
//                          DENOTATION independently of any evaluator, so a mis-transcribed
//                          Sem fails even if an evaluator were mis-transcribed to match.
//
// WHY WF MATTERS (the model has teeth): RepeatRange's minimum is 0 and its maximum is
// kMax * max(inner) ONLY on the non-negative domain. Deleting the `0 <= v` conjunct from WF
// breaks MinExact and MaxExact at the RepeatRange case — which is precisely defect BLS-D5/D15
// (negative values made min()/fixed() unsound) resurfacing as a failed proof instead of a
// field bug. See NEGATIVE CONTROLS below.
//
// ASSURANCE SCOPE: this proves the abstract mathematical algebra, not the C++ implementation.
// The link to lib/Semantics/BitLengthSet.cpp is transcription discipline plus the differential
// batteries. Saturation and the RunSet/residue kernels are checked by independent executable
// oracles and sanitizers.
//
// NEGATIVE CONTROLS (each mutation must break the named proof; checked manually when the
// model changes):
//   - drop `0 <= v` from WF(Leaf)            -> MinExact/MaxExact fail at RepeatRange
//   - change MinEval(RepeatRange) to min(...) -> MinExact fails (0 is the k = 0 term)
//   - change MaxEval(Union) to `min`          -> MaxExact fails
//   - drop `vs != {}` from WF(Leaf)           -> SemNonEmpty fails; SetMin precondition fails
//   - weaken RoundUp to round DOWN            -> RoundUpProps and PadSetSound fail — caught
//                                                even though Sem, the evaluators, AND the
//                                                idempotence/multiples laws all share the
//                                                mutated definition (the co-mutation case)
//
// Verified with: dafny verify spec/dafny/BitLengthSet.dfy   (Dafny 4.11, CI-enforced)

module BitLengthSetModel {

  // ==========================================================================
  // Expression DAG — mirrors BitLengthSet::Node::Kind.
  //
  // TRACEABILITY (the requirement each constructor implements). Authority: the OpenCyphal
  // Specification v1.0, ch. 3 (DSDL), "Serialized representations"; pydsdl's BitLengthSet is
  // the peer implementation of the same algebra. The C++ site listed for each constructor is
  // where the analyzer builds that node, so the map from language rule to algebra is auditable
  // end to end:
  //
  //   Leaf        — a scalar/void field's possible widths (a fixed-width primitive is a
  //                 singleton). Built throughout Analyzer.cpp layout resolution.
  //   Add         — CONCATENATION: fields of a structure serialize in declaration order, so
  //                 lengths add (Minkowski sum over the possibilities).
  //                 Analyzer.cpp analyzeSection: `structureOffset + layout.bls`.
  //   Union       — TAGGED-UNION ALTERNATIVES: exactly one alternative serializes, so the
  //                 payload length set is the union over alternatives (the tag itself is a
  //                 separate Add). Analyzer.cpp computeUnionOffsetFromSeenFields:
  //                 `(BitLengthSet(tagBits) + payloadSet).padToAlignment(8)`.
  //   Pad         — ALIGNMENT PADDING: composite boundaries round up to the alignment (byte
  //                 alignment in practice). Analyzer.cpp: `padToAlignment(layout.alignment)`
  //                 between fields, `.padToAlignment(8)` at section ends.
  //   Repeat      — FIXED-LENGTH ARRAYS: exactly `k` elements serialize back to back.
  //                 Analyzer.cpp: `scalarLayout.bls.repeat(capacity)`.
  //   RepeatRange — VARIABLE-LENGTH ARRAY PAYLOAD: any count in [0, capacity] of elements
  //                 (the implicit length prefix is accounted separately by the caller, as an
  //                 Add — mirroring the C++ header's note). Analyzer.cpp:
  //                 `bls + scalarLayout.bls.repeatRange(capacity)`; also delimited-composite
  //                 payloads: `BitLengthSet(8).repeatRange(extent / 8)`.
  // ==========================================================================

  datatype Expr =
    | Leaf(vs: set<int>)
    | Add(l: Expr, r: Expr)
    | Union(ul: Expr, ur: Expr)
    | Pad(pe: Expr, align: int)
    | Repeat(re: Expr, k: nat)
    | RepeatRange(rre: Expr, kMax: nat)

  // Well-formedness: what the C++ constructors establish by clamping — leaves are non-empty
  // (I1: empty input coerces to {0}) with non-negative values (value domain: negatives clamp
  // to 0), and alignments are >= 1 (padToAlignment clamps). Repeat counts are `nat` by type.
  ghost predicate WF(e: Expr)
  {
    match e
    case Leaf(vs) => vs != {} && forall v <- vs :: 0 <= v
    case Add(l, r) => WF(l) && WF(r)
    case Union(l, r) => WF(l) && WF(r)
    case Pad(inner, a) => 1 <= a && WF(inner)
    case Repeat(inner, _) => WF(inner)
    case RepeatRange(inner, _) => WF(inner)
  }

  // ==========================================================================
  // Denotational semantics — the header's "Denotationally, with S(x) ..." table.
  // ==========================================================================

  ghost function MSum(a: set<int>, b: set<int>): set<int>
  {
    set x, y | x in a && y in b :: x + y
  }

  function RoundUp(v: int, a: int): int
    requires 1 <= a
  {
    if v % a == 0 then v else v + (a - v % a)
  }

  ghost function PadSet(s: set<int>, a: int): set<int>
    requires 1 <= a
  {
    set v | v in s :: RoundUp(v, a)
  }

  // k-fold Minkowski self-sum; k == 0 denotes {0} (the empty concatenation).
  ghost function RepeatSem(s: set<int>, k: nat): set<int>
  {
    if k == 0 then {0} else MSum(RepeatSem(s, k - 1), s)
  }

  // Union of the j-fold self-sums for j in [0, kMax]; always contains 0 (the j = 0 term).
  ghost function RepeatRangeSem(s: set<int>, kMax: nat): set<int>
  {
    if kMax == 0 then {0} else RepeatRangeSem(s, kMax - 1) + RepeatSem(s, kMax)
  }

  ghost function Sem(e: Expr): set<int>
    requires WF(e)
  {
    match e
    case Leaf(vs) => vs
    case Add(l, r) => MSum(Sem(l), Sem(r))
    case Union(l, r) => Sem(l) + Sem(r)
    case Pad(inner, a) => PadSet(Sem(inner), a)
    case Repeat(inner, k) => RepeatSem(Sem(inner), k)
    case RepeatRange(inner, kMax) => RepeatRangeSem(Sem(inner), kMax)
  }

  const MaxInt64: int := 9223372036854775807

  // Ordinary addition, padding, and repetition modulo d are valid for the C++ denotation only
  // when every reachable mathematical value fits int64. The C++ saturationReachable() analysis
  // computes this condition bottom-up from exact achievable maxima.
  ghost predicate FitsInt64(e: Expr)
    requires WF(e)
  {
    forall value <- Sem(e) :: value <= MaxInt64
  }

  // ==========================================================================
  // Arithmetic support: multiplication positivity and Euclidean div/mod facts.
  // Small, reusable arithmetic kit.
  // ==========================================================================

  lemma MulNonNeg(x: int, y: int)
    requires 0 <= x && 0 <= y
    ensures 0 <= x * y
    decreases y
  {
    if y > 0 {
      MulNonNeg(x, y - 1);
      assert x * y == x * (y - 1) + x;
    }
  }

  lemma MulGeFactor(a: int, d: int)
    requires 1 <= a && 1 <= d
    ensures a <= a * d
  {
    assert a * d - a == a * (d - 1);
    MulNonNeg(a, d - 1);
  }

  // Euclidean quotient/remainder are unique — the bridge from "x == a*q + r with r in [0, a)"
  // to conclusions about x / a and x % a.
  lemma DivModUnique(x: int, a: int, q: int, r: int)
    requires 1 <= a && x == a * q + r && 0 <= r < a
    ensures x / a == q && x % a == r
  {
    var q0 := x / a;
    var r0 := x % a;
    assert x == a * q0 + r0 && 0 <= r0 < a;
    assert a * (q - q0) == r0 - r;
    if q > q0 {
      MulGeFactor(a, q - q0);
      assert false;
    }
    if q < q0 {
      assert a * (q0 - q) == r - r0;
      MulGeFactor(a, q0 - q);
      assert false;
    }
  }

  lemma MulModZero(k: int, a: int)
    requires 1 <= a
    ensures (a * k) % a == 0
  {
    DivModUnique(a * k, a, k, 0);
  }

  // RoundUp is the least multiple of `a` at or above `v` — characterized by these three facts.
  lemma RoundUpProps(v: int, a: int)
    requires 1 <= a
    ensures RoundUp(v, a) % a == 0
    ensures v <= RoundUp(v, a) < v + a
  {
    if v % a != 0 {
      var q := v / a;
      assert v == a * q + v % a;
      assert RoundUp(v, a) == a * (q + 1);
      MulModZero(q + 1, a);
    }
  }

  // Two distinct multiples of `a` differ by at least `a`.
  lemma MultiplesApart(x: int, y: int, a: int)
    requires 1 <= a && x % a == 0 && y % a == 0 && x < y
    ensures a <= y - x
  {
    var qx := x / a;
    var qy := y / a;
    assert x == a * qx && y == a * qy;
    assert a * (qy - qx) == y - x;
    if qy <= qx {
      MulNonNeg(a, qx - qy);
      assert a * (qx - qy) == x - y;
      assert false;
    }
    MulGeFactor(a, qy - qx);
  }

  lemma RoundUpLeast(v: int, a: int, w: int)
    requires 1 <= a && w % a == 0 && v <= w
    ensures RoundUp(v, a) <= w
  {
    RoundUpProps(v, a);
    if w < RoundUp(v, a) {
      MultiplesApart(w, RoundUp(v, a), a);
      assert false;
    }
  }

  lemma RoundUpMono(u: int, v: int, a: int)
    requires 1 <= a && u <= v
    ensures RoundUp(u, a) <= RoundUp(v, a)
  {
    RoundUpProps(v, a);
    RoundUpLeast(u, a, RoundUp(v, a));
  }

  // ==========================================================================
  // Set extrema (ghost): the definite descriptions the leaf evaluators read off.
  // ==========================================================================

  lemma MinExists(s: set<int>)
    requires s != {}
    ensures exists m :: m in s && forall v <- s :: m <= v
    decreases s
  {
    var x :| x in s;
    var rest := s - {x};
    if rest == {} {
      forall v <- s ensures v == x { assert v !in rest; }
      assert x in s && forall v <- s :: x <= v;
    } else {
      MinExists(rest);
      var m :| m in rest && forall v <- rest :: m <= v;
      var lo := if x <= m then x else m;
      forall v <- s ensures lo <= v {
        if v != x { assert v in rest; }
      }
      assert lo in s;
    }
  }

  lemma MaxExists(s: set<int>)
    requires s != {}
    ensures exists m :: m in s && forall v <- s :: v <= m
    decreases s
  {
    var x :| x in s;
    var rest := s - {x};
    if rest == {} {
      forall v <- s ensures v == x { assert v !in rest; }
      assert x in s && forall v <- s :: v <= x;
    } else {
      MaxExists(rest);
      var m :| m in rest && forall v <- rest :: v <= m;
      var hi := if x >= m then x else m;
      forall v <- s ensures v <= hi {
        if v != x { assert v in rest; }
      }
      assert hi in s;
    }
  }

  ghost function SetMin(s: set<int>): int
    requires s != {}
    ensures SetMin(s) in s && forall v <- s :: SetMin(s) <= v
  {
    assert exists m :: m in s && forall v <- s :: m <= v by { MinExists(s); }
    var m :| m in s && forall v <- s :: m <= v;
    m
  }

  ghost function SetMax(s: set<int>): int
    requires s != {}
    ensures SetMax(s) in s && forall v <- s :: v <= SetMax(s)
  {
    assert exists m :: m in s && forall v <- s :: v <= m by { MaxExists(s); }
    var m :| m in s && forall v <- s :: v <= m;
    m
  }

  // ==========================================================================
  // Structural support: I1 (non-emptiness) and the value-domain invariant.
  // ==========================================================================

  lemma RepeatNonEmpty(s: set<int>, k: nat)
    requires s != {}
    ensures RepeatSem(s, k) != {}
  {
    if k > 0 {
      RepeatNonEmpty(s, k - 1);
      var x :| x in RepeatSem(s, k - 1);
      var y :| y in s;
      assert x + y in RepeatSem(s, k);
    } else {
      assert 0 in RepeatSem(s, 0);
    }
  }

  lemma RepeatRangeHasZero(s: set<int>, kMax: nat)
    ensures 0 in RepeatRangeSem(s, kMax)
  {
    if kMax > 0 { RepeatRangeHasZero(s, kMax - 1); }
  }

  lemma SemNonEmpty(e: Expr)
    requires WF(e)
    ensures Sem(e) != {}
  {
    match e
    case Leaf(vs) =>
    case Add(l, r) =>
      SemNonEmpty(l);
      SemNonEmpty(r);
      var x :| x in Sem(l);
      var y :| y in Sem(r);
      assert x + y in Sem(e);
    case Union(l, r) =>
      SemNonEmpty(l);
      var x :| x in Sem(l);
      assert x in Sem(e);
    case Pad(inner, a) =>
      SemNonEmpty(inner);
      var u :| u in Sem(inner);
      assert RoundUp(u, a) in Sem(e);
    case Repeat(inner, k) =>
      SemNonEmpty(inner);
      RepeatNonEmpty(Sem(inner), k);
    case RepeatRange(inner, kMax) =>
      RepeatRangeHasZero(Sem(inner), kMax);
  }

  lemma RepeatNonNeg(s: set<int>, k: nat)
    requires forall v <- s :: 0 <= v
    ensures forall v <- RepeatSem(s, k) :: 0 <= v
  {
    if k > 0 {
      RepeatNonNeg(s, k - 1);
      forall v <- RepeatSem(s, k) ensures 0 <= v {
        var x, y :| x in RepeatSem(s, k - 1) && y in s && v == x + y;
      }
    }
  }

  lemma RepeatRangeNonNeg(s: set<int>, kMax: nat)
    requires forall v <- s :: 0 <= v
    ensures forall v <- RepeatRangeSem(s, kMax) :: 0 <= v
  {
    if kMax > 0 {
      RepeatRangeNonNeg(s, kMax - 1);
      RepeatNonNeg(s, kMax);
    }
  }

  lemma SemNonNeg(e: Expr)
    requires WF(e)
    ensures forall v <- Sem(e) :: 0 <= v
  {
    match e
    case Leaf(vs) =>
    case Add(l, r) =>
      SemNonNeg(l);
      SemNonNeg(r);
      forall v <- Sem(e) ensures 0 <= v {
        var x, y :| x in Sem(l) && y in Sem(r) && v == x + y;
      }
    case Union(l, r) =>
      SemNonNeg(l);
      SemNonNeg(r);
    case Pad(inner, a) =>
      SemNonNeg(inner);
      forall v <- Sem(e) ensures 0 <= v {
        var u :| u in Sem(inner) && v == RoundUp(u, a);
        RoundUpProps(u, a);
      }
    case Repeat(inner, k) =>
      SemNonNeg(inner);
      RepeatNonNeg(Sem(inner), k);
    case RepeatRange(inner, kMax) =>
      SemNonNeg(inner);
      RepeatRangeNonNeg(Sem(inner), kMax);
  }

  // ==========================================================================
  // Repeat / RepeatRange extrema — the closed forms the C++ evaluators use.
  // ==========================================================================

  lemma RepeatLower(s: set<int>, k: nat, lo: int)
    requires s != {} && lo in s && forall v <- s :: lo <= v
    ensures k * lo in RepeatSem(s, k)
    ensures forall v <- RepeatSem(s, k) :: k * lo <= v
  {
    if k == 0 {
      assert k * lo == 0;
    } else {
      RepeatLower(s, k - 1, lo);
      assert (k - 1) * lo + lo == k * lo;
      assert k * lo in RepeatSem(s, k);
      forall v <- RepeatSem(s, k) ensures k * lo <= v {
        var x, y :| x in RepeatSem(s, k - 1) && y in s && v == x + y;
      }
    }
  }

  lemma RepeatUpper(s: set<int>, k: nat, hi: int)
    requires s != {} && hi in s && forall v <- s :: v <= hi
    ensures k * hi in RepeatSem(s, k)
    ensures forall v <- RepeatSem(s, k) :: v <= k * hi
  {
    if k == 0 {
      assert k * hi == 0;
    } else {
      RepeatUpper(s, k - 1, hi);
      assert (k - 1) * hi + hi == k * hi;
      assert k * hi in RepeatSem(s, k);
      forall v <- RepeatSem(s, k) ensures v <= k * hi {
        var x, y :| x in RepeatSem(s, k - 1) && y in s && v == x + y;
      }
    }
  }

  // RepeatRange's maximum is the k = kMax term — TRUE ONLY on the non-negative domain
  // (`0 <= hi`): with a negative maximum the k = 0 term {0} would exceed kMax * hi. This is
  // the value-domain dependency the C++ header documents ("on the non-negative value domain")
  // and defect BLS-D5/D15 violated.
  lemma RepeatRangeUpper(s: set<int>, kMax: nat, hi: int)
    requires s != {} && hi in s && 0 <= hi && forall v <- s :: v <= hi
    ensures kMax * hi in RepeatRangeSem(s, kMax)
    ensures forall v <- RepeatRangeSem(s, kMax) :: v <= kMax * hi
  {
    if kMax == 0 {
      assert kMax * hi == 0;
    } else {
      RepeatRangeUpper(s, kMax - 1, hi);
      RepeatUpper(s, kMax, hi);
      assert kMax * hi - (kMax - 1) * hi == hi;
      forall v <- RepeatRangeSem(s, kMax) ensures v <= kMax * hi {
        if v in RepeatRangeSem(s, kMax - 1) {
          assert v <= (kMax - 1) * hi;
        } else {
          assert v in RepeatSem(s, kMax);
        }
      }
    }
  }

  // ==========================================================================
  // The interval evaluators — transcriptions of the C++ min()/max() derivations
  // (BitLengthSet.cpp, Node::min/Node::max switch statements) — and their
  // exactness theorems. TOTAL: no refusal path, exact at any cardinality.
  // ==========================================================================

  ghost function MinEval(e: Expr): int
    requires WF(e)
  {
    match e
    case Leaf(vs) => SetMin(vs)
    case Add(l, r) => MinEval(l) + MinEval(r)
    case Union(l, r) => if MinEval(l) <= MinEval(r) then MinEval(l) else MinEval(r)
    case Pad(inner, a) => RoundUp(MinEval(inner), a)
    case Repeat(inner, k) => k * MinEval(inner)
    case RepeatRange(_, _) => 0
  }

  ghost function MaxEval(e: Expr): int
    requires WF(e)
  {
    match e
    case Leaf(vs) => SetMax(vs)
    case Add(l, r) => MaxEval(l) + MaxEval(r)
    case Union(l, r) => if MaxEval(l) >= MaxEval(r) then MaxEval(l) else MaxEval(r)
    case Pad(inner, a) => RoundUp(MaxEval(inner), a)
    case Repeat(inner, k) => k * MaxEval(inner)
    case RepeatRange(inner, kMax) => kMax * MaxEval(inner)
  }

  lemma MinExact(e: Expr)
    requires WF(e)
    ensures MinEval(e) in Sem(e)
    ensures forall v <- Sem(e) :: MinEval(e) <= v
  {
    match e
    case Leaf(vs) =>
    case Add(l, r) =>
      MinExact(l);
      MinExact(r);
      assert MinEval(l) + MinEval(r) in Sem(e);
      forall v <- Sem(e) ensures MinEval(e) <= v {
        var x, y :| x in Sem(l) && y in Sem(r) && v == x + y;
      }
    case Union(l, r) =>
      MinExact(l);
      MinExact(r);
    case Pad(inner, a) =>
      MinExact(inner);
      assert RoundUp(MinEval(inner), a) in Sem(e);
      forall v <- Sem(e) ensures MinEval(e) <= v {
        var u :| u in Sem(inner) && v == RoundUp(u, a);
        RoundUpMono(MinEval(inner), u, a);
      }
    case Repeat(inner, k) =>
      MinExact(inner);
      SemNonEmpty(inner);
      RepeatLower(Sem(inner), k, MinEval(inner));
    case RepeatRange(inner, kMax) =>
      SemNonEmpty(inner);
      SemNonNeg(inner);
      RepeatRangeHasZero(Sem(inner), kMax);
      RepeatRangeNonNeg(Sem(inner), kMax);
  }

  lemma MaxExact(e: Expr)
    requires WF(e)
    ensures MaxEval(e) in Sem(e)
    ensures forall v <- Sem(e) :: v <= MaxEval(e)
  {
    match e
    case Leaf(vs) =>
    case Add(l, r) =>
      MaxExact(l);
      MaxExact(r);
      assert MaxEval(l) + MaxEval(r) in Sem(e);
      forall v <- Sem(e) ensures v <= MaxEval(e) {
        var x, y :| x in Sem(l) && y in Sem(r) && v == x + y;
      }
    case Union(l, r) =>
      MaxExact(l);
      MaxExact(r);
    case Pad(inner, a) =>
      MaxExact(inner);
      assert RoundUp(MaxEval(inner), a) in Sem(e);
      forall v <- Sem(e) ensures v <= MaxEval(e) {
        var u :| u in Sem(inner) && v == RoundUp(u, a);
        RoundUpMono(u, MaxEval(inner), a);
      }
    case Repeat(inner, k) =>
      MaxExact(inner);
      SemNonEmpty(inner);
      RepeatUpper(Sem(inner), k, MaxEval(inner));
    case RepeatRange(inner, kMax) =>
      MaxExact(inner);
      SemNonEmpty(inner);
      SemNonNeg(inner);
      // The non-negative domain is what licenses "the maximum is the k = kMax term".
      assert 0 <= MaxEval(inner);
      RepeatRangeUpper(Sem(inner), kMax, MaxEval(inner));
  }

  // ==========================================================================
  // Algebraic laws — the C++ header's "Algebraic laws" section as theorems over Sem ALONE.
  //
  // These are Sem-shape cross-checks (hardening rung 1): they constrain the denotation
  // independently of any evaluator, so a mis-transcribed Sem — one that no longer denotes
  // Minkowski sums, genuine set union, or least-multiple padding — fails here even if some
  // evaluator were mis-transcribed to match. RepeatSplit is also the load-bearing lemma
  // behind the C++ RunSet repeat strategies: k-fold sums compose additively, which licenses
  // both linear iteration and doubling.
  // ==========================================================================

  lemma MSumComm(a: set<int>, b: set<int>)
    ensures MSum(a, b) == MSum(b, a)
  {
    forall v | v in MSum(a, b) ensures v in MSum(b, a) {
      var x, y :| x in a && y in b && v == x + y;
      assert v == y + x;
    }
    forall v | v in MSum(b, a) ensures v in MSum(a, b) {
      var x, y :| x in b && y in a && v == x + y;
      assert v == y + x;
    }
  }

  lemma MSumAssoc(a: set<int>, b: set<int>, c: set<int>)
    ensures MSum(MSum(a, b), c) == MSum(a, MSum(b, c))
  {
    forall v | v in MSum(MSum(a, b), c) ensures v in MSum(a, MSum(b, c)) {
      var xy, z :| xy in MSum(a, b) && z in c && v == xy + z;
      var x, y :| x in a && y in b && xy == x + y;
      assert y + z in MSum(b, c);
      assert v == x + (y + z);
    }
    forall v | v in MSum(a, MSum(b, c)) ensures v in MSum(MSum(a, b), c) {
      var x, yz :| x in a && yz in MSum(b, c) && v == x + yz;
      var y, z :| y in b && z in c && yz == y + z;
      assert x + y in MSum(a, b);
      assert v == (x + y) + z;
    }
  }

  lemma MSumIdentity(a: set<int>)
    ensures MSum(a, {0}) == a && MSum({0}, a) == a
  {
    forall v | v in MSum(a, {0}) ensures v in a {
      var x, y :| x in a && y in {0} && v == x + y;
    }
    forall v | v in a ensures v in MSum(a, {0}) {
      assert 0 in {0} && v == v + 0;
    }
    MSumComm(a, {0});
  }

  lemma MSumDistribUnion(a: set<int>, b: set<int>, c: set<int>)
    ensures MSum(a, b + c) == MSum(a, b) + MSum(a, c)
  {
    forall v | v in MSum(a, b + c) ensures v in MSum(a, b) + MSum(a, c) {
      var x, y :| x in a && y in b + c && v == x + y;
    }
    forall v | v in MSum(a, b) + MSum(a, c) ensures v in MSum(a, b + c) {
      if v in MSum(a, b) {
        var x, y :| x in a && y in b && v == x + y;
        assert y in b + c;
      } else {
        var x, y :| x in a && y in c && v == x + y;
        assert y in b + c;
      }
    }
  }

  // k-fold sums compose additively: repeat(i + j) == repeat(i) + repeat(j). This is the
  // additivity that justifies evaluating huge repeat counts incrementally (or by doubling)
  // instead of by definition — the correctness backbone of RunSet::repeated().
  lemma RepeatSplit(s: set<int>, i: nat, j: nat)
    ensures RepeatSem(s, i + j) == MSum(RepeatSem(s, i), RepeatSem(s, j))
    decreases j
  {
    if j == 0 {
      MSumIdentity(RepeatSem(s, i));
    } else {
      calc {
        RepeatSem(s, i + j);
      ==
        MSum(RepeatSem(s, i + j - 1), s);
      ==  { RepeatSplit(s, i, j - 1); }
        MSum(MSum(RepeatSem(s, i), RepeatSem(s, j - 1)), s);
      ==  { MSumAssoc(RepeatSem(s, i), RepeatSem(s, j - 1), s); }
        MSum(RepeatSem(s, i), MSum(RepeatSem(s, j - 1), s));
      ==
        MSum(RepeatSem(s, i), RepeatSem(s, j));
      }
    }
  }

  lemma RepeatOne(s: set<int>)
    ensures RepeatSem(s, 1) == s
  {
    MSumIdentity(s);
  }

  // Every padded element is a multiple of the alignment, padding an already-padded set is the
  // identity, and alignment 1 is the identity map — the header's padToAlignment @post trio.
  lemma PadSetMultiples(s: set<int>, a: int)
    requires 1 <= a
    ensures forall v <- PadSet(s, a) :: v % a == 0
  {
    forall v | v in PadSet(s, a) ensures v % a == 0 {
      var u :| u in s && v == RoundUp(u, a);
      RoundUpProps(u, a);
    }
  }

  lemma PadSetIdempotent(s: set<int>, a: int)
    requires 1 <= a
    ensures PadSet(PadSet(s, a), a) == PadSet(s, a)
  {
    PadSetMultiples(s, a);
    forall v | v in PadSet(PadSet(s, a), a) ensures v in PadSet(s, a) {
      var u :| u in PadSet(s, a) && v == RoundUp(u, a);
      assert v == u;  // u is a multiple of a, so RoundUp is the identity on it
    }
    forall v | v in PadSet(s, a) ensures v in PadSet(PadSet(s, a), a) {
      assert RoundUp(v, a) == v;
    }
  }

  // Padding rounds UP: every padded value descends from an input at most `a - 1` below it.
  // This is the law that pins the direction — idempotence and the multiples property are both
  // satisfied by a round-DOWN mutant, so without this a consistent mis-transcription of Sem
  // and evaluator together could verify. (The shared RoundUp definition makes such a
  // co-mutation a single edit; this lemma and RoundUpProps both break on it.)
  lemma PadSetSound(s: set<int>, a: int)
    requires 1 <= a
    ensures forall v <- PadSet(s, a) :: exists u :: u in s && u <= v < u + a
  {
    forall v | v in PadSet(s, a) ensures exists u :: u in s && u <= v < u + a {
      var u :| u in s && v == RoundUp(u, a);
      RoundUpProps(u, a);
    }
  }

  lemma PadSetIdentityAtOne(s: set<int>)
    ensures PadSet(s, 1) == s
  {
    forall v | v in PadSet(s, 1) ensures v in s {
      var u :| u in s && v == RoundUp(u, 1);
    }
    forall v | v in s ensures v in PadSet(s, 1) {
      assert RoundUp(v, 1) == v;
    }
  }

  // The recursive RepeatRangeSem equals the header's phrasing: "union of repeat(j) for j in
  // [0, kMax]" — pinning the two formulations to each other.
  lemma RepeatRangeCharacterization(s: set<int>, kMax: nat, v: int)
    ensures v in RepeatRangeSem(s, kMax) <==> exists j: nat :: j <= kMax && v in RepeatSem(s, j)
    decreases kMax
  {
    if kMax == 0 {
      if v in RepeatRangeSem(s, 0) {
        assert 0 <= kMax && v in RepeatSem(s, 0);  // witness j := 0
      }
    } else {
      RepeatRangeCharacterization(s, kMax - 1, v);
      if v in RepeatRangeSem(s, kMax) && v in RepeatSem(s, kMax) {
        assert kMax <= kMax && v in RepeatSem(s, kMax);  // witness j := kMax
      }
      if exists j: nat :: j <= kMax && v in RepeatSem(s, j) {
        var j: nat :| j <= kMax && v in RepeatSem(s, j);
        if j < kMax {
          assert j <= kMax - 1 && v in RepeatSem(s, j);  // re-witness for the IH bound
        }
      }
    }
  }

  // ==========================================================================
  // The header's laws verbatim, at the expression level — corollaries of the operator
  // lemmas, stated in the same shape a reader of BitLengthSet.h expects.
  // ==========================================================================

  lemma LawAddCommutes(a: Expr, b: Expr)
    requires WF(a) && WF(b)
    ensures Sem(Add(a, b)) == Sem(Add(b, a))
  {
    MSumComm(Sem(a), Sem(b));
  }

  lemma LawAddAssociates(a: Expr, b: Expr, c: Expr)
    requires WF(a) && WF(b) && WF(c)
    ensures Sem(Add(Add(a, b), c)) == Sem(Add(a, Add(b, c)))
  {
    MSumAssoc(Sem(a), Sem(b), Sem(c));
  }

  lemma LawAddIdentity(e: Expr)
    requires WF(e)
    ensures Sem(Add(e, Leaf({0}))) == Sem(e)
  {
    MSumIdentity(Sem(e));
  }

  lemma LawUnionIdempotent(e: Expr)
    requires WF(e)
    ensures Sem(Union(e, e)) == Sem(e)
  {
  }

  lemma LawAddDistributesOverUnion(a: Expr, b: Expr, c: Expr)
    requires WF(a) && WF(b) && WF(c)
    ensures WF(Add(a, Union(b, c))) && WF(Union(Add(a, b), Add(a, c)))
    ensures Sem(Add(a, Union(b, c))) == Sem(Union(Add(a, b), Add(a, c)))
  {
    MSumDistribUnion(Sem(a), Sem(b), Sem(c));
  }

  lemma LawRepeatZero(e: Expr)
    requires WF(e)
    ensures Sem(Repeat(e, 0)) == {0}
  {
  }

  lemma LawRepeatOne(e: Expr)
    requires WF(e)
    ensures Sem(Repeat(e, 1)) == Sem(e)
  {
    RepeatOne(Sem(e));
  }

  lemma LawRepeatAdditive(e: Expr, i: nat, j: nat)
    requires WF(e)
    ensures WF(Add(Repeat(e, i), Repeat(e, j)))
    ensures Sem(Repeat(e, i + j)) == Sem(Add(Repeat(e, i), Repeat(e, j)))
  {
    RepeatSplit(Sem(e), i, j);
  }

  lemma LawPadIdempotent(e: Expr, a: int)
    requires WF(e) && 1 <= a
    ensures Sem(Pad(Pad(e, a), a)) == Sem(Pad(e, a))
  {
    PadSetIdempotent(Sem(e), a);
  }

  lemma LawPadIdentityAtOne(e: Expr)
    requires WF(e)
    ensures Sem(Pad(e, 1)) == Sem(e)
  {
    PadSetIdentityAtOne(Sem(e));
  }

  lemma LawRepeatRangeZero(e: Expr)
    requires WF(e)
    ensures Sem(RepeatRange(e, 0)) == {0}
  {
  }

  lemma LawRepeatRangeContainsZero(e: Expr, kMax: nat)
    requires WF(e)
    ensures 0 in Sem(RepeatRange(e, kMax))
  {
    RepeatRangeHasZero(Sem(e), kMax);
  }

  // ==========================================================================
  // fixed() decides |S| == 1 exactly (the C++ computes it as min() == max()).
  // ==========================================================================

  lemma SubsetCard(a: set<int>, b: set<int>)
    requires a <= b
    ensures |a| <= |b|
    decreases a
  {
    if a != {} {
      var x :| x in a;
      SubsetCard(a - {x}, b - {x});
    }
  }

  lemma SingletonElems(s: set<int>, x: int, y: int)
    requires |s| == 1 && x in s && y in s
    ensures x == y
  {
    if x != y {
      assert {x, y} <= s;
      SubsetCard({x, y}, s);
    }
  }

  lemma FixedCharacterization(e: Expr)
    requires WF(e)
    ensures (MinEval(e) == MaxEval(e)) <==> (|Sem(e)| == 1)
  {
    MinExact(e);
    MaxExact(e);
    if MinEval(e) == MaxEval(e) {
      forall v <- Sem(e) ensures v == MinEval(e) { }
      assert Sem(e) == {MinEval(e)};
    }
    if |Sem(e)| == 1 {
      SingletonElems(Sem(e), MinEval(e), MaxEval(e));
    }
  }
}
