//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements the exact arithmetic-progression-run set representation.
///
/// Everything here follows one rule: EXACT OR REFUSE. Arithmetic goes through checked helpers
/// that fail on int64 overflow instead of saturating, and structure-multiplying operations
/// charge a budget that fails the operation instead of truncating the result. See RunSet.h for
/// the representation invariants (sorted by start, pairwise set-disjoint, never empty).
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Semantics/RunSet.h"
#include "llvmdsdl/Support/IntegerMath.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <numeric>
#include <utility>

namespace llvmdsdl
{
namespace
{

/// Constructs a normalized representable run. This is the single validation point for runs
/// synthesized by arithmetic operations; callers refuse when it returns nullopt.
std::optional<Run> makeRun(std::int64_t start, std::int64_t stride, std::int64_t count)
{
    if (count < 1 || stride < 1)
    {
        return std::nullopt;
    }
    if (count == 1)
    {
        return Run{start, 1, 1};
    }
    const __int128 span = static_cast<__int128>(count - 1) * stride;
    if (span > std::numeric_limits<std::int64_t>::max() ||
        static_cast<__int128>(start) + span > std::numeric_limits<std::int64_t>::max())
    {
        return std::nullopt;
    }
    return Run{start, stride, count};
}

/// Constructs the run from `start` through `last`, inclusive.
std::optional<Run> makeRunThrough(std::int64_t start, std::int64_t stride, std::int64_t last)
{
    if (stride < 1 || last < start)
    {
        return std::nullopt;
    }
    const __int128 distance = static_cast<__int128>(last) - start;
    if (distance % stride != 0)
    {
        return std::nullopt;
    }
    const __int128 count = distance / stride + 1;
    if (count > std::numeric_limits<std::int64_t>::max())
    {
        return std::nullopt;
    }
    return makeRun(start, stride, static_cast<std::int64_t>(count));
}

/// Extended gcd: returns g = gcd(a, b) and Bezout x with a*x === g (mod b). Inputs positive.
/// __int128 intermediates keep the Bezout coefficients from overflowing for int64 inputs.
std::int64_t extendedGcd(std::int64_t a, std::int64_t b, __int128& x)
{
    __int128 x0 = 1;
    __int128 x1 = 0;
    __int128 r0 = a;
    __int128 r1 = b;
    while (r1 != 0)
    {
        const __int128 q = r0 / r1;
        r0 -= q * r1;
        std::swap(r0, r1);
        x0 -= q * x1;
        std::swap(x0, x1);
    }
    x = x0;
    return static_cast<std::int64_t>(r0);
}

/// The intersection of two runs is itself an arithmetic progression: elements satisfying both
/// congruences x === a.start (mod a.stride) and x === b.start (mod b.stride) form (by CRT) a
/// progression with stride lcm(a.stride, b.stride), clipped to the ranges of both runs. Returns
/// nullopt when the intersection is empty; a Run when it is not. `overflowed` is set (and
/// nullopt returned) when lcm or clip arithmetic leaves int64 — callers treat that as budget
/// failure, keeping the exactness contract.
std::optional<Run> intersectRuns(const Run& a, const Run& b, bool& overflowed)
{
    overflowed = false;

    // Range pre-clip: the intersection lives in [lo, hi].
    const std::int64_t lo = std::max(a.start, b.start);
    const std::int64_t hi = std::min(a.last(), b.last());
    if (lo > hi)
    {
        return std::nullopt;
    }

    // Solve x === a.start (mod da), x === b.start (mod db).
    const std::int64_t da   = a.stride;
    const std::int64_t db   = b.stride;
    __int128           bez  = 0;
    const std::int64_t g    = extendedGcd(da, db, bez);
    const __int128     diff = static_cast<__int128>(b.start) - a.start;  // __int128: safe for any int64 pair
    if (diff % g != 0)
    {
        return std::nullopt;  // congruences incompatible: empty intersection
    }
    const __int128 lcm128 = (static_cast<__int128>(da) / g) * db;
    if (lcm128 > INT64_MAX)
    {
        // Stride beyond int64: at most one element can satisfy both congruences inside the
        // clipped range (the range span is < int64 while the period exceeds it). Find it.
        // x = a.start + da * t where t === (diff/g) * inv(da/g) (mod db/g).
        const __int128 m  = db / g;
        __int128       t0 = (diff / g) % m * (((bez % m) + m) % m) % m;
        t0                = ((t0 % m) + m) % m;
        const __int128 x0 = a.start + static_cast<__int128>(da) * t0;
        if (x0 < lo || x0 > hi)
        {
            return std::nullopt;
        }
        return makeRun(static_cast<std::int64_t>(x0), 1, 1);
    }
    const std::int64_t lcm = static_cast<std::int64_t>(lcm128);

    // Smallest solution of the pair of congruences, then advance into [lo, hi].
    const __int128 m  = db / g;
    __int128       t0 = (diff / g) % m * (((bez % m) + m) % m) % m;
    t0                = ((t0 % m) + m) % m;
    __int128 x0       = a.start + static_cast<__int128>(da) * t0;  // smallest x >= a.start
    if (x0 < lo)
    {
        const __int128 steps = (static_cast<__int128>(lo) - x0 + lcm - 1) / lcm;
        x0 += steps * lcm;
    }
    if (x0 > hi)
    {
        return std::nullopt;
    }
    const __int128 n = (static_cast<__int128>(hi) - x0) / lcm + 1;
    if (n == 1)
    {
        return makeRun(static_cast<std::int64_t>(x0), 1, 1);
    }
    const auto run = makeRun(static_cast<std::int64_t>(x0), lcm, static_cast<std::int64_t>(n));
    if (!run)
    {
        overflowed = true;
    }
    return run;
}

/// Exact set difference `r \ cut` where `cut` is a sub-progression of `r` (cut.stride is a
/// multiple of r.stride and every cut element lies on r). Emits the surviving elements as at
/// most `cut.stride / r.stride + 1` runs: the head before the cut, the interleaved residue
/// classes the cut skips, and the tail after it. Charges one budget unit per emitted run;
/// returns false on budget exhaustion.
bool subtractSubProgression(const Run& r, const Run& cut, std::vector<Run>& out, std::size_t& budget)
{
    const auto emit = [&](Run piece) -> bool {
        if (budget == 0)
        {
            return false;
        }
        --budget;
        const auto checked = makeRun(piece.start, piece.stride, piece.count);
        if (!checked)
        {
            return false;
        }
        out.push_back(*checked);
        return true;
    };

    // Head: elements of r strictly before the first cut element.
    if (cut.start > r.start)
    {
        const std::int64_t n = (cut.start - r.start) / r.stride;  // exact: cut lies on r
        if (!emit(Run{r.start, r.stride, n}))
        {
            return false;
        }
    }

    // Body: between consecutive cut elements, (q - 1) survivors where q = cut.stride/r.stride.
    const std::int64_t q = cut.stride / r.stride;
    if (q > 1 && cut.count >= 1)
    {
        // Survivors form q-1 residue classes of stride cut.stride, each spanning the cut range.
        // Class j (1 <= j < q) starts at cut.start + j*r.stride and has cut.count - 1 elements
        // between cut elements, plus possibly one more if the class extends past the last cut
        // element while remaining within r — the tail handling below covers that region, so
        // clip each class at the last cut element.
        for (std::int64_t j = 1; j < q; ++j)
        {
            // Checked: an overflowing classStart is logically "past r.last()" (bounded by
            // cut.start + cut.stride under the sub-progression precondition, but proven by
            // arithmetic here rather than by argument).
            std::int64_t classOffset = 0;
            std::int64_t classStart  = 0;
            if (!checkedMultiply(j, r.stride, classOffset) || !checkedAdd(cut.start, classOffset, classStart) ||
                classStart > r.last())
            {
                break;
            }
            // Elements of this class before the last cut element (they alternate with cut
            // elements, one per cut gap).
            const std::int64_t n = std::min(cut.count - 1, (r.last() - classStart) / cut.stride + 1);
            if (n >= 1)
            {
                if (!emit(Run{classStart, cut.stride, n}))
                {
                    return false;
                }
            }
        }
    }

    // Tail: elements of r strictly after the last cut element. Checked: r.last() can be
    // INT64_MAX (a legal element), in which case cut.last() + r.stride overflows and the
    // correct reading is simply "no tail".
    std::int64_t afterCut = 0;
    if (checkedAdd(cut.last(), r.stride, afterCut) && afterCut <= r.last())
    {
        const std::int64_t n = (r.last() - afterCut) / r.stride + 1;
        if (!emit(Run{afterCut, r.stride, n}))
        {
            return false;
        }
    }
    return true;
}

}  // namespace

RunSet::RunSet(std::int64_t value)
{
    runs_.push_back(Run{value, 1, 1});
}

RunSet RunSet::fromValues(const FlatSet<std::int64_t>& values)
{
    RunSet out;
    if (values.empty())
    {
        out.runs_.push_back(Run{0, 1, 1});  // I1 coercion, mirroring BitLengthSet
        return out;
    }
    // Greedy maximal runs over the sorted input: extend while the gap stays constant. The gap
    // is computed in 128-bit: two extreme int64 inputs can be further apart than int64 can
    // represent, and such a pair must simply stay two singleton runs (the representability
    // invariant) rather than form a run whose stride silently overflowed. Each extension also
    // validates the complete run span; a larger evenly spaced sequence may require multiple
    // representable runs.
    auto it = values.begin();
    Run  cur{*it, 1, 1};
    ++it;
    for (; it != values.end(); ++it)
    {
        const __int128 gap = static_cast<__int128>(*it) - cur.last();
        if (cur.count == 1 && gap <= std::numeric_limits<std::int64_t>::max())
        {
            cur.stride = static_cast<std::int64_t>(gap);
            cur.count  = 2;
        }
        else if (cur.count > 1 && gap == cur.stride)
        {
            std::int64_t nextCount = 0;
            const auto   extended =
                checkedAdd(cur.count, 1, nextCount) ? makeRun(cur.start, cur.stride, nextCount) : std::optional<Run>{};
            if (extended)
            {
                cur = *extended;
            }
            else
            {
                out.runs_.push_back(cur);
                cur = Run{*it, 1, 1};
            }
        }
        else
        {
            out.runs_.push_back(cur);
            cur = Run{*it, 1, 1};
        }
    }
    out.runs_.push_back(cur);
    return out;
}

std::optional<std::int64_t> RunSet::count() const
{
    std::int64_t total = 0;
    for (const auto& r : runs_)
    {
        if (!checkedAdd(total, r.count, total))
        {
            return std::nullopt;
        }
    }
    return total;
}

std::int64_t RunSet::min() const
{
    return runs_.front().start;  // sorted by start; never empty
}

std::int64_t RunSet::max() const
{
    std::int64_t m = runs_.front().last();
    for (const auto& r : runs_)
    {
        m = std::max(m, r.last());
    }
    return m;
}

bool RunSet::contains(std::int64_t value) const
{
    for (const auto& r : runs_)
    {
        if (value < r.start || value > r.last())
        {
            continue;
        }
        if ((value - r.start) % r.stride == 0)
        {
            return true;
        }
    }
    return false;
}

bool RunSet::isSubsetOf(const RunSet& other) const
{
    // |r intersect other| computed as a sum over other's runs is exact because other's runs are
    // pairwise set-disjoint (representation invariant). r is a subset iff nothing is lost.
    for (const auto& r : runs_)
    {
        std::int64_t covered = 0;
        for (const auto& o : other.runs_)
        {
            bool       overflowed = false;
            const auto isect      = intersectRuns(r, o, overflowed);
            if (overflowed)
            {
                return false;  // cannot verify => not provably subset; callers treat as failure
            }
            if (isect)
            {
                if (!checkedAdd(covered, isect->count, covered))
                {
                    return false;  // cannot verify without overflowing => not provably subset
                }
            }
        }
        if (covered != r.count)
        {
            return false;
        }
    }
    return true;
}

bool RunSet::equals(const RunSet& other) const
{
    const auto ca = count();
    const auto cb = other.count();
    if (ca && cb && *ca != *cb)
    {
        return false;
    }
    return isSubsetOf(other) && other.isSubsetOf(*this);
}

std::optional<RunSet> RunSet::shifted(std::int64_t delta) const
{
    RunSet out;
    out.runs_.reserve(runs_.size());
    for (auto r : runs_)
    {
        if (!checkedAdd(r.start, delta, r.start))
        {
            return std::nullopt;
        }
        std::int64_t lastCheck = 0;
        if (!checkedMultiply(r.count - 1, r.stride, lastCheck) || !checkedAdd(r.start, lastCheck, lastCheck))
        {
            return std::nullopt;
        }
        out.runs_.push_back(r);
    }
    return out;
}

bool RunSet::insertRun(Run run, std::size_t& budget)
{
    const auto checked = makeRun(run.start, run.stride, run.count);
    if (!checked)
    {
        return false;
    }
    run = *checked;
    // Work queue of pieces still to place: overlap resolution may split a piece into fragments
    // that must each be re-tested against the existing runs.
    std::vector<Run> pending{run};
    while (!pending.empty())
    {
        if (budget == 0)
        {
            return false;
        }
        --budget;
        Run  piece    = pending.back();
        bool consumed = false;
        pending.pop_back();

        for (std::size_t i = 0; i < runs_.size(); ++i)
        {
            const Run& existing = runs_[i];
            if (piece.last() < existing.start || piece.start > existing.last())
            {
                continue;  // ranges disjoint => sets disjoint
            }
            bool       overflowed = false;
            const auto isect      = intersectRuns(piece, existing, overflowed);
            if (overflowed)
            {
                return false;
            }
            if (!isect)
            {
                continue;  // ranges overlap but sets do not
            }
            if (isect->count == piece.count)
            {
                consumed = true;  // piece entirely contained in an existing run: nothing to add
                break;
            }
            // Remove the shared elements from the piece and retry the fragments. The
            // intersection is a sub-progression of the piece by construction (its stride is a
            // multiple of piece.stride and its elements lie on the piece).
            std::vector<Run> fragments;
            if (!subtractSubProgression(piece, *isect, fragments, budget))
            {
                return false;
            }
            pending.insert(pending.end(), fragments.begin(), fragments.end());
            consumed = true;
            break;
        }
        if (consumed)
        {
            continue;
        }
        // No overlap with any existing run: insert preserving start order.
        const auto pos = std::lower_bound(runs_.begin(), runs_.end(), piece.start, [](const Run& r, std::int64_t s) {
            return r.start < s;
        });
        runs_.insert(pos, piece);
    }
    return true;
}

void RunSet::coalesce()
{
    if (runs_.size() < 2)
    {
        return;
    }
    std::vector<Run> merged;
    merged.reserve(runs_.size());
    merged.push_back(runs_.front());
    for (std::size_t i = 1; i < runs_.size(); ++i)
    {
        Run&       cur  = merged.back();
        const Run& next = runs_[i];
        // Same greedy rules as fromValues, generalized to runs: merge only structurally
        // contiguous same-stride (or singleton) neighbours. Purely compaction; the denoted
        // set is unchanged, and failing to merge is always safe.
        // All probes are checked: a run may legally end at INT64_MAX, where last() + stride
        // overflows (reads as "not adjacent"), and a singleton pair whose gap exceeds int64
        // must stay two runs (the representability invariant) rather than merge.
        std::int64_t gap = 0;
        if (cur.count == 1 && next.count == 1 && next.start > cur.start &&
            cur.start != std::numeric_limits<std::int64_t>::min() && checkedAdd(next.start, -cur.start, gap))
        {
            cur.stride = gap;
            cur.count  = 2;
            continue;
        }
        std::int64_t adjacent    = 0;
        std::int64_t mergedCount = 0;
        if (cur.count >= 2 && next.count == 1 && checkedAdd(cur.last(), cur.stride, adjacent) &&
            next.start == adjacent && checkedAdd(cur.count, 1, mergedCount))
        {
            if (const auto run = makeRun(cur.start, cur.stride, mergedCount))
            {
                cur = *run;
                continue;
            }
        }
        if (cur.count == 1 && next.count >= 2 && checkedAdd(cur.start, next.stride, adjacent) &&
            next.start == adjacent && checkedAdd(next.count, 1, mergedCount))
        {
            if (const auto run = makeRun(cur.start, next.stride, mergedCount))
            {
                cur = *run;
                continue;
            }
        }
        if (cur.count >= 2 && next.count >= 2 && next.stride == cur.stride &&
            checkedAdd(cur.last(), cur.stride, adjacent) && next.start == adjacent &&
            checkedAdd(cur.count, next.count, mergedCount))
        {
            if (const auto run = makeRun(cur.start, cur.stride, mergedCount))
            {
                cur = *run;
                continue;
            }
        }
        merged.push_back(next);
    }
    runs_ = std::move(merged);
}

namespace
{

/// Enumeration ceiling for the small-set shortcut in `unite`/`sum`. Small ragged sets (many
/// near-singleton runs) are cheaper — and structurally simpler — to combine by enumerating
/// values and re-decomposing greedily than by CRT overlap resolution; the shortcut is exactly
/// equivalent, it merely chooses the representation-friendly path. Large sets skip it and use
/// the structural path, whose closed forms are what make huge STRUCTURED sets tractable.
constexpr std::int64_t kEnumUnionLimit = 131072;
constexpr std::int64_t kEnumSumLimit   = 65536;    // bound on |A| * |B| for pairwise enumeration
constexpr std::int64_t kSumSpanLimit   = 1 << 20;  // bound on result span for the bitmap path

}  // namespace

std::optional<RunSet> RunSet::unite(const RunSet& a, const RunSet& b)
{
    // Small-set shortcut: enumerate, merge, re-decompose. Exact and cheap for ragged sets.
    const auto   ca       = a.count();
    const auto   cb       = b.count();
    std::int64_t combined = 0;
    if (ca && cb && checkedAdd(*ca, *cb, combined) && combined <= kEnumUnionLimit)
    {
        const auto ma = a.materialize(static_cast<std::size_t>(kEnumUnionLimit));
        const auto mb = b.materialize(static_cast<std::size_t>(kEnumUnionLimit));
        if (ma && mb)
        {
            FlatSet<std::int64_t> merged = *ma;
            merged.insert(mb->begin(), mb->end());
            return fromValues(merged);
        }
    }
    RunSet      out    = a;
    std::size_t budget = kOpBudget;
    for (const auto& r : b.runs_)
    {
        if (!out.insertRun(r, budget))
        {
            return std::nullopt;
        }
    }
    out.coalesce();
    return out;
}

std::optional<RunSet> RunSet::sum(const RunSet& a, const RunSet& b)
{
    // Small-SPAN shortcut: when the result fits a modest interval, mark sums in a span bitmap
    // and re-decompose greedily. Exact, and cheap precisely where the structural path is weak:
    // ragged operands whose sumset saturates the interval (the early-saturation break makes the
    // dense case nearly free). An iteration cap bounds the adversarial sparse-but-collisive
    // case; on cap it falls THROUGH to the structural path rather than failing.
    const auto ca = a.count();
    const auto cb = b.count();
    {
        const __int128 lo   = static_cast<__int128>(a.min()) + b.min();
        const __int128 hi   = static_cast<__int128>(a.max()) + b.max();
        const __int128 span = hi - lo + 1;
        if (lo >= INT64_MIN && hi <= INT64_MAX && span <= kSumSpanLimit)
        {
            // Word-parallel sumset: build the smaller operand's occupancy bitmap once, then for
            // every element x of the other operand OR that bitmap, shifted by x, into the result
            // — 64 result positions per word operation. Cost is |A| * span(B)/64 word-ops, which
            // handles the nearly-dense mid-size sets (the structural path's weak spot) in
            // microseconds. A word-op cap bounds the adversarial corner; on cap it falls through
            // to the remaining strategies rather than failing.
            const RunSet& small =
                (static_cast<__int128>(a.max()) - a.min() <= static_cast<__int128>(b.max()) - b.min()) ? a : b;
            const RunSet&         large       = (&small == &a) ? b : a;
            const std::int64_t    spanSmall   = small.max() - small.min() + 1;
            const auto            mSmall      = small.materialize(static_cast<std::size_t>(kSumSpanLimit));
            const auto            mLarge      = large.materialize(static_cast<std::size_t>(kSumSpanLimit));
            constexpr std::size_t kWordOpsCap = 1U << 26U;
            const std::size_t     smallWords  = (static_cast<std::size_t>(spanSmall) + 63U) / 64U;
            if (mSmall && mLarge && mLarge->size() * (smallWords + 1) <= kWordOpsCap)
            {
                std::vector<std::uint64_t> smallBits(smallWords, 0);
                for (const auto v : *mSmall)
                {
                    const auto rel = static_cast<std::size_t>(v - small.min());
                    smallBits[rel / 64U] |= (std::uint64_t{1} << (rel % 64U));
                }
                const std::size_t          resultWords = (static_cast<std::size_t>(span) + 63U) / 64U;
                std::vector<std::uint64_t> result(resultWords + 1, 0);  // +1: shifted-OR spill slot
                for (const auto x : *mLarge)
                {
                    const auto offset = static_cast<std::size_t>((x + small.min()) - static_cast<std::int64_t>(lo));
                    const std::size_t wordShift = offset / 64U;
                    const unsigned    bitShift  = static_cast<unsigned>(offset % 64U);
                    for (std::size_t w = 0; w < smallWords; ++w)
                    {
                        result[wordShift + w] |= smallBits[w] << bitShift;
                        if (bitShift != 0)
                        {
                            result[wordShift + w + 1] |= smallBits[w] >> (64U - bitShift);
                        }
                    }
                }
                std::vector<std::int64_t> values;
                for (std::size_t w = 0; w < result.size(); ++w)
                {
                    std::uint64_t bits = result[w];
                    while (bits != 0)
                    {
                        const auto bit = static_cast<std::size_t>(std::countr_zero(bits));
                        bits &= bits - 1;
                        values.push_back(static_cast<std::int64_t>(lo) + static_cast<std::int64_t>(w * 64U + bit));
                    }
                }
                return fromValues(FlatSet<std::int64_t>(sorted_unique_t{}, values.begin(), values.end()));
            }
        }
    }
    // Small-PRODUCT shortcut: few pair sums, enumerate directly.
    if (ca && cb && *ca <= kEnumSumLimit && *cb <= kEnumSumLimit && *ca * *cb <= kEnumSumLimit)
    {
        const auto ma = a.materialize(static_cast<std::size_t>(kEnumSumLimit));
        const auto mb = b.materialize(static_cast<std::size_t>(kEnumSumLimit));
        if (ma && mb)
        {
            std::vector<std::int64_t> values;
            values.reserve(static_cast<std::size_t>(*ca * *cb));
            for (const auto x : *ma)
            {
                for (const auto y : *mb)
                {
                    std::int64_t s = 0;
                    if (!checkedAdd(x, y, s))
                    {
                        return std::nullopt;
                    }
                    values.push_back(s);
                }
            }
            std::sort(values.begin(), values.end());
            values.erase(std::unique(values.begin(), values.end()), values.end());
            return fromValues(FlatSet<std::int64_t>(sorted_unique_t{}, values.begin(), values.end()));
        }
    }
    RunSet      out;
    std::size_t budget = kOpBudget;
    bool        first  = true;
    for (const auto& ra : a.runs_)
    {
        for (const auto& rb : b.runs_)
        {
            // Pairwise run sumset, exact by cases.
            std::vector<Run> pieces;
            const auto       pushPiece = [&pieces](std::int64_t start, std::int64_t stride, std::int64_t count) {
                const auto run = makeRun(start, stride, count);
                if (run)
                {
                    pieces.push_back(*run);
                }
                return run.has_value();
            };
            std::int64_t s = 0;
            if (!checkedAdd(ra.start, rb.start, s))
            {
                return std::nullopt;
            }
            std::int64_t lastSum = 0;
            if (!checkedAdd(ra.last(), rb.last(), lastSum))
            {
                return std::nullopt;
            }
            if (ra.count == 1 && rb.count == 1)
            {
                if (!pushPiece(s, 1, 1))
                {
                    return std::nullopt;
                }
            }
            else if (ra.count == 1)
            {
                if (!pushPiece(s, rb.stride, rb.count))
                {
                    return std::nullopt;
                }
            }
            else if (rb.count == 1)
            {
                if (!pushPiece(s, ra.stride, ra.count))
                {
                    return std::nullopt;
                }
            }
            else if (ra.stride == rb.stride)
            {
                // Same stride: the sumset is one run of count na + nb - 1.
                std::int64_t joinedCount = 0;
                if (!checkedAdd(ra.count, rb.count, joinedCount) || !checkedAdd(joinedCount, -1, joinedCount))
                {
                    return std::nullopt;
                }
                const auto piece = makeRun(s, ra.stride, joinedCount);
                if (!piece)
                {
                    return std::nullopt;
                }
                pieces.push_back(*piece);
            }
            else
            {
                // Different strides: expand the smaller run into singleton shifts of the larger.
                const Run& big   = (ra.count >= rb.count) ? ra : rb;
                const Run& small = (ra.count >= rb.count) ? rb : ra;
                if (static_cast<std::size_t>(small.count) > budget)
                {
                    return std::nullopt;
                }
                for (std::int64_t i = 0; i < small.count; ++i)
                {
                    std::int64_t shift = 0;
                    if (!checkedMultiply(i, small.stride, shift) || !checkedAdd(s, shift, shift))
                    {
                        return std::nullopt;
                    }
                    if (!pushPiece(shift, big.stride, big.count))
                    {
                        return std::nullopt;
                    }
                }
            }
            (void) lastSum;  // computed purely as an overflow probe on the extremes
            for (const auto& piece : pieces)
            {
                if (first)
                {
                    out.runs_.push_back(piece);
                    first = false;
                }
                else if (!out.insertRun(piece, budget))
                {
                    return std::nullopt;
                }
            }
        }
    }
    out.coalesce();
    return out;
}

std::optional<RunSet> RunSet::paddedTo(std::int64_t alignment) const
{
    if (alignment <= 1)
    {
        return *this;
    }
    RunSet      out;
    std::size_t budget = kOpBudget;
    bool        first  = true;
    const auto  padUp  = [&](std::int64_t v, std::int64_t& padded) -> bool {
        // roundUp(v, a) for the non-negative domain used by bit lengths; negative v rounds
        // toward zero which is still exact ceil semantics for our purposes.
        const std::int64_t rem = v % alignment;
        if (rem == 0)
        {
            padded = v;
            return true;
        }
        if (v > 0)
        {
            return checkedAdd(v, alignment - rem, padded);
        }
        padded = v - rem;
        return true;
    };
    for (const auto& r : runs_)
    {
        std::vector<Run> pieces;
        const auto       pushPiece = [&pieces](std::int64_t start, std::int64_t stride, std::int64_t count) {
            const auto run = makeRun(start, stride, count);
            if (run)
            {
                pieces.push_back(*run);
            }
            return run.has_value();
        };
        if (r.stride % alignment == 0)
        {
            // Alignment divides the stride: padding shifts the whole run uniformly.
            std::int64_t s = 0;
            if (!padUp(r.start, s))
            {
                return std::nullopt;
            }
            if (!pushPiece(s, r.stride, r.count))
            {
                return std::nullopt;
            }
        }
        else
        {
            // Decompose by phase: elements congruent mod lcm(stride, alignment) pad uniformly.
            std::int64_t g      = std::gcd(r.stride, alignment);
            std::int64_t period = 0;
            if (!checkedMultiply(r.stride / g, alignment, period))
            {
                return std::nullopt;
            }
            const std::int64_t classes = period / r.stride;  // = alignment / g
            if (static_cast<std::size_t>(classes) > budget)
            {
                return std::nullopt;
            }
            for (std::int64_t j = 0; j < classes; ++j)
            {
                std::int64_t classStart = 0;
                if (!checkedMultiply(j, r.stride, classStart) || !checkedAdd(r.start, classStart, classStart))
                {
                    return std::nullopt;
                }
                if (classStart > r.last())
                {
                    break;
                }
                const std::int64_t n = (r.last() - classStart) / period + 1;
                std::int64_t       s = 0;
                if (!padUp(classStart, s))
                {
                    return std::nullopt;
                }
                if (!pushPiece(s, period, n))
                {
                    return std::nullopt;
                }
            }
        }
        for (const auto& piece : pieces)
        {
            if (first)
            {
                out.runs_.push_back(piece);
                first = false;
            }
            else if (!out.insertRun(piece, budget))
            {
                return std::nullopt;
            }
        }
    }
    out.coalesce();
    return out;
}

std::optional<RunSet> RunSet::repeated(std::int64_t count) const
{
    if (count <= 0)
    {
        return RunSet(0);
    }
    if (count == 1)
    {
        return *this;
    }
    // Linear iteration T_j = T_{j-1} + S with a dense-fixpoint jump. k-fold sumsets saturate:
    // once two CONSECUTIVE iterates are single runs with the same stride whose endpoints moved
    // by exactly (min, max), the recurrence is pinned forever — the second iterate being a
    // single stride-g run certifies that every element of S is congruent mod g (each shifted
    // copy of the previous dense run lands on the same residue), so every later sum shifts the
    // run by min and extends it by max, and the remaining iterations collapse to arithmetic.
    const std::int64_t     m              = min();
    const std::int64_t     M              = max();
    constexpr std::int64_t kMaxIterations = 4096;
    RunSet                 term           = *this;
    for (std::int64_t j = 2; j <= count; ++j)
    {
        const bool prevSingle = (term.runs_.size() == 1);
        const Run  prev       = term.runs_.front();
        auto       next       = sum(term, *this);
        if (!next)
        {
            return std::nullopt;
        }
        term = std::move(*next);
        if (prevSingle && term.runs_.size() == 1)
        {
            const Run&   cur           = term.runs_.front();
            std::int64_t expectedStart = 0;
            std::int64_t expectedLast  = 0;
            if (cur.stride == prev.stride && checkedAdd(prev.start, m, expectedStart) &&
                checkedAdd(prev.last(), M, expectedLast) && cur.start == expectedStart && cur.last() == expectedLast)
            {
                const std::int64_t remaining = count - j;
                if (remaining == 0)
                {
                    return term;
                }
                std::int64_t startShift = 0;
                std::int64_t endShift   = 0;
                std::int64_t start      = 0;
                std::int64_t end        = 0;
                if (!checkedMultiply(remaining, m, startShift) || !checkedAdd(cur.start, startShift, start) ||
                    !checkedMultiply(remaining, M, endShift) || !checkedAdd(cur.last(), endShift, end))
                {
                    return std::nullopt;
                }
                RunSet     out;
                const auto run = makeRunThrough(start, cur.stride, end);
                if (!run)
                {
                    return std::nullopt;
                }
                out.runs_.push_back(*run);
                return out;
            }
        }
        if (j >= kMaxIterations)
        {
            return std::nullopt;  // never converged within budget: refuse, do not approximate
        }
    }
    return term;
}

std::optional<RunSet> RunSet::repeatRange(std::int64_t countMax) const
{
    if (countMax <= 0)
    {
        return RunSet(0);
    }
    // Singleton {c}: the union is {0, c, 2c, ..., n*c} directly.
    const auto totalCount = count();
    if (totalCount && *totalCount == 1)
    {
        const std::int64_t c = min();
        if (c == 0)
        {
            return RunSet(0);
        }
        std::int64_t runCount = 0;
        if (!checkedAdd(countMax, 1, runCount))
        {
            return std::nullopt;
        }
        const __int128 span = static_cast<__int128>(countMax) * c;
        if (span < std::numeric_limits<std::int64_t>::min() || span > std::numeric_limits<std::int64_t>::max())
        {
            return std::nullopt;
        }
        const __int128 stride = c < 0 ? -static_cast<__int128>(c) : c;
        if (stride > std::numeric_limits<std::int64_t>::max())
        {
            if (countMax == 1)
            {
                return fromValues(FlatSet<std::int64_t>{static_cast<std::int64_t>(span), 0});
            }
            return std::nullopt;
        }
        const std::int64_t start = c < 0 ? static_cast<std::int64_t>(span) : 0;
        const auto         run   = makeRun(start, static_cast<std::int64_t>(stride), runCount);
        if (!run)
        {
            return std::nullopt;
        }
        RunSet out;
        out.runs_.push_back(*run);
        return out;
    }

    const std::int64_t m = min();
    const std::int64_t M = max();
    if (m < 0)
    {
        // The closed-form tail below relies on non-negative extrema. Signed sets use bounded
        // direct iteration; exhausting the bound is a refusal, never a partial union.
        constexpr std::int64_t kMaxSignedIterations = 4096;
        std::optional<RunSet>  acc                  = RunSet(0);
        RunSet                 term                 = *this;
        for (std::int64_t k = 1; k <= countMax; ++k)
        {
            acc = unite(*acc, term);
            if (!acc)
            {
                return std::nullopt;
            }
            if (k == countMax)
            {
                return acc;
            }
            if (k >= kMaxSignedIterations)
            {
                return std::nullopt;
            }
            auto next = sum(term, *this);
            if (!next)
            {
                return std::nullopt;
            }
            term = std::move(*next);
        }
    }

    // 0 in S: k-fold sums are monotone (T_{k-1} + 0 subset of T_k), so the union is repeat(n).
    if (contains(0))
    {
        return repeated(countMax);
    }

    // gcd of the element differences: the eventual dense stride of the k-fold sums.
    std::int64_t g = 0;
    for (const auto& r : runs_)
    {
        if (r.count > 1)
        {
            g = std::gcd(g, r.stride);
        }
        const __int128 startGap = static_cast<__int128>(r.start) - m;
        if (startGap < 0 || startGap > std::numeric_limits<std::int64_t>::max())
        {
            return std::nullopt;
        }
        g = std::gcd(g, static_cast<std::int64_t>(startGap));
    }
    if (g == 0)
    {
        g = 1;
    }

    // Iterate the k-fold sums, accumulating the union, until either k reaches countMax (small
    // ranges: exact by direct iteration) or T_k becomes a dense single run [k*m, k*M] stride g
    // (large ranges: the tail collapses to closed-form phase families below).
    std::optional<RunSet>  acc            = RunSet(0);
    RunSet                 term           = *this;  // T_1
    std::int64_t           k              = 1;
    constexpr std::int64_t kMaxIterations = 4096;
    for (;; ++k)
    {
        acc = unite(*acc, term);
        if (!acc)
        {
            return std::nullopt;
        }
        if (k == countMax)
        {
            return acc;
        }
        // Dense single-run detection: T_k == [k*m, k*M] step g exactly.
        std::int64_t km = 0;
        std::int64_t kM = 0;
        if (!checkedMultiply(k, m, km) || !checkedMultiply(k, M, kM))
        {
            return std::nullopt;
        }
        if (term.runs_.size() == 1)
        {
            const Run& r = term.runs_.front();
            if (r.start == km && r.last() == kM && (r.stride == g || r.count == 1))
            {
                break;  // dense: switch to closed form for the tail
            }
        }
        if (k >= kMaxIterations)
        {
            return std::nullopt;  // never converged within budget: refuse, do not approximate
        }
        auto next = sum(term, *this);
        if (!next)
        {
            return std::nullopt;
        }
        term = std::move(*next);
    }

    // Closed-form tail: for j in (k, countMax], T_j is the dense run [j*m, j*M] step g (density
    // is preserved once reached: shifting a dense run by every element of S and uniting keeps it
    // dense because S's internal gaps are at most M - m <= span of T_k). Group the j's by the
    // phase (j*m) mod g; within one family (j stepping by P = g / gcd(g, m)) successive runs
    // chain into a single run once (j+P)*m <= j*M + g, which is monotone in j. Emit pre-chain
    // runs individually and each chained family as one run.
    const std::int64_t mModG  = euclideanModulo(m, g);
    const std::int64_t P      = (mModG == 0) ? 1 : (g / std::gcd(g, mModG));
    std::size_t        budget = kOpBudget;
    std::int64_t       j      = k + 1;
    for (;;)
    {
        if (j > countMax)
        {
            break;
        }
        // Emit T_j individually until every family at or beyond j chains.
        std::int64_t jm                  = 0;
        std::int64_t jM                  = 0;
        std::int64_t nextSameFamilyStart = 0;
        if (!checkedMultiply(j, m, jm) || !checkedMultiply(j, M, jM))
        {
            return std::nullopt;
        }
        bool         chains         = false;
        std::int64_t nextSameFamily = 0;
        if (checkedAdd(j, P, nextSameFamily) && checkedMultiply(nextSameFamily, m, nextSameFamilyStart))
        {
            std::int64_t reach = 0;
            if (checkedAdd(jM, g, reach))
            {
                chains = nextSameFamilyStart <= reach;
            }
        }
        if (chains)
        {
            break;
        }
        if (j - k > kMaxIterations)
        {
            return std::nullopt;
        }
        const auto run = makeRunThrough(jm, g, jM);
        if (!run)
        {
            return std::nullopt;
        }
        RunSet dense;
        dense.runs_.push_back(*run);
        acc = unite(*acc, dense);
        if (!acc)
        {
            return std::nullopt;
        }
        if (j == countMax)
        {
            return acc;
        }
        ++j;
    }
    // Chained families: for each residue class of j in [j, countMax] modulo P, the union of its
    // dense runs is one run from the first j's start to the last j's end, stride g.
    for (std::int64_t f = 0; f < P; ++f)
    {
        std::int64_t j0 = 0;
        if (!checkedAdd(j, f, j0) || j0 > countMax)
        {
            break;
        }
        const std::int64_t jEnd = j0 + ((countMax - j0) / P) * P;
        std::int64_t       lo   = 0;
        std::int64_t       hi   = 0;
        if (!checkedMultiply(j0, m, lo) || !checkedMultiply(jEnd, M, hi))
        {
            return std::nullopt;
        }
        const auto run = makeRunThrough(lo, g, hi);
        if (!run)
        {
            return std::nullopt;
        }
        RunSet family;
        family.runs_.push_back(*run);
        acc = unite(*acc, family);
        if (!acc)
        {
            return std::nullopt;
        }
        if (budget == 0)
        {
            return std::nullopt;
        }
        --budget;
    }
    return acc;
}

std::optional<FlatSet<std::int64_t>> RunSet::residues(std::int64_t divisor) const
{
    if (divisor < 1)
    {
        return std::nullopt;
    }
    constexpr std::size_t kResidueBudget = 65536;
    FlatSet<std::int64_t> unique;
    for (const auto& r : runs_)
    {
        // Residues of start + i*stride (mod d) repeat with period d / gcd(stride mod d, d):
        // after that many steps the walk returns to its starting residue (it traverses the
        // coset of the subgroup generated by stride). Beyond the period no new residues appear.
        const std::int64_t step   = r.stride % divisor;
        const std::int64_t period = divisor / std::gcd(step, divisor);  // gcd(0, d) == d
        const std::int64_t k      = std::min(r.count, period);
        // The budget applies to the UNIQUE residue set (the documented contract). Within one
        // run the walked residues are pairwise distinct (the walk stops at the cycle length),
        // so a single run with k beyond the budget already proves the result exceeds it; the
        // cross-run duplicates are removed by the FlatSet before the budget is re-checked.
        if (static_cast<std::size_t>(k) > kResidueBudget)
        {
            return std::nullopt;  // this run alone contributes more distinct residues than the budget
        }
        std::vector<std::int64_t> batch;
        batch.reserve(static_cast<std::size_t>(k));
        std::int64_t cur = euclideanModulo(r.start, divisor);
        for (std::int64_t i = 0; i < k; ++i)
        {
            batch.push_back(cur);
            const __int128 next = static_cast<__int128>(cur) + step;
            cur                 = static_cast<std::int64_t>(next >= divisor ? next - divisor : next);
        }
        unique.insert(batch.begin(), batch.end());  // FlatSet range-insert sorts, merges, dedups
        if (unique.size() > kResidueBudget)
        {
            return std::nullopt;  // exact UNIQUE residue set larger than the output budget
        }
    }
    return unique;
}

std::optional<FlatSet<std::int64_t>> RunSet::materialize(std::size_t limit) const
{
    const auto total = count();
    if (!total || static_cast<std::size_t>(*total) > limit)
    {
        return std::nullopt;
    }
    std::vector<std::int64_t> values;
    values.reserve(static_cast<std::size_t>(*total));
    for (const auto& r : runs_)
    {
        for (std::int64_t i = 0; i < r.count; ++i)
        {
            values.push_back(r.start + i * r.stride);
        }
    }
    std::sort(values.begin(), values.end());
    return FlatSet<std::int64_t>(sorted_unique_t{}, values.begin(), values.end());
}

bool RunSet::valid() const
{
    if (runs_.empty())
    {
        return false;
    }
    for (std::size_t i = 0; i < runs_.size(); ++i)
    {
        const Run& r = runs_[i];
        if (r.count < 1 || r.stride < 1 || (r.count == 1 && r.stride != 1))
        {
            return false;
        }
        // Representability: last() and the span must fit int64 (audited in 128-bit).
        const __int128 span = static_cast<__int128>(r.count - 1) * r.stride;
        if (span > std::numeric_limits<std::int64_t>::max() ||
            static_cast<__int128>(r.start) + span > std::numeric_limits<std::int64_t>::max())
        {
            return false;
        }
        if (i > 0 && runs_[i - 1].start >= r.start)
        {
            return false;
        }
        for (std::size_t j = i + 1; j < runs_.size(); ++j)
        {
            bool       overflowed = false;
            const auto isect      = intersectRuns(r, runs_[j], overflowed);
            if (overflowed || isect)
            {
                return false;
            }
        }
    }
    return true;
}

}  // namespace llvmdsdl
