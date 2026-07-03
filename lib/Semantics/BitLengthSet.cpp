//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements symbolic bit-length set algebra.
///
/// This engine supports arithmetic and union composition over bit-length expressions used by semantic extent reasoning.
///
/// The authoritative behavioral contract (denotational semantics, value-domain preconditions,
/// invariants I1..I4, algebraic laws, and the exactness model) lives in the class-level
/// specification in BitLengthSet.h. The comments in this file document how each expression
/// node realizes that contract, and in particular the per-node truncation policies of
/// `expand()`, which are implementation details deliberately left unspecified by the public
/// contract.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Semantics/BitLengthSet.h"

#include <algorithm>
#include <map>
#include <numeric>
#include <sstream>
#include <iterator>
#include <utility>
#include <vector>

namespace llvmdsdl
{

namespace
{

/// @brief Hard ceiling on any modulus used during symbolic residue evaluation (`modulo()`).
///
/// Alignment-driven divisors in real DSDL are powers of two (<= 64), and the only operation
/// that widens the working modulus is `Pad`, which lifts it to `lcm(alignment, divisor)` —
/// still a small power of two. This cap is never approached in practice; it exists solely so
/// the pathological case (a `Pad` whose alignment is coprime to a large divisor) bails out to
/// the `expand()`-based fallback instead of allocating an enormous residue set. It also bounds a
/// `ResidueSet` to `kResidueModulusCap` bits = 8 KiB.
constexpr std::int64_t kResidueModulusCap = 1 << 16;

/// @brief Index of the lowest set bit of a non-zero 64-bit word (count of trailing zeros).
inline int countTrailingZeros(const std::uint64_t v)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(v);
#else
    int n = 0;
    for (std::uint64_t x = v; (x & 1ULL) == 0ULL; x >>= 1)
    {
        ++n;
    }
    return n;
#endif
}

/// @brief Dense bitmask over the residues `{0, ..., m-1}` of Z/m: bit `i` is set iff `i` is in
///        the set.
///
/// This is the working representation for `modulo()`. Residues live in a small bounded range
/// (`m <= kResidueModulusCap`, and `m <= 64` for realistic power-of-two alignments), so a bitmask
/// gives O(1) membership, contiguous cache-resident storage, and word-parallel union — versus the
/// pointer-chasing, per-element heap allocation of a node-based `std::set`. The first 64 residues
/// live inline in `low`, so the common `m <= 64` case performs NO heap allocation (`high` stays an
/// empty vector); wider moduli spill the remaining words into `high`. All arithmetic keeps the
/// bits above `m` clear, so `(low, high)` is a canonical key for cycle detection.
struct ResidueSet final
{
    std::int64_t               modulus{1};
    std::uint64_t              low{0};  ///< residues [0, 64)
    std::vector<std::uint64_t> high;    ///< residues [64, modulus); empty when modulus <= 64

    explicit ResidueSet(const std::int64_t m)
        : modulus(m)
    {
        const std::size_t words = static_cast<std::size_t>((m + 63) / 64);
        if (words > 1)
        {
            high.assign(words - 1, 0ULL);
        }
    }

    [[nodiscard]] std::size_t wordCount() const
    {
        return 1 + high.size();
    }

    [[nodiscard]] std::uint64_t word(const std::size_t i) const
    {
        return i == 0 ? low : high[i - 1];
    }

    void add(const std::int64_t residue)
    {
        const std::size_t   wi  = static_cast<std::size_t>(residue) >> 6U;
        const std::uint64_t bit = std::uint64_t{1} << (static_cast<std::size_t>(residue) & 63U);
        if (wi == 0)
        {
            low |= bit;
        }
        else
        {
            high[wi - 1] |= bit;
        }
    }

    void unionWith(const ResidueSet& other)
    {
        low |= other.low;
        for (std::size_t i = 0; i < high.size(); ++i)
        {
            high[i] |= other.high[i];
        }
    }

    /// @brief Applies `fn(residue)` to each present residue in ascending order.
    template <typename Fn>
    void forEach(Fn&& fn) const
    {
        for (std::size_t wi = 0; wi < wordCount(); ++wi)
        {
            std::uint64_t w = word(wi);
            while (w != 0)
            {
                fn(static_cast<std::int64_t>(wi * 64) + countTrailingZeros(w));
                w &= w - 1;  // clear lowest set bit
            }
        }
    }

