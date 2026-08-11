// SPDX-License-Identifier: MIT
//
// Formal model of the BitLengthSet expression algebra and its evaluators.
//
// SCOPE (stage 1 of the staged plan): the expression DAG's denotational semantics and the
// interval evaluators. `Sem` is the normative meaning of a BitLengthSet expression — the
// direct transcription of the "Denotationally, ..." table in
// include/llvmdsdl/Semantics/BitLengthSet.h — and every evaluator theorem is stated against
// it. Later stages add the RunSet kernel (CRT run intersection, the dense-fixpoint jump in
// repeat, the repeat-range phase families) and the residue evaluator.
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
//
// WHY WF MATTERS (the model has teeth): RepeatRange's minimum is 0 and its maximum is
// kMax * max(inner) ONLY on the non-negative domain. Deleting the `0 <= v` conjunct from WF
// breaks MinExact and MaxExact at the RepeatRange case — which is precisely defect BLS-D5/D15
// (negative values made min()/fixed() unsound) resurfacing as a failed proof instead of a
// field bug. See NEGATIVE CONTROLS below.
//
// ASSURANCE SCOPE: this proves the abstract algorithm, not the emitted C++. The link to
// lib/Semantics/BitLengthSet.cpp is transcription discipline plus the differential batteries
// (test/unit/BitLengthSetTests.cpp reference model, the RunSet fuzz harness); the staged plan
// ends with a verified executable oracle compiled from this model.
//
// NEGATIVE CONTROLS (each mutation must break the named proof; checked manually when the
// model changes):
//   - drop `0 <= v` from WF(Leaf)            -> MinExact/MaxExact fail at RepeatRange
//   - change MinEval(RepeatRange) to min(...) -> MinExact fails (0 is the k = 0 term)
//   - change MaxEval(Union) to `min`          -> MaxExact fails
//   - drop `vs != {}` from WF(Leaf)           -> SemNonEmpty fails; SetMin precondition fails
//   - weaken RoundUp to round DOWN            -> MinExact fails at Pad (result not in S)
//
// Verified with: dafny verify spec/dafny/BitLengthSet.dfy   (Dafny 4.11, CI-enforced)

module BitLengthSetModel {

  // ==========================================================================
  // Expression DAG — mirrors BitLengthSet::Node::Kind.
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

  // ==========================================================================
  // Arithmetic support: multiplication positivity and Euclidean div/mod facts.
  // Small, reusable kit — stage 5's residue evaluator leans on the same lemmas.
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
