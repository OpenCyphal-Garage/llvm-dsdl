//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Regression tests for the BitLengthSet SPECIFICATION (see BitLengthSet.h).
///
/// The tests target the documented contract — the denotational semantics, invariants I1..I4, the
/// algebraic laws, and the exactness model — rather than any implementation detail. Expected
/// values come from an independent reference model (`refAdd`, `refPad`, `refRepeat`, ...) that
/// transcribes the specification directly, so a regression in either the class or the reference
/// shows up as a mismatch. Coverage also includes the robustness properties that are easy to get
/// wrong: value-domain clamping and saturation, exactness signalling, bounded expansion of huge
/// repeat counts, iterative/memoized evaluation of deep and heavily-shared graphs, and moved-from
/// usability.
///
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "llvmdsdl/Semantics/BitLengthSet.h"

namespace
{

using llvmdsdl::BitLengthSet;
using ValueSet = std::set<std::int64_t>;

//===----------------------------------------------------------------------===//
// Reference model: direct transcription of the specification's denotational semantics.
//===----------------------------------------------------------------------===//

ValueSet refAdd(const ValueSet& a, const ValueSet& b)
{
    ValueSet out;
    for (const auto x : a)
    {
        for (const auto y : b)
        {
            out.insert(x + y);
        }
    }
    return out;
}

ValueSet refPad(const ValueSet& a, std::int64_t alignment)
{
    const auto al = std::max<std::int64_t>(1, alignment);  // spec: alignment < 1 clamps to 1
    ValueSet   out;
    for (const auto v : a)
    {
        const auto rem = v % al;  // non-negative domain only
        out.insert(rem == 0 ? v : v + (al - rem));
    }
    return out;
}

ValueSet refRepeat(const ValueSet& a, std::int64_t count)
{
    ValueSet acc{0};  // spec: count <= 0 denotes {0}
    for (std::int64_t i = 0; i < count; ++i)
    {
        acc = refAdd(acc, a);
    }
    return acc;
}

ValueSet refRepeatRange(const ValueSet& a, std::int64_t countMax)
{
    ValueSet out;
    for (std::int64_t k = 0; k <= std::max<std::int64_t>(0, countMax); ++k)
    {
        const auto r = refRepeat(a, k);
        out.insert(r.begin(), r.end());
    }
    return out;
}

ValueSet refModulo(const ValueSet& a, std::int64_t divisor)
{
    if (divisor <= 0)  // spec: sentinel
    {
        return {0};
    }
    ValueSet out;
    for (const auto v : a)
    {
        out.insert(v % divisor);  // non-negative domain only
    }
    return out;
}

// Generic over container type: the class returns std::flat_set from expand()/modulo() while the
// reference model and constructor inputs use std::set, so the harness compares any two sorted
// ranges of int64.
template <typename Range>
bool isSubset(const Range& sub, const ValueSet& super)
{
    for (const auto v : sub)
    {
        if (super.count(v) == 0)
        {
            return false;
        }
    }
    return true;
}

template <typename Range>
std::string setToString(const Range& s)
{
    std::ostringstream out;
    out << '{';
    bool first = true;
    for (const auto v : s)
    {
        if (!first)
        {
            out << ',';
        }
        out << v;
        first = false;
    }
    out << '}';
    return out.str();
}

//===----------------------------------------------------------------------===//
// Minimal harness.
//===----------------------------------------------------------------------===//

struct TestContext final
{
    int failures = 0;

    void expect(const bool condition, const std::string& what)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "BitLengthSet spec FAIL: " << what << "\n";
        }
    }

    // Compare any two sorted int64 ranges (std::set, std::flat_set, or an initializer_list of
    // expected values). Two overloads so braced `{...}` literals resolve to the initializer_list
    // form while containers use the generic one.
    template <typename Actual, typename Expected>
    void expectSetEq(const Actual& actual, const Expected& expected, const std::string& what)
    {
        expectSetEqImpl(actual, expected, what);
    }

    template <typename Actual>
    void expectSetEq(const Actual& actual, std::initializer_list<std::int64_t> expected, const std::string& what)
    {
        expectSetEqImpl(actual, expected, what);
    }

    // Optional-aware overloads for exact-or-refuse surfaces (modulo()): a refusal is a test
    // failure with its own message — never silently treated as an empty set.
    template <typename Actual, typename Expected>
    void expectSetEq(const std::optional<Actual>& actual, const Expected& expected, const std::string& what)
    {
        if (!actual)
        {
            ++failures;
            std::cerr << "BitLengthSet spec FAIL: " << what << " (exact evaluation refused)\n";
            return;
        }
        expectSetEqImpl(*actual, expected, what);
    }

    template <typename Actual>
    void expectSetEq(const std::optional<Actual>&         actual,
                     std::initializer_list<std::int64_t>  expected,
                     const std::string&                   what)
    {
        if (!actual)
        {
            ++failures;
            std::cerr << "BitLengthSet spec FAIL: " << what << " (exact evaluation refused)\n";
            return;
        }
        expectSetEqImpl(*actual, expected, what);
    }

    template <typename Actual, typename Expected>
    void expectSetEqImpl(const Actual& actual, const Expected& expected, const std::string& what)
    {
        if (!std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()))
        {
            ++failures;
            std::cerr << "BitLengthSet spec FAIL: " << what << "\n  expected " << setToString(expected)
                      << "\n  actual   " << setToString(actual) << "\n";
        }
    }
};

//===----------------------------------------------------------------------===//
// Test sections. Each cites the header spec clause it verifies.
//===----------------------------------------------------------------------===//