    // Ordering / equality so a ResidueSet can key a std::map / std::set during cycle detection.
    // Every key in one detection shares the same modulus, so comparing (low, high) is sound.
    bool operator==(const ResidueSet& other) const
    {
        return low == other.low && high == other.high;
    }
    bool operator<(const ResidueSet& other) const
    {
        return low != other.low ? low < other.low : high < other.high;
    }
};

/// @brief Materializes a `ResidueSet` as the `std::set` the public `modulo()` API returns.
std::set<std::int64_t> toStdSet(const ResidueSet& s)
{
    std::set<std::int64_t> out;
    s.forEach([&out](const std::int64_t r) { out.insert(r); });
    return out;
}

/// @brief The singleton residue mask `{0}` modulo `m`.
ResidueSet zeroResidue(const std::int64_t m)
{
    ResidueSet s(m);
    s.add(0);
    return s;
}

/// @brief `{ (x + y) mod m : x in a, y in b }`, computed by bit-scan over both masks.
ResidueSet minkowskiSumMod(const ResidueSet& a, const ResidueSet& b)
{
    ResidueSet out(a.modulus);
    a.forEach([&](const std::int64_t x) {
        b.forEach([&](const std::int64_t y) {
            std::int64_t s = x + y;  // each operand < m, so the sum is < 2m
            if (s >= a.modulus)
            {
                s -= a.modulus;
            }
            out.add(s);
        });
    });
    return out;
}

/// @brief Computes `lcm(a, b)` for positive `a`, `b`, guarding against overflow and the cap.
/// @return False (leaving `result` unspecified) when the lcm would exceed `kResidueModulusCap`.
bool cappedLcm(const std::int64_t a, const std::int64_t b, std::int64_t& result)
{
    const std::int64_t g = std::gcd(a, b);
    if (g <= 0)
    {
        return false;
    }
    const std::int64_t step = a / g;    // a / gcd(a, b)
    if (step > kResidueModulusCap / b)  // step * b would exceed the cap (both positive)
    {
        return false;
    }
    result = step * b;
    return true;
}

/// @brief Residues of the `count`-fold Minkowski self-sum of `r` modulo `m` (A_count).
///
/// The sequence A_0 = {0}, A_k = A_{k-1} (+) r (mod m) is a deterministic walk over subsets of
/// Z/m, hence eventually periodic. Cycle detection answers arbitrarily large `count` in
/// O(preperiod + period) mask operations, so a huge fixed-array repeat count cannot blow up this
/// path (contrast BLS-D8 on `expand()`). The result is exact.
ResidueSet exactlyKSumMod(const ResidueSet& r, const std::int64_t count, const std::int64_t m)
{
    ResidueSet cur = zeroResidue(m);  // A_0
    if (count <= 0)
    {
        return cur;
    }
    std::vector<ResidueSet>            seq{cur};
    std::map<ResidueSet, std::int64_t> seen{{cur, 0}};
    for (std::int64_t i = 1; i <= count; ++i)
    {
        cur           = minkowskiSumMod(cur, r);
        const auto it = seen.find(cur);
        if (it != seen.end())
        {
            const std::int64_t j      = it->second;
            const std::int64_t period = i - j;
            const std::int64_t idx    = j + (count - j) % period;
            return seq[static_cast<std::size_t>(idx)];
        }
        seen.emplace(cur, i);
        seq.push_back(cur);
    }
    return cur;
}

/// @brief Residues of `union over k in [0, countMax] of the k-fold self-sum of r` modulo `m`.
///
/// The union is monotone and bounded by `m` elements; once the underlying sumset sequence A_k
/// repeats a previously seen value, every later term is already accounted for, so the loop stops
/// there. Exact, and O(preperiod + period) regardless of `countMax` (contrast BLS-D8).
ResidueSet unionUpToKSumMod(const ResidueSet& r, const std::int64_t countMax, const std::int64_t m)
{
    ResidueSet u = zeroResidue(m);  // the k = 0 term
    if (countMax <= 0)
    {
        return u;
    }
    ResidueSet           cur = zeroResidue(m);
    std::set<ResidueSet> seen{cur};
    for (std::int64_t i = 1; i <= countMax; ++i)
    {
        cur = minkowskiSumMod(cur, r);
        u.unionWith(cur);
        if (!seen.insert(cur).second)
        {
            break;  // A_i repeats an earlier A_j; all subsequent terms are already in `u`
        }
    }
    return u;
}

}  // namespace

