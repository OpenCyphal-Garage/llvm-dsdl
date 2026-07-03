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
/// These tests are written against the documented contract — the denotational semantics,
/// invariants I1..I4, the algebraic laws, and the exactness model — not against the current
/// implementation. Expected values are computed by an independent reference model
/// (`refAdd`, `refPad`, `refRepeat`, ...) that transcribes the specification directly.
///
/// Spec clauses that the current implementation is known to violate are exercised via
/// `expectDefect(...)` (an XFAIL marker): they do not fail the suite while the defect stands,
/// and they print a loud note when a defect stops reproducing so the test can be promoted to
/// an enforced `expect(...)`. Defect IDs (BLS-D1, ...) refer to the defect log in the
/// BitLengthSet analysis report and the cross-references in BitLengthSet.h/.cpp.
///
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
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

bool isSubset(const ValueSet& sub, const ValueSet& super)
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

std::string setToString(const ValueSet& s)
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
    int failures       = 0;
    int defectsPresent = 0;
    int defectsFixed   = 0;

    void expect(const bool condition, const std::string& what)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "BitLengthSet spec FAIL: " << what << "\n";
        }
    }

    void expectSetEq(const ValueSet& actual, const ValueSet& expected, const std::string& what)
    {
        if (actual != expected)
        {
            ++failures;
            std::cerr << "BitLengthSet spec FAIL: " << what << "\n  expected " << setToString(expected)
                      << "\n  actual   " << setToString(actual) << "\n";
        }
    }

    /// XFAIL marker: `specHolds` states the SPEC assertion. While the defect stands this
    /// prints a note and does not fail the suite; once fixed it asks to be promoted.
    void expectDefect(const char* id, const bool specHolds, const std::string& what)
    {
        if (specHolds)
        {
            ++defectsFixed;
            std::cout << "BitLengthSet: known defect " << id << " no longer reproduces (" << what
                      << "); promote this check to an enforced expect().\n";
        }
        else
        {
            ++defectsPresent;
            std::cout << "BitLengthSet: known defect " << id << " reproduced; spec assertion deferred: " << what
                      << "\n";
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

    // Completeness at (and past) the former expand()-based limit — now irrelevant to exactness.
    ValueSet big;
    for (std::int64_t i = 0; i < 16384; ++i)
    {
        big.insert(i);
    }
    t.expectSetEq(BitLengthSet(big).modulo(5), {0, 1, 2, 3, 4}, "modulo complete at the default expansion limit");

    // BLS-D1 (FIXED): with symbolic residues, a residue carried only by the single largest
    // member survives even though the set has ~20000 elements, far beyond any expand() limit.
    ValueSet aligned;
    for (std::int64_t i = 1; i <= 20000; ++i)
    {
        aligned.insert(16 * i);
    }
    const auto residues = (BitLengthSet(aligned) | BitLengthSet(16 * 20000 + 7)).modulo(8);
    t.expectSetEq(residues, {0, 7}, "modulo(8) is complete and sound for a ~20000-element set (BLS-D1 fixed)");

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

    // BLS-D4: a leaf larger than the limit is returned whole; spec caps the result at limit.
    t.expectDefect("BLS-D4",
                   BitLengthSet(ValueSet{1, 2, 3}).expand(2).size() <= 2,
                   "leaf expansion must respect the limit");

    // BLS-D3: I1 robustness — expansion should never be empty, even at the (precondition-
    // violating) limit of 0, where the union trim currently erases every element.
    t.expectDefect("BLS-D3",
                   !(BitLengthSet(5) | BitLengthSet(3)).expand(0).empty(),
                   "expansion must never return the empty set (I1)");
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
    t.expect(mod.count(0) == 1 && mod.count(8) == 1, "composed modulo(16) (original regression)");
}

}  // namespace

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
    testStr(t);
    testPersistence(t);
    testDsdlCompositionPatterns(t);

    if (t.defectsPresent > 0)
    {
        std::cout << "BitLengthSet: " << t.defectsPresent
                  << " known spec defect(s) reproduced (deferred, not failing the suite)\n";
    }
    if (t.failures > 0)
    {
        std::cerr << "BitLengthSet spec tests: " << t.failures << " failure(s)\n";
        return false;
    }
    return true;
}