// Spec: constructors' denotations; invariant I1 (non-empty, {0} coercion); fixed().
void testConstructionAndInvariants(TestContext& t)
{
    const BitLengthSet dflt;
    t.expect(dflt.min() == 0 && dflt.max() == 0 && dflt.fixed(), "default ctor denotes {0}");
    t.expectSetEq(dflt.expand(), {0}, "default ctor expands to {0}");

    const BitLengthSet single(8);
    t.expect(single.min() == 8 && single.max() == 8 && single.fixed(), "singleton ctor bounds");
    t.expectSetEq(single.expand(), {8}, "singleton ctor expands to {8}");

    const BitLengthSet multi(ValueSet{8, 16, 24});
    t.expect(multi.min() == 8 && multi.max() == 24 && !multi.fixed(), "explicit set bounds");
    t.expectSetEq(multi.expand(), {8, 16, 24}, "explicit set expands exactly");

    const BitLengthSet coerced((ValueSet{}));
    t.expect(coerced.min() == 0 && coerced.max() == 0 && coerced.fixed(), "empty set coerces to {0} (I1)");
    t.expectSetEq(coerced.expand(), {0}, "empty set expands to {0} (I1)");
}

// Spec: exactness of min()/max()/fixed() and their consistency with exact expansion (I2, I4).
void testBoundsExactness(TestContext& t)
{
    const std::vector<BitLengthSet> battery = {
        BitLengthSet(),
        BitLengthSet(8),
        BitLengthSet(ValueSet{0, 1}),
        BitLengthSet(ValueSet{1, 3}),
        BitLengthSet(ValueSet{0, 1, 7, 8, 9}),
        BitLengthSet(8).repeat(3),
        BitLengthSet(ValueSet{1, 3}).repeatRange(3),
        (BitLengthSet(32) + BitLengthSet(8).repeatRange(3)).padToAlignment(8),
        (BitLengthSet(8) | BitLengthSet(ValueSet{16, 24})) + BitLengthSet(8),
    };
    for (std::size_t i = 0; i < battery.size(); ++i)
    {
        const auto& s        = battery[i];
        const auto  expanded = s.expand();
        const auto  tag      = " (battery #" + std::to_string(i) + ")";
        t.expect(!expanded.empty(), "expansion is never empty (I1)" + tag);
        t.expect(s.min() == *expanded.begin(), "min() equals smallest expanded value" + tag);
        t.expect(s.max() == *expanded.rbegin(), "max() equals largest expanded value" + tag);
        t.expect(s.min() <= s.max(), "min() <= max() (I2)" + tag);
        t.expect(s.fixed() == (expanded.size() == 1), "fixed() iff singleton" + tag);
    }
}

// Spec: operator+ denotes the Minkowski sum; {0} is its identity; commutative/associative.
void testAdditionSemantics(TestContext& t)
{
    const BitLengthSet a(ValueSet{1, 2});
    const BitLengthSet b(ValueSet{10, 20});
    t.expectSetEq((a + b).expand(), {11, 12, 21, 22}, "operator+ denotes Minkowski sum");

    const std::vector<ValueSet> sets = {{0}, {8}, {0, 1}, {1, 3}, {0, 1, 7, 8, 9}};
    for (const auto& x : sets)
    {
        const BitLengthSet sx(x);
        t.expectSetEq((sx + BitLengthSet(0)).expand(), x, "{0} is the identity of + for " + setToString(x));
        t.expectSetEq((BitLengthSet() + sx).expand(), x, "default ctor is the identity of + for " + setToString(x));
        for (const auto& y : sets)
        {
            const BitLengthSet sy(y);
            t.expectSetEq((sx + sy).expand(),
                          refAdd(x, y),
                          "sum vs reference " + setToString(x) + "+" + setToString(y));
            t.expectSetEq((sx + sy).expand(),
                          (sy + sx).expand(),
                          "+ commutes for " + setToString(x) + "," + setToString(y));
        }
    }

    const BitLengthSet c(ValueSet{0, 5});
    t.expectSetEq(((a + b) + c).expand(), (a + (b + c)).expand(), "+ associates");
}

// Spec: operator| denotes set union; commutative, associative, idempotent; + distributes over |.
void testUnionSemantics(TestContext& t)
{
    const BitLengthSet a(ValueSet{1, 2});
    const BitLengthSet b(ValueSet{2, 3});
    const BitLengthSet c(ValueSet{8});

    t.expectSetEq((a | b).expand(), {1, 2, 3}, "operator| denotes set union");
    t.expectSetEq((a | a).expand(), a.expand(), "| is idempotent");
    t.expectSetEq((a | b).expand(), (b | a).expand(), "| commutes");
    t.expectSetEq(((a | b) | c).expand(), (a | (b | c)).expand(), "| associates");
    t.expectSetEq((a + (b | c)).expand(), ((a + b) | (a + c)).expand(), "+ distributes over |");
}

// Spec: padToAlignment denotation, clamp, idempotence, alignment postcondition, exact bounds.
void testPadToAlignment(TestContext& t)
{
    const ValueSet     raw{0, 1, 7, 8, 9};
    const BitLengthSet s(raw);

    t.expectSetEq(s.padToAlignment(8).expand(), {0, 8, 16}, "pad rounds each value up to the multiple");
    t.expectSetEq(s.padToAlignment(8).expand(), refPad(raw, 8), "pad vs reference model");
    t.expectSetEq(BitLengthSet(ValueSet{0, 8, 16}).padToAlignment(8).expand(),
                  {0, 8, 16},
                  "pad is identity on aligned sets");
    t.expectSetEq(s.padToAlignment(1).expand(), raw, "pad to 1 is the identity");
    t.expectSetEq(s.padToAlignment(0).expand(), raw, "alignment < 1 clamps to 1 (identity)");
    t.expectSetEq(s.padToAlignment(-4).expand(), raw, "negative alignment clamps to 1 (identity)");
    t.expectSetEq(s.padToAlignment(8).padToAlignment(8).expand(), s.padToAlignment(8).expand(), "pad is idempotent");

    const auto padded = s.padToAlignment(8);
    for (const auto v : padded.expand())
    {
        t.expect(v % 8 == 0, "every padded element is a multiple of the alignment");
    }
    t.expect(padded.min() == 0 && padded.max() == 16, "pad bounds are the padded bounds (monotone)");
}