/// @brief Immutable expression node of the persistent bit-length-set DAG.
///
/// Node meanings and operand usage (S(n) = the value set denoted by node n):
///
///   - `Leaf`        : S = `values` (non-empty by construction; public constructors coerce an
///                     empty input to {0}, upholding invariant I1). `lhs`/`rhs`/`param` unused.
///   - `Add`         : S = { a + b : a in S(lhs), b in S(rhs) } (Minkowski sum). `param` unused.
///   - `Union`       : S = S(lhs) union S(rhs). `param` unused.
///   - `Pad`         : S = { roundUp(v, param) : v in S(lhs) }, alignment `param` clamped to
///                     >= 1 both at construction and defensively at evaluation.
///   - `Repeat`      : S = `param`-fold Minkowski self-sum of S(lhs); `param` clamped to >= 0
///                     at construction; `param == 0` denotes {0}.
///   - `RepeatRange` : S = union over k in [0, param] of the k-fold self-sum of S(lhs);
///                     always contains 0 (the k = 0 term).
///
/// Nodes are immutable after construction and shared via `shared_ptr<const Node>`, which is
/// what makes copies of `BitLengthSet` O(1) and concurrent reads safe (invariant I3).
///
/// Evaluation walks the DAG recursively WITHOUT memoization: a subgraph shared by m paths is
/// evaluated m times, and recursion depth equals expression depth (see the complexity caveats
/// in the header: BLS-D8..D10).
struct BitLengthSet::Node final
{
    enum class Kind
    {
        Leaf,
        Add,
        Union,
        Pad,
        Repeat,
        RepeatRange,
    } kind{Kind::Leaf};

    std::set<std::int64_t>      values;
    std::shared_ptr<const Node> lhs;
    std::shared_ptr<const Node> rhs;
    std::int64_t                param{0};

    /// @brief Exact symbolic minimum of S (never enumerates; invariant I4).
    ///
    /// Per-kind derivation, exact on the non-negative value domain:
    ///   Leaf: smallest stored value. Add: min(lhs) + min(rhs). Union: min of the two minima.
    ///   Pad: roundUp(min(lhs), a) — correct because rounding-up is monotone.
    ///   Repeat: param * min(lhs) — picking the minimum for every draw minimizes the sum.
    ///   RepeatRange: 0 — the k = 0 term; the smallest element only for non-negative domains.
    /// The `values.empty()` guard on Leaf is defensive: public constructors never produce an
    /// empty leaf (I1).
    [[nodiscard]] std::int64_t min() const
    {
        switch (kind)
        {
        case Kind::Leaf:
            return values.empty() ? 0 : *values.begin();
        case Kind::Add:
            return lhs->min() + rhs->min();
        case Kind::Union:
            return std::min(lhs->min(), rhs->min());
        case Kind::Pad: {
            const auto v   = lhs->min();
            const auto a   = std::max<std::int64_t>(1, param);
            const auto rem = v % a;
            return rem == 0 ? v : v + (a - rem);
        }
        case Kind::Repeat:
            return lhs->min() * std::max<std::int64_t>(0, param);
        case Kind::RepeatRange:
            return 0;
        }
        return 0;
    }

    /// @brief Exact symbolic maximum of S (never enumerates; invariant I4).
    ///
    /// Mirrors `min()`: Leaf takes the largest stored value; Add sums the maxima; Union takes
    /// the larger maximum; Pad rounds the child maximum up (monotone); Repeat and RepeatRange
    /// both yield param * max(lhs) — for RepeatRange this is the k = param term, the maximum
    /// only on the non-negative value domain.
    [[nodiscard]] std::int64_t max() const
    {
        switch (kind)
        {
        case Kind::Leaf:
            return values.empty() ? 0 : *values.rbegin();
        case Kind::Add:
            return lhs->max() + rhs->max();
        case Kind::Union:
            return std::max(lhs->max(), rhs->max());
        case Kind::Pad: {
            const auto v   = lhs->max();
            const auto a   = std::max<std::int64_t>(1, param);
            const auto rem = v % a;
            return rem == 0 ? v : v + (a - rem);
        }
        case Kind::Repeat:
            return lhs->max() * std::max<std::int64_t>(0, param);
        case Kind::RepeatRange:
            return lhs->max() * std::max<std::int64_t>(0, param);
        }
        return 0;
    }

