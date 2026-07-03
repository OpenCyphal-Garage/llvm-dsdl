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
#include <bit>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace llvmdsdl
{

namespace
{

/// @brief Saturating signed addition for the non-negative bit-length domain.
///
/// On overflow the result clamps to `INT64_MAX` instead of wrapping, so a pathological definition
/// (huge `@extent`, array capacity, or deep nesting) yields a defined, saturated length rather
/// than signed-overflow undefined behaviour (BLS-D6). Operands are non-negative bit lengths (the
/// constructors clamp negatives to 0 — BLS-D5), so overflow is always toward `INT64_MAX`.
/// Realistic bit lengths are far below the ceiling, so this never triggers in practice.
inline std::int64_t satAdd(const std::int64_t a, const std::int64_t b)
{
#if defined(__GNUC__) || defined(__clang__)
    std::int64_t r = 0;
    if (__builtin_add_overflow(a, b, &r))
    {
        return std::numeric_limits<std::int64_t>::max();
    }
    return r;
#else
    const std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    if (a > 0 && b > 0 && a > kMax - b)
    {
        return kMax;
    }
    return a + b;
#endif
}

/// @brief Saturating signed multiplication for the non-negative bit-length domain (see `satAdd`).
inline std::int64_t satMul(const std::int64_t a, const std::int64_t b)
{
#if defined(__GNUC__) || defined(__clang__)
    std::int64_t r = 0;
    if (__builtin_mul_overflow(a, b, &r))
    {
        return std::numeric_limits<std::int64_t>::max();
    }
    return r;
#else
    const std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    if (a != 0 && b > kMax / a)
    {
        return kMax;
    }
    return a * b;
#endif
}

/// @brief Rounds `v >= 0` up to the next multiple of `a >= 1`, saturating on overflow (see `satAdd`).
inline std::int64_t satRoundUp(const std::int64_t v, const std::int64_t a)
{
    const std::int64_t rem = v % a;
    return rem == 0 ? v : satAdd(v, a - rem);
}

/// @brief Hard ceiling on any modulus used during symbolic residue evaluation (`modulo()`).
///
/// Alignment-driven divisors in real DSDL are powers of two (<= 64), and the only operation
/// that widens the working modulus is `Pad`, which lifts it to `lcm(alignment, divisor)` —
/// still a small power of two. This cap is never approached in practice; it exists solely so
/// the pathological case (a `Pad` whose alignment is coprime to a large divisor) bails out to
/// the `expand()`-based fallback instead of allocating an enormous residue set. It also bounds a
/// `ResidueSet` to `kResidueModulusCap` bits = 8 KiB.
constexpr std::int64_t kResidueModulusCap = 1 << 16;

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
                // std::countr_zero is total (returns 64 for 0) and needs no non-zero precondition,
                // though the loop guard already keeps `w` non-zero here.
                fn(static_cast<std::int64_t>(wi * 64) + std::countr_zero(w));
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
/// Evaluation is ITERATIVE and MEMOIZED: `min`/`max`/`expand`/`str`/`residues` first collect the
/// distinct reachable nodes in post-order (`collectPostOrder`) and then compute each node once
/// from its already-computed children. This visits a subgraph shared by m paths a single time
/// (not m — fixes the exponential blow-up BLS-D9) and uses no call-stack recursion, so arbitrarily
/// deep expressions do not overflow the stack (BLS-D10). Teardown is likewise iterative — see the
/// custom destructor — so destroying a deep chain does not recurse either.
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

    Node() = default;

    /// @brief Iterative teardown so destroying a deep chain does not recurse (BLS-D10).
    ///
    /// The default destructor would release `lhs`/`rhs` recursively: freeing the head of an
    /// N-deep chain would nest N destructor calls and overflow the stack. Instead, move children
    /// into an explicit worklist and release them one at a time; whenever we hold the last
    /// reference to a node, steal ITS children first so its own destruction finds nothing to
    /// recurse into. The `const_cast` is safe: `use_count() == 1` means we are the sole owner of a
    /// node that is about to be destroyed, so mutating it is unobservable.
    ~Node()
    {
        std::vector<std::shared_ptr<const Node>> pending;
        if (lhs)
        {
            pending.push_back(std::move(lhs));
        }
        if (rhs)
        {
            pending.push_back(std::move(rhs));
        }
        while (!pending.empty())
        {
            std::shared_ptr<const Node> node = std::move(pending.back());
            pending.pop_back();
            if (node.use_count() == 1)
            {
                Node* const owned = const_cast<Node*>(node.get());
                if (owned->lhs)
                {
                    pending.push_back(std::move(owned->lhs));
                }
                if (owned->rhs)
                {
                    pending.push_back(std::move(owned->rhs));
                }
            }
            // `node` drops here; its children (if any) were already stolen, so no recursion.
        }
    }

    /// @brief Collects every distinct node reachable from `root` in post-order (children before
    ///        parents), deduplicating shared subgraphs. Iterative — no recursion (BLS-D9/D10).
    [[nodiscard]] static std::vector<const Node*> collectPostOrder(const Node* root)
    {
        std::vector<const Node*>                  order;
        std::unordered_set<const Node*>           seen;
        std::vector<std::pair<const Node*, bool>> stack;
        stack.emplace_back(root, false);
        while (!stack.empty())
        {
            const auto entry = stack.back();
            stack.pop_back();
            if (entry.second)
            {
                order.push_back(entry.first);
                continue;
            }
            if ((entry.first == nullptr) || !seen.insert(entry.first).second)
            {
                continue;
            }
            stack.emplace_back(entry.first, true);
            if (entry.first->lhs)
            {
                stack.emplace_back(entry.first->lhs.get(), false);
            }
            if (entry.first->rhs)
            {
                stack.emplace_back(entry.first->rhs.get(), false);
            }
        }
        return order;
    }

    /// @brief Exact symbolic minimum of S (never enumerates; invariant I4).
    ///
    /// Per-kind derivation, exact on the non-negative value domain:
    ///   Leaf: smallest stored value. Add: min(lhs) + min(rhs). Union: min of the two minima.
    ///   Pad: roundUp(min(lhs), a) — correct because rounding-up is monotone.
    ///   Repeat: param * min(lhs) — picking the minimum for every draw minimizes the sum.
    ///   RepeatRange: 0 — the k = 0 term; the smallest element only for non-negative domains.
    /// Sums and products saturate at INT64_MAX rather than overflowing (BLS-D6). The
    /// `values.empty()` guard on Leaf is defensive: public constructors never produce an empty
    /// leaf (I1).
    [[nodiscard]] std::int64_t min() const
    {
        const auto                                    order = collectPostOrder(this);
        std::unordered_map<const Node*, std::int64_t> memo;
        memo.reserve(order.size() * 2);
        for (const Node* const n : order)
        {
            std::int64_t v = 0;
            switch (n->kind)
            {
            case Kind::Leaf:
                v = n->values.empty() ? 0 : *n->values.begin();
                break;
            case Kind::Add:
                v = satAdd(memo.at(n->lhs.get()), memo.at(n->rhs.get()));
                break;
            case Kind::Union:
                v = std::min(memo.at(n->lhs.get()), memo.at(n->rhs.get()));
                break;
            case Kind::Pad:
                v = satRoundUp(memo.at(n->lhs.get()), std::max<std::int64_t>(1, n->param));
                break;
            case Kind::Repeat:
                v = satMul(memo.at(n->lhs.get()), std::max<std::int64_t>(0, n->param));
                break;
            case Kind::RepeatRange:
                v = 0;
                break;
            }
            memo.emplace(n, v);
        }
        return memo.at(this);
    }

    /// @brief Exact symbolic maximum of S (never enumerates; invariant I4).
    ///
    /// Mirrors `min()`: Leaf takes the largest stored value; Add sums the maxima; Union takes
    /// the larger maximum; Pad rounds the child maximum up (monotone); Repeat and RepeatRange
    /// both yield param * max(lhs) — for RepeatRange this is the k = param term, the maximum
    /// only on the non-negative value domain. Iterative and memoized (BLS-D9/D10).
    [[nodiscard]] std::int64_t max() const
    {
        const auto                                    order = collectPostOrder(this);
        std::unordered_map<const Node*, std::int64_t> memo;
        memo.reserve(order.size() * 2);
        for (const Node* const n : order)
        {
            std::int64_t v = 0;
            switch (n->kind)
            {
            case Kind::Leaf:
                v = n->values.empty() ? 0 : *n->values.rbegin();
                break;
            case Kind::Add:
                v = satAdd(memo.at(n->lhs.get()), memo.at(n->rhs.get()));
                break;
            case Kind::Union:
                v = std::max(memo.at(n->lhs.get()), memo.at(n->rhs.get()));
                break;
            case Kind::Pad:
                v = satRoundUp(memo.at(n->lhs.get()), std::max<std::int64_t>(1, n->param));
                break;
            case Kind::Repeat:
            case Kind::RepeatRange:
                v = satMul(memo.at(n->lhs.get()), std::max<std::int64_t>(0, n->param));
                break;
            }
            memo.emplace(n, v);
        }
        return memo.at(this);
    }

    /// @brief Materializes S bottom-up with a completeness flag, capping intermediates at `limit`.
    ///
    /// Returns `{values, exact}` where `values` is a subset of S with `|values| <= limit` and
    /// `exact` is true iff `values == S`. `limit` is assumed `>= 1` (the public entry point
    /// clamps it), which keeps every result non-empty (invariant I1) and fixes the former
    /// empty-set corner (BLS-D3). Truncation favours the smaller elements and always flips
    /// `exact` to false. Per-kind:
    ///
    ///   - Leaf: the stored set, truncated to its smallest `limit` values if larger (BLS-D4);
    ///     exact iff it fit.
    ///   - Add: distinct sums of the children's (already capped) expansions, keeping the smallest
    ///     `limit`; exact iff both children are exact and no (limit+1)-th distinct sum appeared.
    ///   - Union: union of the children, trimmed to the smallest `limit`; exact iff both children
    ///     are exact and no trim was needed.
    ///   - Pad: each child value rounded up; non-expansive, so it never itself truncates; exact
    ///     iff the child is exact.
    ///   - Repeat / RepeatRange: iterated self-sum, but bounded to O(convergence) rounds rather
    ///     than O(`param`) so a huge count cannot hang the compiler (BLS-D8). The item is shifted
    ///     to contain 0 (`V' = item - min`), which makes the shifted k-fold sumset monotone in k;
    ///     its smallest-`limit` view reaches a fixpoint, and `Repeat` stops there and adds
    ///     `param * min` back. `RepeatRange` additionally stops once every later term's minimum
    ///     (`k * min`) has moved past the kept window. Exact iff the child is exact and no round
    ///     overflowed `limit`.
    /// @brief Computes one node's expansion from its children's (already-computed) expansions.
    ///
    /// Same per-kind logic as the recursive form, but children are read from `memo` (populated in
    /// post-order) instead of recursing — so `expandChecked` visits each node once (BLS-D9) and
    /// uses no call stack (BLS-D10). See the method doc for the per-kind semantics and BLS-D8's
    /// bounded `Repeat`/`RepeatRange`.
    [[nodiscard]] static BitLengthSet::Expansion expandNode(
        const Node*                                                     n,
        std::size_t                                                     limit,
        const std::unordered_map<const Node*, BitLengthSet::Expansion>& memo)
    {
        switch (n->kind)
        {
        case Kind::Leaf: {
            if (n->values.size() <= limit)
            {
                return {n->values, true};
            }
            std::set<std::int64_t> out(n->values.begin(),
                                       std::next(n->values.begin(), static_cast<std::ptrdiff_t>(limit)));
            return {std::move(out), false};
        }
        case Kind::Add: {
            const auto&            l    = memo.at(n->lhs.get());
            const auto&            r    = memo.at(n->rhs.get());
            const std::int64_t     rmin = *r.values.begin();  // r is sorted and non-empty (I1)
            std::set<std::int64_t> out;
            bool                   overflow = false;
            // `l.values`/`r.values` are sorted ascending. Once `out` holds the smallest `limit`
            // sums its max only shrinks, so we can prune whole rows/tails whose sums already exceed
            // it — bounding the common (truncating) case to O(limit) instead of |l|*|r| (BLS-D11).
            // The remaining exact case (both children ~limit/2 with a minimal sumset) is inherently
            // quadratic in |l|*|r|, capped at ~(limit/2)^2, since the full sumset must be visited.
            for (const auto lv : l.values)
            {
                if (out.size() >= limit && satAdd(lv, rmin) > *out.rbegin())
                {
                    overflow = true;  // this lv's smallest sum is past the window; so is every later lv
                    break;
                }
                for (const auto rv : r.values)
                {
                    const auto s = satAdd(lv, rv);
                    if (out.size() >= limit && s > *out.rbegin())
                    {
                        overflow = true;  // a real sum beyond the kept window -> proper subset
                        break;            // rv ascending, so the remaining sums are larger too
                    }
                    out.insert(s);
                    if (out.size() > limit)
                    {
                        out.erase(std::prev(out.end()));  // keep the smallest `limit`
                        overflow = true;
                    }
                }
            }
            return {std::move(out), l.exact && r.exact && !overflow};
        }
        case Kind::Union: {
            const auto&            l   = memo.at(n->lhs.get());
            const auto&            r   = memo.at(n->rhs.get());
            std::set<std::int64_t> out = l.values;
            out.insert(r.values.begin(), r.values.end());
            bool overflow = false;
            while (out.size() > limit)
            {
                out.erase(std::prev(out.end()));
                overflow = true;
            }
            return {std::move(out), l.exact && r.exact && !overflow};
        }
        case Kind::Pad: {
            const auto&            l = memo.at(n->lhs.get());
            const auto             a = std::max<std::int64_t>(1, n->param);
            std::set<std::int64_t> out;
            for (const auto v : l.values)
            {
                out.insert(satRoundUp(v, a));  // padding is non-expansive: |out| <= |l.values|
            }
            return {std::move(out), l.exact};
        }
        case Kind::Repeat: {
            if (n->param <= 0)
            {
                return {{0}, true};
            }
            const auto&        item  = memo.at(n->lhs.get());
            bool               exact = item.exact;
            const std::int64_t base  = *item.values.begin();  // min; item.values is non-empty (I1)
            // Work in the 0-shifted domain V' = { v - base }, which contains 0. The exactly-k
            // sumset of V' is then monotone in k, so its smallest-`limit` view reaches a fixpoint
            // in O(convergence) rounds; detecting it removes the O(param) dependence (BLS-D8).
            std::set<std::int64_t> shifted;
            for (const auto v : item.values)
            {
                shifted.insert(v - base);
            }
            auto acc = std::set<std::int64_t>{0};  // A'_0
            for (std::int64_t i = 0; i < n->param; ++i)
            {
                std::set<std::int64_t> next;
                bool                   overflow = false;
                for (const auto a : acc)
                {
                    for (const auto b : shifted)
                    {
                        next.insert(satAdd(a, b));
                        if (next.size() > limit)
                        {
                            next.erase(std::prev(next.end()));
                            overflow = true;
                            break;
                        }
                    }
                    if (overflow)
                    {
                        break;
                    }
                }
                exact                = exact && !overflow;
                const bool converged = (next == acc);  // fixpoint: further rounds reproduce `acc`
                acc                  = std::move(next);
                if (converged)
                {
                    break;
                }
            }
            // Undo the shift: the exactly-param sumset of the original set is `param*base + A'_param`.
            const std::int64_t     shift = satMul(n->param, base);
            std::set<std::int64_t> out;
            for (const auto x : acc)
            {
                out.insert(satAdd(shift, x));
            }
            return {std::move(out), exact};
        }
        case Kind::RepeatRange: {
            const auto maxCount = std::max<std::int64_t>(0, n->param);
            if (maxCount == 0)
            {
                return {{0}, true};
            }
            const auto&            item  = memo.at(n->lhs.get());
            bool                   exact = item.exact;
            const std::int64_t     base  = *item.values.begin();  // min; item.values is non-empty (I1)
            std::set<std::int64_t> shifted;
            for (const auto v : item.values)
            {
                shifted.insert(v - base);
            }
            std::set<std::int64_t> out{0};                           // the k = 0 term
            auto                   acc = std::set<std::int64_t>{0};  // A'_0
            for (std::int64_t i = 1; i <= maxCount; ++i)
            {
                // Term k = i starts at `i*base`; once that passes the kept window, and every later
                // term starts even higher, none can enter the smallest-`limit` result (BLS-D8).
                // Those later terms are non-empty elements of S that we are dropping, so the result
                // is a proper subset — mark it inexact before bailing.
                const std::int64_t shift = satMul(i, base);  // term k = i is `shift + A'_i`
                if (base > 0 && out.size() >= limit && shift > *out.rbegin())
                {
                    exact = false;
                    break;
                }
                std::set<std::int64_t> next;
                bool                   overflow = false;
                for (const auto a : acc)
                {
                    for (const auto b : shifted)
                    {
                        next.insert(satAdd(a, b));
                        if (next.size() > limit)
                        {
                            next.erase(std::prev(next.end()));
                            overflow = true;
                            break;
                        }
                    }
                    if (overflow)
                    {
                        break;
                    }
                }
                for (const auto x : next)  // A_i (original) = i*base + A'_i
                {
                    out.insert(satAdd(shift, x));
                    if (out.size() > limit)
                    {
                        out.erase(std::prev(out.end()));
                        overflow = true;
                    }
                }
                exact                = exact && !overflow;
                const bool converged = (next == acc);
                acc                  = std::move(next);
                // base == 0: every term equals A'_i and out is their nested union, so once the
                // shifted sumset is a fixpoint the union can no longer grow.
                if (base == 0 && converged)
                {
                    break;
                }
            }
            return {std::move(out), exact};
        }
        }
        return {{0}, true};
    }

    /// @brief Iterative, memoized driver for `expandNode` (BLS-D9/D10).
    [[nodiscard]] BitLengthSet::Expansion expandChecked(std::size_t limit) const
    {
        const auto                                               order = collectPostOrder(this);
        std::unordered_map<const Node*, BitLengthSet::Expansion> memo;
        memo.reserve(order.size() * 2);
        for (const Node* const n : order)
        {
            memo.emplace(n, expandNode(n, limit, memo));
        }
        return std::move(memo.at(this));
    }

    /// @brief Exact residue mask `{ v mod modulus : v in S }`, computed symbolically.
    ///
    /// Works entirely in the `ResidueSet` bitmask domain and never enumerates S: every
    /// intermediate result is a subset of Z/modulus (at most `modulus` bits), so it is exact and
    /// cheap even when S is astronomically large. This is what makes `modulo()` complete (fixes
    /// BLS-D1). On success the root's residues are unioned into `out`.
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
    ///         back to the `expand()`-based approximation); on false, `out` is left unchanged.
    ///
    /// Iterative and memoized over `(node, modulus)` contexts (a `Pad` evaluates its child at the
    /// widened modulus `lcm(a, modulus)`, so one node can appear at several moduli): this visits a
    /// shared subgraph once per distinct context (BLS-D9) and uses no call stack (BLS-D10). The
    /// root's residues are unioned into `out` on success.
    [[nodiscard]] bool residues(std::int64_t modulus, ResidueSet& out) const
    {
        using Ctx = std::pair<const Node*, std::int64_t>;  // (node, working modulus)

        // Discovery: collect (node, modulus) contexts in post-order, deduplicated. Bail if any
        // working modulus would exceed the cap (a `Pad` with an alignment coprime to a large mod).
        std::vector<Ctx>                  order;
        std::set<Ctx>                     seen;
        std::vector<std::pair<Ctx, bool>> stack;
        stack.push_back({{this, modulus}, false});
        while (!stack.empty())
        {
            const auto item = stack.back();
            stack.pop_back();
            const Node* const  node = item.first.first;
            const std::int64_t mod  = item.first.second;
            if (mod > kResidueModulusCap)
            {
                return false;
            }
            if (item.second)
            {
                order.push_back(item.first);
                continue;
            }
            if (!seen.insert(item.first).second)
            {
                continue;
            }
            stack.push_back({item.first, true});
            switch (node->kind)
            {
            case Kind::Leaf:
                break;
            case Kind::Add:
            case Kind::Union:
                stack.push_back({{node->lhs.get(), mod}, false});
                stack.push_back({{node->rhs.get(), mod}, false});
                break;
            case Kind::Pad: {
                std::int64_t widened = 0;
                if (!cappedLcm(std::max<std::int64_t>(1, node->param), mod, widened))
                {
                    return false;
                }
                stack.push_back({{node->lhs.get(), widened}, false});
                break;
            }
            case Kind::Repeat:
            case Kind::RepeatRange:
                stack.push_back({{node->lhs.get(), mod}, false});
                break;
            }
        }

        // Evaluation: compute each context once, children before parents.
        std::map<Ctx, ResidueSet> memo;
        for (const auto& ctx : order)
        {
            const Node* const  node = ctx.first;
            const std::int64_t mod  = ctx.second;
            ResidueSet         rs(mod);
            switch (node->kind)
            {
            case Kind::Leaf:
                for (const auto v : node->values)
                {
                    rs.add(((v % mod) + mod) % mod);  // normalize sign into [0, mod)
                }
                if (node->values.empty())
                {
                    rs.add(0);  // invariant I1: an empty leaf denotes {0}
                }
                break;
            case Kind::Add:
                rs = minkowskiSumMod(memo.at({node->lhs.get(), mod}), memo.at({node->rhs.get(), mod}));
                break;
            case Kind::Union:
                rs.unionWith(memo.at({node->lhs.get(), mod}));
                rs.unionWith(memo.at({node->rhs.get(), mod}));
                break;
            case Kind::Pad: {
                const auto   a       = std::max<std::int64_t>(1, node->param);
                std::int64_t widened = 0;
                (void) cappedLcm(a, mod, widened);  // succeeded during discovery
                memo.at({node->lhs.get(), widened}).forEach([&](const std::int64_t r) {
                    const auto rem    = r % a;
                    const auto padded = (rem == 0) ? r : r + (a - rem);
                    rs.add(padded % mod);
                });
                break;
            }
            case Kind::Repeat:
                rs.unionWith(
                    exactlyKSumMod(memo.at({node->lhs.get(), mod}), std::max<std::int64_t>(0, node->param), mod));
                break;
            case Kind::RepeatRange:
                rs.unionWith(
                    unionUpToKSumMod(memo.at({node->lhs.get(), mod}), std::max<std::int64_t>(0, node->param), mod));
                break;
            }
            memo.emplace(ctx, std::move(rs));
        }

        out.unionWith(memo.at({this, modulus}));
        return true;
    }

    /// @brief Renders the expression structure (not the expanded set); grammar in the header.
    ///
    /// Leaf values print ascending (std::set order); `param` prints post-clamping. Diagnostic
    /// aid only — not a stable serialization format.
    [[nodiscard]] std::string str() const
    {
        // Iterative token stack so a deep chain does not overflow the call stack (BLS-D10). Each
        // frame is either "expand this node" (non-null node) or "emit this literal" (null node).
        // Composite tokens are pushed in REVERSE so the LIFO stack emits them left-to-right; this
        // reproduces the recursive tree rendering exactly (shared subgraphs still print per
        // occurrence, since `str()` renders the tree, not the DAG).
        std::ostringstream                               out;
        std::vector<std::pair<const Node*, std::string>> stack;
        const auto pushLit  = [&stack](std::string s) { stack.emplace_back(nullptr, std::move(s)); };
        const auto pushNode = [&stack](const Node* p) { stack.emplace_back(p, std::string{}); };
        stack.emplace_back(this, std::string{});
        while (!stack.empty())
        {
            const auto frame = std::move(stack.back());
            stack.pop_back();
            const Node* const n = frame.first;
            if (n == nullptr)
            {
                out << frame.second;
                continue;
            }
            switch (n->kind)
            {
            case Kind::Leaf: {
                out << '{';
                bool first = true;
                for (const auto v : n->values)
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
                pushLit(")");
                pushNode(n->rhs.get());
                pushLit(",");
                pushNode(n->lhs.get());
                pushLit("concat(");
                break;
            case Kind::Union:
                pushLit(")");
                pushNode(n->rhs.get());
                pushLit(",");
                pushNode(n->lhs.get());
                pushLit("union(");
                break;
            case Kind::Pad:
                pushLit(")");
                pushLit("," + std::to_string(n->param));
                pushNode(n->lhs.get());
                pushLit("pad(");
                break;
            case Kind::Repeat:
                pushLit(")");
                pushLit("," + std::to_string(n->param));
                pushNode(n->lhs.get());
                pushLit("repeat(");
                break;
            case Kind::RepeatRange:
                pushLit(")");
                pushLit("," + std::to_string(n->param));
                pushNode(n->lhs.get());
                pushLit("repeat_range(");
                break;
            }
        }
        return out.str();
    }
};

BitLengthSet::BitLengthSet()
    : root_(zeroLeaf())  // share the process-wide {0} leaf — no allocation (BLS-D14)
{
}

BitLengthSet::BitLengthSet(std::int64_t value)
    : BitLengthSet(std::set<std::int64_t>{value})
{
}

BitLengthSet::BitLengthSet(std::set<std::int64_t> values)
{
    auto leaf  = std::make_shared<Node>();
    leaf->kind = Node::Kind::Leaf;
    // Value domain: bit lengths are non-negative. A negative element is a caller precondition
    // violation; clamp it to 0 so the set stays in-domain and the saturating arithmetic in
    // min()/max()/expand() remains sound (BLS-D5). Realistic inputs never hit this.
    bool hasNegative = false;
    for (const auto v : values)
    {
        if (v < 0)
        {
            hasNegative = true;
            break;
        }
    }
    if (hasNegative)
    {
        for (const auto v : values)
        {
            leaf->values.insert(v < 0 ? 0 : v);
        }
    }
    else
    {
        leaf->values = std::move(values);
    }
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

const std::shared_ptr<const BitLengthSet::Node>& BitLengthSet::zeroLeaf()
{
    // One immutable, shared {0} leaf for the whole process. Immutability makes sharing safe, and
    // resetting a moved-from source to it is a refcount bump — moves stay O(1) and allocation-free.
    static const std::shared_ptr<const Node> leaf = [] {
        auto n    = std::make_shared<Node>();
        n->kind   = Node::Kind::Leaf;
        n->values = {0};
        return std::shared_ptr<const Node>(std::move(n));
    }();
    return leaf;
}

BitLengthSet::BitLengthSet(BitLengthSet&& other) noexcept
    : root_(std::move(other.root_))
{
    // Leave the source denoting {0} rather than null, so any later use is well-defined (BLS-D7).
    other.root_ = zeroLeaf();
}

BitLengthSet& BitLengthSet::operator=(BitLengthSet&& other) noexcept
{
    if (this != &other)
    {
        root_       = std::move(other.root_);
        other.root_ = zeroLeaf();
    }
    return *this;
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

bool BitLengthSet::is_aligned_at(std::int64_t alignment) const
{
    if (alignment < 1)
    {
        return true;  // trivially aligned
    }
    // Exact, since modulo() is exact: aligned iff every possible length reduces to residue 0.
    const auto residues = modulo(alignment);
    return residues.size() == 1 && *residues.begin() == 0;
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
    return expandChecked(limit).values;
}

BitLengthSet::Expansion BitLengthSet::expandChecked(std::size_t limit) const
{
    // Clamp to >= 1 so the result is never empty (invariant I1; fixes BLS-D3's expand(0) corner).
    return root_->expandChecked(std::max<std::size_t>(1, limit));
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

bool operator==(const BitLengthSet& lhs, const BitLengthSet& rhs)
{
    // Cheap necessary conditions first; then require both to expand exactly to the same values.
    // `exact` on both sides is what makes this sound (no false positive): two sets that agree only
    // on their smallest `limit` elements but differ beyond it are not declared equal. See header.
    if (lhs.min() != rhs.min() || lhs.max() != rhs.max())
    {
        return false;
    }
    const auto l = lhs.expandChecked();
    const auto r = rhs.expandChecked();
    return l.exact && r.exact && l.values == r.values;
}

bool operator!=(const BitLengthSet& lhs, const BitLengthSet& rhs)
{
    return !(lhs == rhs);
}

}  // namespace llvmdsdl