// Spec: repeat denotes the k-fold Minkowski self-sum; clamps; identities; scaled bounds.
void testRepeat(TestContext& t)
{
    const ValueSet     raw{1, 3};
    const BitLengthSet s(raw);

    t.expectSetEq(s.repeat(0).expand(), {0}, "repeat(0) denotes {0}");
    t.expectSetEq(s.repeat(-2).expand(), {0}, "negative count clamps to 0");
    t.expectSetEq(s.repeat(1).expand(), raw, "repeat(1) denotes the same set");
    t.expectSetEq(s.repeat(2).expand(), {2, 4, 6}, "repeat(2) on {1,3}");
    t.expectSetEq(s.repeat(3).expand(), refRepeat(raw, 3), "repeat(3) vs reference model");
    t.expectSetEq(s.repeat(3).expand(), (s + s + s).expand(), "repeat(3) equals threefold +");
    t.expect(s.repeat(3).min() == 3 * s.min() && s.repeat(3).max() == 3 * s.max(),
             "repeat scales min/max by the count");

    const BitLengthSet fixed8(8);
    t.expect(fixed8.repeat(3).fixed() && fixed8.repeat(3).min() == 24, "repeat of a singleton stays fixed");
}

// Spec: repeatRange denotes the union of repeat(0..k); always contains 0; bounds.
void testRepeatRange(TestContext& t)
{
    const ValueSet     raw{1, 3};
    const BitLengthSet s(raw);

    t.expectSetEq(s.repeatRange(0).expand(), {0}, "repeatRange(0) denotes {0}");
    t.expectSetEq(s.repeatRange(-1).expand(), {0}, "negative countMax clamps to 0");
    t.expectSetEq(s.repeatRange(3).expand(), refRepeatRange(raw, 3), "repeatRange(3) vs reference model");
    t.expect(s.repeatRange(3).expand().count(0) == 1, "repeatRange always contains 0 (k = 0 term)");
    t.expect(s.repeatRange(3).min() == 0, "repeatRange min is 0");
    t.expect(s.repeatRange(3).max() == 3 * s.max(), "repeatRange max is countMax * max");

    const BitLengthSet b = BitLengthSet(8).repeatRange(3);
    t.expectSetEq(b.expand(), {0, 8, 16, 24}, "repeatRange(3) on {8}");
    t.expect(b.min() == 0 && b.max() == 24, "repeatRange bounds on {8} (original regression)");
}

// Spec: modulo denotes the EXACT residue set for any set size (symbolic per-node residues).
void testModulo(TestContext& t)
{
    t.expectSetEq(BitLengthSet(ValueSet{32, 40, 48, 56}).modulo(16), {0, 8}, "modulo(16) residues");
    t.expectSetEq(BitLengthSet(ValueSet{0, 1, 7, 8, 9}).modulo(8),
                  refModulo({0, 1, 7, 8, 9}, 8),
                  "modulo vs reference model");
    t.expectSetEq(BitLengthSet(ValueSet{5, 10, 15}).modulo(1), {0}, "modulo(1) is {0}");
    t.expectSetEq(BitLengthSet(ValueSet{5, 10}).modulo(100), {5, 10}, "divisor above max keeps values");
    t.expectSetEq(BitLengthSet(8).modulo(0), {0}, "divisor 0 yields sentinel {0}");
    t.expectSetEq(BitLengthSet(8).modulo(-8), {0}, "negative divisor yields sentinel {0}");

    // modulo is exact regardless of set size (it does not go through expand()), including at and
    // past the default expansion limit.
    ValueSet big;
    for (std::int64_t i = 0; i < 16384; ++i)
    {
        big.insert(i);
    }
    t.expectSetEq(BitLengthSet(big).modulo(5), {0, 1, 2, 3, 4}, "modulo complete at the default expansion limit");

    // A residue carried only by the single largest member survives even though the set has ~20000
    // elements, far beyond any expand() limit.
    ValueSet aligned;
    for (std::int64_t i = 1; i <= 20000; ++i)
    {
        aligned.insert(16 * i);
    }
    const auto residues = (BitLengthSet(aligned) | BitLengthSet(16 * 20000 + 7)).modulo(8);
    t.expectSetEq(residues, {0, 7}, "modulo(8) is complete and sound for a ~20000-element set");

    // Symbolic residues compose exactly across the whole algebra, without enumerating S, and
    // without blowing up on huge repeat counts (would time out under an expand()-based modulo).
    t.expectSetEq((BitLengthSet(32) + BitLengthSet(8).repeatRange(3)).padToAlignment(8).modulo(16),
                  {0, 8},
                  "modulo through Add + RepeatRange + Pad (composed-set regression)");
    t.expectSetEq(BitLengthSet(8).repeatRange(2000000).modulo(8),
                  {0},
                  "modulo of a 2e6-capacity repeatRange is exact and fast");
    t.expectSetEq(BitLengthSet(3).repeat(1000000).modulo(8),
                  refModulo({3000000}, 8),
                  "modulo of a 1e6-count repeat matches the single reachable value");
    // Residue union stabilizes by small k, so a small reference count matches the 1e6 query.
    t.expectSetEq(BitLengthSet(ValueSet{1, 3}).repeatRange(1000000).modulo(8),
                  refModulo(refRepeatRange({1, 3}, 8), 8),
                  "modulo of a 1e6-cap repeatRange saturates to all residues");
    // Pad widens the working modulus to lcm(alignment, divisor); verify an odd divisor case.
    t.expectSetEq(BitLengthSet(ValueSet{1, 2, 3, 4, 5, 6, 7, 8}).padToAlignment(4).modulo(6),
                  refModulo(refPad({1, 2, 3, 4, 5, 6, 7, 8}, 4), 6),
                  "modulo after padding with an alignment coprime-ish to the divisor");
}