    /// @brief Materializes S bottom-up, capping every intermediate set at `limit` elements.
    ///
    /// Contract (see header): the result is a subset of S, exact when no intermediate set
    /// exceeds `limit`; otherwise an unspecified subset. The per-kind truncation policies —
    /// implementation details, deliberately unspecified publicly — are:
    ///
    ///   - Leaf: returns the stored set whole, IGNORING `limit` (BLS-D4).
    ///   - Add: enumerates lhs-major, rhs-minor over the (already capped) child expansions and
    ///     stops as soon as `limit` distinct sums exist; because enumeration order is not
    ///     globally sorted, the kept subset is arbitrary (may retain large sums while dropping
    ///     smaller ones). Cost can reach |lhs| * |rhs| inserts when sums collide (BLS-D11).
    ///   - Union: merges both (capped) child expansions, then discards the LARGEST elements
    ///     down to `limit` — note `limit == 0` discards everything, the empty-set corner
    ///     behind the `limit >= 1` precondition (BLS-D3).
    ///   - Pad: rounds each child value up; monotone, so effectively keeps the smallest.
    ///   - Repeat: iterated Minkowski self-sum, capping the accumulator each round; runs
    ///     exactly `param` rounds even when the accumulator has converged (e.g. on {0}) or
    ///     saturated at `limit` (BLS-D8).
    ///   - RepeatRange: like Repeat, additionally unioning every round's partial sums into the
    ///     result (seeded with {0} for k = 0) and trimming the largest elements to `limit`;
    ///     also runs exactly `param` rounds (BLS-D8).
    [[nodiscard]] std::set<std::int64_t> expand(std::size_t limit) const
    {
        switch (kind)
        {
        case Kind::Leaf:
            return values;
        case Kind::Add: {
            std::set<std::int64_t> out;
            const auto             l = lhs->expand(limit);
            const auto             r = rhs->expand(limit);
            for (const auto lv : l)
            {
                for (const auto rv : r)
                {
                    out.insert(lv + rv);
                    if (out.size() >= limit)
                    {
                        return out;
                    }
                }
            }
            return out;
        }
        case Kind::Union: {
            auto       out = lhs->expand(limit);
            const auto r   = rhs->expand(limit);
            out.insert(r.begin(), r.end());
            while (out.size() > limit)
            {
                out.erase(std::prev(out.end()));
            }
            return out;
        }
        case Kind::Pad: {
            std::set<std::int64_t> out;
            const auto             l = lhs->expand(limit);
            const auto             a = std::max<std::int64_t>(1, param);
            for (auto v : l)
            {
                const auto rem = v % a;
                if (rem != 0)
                {
                    v += (a - rem);
                }
                out.insert(v);
                if (out.size() >= limit)
                {
                    return out;
                }
            }
            return out;
        }
        case Kind::Repeat: {
            if (param <= 0)
            {
                return {0};
            }
            auto       acc  = std::set<std::int64_t>{0};
            const auto item = lhs->expand(limit);
            for (std::int64_t i = 0; i < param; ++i)
            {
                std::set<std::int64_t> next;
                for (const auto a : acc)
                {
                    for (const auto b : item)
                    {
                        next.insert(a + b);
                        if (next.size() >= limit)
                        {
                            break;
                        }
                    }
                    if (next.size() >= limit)
                    {
                        break;
                    }
                }
                acc = std::move(next);
            }
            return acc;
        }
        case Kind::RepeatRange: {
            std::set<std::int64_t> out{0};
            auto                   acc      = std::set<std::int64_t>{0};
            const auto             item     = lhs->expand(limit);
            const auto             maxCount = std::max<std::int64_t>(0, param);
            for (std::int64_t i = 1; i <= maxCount; ++i)
            {
                std::set<std::int64_t> next;
                for (const auto a : acc)
                {
                    for (const auto b : item)
                    {
                        next.insert(a + b);
                        if (next.size() >= limit)
                        {
                            break;
                        }
                    }
                    if (next.size() >= limit)
                    {
                        break;
                    }
                }
                out.insert(next.begin(), next.end());
                while (out.size() > limit)
                {
                    out.erase(std::prev(out.end()));
                }
                acc = std::move(next);
            }
            return out;
        }
        }
        return {0};
    }

    /// @brief Exact residue mask `{ v mod modulus : v in S }`, computed symbolically.
    ///
    /// Works entirely in the `ResidueSet` bitmask domain and never enumerates S: every
    /// intermediate result is a subset of Z/modulus (at most `modulus` bits), so it is exact and
    /// cheap even when S is astronomically large. This is what makes `modulo()` complete (fixes
    /// BLS-D1). Accumulates into `out` (so `Union` is a plain mask union of the two children).
    ///
    /// Per-kind derivation (modulus `m >= 1`):
    ///   - Leaf: reduce each stored value mod `m`.
    ///   - Add: Minkowski sum of the children's residues mod `m` — exact because
    ///     `(x + y) mod m` depends only on `x mod m` and `y mod m`.
    ///   - Union: union of the children's residues.
    ///   - Pad: `roundUp(v, a) mod m` depends on `v mod lcm(a, m)`, so the child is evaluated at
    ///     the widened modulus `m' = lcm(a, m)` and each representative is padded then reduced.
    ///   - Repeat / RepeatRange: cycle-detecting sumset iteration over Z/m (see the helpers).
    ///
    /// @return False when a required modulus exceeds `kResidueModulusCap` (the caller then falls
    ///         back to the `expand()`-based approximation); on false, `out` is left partial and
    ///         must be discarded.
    [[nodiscard]] bool residues(std::int64_t modulus, ResidueSet& out) const
    {
        if (modulus > kResidueModulusCap)
        {
            return false;
        }
        switch (kind)
        {
        case Kind::Leaf: {
            for (const auto v : values)
            {
                out.add(((v % modulus) + modulus) % modulus);  // normalize sign into [0, m)
            }
            if (values.empty())
            {
                out.add(0);  // invariant I1: an empty leaf denotes {0}
            }
            return true;
        }
        case Kind::Add: {
            ResidueSet l(modulus);
            ResidueSet r(modulus);
            if (!lhs->residues(modulus, l) || !rhs->residues(modulus, r))
            {
                return false;
            }
            out.unionWith(minkowskiSumMod(l, r));
            return true;
        }
        case Kind::Union:
            return lhs->residues(modulus, out) && rhs->residues(modulus, out);
        case Kind::Pad: {
            const auto   a = std::max<std::int64_t>(1, param);
            std::int64_t widened{};
            if (!cappedLcm(a, modulus, widened))
            {
                return false;
            }
            ResidueSet child(widened);
            if (!lhs->residues(widened, child))
            {
                return false;
            }
            child.forEach([&](const std::int64_t r) {
                const auto rem    = r % a;
                const auto padded = (rem == 0) ? r : r + (a - rem);
                out.add(padded % modulus);
            });
            return true;
        }
        case Kind::Repeat: {
            ResidueSet r(modulus);
            if (!lhs->residues(modulus, r))
            {
                return false;
            }
            out.unionWith(exactlyKSumMod(r, std::max<std::int64_t>(0, param), modulus));
            return true;
        }
        case Kind::RepeatRange: {
            ResidueSet r(modulus);
            if (!lhs->residues(modulus, r))
            {
                return false;
            }
            out.unionWith(unionUpToKSumMod(r, std::max<std::int64_t>(0, param), modulus));
            return true;
        }
        }
        out.add(0);
        return true;
    }

    /// @brief Renders the expression structure (not the expanded set); grammar in the header.
    ///
    /// Leaf values print ascending (std::set order); `param` prints post-clamping. Diagnostic
    /// aid only — not a stable serialization format.
    [[nodiscard]] std::string str() const
    {
        std::ostringstream out;
        switch (kind)
        {
        case Kind::Leaf: {
            out << '{';
            bool first = true;
            for (auto v : values)
            {
                if (!first)
                {
                    out << ',';
                }
                out << v;
                first = false;
            }
            out << '}';
            break;
        }
        case Kind::Add:
            out << "concat(" << lhs->str() << "," << rhs->str() << ")";
            break;
        case Kind::Union:
            out << "union(" << lhs->str() << "," << rhs->str() << ")";
            break;
        case Kind::Pad:
            out << "pad(" << lhs->str() << "," << param << ")";
            break;
        case Kind::Repeat:
            out << "repeat(" << lhs->str() << "," << param << ")";
            break;
        case Kind::RepeatRange:
            out << "repeat_range(" << lhs->str() << "," << param << ")";
            break;
        }
        return out.str();
    }
};