// Spec: expand() exactness condition, soundness under truncation, size cap, limit >= 1.
void testExpand(TestContext& t)
{
    // Exact at the cardinality == limit boundary (every intermediate set fits the limit).
    const BitLengthSet e = BitLengthSet(ValueSet{0, 4}) + BitLengthSet(ValueSet{0, 1, 2, 3});
    t.expectSetEq(e.expand(8), {0, 1, 2, 3, 4, 5, 6, 7}, "expand exact when |S| == limit");
    t.expectSetEq(e.expand(9), {0, 1, 2, 3, 4, 5, 6, 7}, "expand exact when |S| < limit");

    // Sound under-approximation when truncating: subset of S, non-empty, within the cap.
    const ValueSet     truth{0, 5, 10, 15, 20, 25, 30, 35};
    const BitLengthSet f  = BitLengthSet(ValueSet{0, 5}) + BitLengthSet(ValueSet{0, 10, 20, 30});
    const auto         f3 = f.expand(3);
    t.expect(isSubset(f3, truth), "truncated expansion is a subset of S (soundness)");
    t.expect(!f3.empty() && f3.size() <= 3, "truncated expansion is non-empty and within the cap");

    // Truncated composite expansions respect the cap and stay sound.
    const auto u2 = (BitLengthSet(ValueSet{1, 2, 3}) | BitLengthSet(ValueSet{4, 5, 6})).expand(4);
    t.expect(u2.size() <= 4 && !u2.empty(), "union expansion respects the cap");
    t.expect(isSubset(u2, ValueSet{1, 2, 3, 4, 5, 6}), "union expansion is sound");
    const auto r2 = BitLengthSet(ValueSet{0, 1}).repeat(6).expand(4);
    t.expect(r2.size() <= 4 && !r2.empty(), "repeat expansion respects the cap");
    t.expect(isSubset(r2, ValueSet{0, 1, 2, 3, 4, 5, 6}), "repeat expansion is sound");
    t.expect((BitLengthSet(5) | BitLengthSet(3)).expand(1).size() == 1, "expand(1) yields one element");

    // Under-approximation of a large set: sound, and bounded by the symbolic max.
    const BitLengthSet big      = BitLengthSet(8).repeatRange(10000);
    const auto         bigSlice = big.expand(4096);
    t.expect(bigSlice.size() <= 4096 && !bigSlice.empty(), "large expansion respects the cap");
    t.expect(*bigSlice.rbegin() <= big.max(), "expansion never exceeds the symbolic max");
    t.expect(bigSlice.count(0) == 1, "repeatRange expansion retains the k = 0 term");
    for (const auto v : bigSlice)
    {
        t.expect(v % 8 == 0, "large expansion values are sound (multiples of 8)");
    }

    // A leaf larger than the limit is truncated to `limit` of its values.
    const auto leafTrunc = BitLengthSet(ValueSet{1, 2, 3}).expand(2);
    t.expect(leafTrunc.size() <= 2, "leaf expansion respects the limit");
    t.expect(isSubset(leafTrunc, ValueSet{1, 2, 3}), "truncated leaf expansion is sound");

    // I1 robustness — the limit is clamped to >= 1, so expand(0) is never empty.
    t.expect(!(BitLengthSet(5) | BitLengthSet(3)).expand(0).empty(),
             "expansion never returns the empty set, even for limit 0");
    t.expect(BitLengthSet(ValueSet{1, 2, 3}).expand(0).size() == 1, "expand(0) clamps to a one-element result");
}

// Spec: expandChecked() reports exactness (true iff values == S).
void testExpandChecked(TestContext& t)
{
    // Exact: the whole set fits under the limit at every node.
    const auto ok = (BitLengthSet(ValueSet{0, 4}) + BitLengthSet(ValueSet{0, 1, 2, 3})).expandChecked(8);
    t.expect(ok.exact, "expandChecked reports exact when |S| == limit");
    t.expectSetEq(ok.values, {0, 1, 2, 3, 4, 5, 6, 7}, "expandChecked exact values equal S");

    // No false positive at the exact boundary: |S| == limit is still exact, |S| == limit+1 is not.
    const BitLengthSet boundary = BitLengthSet(ValueSet{0, 5}) + BitLengthSet(ValueSet{0, 10, 20, 30});  // |S| = 8
    t.expect(boundary.expandChecked(8).exact, "expandChecked exact when |S| exactly equals limit (no false negative)");
    t.expect(!boundary.expandChecked(7).exact, "expandChecked inexact when |S| exceeds limit by one");

    // Inexact: truncation anywhere flips the flag; values stay a sound subset within the cap.
    const auto trunc = boundary.expandChecked(3);
    t.expect(!trunc.exact, "expandChecked reports inexact under truncation");
    t.expect(trunc.values.size() <= 3 && !trunc.values.empty(), "inexact values respect the cap and are non-empty");
    t.expect(isSubset(trunc.values, ValueSet{0, 5, 10, 15, 20, 25, 30, 35}), "inexact values are a sound subset");

    // Inexactness propagates up through composition (a truncated child taints the parent).
    const BitLengthSet widePayload = BitLengthSet(8).repeatRange(10000);  // ~10001 distinct values
    t.expect(!(BitLengthSet(32) + widePayload).expandChecked(4096).exact,
             "inexactness propagates through Add from a truncated child");

    // Leaf exactness tracks the limit precisely.
    t.expect(BitLengthSet(ValueSet{1, 2, 3}).expandChecked(3).exact, "leaf exact when it fits the limit");
    t.expect(!BitLengthSet(ValueSet{1, 2, 3}).expandChecked(2).exact, "leaf inexact when truncated");

    // A fixed/small set is always exact regardless of a generous limit.
    t.expect(BitLengthSet(8).expandChecked(1).exact, "singleton is exact at limit 1");
    t.expect(BitLengthSet(8).repeat(3).expandChecked(16).exact, "fixed repeat is exact");
}