BitLengthSet::BitLengthSet()
    : root_(std::make_shared<Node>())
{
    auto leaf    = std::make_shared<Node>();
    leaf->kind   = Node::Kind::Leaf;
    leaf->values = {0};
    root_        = leaf;
}

BitLengthSet::BitLengthSet(std::int64_t value)
    : BitLengthSet(std::set<std::int64_t>{value})
{
}

BitLengthSet::BitLengthSet(std::set<std::int64_t> values)
{
    auto leaf    = std::make_shared<Node>();
    leaf->kind   = Node::Kind::Leaf;
    leaf->values = std::move(values);
    // Invariant I1: the denoted set is never empty; an empty input denotes {0}.
    if (leaf->values.empty())
    {
        leaf->values.insert(0);
    }
    root_ = leaf;
}

BitLengthSet::BitLengthSet(std::shared_ptr<const Node> root)
    : root_(std::move(root))
{
}

std::int64_t BitLengthSet::min() const
{
    return root_->min();
}

std::int64_t BitLengthSet::max() const
{
    return root_->max();
}

bool BitLengthSet::fixed() const
{
    return min() == max();
}

BitLengthSet BitLengthSet::padToAlignment(std::int64_t alignment) const
{
    auto node  = std::make_shared<Node>();
    node->kind = Node::Kind::Pad;
    node->lhs  = root_;
    // Contract: alignment < 1 is silently clamped to 1, making the operation the identity.
    node->param = std::max<std::int64_t>(1, alignment);
    return BitLengthSet(node);
}

BitLengthSet BitLengthSet::repeat(std::int64_t count) const
{
    auto node  = std::make_shared<Node>();
    node->kind = Node::Kind::Repeat;
    node->lhs  = root_;
    // Contract: count < 0 is silently clamped to 0, denoting {0} (the empty concatenation).
    node->param = std::max<std::int64_t>(0, count);
    return BitLengthSet(node);
}

BitLengthSet BitLengthSet::repeatRange(std::int64_t countMax) const
{
    auto node  = std::make_shared<Node>();
    node->kind = Node::Kind::RepeatRange;
    node->lhs  = root_;
    // Contract: countMax < 0 is silently clamped to 0, denoting {0} (only the k = 0 term).
    node->param = std::max<std::int64_t>(0, countMax);
    return BitLengthSet(node);
}

std::set<std::int64_t> BitLengthSet::modulo(std::int64_t divisor) const
{
    // Contract: a non-positive divisor yields the sentinel {0} instead of dividing by zero.
    if (divisor <= 0)
    {
        return {0};
    }
    // Primary path: exact per-node symbolic residues in a dense bitmask (bounded by `divisor`,
    // never truncated). This makes the result complete for every set, however large — the fix
    // for BLS-D1.
    ResidueSet mask(divisor);
    if (root_->residues(divisor, mask))
    {
        return toStdSet(mask);
    }
    // Fallback (not reached for realistic, alignment-driven divisors): symbolic evaluation
    // required a modulus exceeding kResidueModulusCap. Degrade to the older expand()-based
    // approximation, which may be incomplete for very large sets. See kResidueModulusCap.
    std::set<std::int64_t> out;
    const auto             expanded = expand();
    for (const auto v : expanded)
    {
        out.insert(v % divisor);
    }
    return out;
}

std::set<std::int64_t> BitLengthSet::expand(std::size_t limit) const
{
    return root_->expand(limit);
}

std::string BitLengthSet::str() const
{
    return root_->str();
}

BitLengthSet operator+(const BitLengthSet& lhs, const BitLengthSet& rhs)
{
    auto node  = std::make_shared<BitLengthSet::Node>();
    node->kind = BitLengthSet::Node::Kind::Add;
    node->lhs  = lhs.root_;
    node->rhs  = rhs.root_;
    return BitLengthSet(node);
}

BitLengthSet operator|(const BitLengthSet& lhs, const BitLengthSet& rhs)
{
    auto node  = std::make_shared<BitLengthSet::Node>();
    node->kind = BitLengthSet::Node::Kind::Union;
    node->lhs  = lhs.root_;
    node->rhs  = rhs.root_;
    return BitLengthSet(node);
}

}  // namespace llvmdsdl