// Repeat/repeatRange expansion is bounded by convergence, not by the count, so a huge
// count is both correct (exact values) and fast (does not run `count` rounds).
void testExpandBoundedRepeat(TestContext& t)
{
    // Enormous counts still produce exact values via the shift/convergence path.
    t.expectSetEq(BitLengthSet(8).repeat(1000000).expand(), {8000000}, "repeat(1e6) of {8} is exactly {8e6}");
    t.expect(BitLengthSet(8).repeat(1000000).expandChecked().exact, "huge fixed repeat stays exact");

    // Multi-value item: the smallest window of the exactly-N sumset of {8,9} is {8N .. 8N+k-1}.
    t.expectSetEq(BitLengthSet(ValueSet{8, 9}).repeat(1000000).expand(4),
                  {8000000, 8000001, 8000002, 8000003},
                  "repeat(1e6) of {8,9} keeps the smallest window, shifted by count*min");

    // A small count still matches the reference model (guards the shifted path against regressions).
    t.expectSetEq(BitLengthSet(ValueSet{1, 3}).repeat(5).expand(), refRepeat({1, 3}, 5), "repeat(5) vs reference");
    t.expectSetEq(BitLengthSet(ValueSet{2, 3}).repeatRange(6).expand(),
                  refRepeatRange({2, 3}, 6),
                  "repeatRange(6) vs reference");

    // Huge repeatRange: sound, bounded by the symbolic max, retains the k = 0 term, reported inexact.
    const BitLengthSet wide = BitLengthSet(8).repeatRange(50000000);
    const auto         w    = wide.expand(4096);
    t.expect(w.size() <= 4096 && !w.empty(), "huge repeatRange expansion respects the cap");
    t.expect(w.count(0) == 1, "huge repeatRange retains the k = 0 term");
    t.expect(*w.rbegin() <= wide.max(), "huge repeatRange expansion stays within the symbolic max");
    for (const auto v : w)
    {
        t.expect(v % 8 == 0, "huge repeatRange values are sound multiples of 8");
    }
    t.expect(!wide.expandChecked(4096).exact, "huge repeatRange expansion is reported inexact");

    // Wall-clock guard: 50e6 rounds would take minutes; the bounded loop finishes near-instantly.
    const auto  start = std::chrono::steady_clock::now();
    std::size_t sink  = BitLengthSet(8).repeat(50000000).expand(256).size();
    sink += BitLengthSet(ValueSet{8, 16}).repeatRange(50000000).expand(256).size();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    t.expect(sink > 0, "bounded-repeat sink is used");
    t.expect(secs < 2.0, "huge repeat/repeatRange expansion completes quickly");
}

// Value-domain safety — negatives clamp to 0, and arithmetic saturates instead of
// overflowing (no UB). Realistic inputs never reach these regimes; the point is defined behaviour.
void testValueDomainSafety(TestContext& t)
{
    // Negative construction values are clamped to 0, keeping the set in-domain.
    t.expectSetEq(BitLengthSet(-3).expand(), {0}, "negative singleton clamps to {0}");
    t.expectSetEq(BitLengthSet(ValueSet{-5, -1, 4}).expand(), {0, 4}, "negative elements clamp to 0");
    t.expect(BitLengthSet(-8).min() == 0 && BitLengthSet(-8).max() == 0, "clamped singleton bounds are 0");
    // A clamped-negative input keeps min() <= max() (I2).
    const BitLengthSet negRange = BitLengthSet(-8).repeatRange(3);
    t.expect(negRange.min() == 0 && negRange.max() == 0 && negRange.min() <= negRange.max(),
             "repeatRange over a clamped negative keeps min <= max (I2)");
    // Padding and modulo behave on the clamped (non-negative) value.
    t.expectSetEq(BitLengthSet(-3).padToAlignment(8).expand(), {0}, "pad of a clamped negative is 0");
    t.expectSetEq(BitLengthSet(-3).modulo(8), {0}, "modulo of a clamped negative is {0}");

    // Arithmetic saturates at INT64_MAX rather than wrapping (no signed-overflow UB).
    const std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    const BitLengthSet huge(kMax - 2);
    t.expect(huge.padToAlignment(8).max() == kMax, "pad near INT64_MAX saturates instead of wrapping");
    t.expect(huge.padToAlignment(8).min() >= huge.min(), "saturated pad stays monotone (no wrap to negative)");
    t.expect((huge + BitLengthSet(10)).max() == kMax, "sum past INT64_MAX saturates");
    t.expect((huge + BitLengthSet(10)).min() >= 0, "saturated sum never wraps negative");
    t.expect(BitLengthSet(kMax / 2 + 1).repeat(4).max() == kMax, "product past INT64_MAX saturates");
    t.expect(BitLengthSet(kMax).repeat(1000000).max() == kMax, "large repeat product saturates, no UB");
    // Saturated expansion is still a well-formed, non-empty set of non-negative values.
    const auto sat = (huge + BitLengthSet(ValueSet{0, 10})).expand();
    t.expect(!sat.empty() && *sat.begin() >= 0, "saturated expansion is non-empty and non-negative");
}

// is_aligned_at (exact, built on modulo) and value-set operator==/operator!=.
void testAlignmentAndEquality(TestContext& t)
{
    // is_aligned_at: tri-state — Some(true)/Some(false) are exact verdicts, nullopt is refusal.
    // The comparisons are deliberately against optional values: a bare boolean context would
    // test has_value(), silently passing on a false verdict.
    const std::optional<bool> yes{true};
    const std::optional<bool> no{false};
    const BitLengthSet byteAligned = BitLengthSet(8) + BitLengthSet(16) + BitLengthSet(ValueSet{0, 8, 16});
    t.expect(byteAligned.is_aligned_at(8) == yes, "all-multiple-of-8 set is byte-aligned");
    t.expect(byteAligned.is_aligned_at(4) == yes && byteAligned.is_aligned_at(1) == yes,
             "byte-aligned implies 4- and 1-aligned");
    t.expect(BitLengthSet(ValueSet{8, 12}).is_aligned_at(8) == no, "a set with a non-multiple is not byte-aligned");
    t.expect(BitLengthSet(0).is_aligned_at(8) == yes, "the zero-length set is aligned at any boundary");
    t.expect(BitLengthSet(ValueSet{8, 12}).is_aligned_at(0) == yes, "alignment < 1 is trivially aligned");
    // Exact even past the expansion limit (built on symbolic modulo, not expand).
    t.expect(BitLengthSet(8).repeatRange(20000).is_aligned_at(8) == yes,
             "huge byte-multiple set is byte-aligned (exact)");
    // Refusal propagates as nullopt — "cannot evaluate" is never converted into a guess.
    t.expect(!(BitLengthSet(16) + BitLengthSet(8).repeatRange(70000)).is_aligned_at(1000000007).has_value(),
             "undecidable alignment query refuses instead of guessing");

    // operator== / operator!=: provable value-set equality.
    t.expect(BitLengthSet(ValueSet{8, 16, 24}) == BitLengthSet(8).repeatRange(0) + BitLengthSet(ValueSet{8, 16, 24}),
             "identity + a set equals the set");
    t.expect(BitLengthSet(8) + BitLengthSet(16) == BitLengthSet(16) + BitLengthSet(8), "operator+ commutes (==)");
    t.expect((BitLengthSet(8) | BitLengthSet(8)) == BitLengthSet(8), "union with self equals self (==)");
    t.expect(BitLengthSet(8) != BitLengthSet(16), "distinct singletons are unequal");
    t.expect(BitLengthSet(ValueSet{8, 16}) != BitLengthSet(8), "different sets are unequal");
    t.expect(BitLengthSet() == BitLengthSet(0), "default ctor equals {0}");
    t.expect(!(BitLengthSet(8) == BitLengthSet(16)), "operator== is false for distinct sets");
}

// Evaluation is memoized (shared DAGs are not re-walked exponentially) and iterative
// (deep expressions do not overflow the stack, including at destruction).
void testDeepAndSharedGraphs(TestContext& t)
{
    // `s = s + s` for n levels denotes 2^n paths to the leaf. Correct results at n = 40
    // are only reachable with memoization — a per-path walk would not finish. A timing guard
    // pins down that it is sub-exponential.
    const auto   dagStart = std::chrono::steady_clock::now();
    BitLengthSet dag(ValueSet{0, 1});
    for (int i = 0; i < 40; ++i)
    {
        dag = dag + dag;
    }
    t.expect(dag.min() == 0 && dag.max() == (std::int64_t{1} << 40), "shared DAG min/max are correct");
    t.expectSetEq(dag.modulo(8), {0, 1, 2, 3, 4, 5, 6, 7}, "shared DAG modulo is correct");
    const double dagSecs = std::chrono::duration<double>(std::chrono::steady_clock::now() - dagStart).count();
    t.expect(dagSecs < 2.0, "shared DAG evaluation is sub-exponential");

    // An N-deep chain of `+` must evaluate every operation and then destruct without
    // overflowing the call stack.
    const int  depth     = 200000;
    const auto deepStart = std::chrono::steady_clock::now();
    {
        BitLengthSet chain(0);
        for (int i = 0; i < depth; ++i)
        {
            chain = chain + BitLengthSet(1);
        }
        t.expect(chain.min() == depth && chain.max() == depth, "deep chain min/max computed iteratively");
        t.expect(chain.fixed(), "deep chain is fixed-size");
        t.expectSetEq(chain.modulo(4), {0}, "deep chain modulo computed iteratively");
        t.expect(!chain.expand(8).empty(), "deep chain expand computed iteratively");
        t.expect(chain.str().size() > static_cast<std::size_t>(depth), "deep chain str rendered iteratively");
    }  // chain destructor runs here — must not overflow the stack
    const double deepSecs = std::chrono::duration<double>(std::chrono::steady_clock::now() - deepStart).count();
    t.expect(deepSecs < 10.0, "deep chain build/eval/teardown completes without stack overflow");
}

// Spec: str() grammar (leaf ascending order, post-clamp parameters, operator spellings).
void testStr(TestContext& t)
{
    t.expect(BitLengthSet().str() == "{0}", "str of default ctor");
    t.expect(BitLengthSet(8).str() == "{8}", "str of singleton");
    t.expect(BitLengthSet(ValueSet{3, 1, 2}).str() == "{1,2,3}", "str leaf values ascend");
    t.expect((BitLengthSet(8) + BitLengthSet(16)).str() == "concat({8},{16})", "str of concat");
    t.expect((BitLengthSet(8) | BitLengthSet(16)).str() == "union({8},{16})", "str of union");
    t.expect(BitLengthSet(8).padToAlignment(8).str() == "pad({8},8)", "str of pad");
    t.expect(BitLengthSet(8).padToAlignment(-2).str() == "pad({8},1)", "str of pad shows post-clamp param");
    t.expect(BitLengthSet(8).repeat(3).str() == "repeat({8},3)", "str of repeat");
    t.expect(BitLengthSet(8).repeat(-1).str() == "repeat({8},0)", "str of repeat shows post-clamp param");
    t.expect(BitLengthSet(8).repeatRange(3).str() == "repeat_range({8},3)", "str of repeat_range");
}

// Spec: invariant I3 — objects are immutable values; derivation never mutates operands.
void testPersistence(TestContext& t)
{
    const BitLengthSet s(8);
    const BitLengthSet r = s.repeat(3);
    const BitLengthSet u = r | s;
    const BitLengthSet v = u + r;

    t.expectSetEq(s.expand(), {8}, "operand unchanged after derivations (s)");
    t.expectSetEq(r.expand(), {24}, "operand unchanged after derivations (r)");
    t.expectSetEq(u.expand(), {8, 24}, "operand unchanged after derivations (u)");
    t.expectSetEq(v.expand(), {32, 48}, "derived set has its own value");

    BitLengthSet       mutated = r;
    const BitLengthSet copy    = mutated;
    mutated                    = mutated + s;
    t.expectSetEq(copy.expand(), {24}, "copies keep their value when the source is reassigned");
    t.expectSetEq(mutated.expand(), {32}, "reassigned variable holds the new value");

    // A moved-from object is left denoting {0} — every call on it is well-defined, not a
    // null-root crash. Both move-construction and move-assignment reset the source.
    BitLengthSet       movedFromCtor(ValueSet{8, 16, 24});
    const BitLengthSet movedIntoCtor = std::move(movedFromCtor);
    t.expectSetEq(movedIntoCtor.expand(), {8, 16, 24}, "move-constructed target keeps the value");
    t.expectSetEq(movedFromCtor.expand(), {0}, "move-constructed source is left denoting {0}");
    t.expect(movedFromCtor.min() == 0 && movedFromCtor.max() == 0 && movedFromCtor.fixed(),
             "moved-from source has well-defined bounds, not a null-root crash");
    t.expect(movedFromCtor.str() == "{0}", "moved-from source renders as {0}");
    t.expectSetEq((movedFromCtor + BitLengthSet(8)).expand(), {8}, "moved-from source is still usable in operators");

    BitLengthSet movedFromAssign(40);
    BitLengthSet assignTarget(1);
    assignTarget = std::move(movedFromAssign);
    t.expectSetEq(assignTarget.expand(), {40}, "move-assigned target keeps the value");
    t.expectSetEq(movedFromAssign.expand(), {0}, "move-assigned source is left denoting {0}");
}

// Spec examples: the composition patterns the semantic analyzer builds (structs, unions,
// arrays, delimited composites) — the class's reason to exist.
void testDsdlCompositionPatterns(TestContext& t)
{
    // Sequential struct layout: `uint8 foo; uint16[3] bar` => 8 + 48 = 56 bits, fixed.
    BitLengthSet offset(0);
    offset = offset.padToAlignment(1) + BitLengthSet(8);
    offset = offset.padToAlignment(1) + BitLengthSet(16).repeat(3);
    offset = offset.padToAlignment(8);
    t.expect(offset.fixed() && offset.min() == 56, "struct pattern: u8 + u16[3] is 56 bits fixed");

    // Tagged union: 8-bit tag + (option {8} | option {16,24}), byte-aligned.
    const BitLengthSet unionSet =
        (BitLengthSet(8) + (BitLengthSet(8) | BitLengthSet(ValueSet{16, 24}))).padToAlignment(8);
    t.expectSetEq(unionSet.expand(), {16, 24, 32}, "union pattern: tag + alternatives");

    // Variable array: 8-bit length prefix + up to 2 elements of 16 bits.
    const BitLengthSet varArray = BitLengthSet(8) + BitLengthSet(16).repeatRange(2);
    t.expectSetEq(varArray.expand(), {8, 24, 40}, "variable array pattern: prefix + repeatRange");
    t.expect(varArray.min() == 8 && varArray.max() == 40, "variable array bounds");

    // Delimited (non-sealed) composite: 32-bit header + 0..extent/8 bytes of payload.
    const std::int64_t extent    = 64;
    const BitLengthSet delimited = BitLengthSet(32) + BitLengthSet(8).repeatRange(extent / 8);
    t.expectSetEq(delimited.expand(), {32, 40, 48, 56, 64, 72, 80, 88, 96}, "delimited composite pattern");

    // Original composed regression: (32 + {8}.repeatRange(3)) byte-aligned.
    const BitLengthSet composed = (BitLengthSet(32) + BitLengthSet(8).repeatRange(3)).padToAlignment(8);
    t.expect(composed.min() == 32 && composed.max() == 56, "composed set bounds (original regression)");
    const auto mod = composed.modulo(16);
    t.expect(mod.has_value() && mod->count(0) == 1 && mod->count(8) == 1, "composed modulo(16) (original regression)");
}

}  // namespace

// Spec (RunSet exactness contract): whenever runSet() returns a value it denotes EXACTLY S —
// verified differentially against the reference model over a composed-operation battery — and
// its count/membership/subset/equality closed forms agree with enumeration. Also: operator== is
// exact beyond the expand() limit (formerly conservative there), and huge structured sets
// evaluate in closed form without enumeration.
void testRunSet(TestContext& t)
{
    using llvmdsdl::RunSet;

    // Differential battery: parallel (symbolic, reference) composition, then compare the
    // materialized RunSet against the reference model for every case.
    struct Case final
    {
        BitLengthSet bls;
        ValueSet     ref;
        std::string  name;
    };
    std::vector<Case> cases;
    const auto        add = [&](BitLengthSet b, ValueSet r, std::string name) {
        cases.push_back(Case{std::move(b), std::move(r), std::move(name)});
    };

    const std::vector<ValueSet> bases = {
        {0}, {8}, {0, 8}, {1, 3}, {0, 1, 7, 8, 9}, {5, 8, 9, 11, 12, 43}, {8, 24}, {3, 9}};
    for (const auto& base : bases)
    {
        const BitLengthSet b((ValueSet(base)));
        add(b, base, "base " + setToString(base));
        for (const std::int64_t k : {0, 1, 2, 3, 5})
        {
            add(b.repeat(k), refRepeat(base, k), "repeat(" + std::to_string(k) + ") of " + setToString(base));
            add(b.repeatRange(k), refRepeatRange(base, k), "repeatRange(" + std::to_string(k) + ") of " + setToString(base));
        }
        for (const std::int64_t a : {2, 3, 8})
        {
            add(b.padToAlignment(a), refPad(base, a), "pad(" + std::to_string(a) + ") of " + setToString(base));
        }
        for (const auto& other : bases)
        {
            const BitLengthSet o((ValueSet(other)));
            ValueSet           u = base;
            u.insert(other.begin(), other.end());
            add(b + o, refAdd(base, other), "add " + setToString(base) + "+" + setToString(other));
            add(b | o, u, "union " + setToString(base) + "|" + setToString(other));
        }
        // Composed DSDL shapes: prefix + repeatRange, padded.
        add((BitLengthSet(16) + b.repeatRange(4)).padToAlignment(8),
            refPad(refAdd({16}, refRepeatRange(base, 4)), 8),
            "composed vla of " + setToString(base));
    }

    for (const auto& c : cases)
    {
        const auto rs = c.bls.runSet();
        t.expect(rs.has_value(), "runSet() available for " + c.name);
        if (!rs)
        {
            continue;
        }
        t.expect(rs->valid(), "runSet invariants hold for " + c.name);
        const auto values = rs->materialize(1U << 20U);
        t.expect(values.has_value(), "runSet materializes for " + c.name);
        if (!values)
        {
            continue;
        }
        t.expectSetEq(*values, c.ref, "runSet exact values for " + c.name);
        const auto n = rs->count();
        t.expect(n && static_cast<std::size_t>(*n) == c.ref.size(), "runSet count exact for " + c.name);
        t.expect(rs->min() == *c.ref.begin() && rs->max() == *c.ref.rbegin(), "runSet bounds for " + c.name);
        for (const std::int64_t probe : {0LL, 1LL, 7LL, 8LL, 16LL, 23LL, 100LL})
        {
            t.expect(rs->contains(probe) == (c.ref.count(probe) != 0),
                     "runSet membership(" + std::to_string(probe) + ") for " + c.name);
        }
        // modulo: exact for divisors below AND above the symbolic-residue cap (the
        // latter exercises the per-run residue path that replaced the silent degrade).
        for (const std::int64_t d : {2LL, 7LL, 8LL, 64LL, 100003LL})
        {
            const auto res = c.bls.modulo(d);
            t.expect(res.has_value(), "modulo(" + std::to_string(d) + ") answers for " + c.name);
            if (res)
            {
                t.expectSetEq(*res, refModulo(c.ref, d), "modulo(" + std::to_string(d) + ") exact for " + c.name);
            }
        }
    }

    // Huge structured sets evaluate in closed form: the uint8[<=9000] offset shape, and a
    // billion-element run — both far beyond any enumeration ceiling.
    {
        const BitLengthSet offset = BitLengthSet(16) + BitLengthSet(8).repeatRange(9000);
        const auto         rs     = offset.runSet();
        t.expect(rs.has_value(), "vla offset shape has a RunSet");
        if (rs)
        {
            t.expect(rs->count().value_or(0) == 9001, "vla offset count exact (9001)");
            t.expect(rs->min() == 16 && rs->max() == 16 + 9000 * 8, "vla offset bounds");
            t.expect(rs->contains(16) && rs->contains(24) && !rs->contains(17), "vla offset membership");
        }
        const auto huge = BitLengthSet(8).repeatRange(1000000000).runSet();
        t.expect(huge.has_value() && huge->count().value_or(0) == 1000000001LL && huge->max() == 8000000000LL,
                 "billion-element repeatRange in closed form");
    }

    // modulo beyond the symbolic-residue cap on a beyond-enumeration set: exact residues
    // in closed form (offsets {16 + 8k : k <= 20000} mod 100000 walk the stride-8 coset — 12500
    // distinct residues); a divisor so large that the residue set equals the whole 70001-element
    // set REFUSES instead of returning the silently truncated set the old degrade produced.
    {
        const BitLengthSet wide = BitLengthSet(16) + BitLengthSet(8).repeatRange(20000);
        const auto         res  = wide.modulo(100000);
        t.expect(res.has_value(), "modulo(100000) answers beyond the residue cap");
        if (res)
        {
            t.expect(res->size() == 12500 && *res->begin() == 0 && *res->rbegin() == 99992,
                     "modulo(100000) closed-form residues of a 20001-element set");
        }
        const BitLengthSet huge = BitLengthSet(16) + BitLengthSet(8).repeatRange(70000);
        t.expect(!huge.modulo(1000000007).has_value(),
                 "modulo refuses when the exact residue set exceeds the output budget");
        t.expect(huge.modulo(8).has_value() && huge.modulo(8)->size() == 1,
                 "small-divisor residues stay exact on the same huge set");
    }

    // PR-review hardening (Copilot findings on the RunSet kernel):
    {
        // Residue budget applies to the UNIQUE result: two disjoint dense runs whose residue
        // images coincide used to blow the pre-dedup budget (80000 raw walks) even though the
        // unique residue set (40000) is comfortably within it.
        const BitLengthSet twoRuns =
            BitLengthSet(1).repeatRange(39999) | (BitLengthSet(50000) + BitLengthSet(1).repeatRange(39999));
        const auto res = twoRuns.modulo(50000);
        t.expect(res.has_value(), "residue budget is charged on the unique result, not raw walks");
        if (res)
        {
            t.expect(res->size() == 40000 && *res->begin() == 0 && *res->rbegin() == 39999,
                     "cross-run duplicate residues dedup exactly");
        }

        // Run representability: extreme-gap inputs must stay singleton runs (a merged run's
        // stride would overflow int64), never a silently corrupt run.
        llvmdsdl::FlatSet<std::int64_t> extremes;
        const std::vector<std::int64_t> ev{std::numeric_limits<std::int64_t>::min(),
                                           std::numeric_limits<std::int64_t>::max()};
        extremes.insert(ev.begin(), ev.end());
        const auto extremeRuns = llvmdsdl::RunSet::fromValues(extremes);
        t.expect(extremeRuns.valid() && extremeRuns.count().value_or(0) == 2 &&
                     extremeRuns.contains(std::numeric_limits<std::int64_t>::min()) &&
                     extremeRuns.contains(std::numeric_limits<std::int64_t>::max()),
                 "extreme-gap values decompose into representable singleton runs");

        // repeatRange at the count ceiling refuses instead of wrapping the run count.
        t.expect(!llvmdsdl::RunSet(8).repeatRange(std::numeric_limits<std::int64_t>::max()).has_value(),
                 "repeatRange at INT64_MAX refuses instead of overflowing the run count");
    }

    // operator== is now exact past the expand() limit: two structurally different constructions
    // of the same 50001-element set compare equal (formerly conservatively unequal), and a
    // genuinely different set compares unequal.
    {
        const BitLengthSet a = BitLengthSet(8).repeatRange(50000);
        const BitLengthSet b = (BitLengthSet(0) | BitLengthSet(8)).repeat(50000);
        t.expect(a == b, "operator== exact on equal 50001-element sets (mixed constructions)");
        const BitLengthSet c = a + BitLengthSet(4);
        t.expect(a != c, "operator!= on shifted 50001-element set");
        const BitLengthSet d = BitLengthSet(8).repeatRange(50001);
        t.expect(a != d, "operator!= on off-by-one-count huge sets");
    }
}

bool runBitLengthSetTests()
{
    TestContext t;

    testConstructionAndInvariants(t);
    testBoundsExactness(t);
    testAdditionSemantics(t);
    testUnionSemantics(t);
    testPadToAlignment(t);
    testRepeat(t);
    testRepeatRange(t);
    testModulo(t);
    testExpand(t);
    testExpandChecked(t);
    testExpandBoundedRepeat(t);
    testValueDomainSafety(t);
    testAlignmentAndEquality(t);
    testDeepAndSharedGraphs(t);
    testStr(t);
    testPersistence(t);
    testDsdlCompositionPatterns(t);
    testRunSet(t);

    if (t.failures > 0)
    {
        std::cerr << "BitLengthSet spec tests: " << t.failures << " failure(s)\n";
        return false;
    }
    return true;
}
