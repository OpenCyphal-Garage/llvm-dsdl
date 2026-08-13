//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Exact finite-integer-set representation as a union of arithmetic-progression runs.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SEMANTICS_RUNSET_H
#define LLVMDSDL_SEMANTICS_RUNSET_H

#include <cstdint>
#include <optional>
#include <vector>

#include "llvmdsdl/Support/FlatSet.h"

namespace llvmdsdl
{

/// @brief One arithmetic-progression run denoting `{ start + i * stride : 0 <= i < count }`.
///
/// Invariants (upheld by every RunSet operation; `RunSet::valid()` audits them):
///   - `count >= 1`;
///   - `stride >= 1`;
///   - `count == 1` implies `stride == 1` (a singleton's stride is meaningless; normalizing it
///     makes structural comparisons in tests deterministic);
///   - REPRESENTABILITY: every element — in particular `last()` — fits `int64`, and so does
///     the span `(count - 1) * stride`. Construction maintains this by checked arithmetic
///     (an operation that would create an unrepresentable run REFUSES instead), which is what
///     makes the unchecked arithmetic in `last()` safe.
struct Run final
{
    std::int64_t start{0};
    std::int64_t stride{1};
    std::int64_t count{1};

    /// @brief The largest element, `start + (count - 1) * stride`.
    /// @note Relies on the representability invariant above; `RunSet::valid()` audits it in
    ///       128-bit arithmetic.
    [[nodiscard]] std::int64_t last() const
    {
        return start + (count - 1) * stride;
    }
};

/// @brief An EXACT finite set of integers represented as a union of arithmetic-progression runs.
///
/// ## Purpose
///
/// `BitLengthSet::expand()` materializes a set element-by-element, so its cost and its exactness
/// ceiling scale with cardinality. A `RunSet` represents the same set in O(#runs) space: the
/// 9001-element offset set of `uint8[<=9000]` is ONE run `(16, 8, 9001)`, and so is the
/// billion-element analogue. Cardinality, membership, subset, and equality queries are answered
/// in closed form without enumeration, which is what makes `_offset_` evaluation exact at any
/// scale (the PR-2 exactness contract).
///
/// ## Exactness contract
///
/// Every operation either returns the EXACT result or returns `std::nullopt` because a
/// complexity budget or an `int64` range check tripped. There is no approximation mode: a
/// `RunSet` you hold always denotes precisely the set its constructor chain described. Budget
/// exhaustion is a resource guard against adversarial inputs (compile-time DoS), not a
/// correctness knob — callers must treat `nullopt` as "cannot evaluate", never as "close
/// enough". Unlike `BitLengthSet::expand()`, which saturates arithmetic at INT64_MAX
/// (documented clamp), RunSet REFUSES on overflow: a set it cannot represent exactly is a set
/// it will not represent at all.
///
/// ## Representation invariants
///
///   - `runs()` is non-empty (the denoted set is never empty, mirroring BitLengthSet I1);
///   - runs are sorted by strictly increasing `start`;
///   - runs are pairwise SET-disjoint (no element belongs to two runs) — this is what makes
///     `count()` a plain sum. Note ranges may still interleave: `{0,8,16} u {3,13,23}` is two
///     range-overlapping but set-disjoint runs.
///
/// The representation is NOT canonical: `{0,4,8,12}` may be held as one stride-4 run or as two
/// interleaved stride-8 runs depending on construction order. Consequently equality is decided
/// SEMANTICALLY by mutual containment, never by comparing run lists. Cardinality is only an
/// optional fast mismatch because a valid RunSet can contain more than `INT64_MAX` elements.
///
/// ## Complexity budget
///
/// Operations that can multiply run counts (union overlap resolution, sumsets of coprime-stride
/// runs, repeat-range iteration) charge a shared per-operation cost counter; when it exceeds
/// `kOpBudget` the operation returns `nullopt`. Realistic DSDL layouts stay minuscule: byte
/// alignment collapses nearly everything to stride-8 runs and the regulated corpus never exceeds
/// a handful of runs per set.
class RunSet final
{
public:
    /// @brief Per-operation complexity budget (produced runs + expanded elements + iterations).
    static constexpr std::size_t kOpBudget = 1U << 16U;

    /// @brief Constructs a singleton `{value}`.
    explicit RunSet(std::int64_t value);

    /// @brief Constructs the exact RunSet of an explicit value set via greedy maximal-run
    ///        decomposition. An empty input denotes `{0}` (I1 coercion, mirroring BitLengthSet).
    [[nodiscard]] static RunSet fromValues(const FlatSet<std::int64_t>& values);

    /// @name Exact queries (no enumeration, any cardinality)
    /// @{

    /// @brief Exact cardinality of the denoted set; `nullopt` only on int64 overflow of the sum.
    [[nodiscard]] std::optional<std::int64_t> count() const;

    /// @brief Smallest element (always defined; the set is never empty).
    [[nodiscard]] std::int64_t min() const;

    /// @brief Largest element (always defined; O(#runs) — ranges may interleave, so the last
    ///        run's end is not necessarily the global maximum).
    [[nodiscard]] std::int64_t max() const;

    /// @brief Exact membership test, O(#runs).
    [[nodiscard]] bool contains(std::int64_t value) const;

    /// @brief Exact subset test `this <= other`, O(#runs(this) * #runs(other)) CRT
    ///        intersections. Relies on `other`'s disjointness invariant to count by summation.
    [[nodiscard]] bool isSubsetOf(const RunSet& other) const;

    /// @brief Exact semantic equality by mutual containment; representation-independent.
    [[nodiscard]] bool equals(const RunSet& other) const;

    /// @brief Exact residue set `{ v mod divisor : v in S }` for any divisor, any cardinality.
    ///
    /// Per run, the residues of `start + i*stride (mod d)` cycle with period
    /// `d / gcd(stride, d)`, so each run contributes `min(count, period)` residues computed
    /// incrementally (no overflow, no enumeration of S). `nullopt` when the residue set itself
    /// would exceed the output budget (a huge divisor makes residues as numerous as the set) or
    /// `divisor < 1` — never an incomplete set.
    [[nodiscard]] std::optional<FlatSet<std::int64_t>> residues(std::int64_t divisor) const;
    /// @}

    /// @name Exact constructions (nullopt = budget or int64 range exceeded, never approximate)
    /// @{

    /// @brief `{ v + delta : v in S }`.
    [[nodiscard]] std::optional<RunSet> shifted(std::int64_t delta) const;

    /// @brief Set union.
    [[nodiscard]] static std::optional<RunSet> unite(const RunSet& a, const RunSet& b);

    /// @brief Minkowski sum `{ a + b : a in A, b in B }`.
    [[nodiscard]] static std::optional<RunSet> sum(const RunSet& a, const RunSet& b);

    /// @brief `{ roundUp(v, alignment) : v in S }`; `alignment < 1` is the identity map
    ///        (mirroring BitLengthSet::padToAlignment's clamp).
    [[nodiscard]] std::optional<RunSet> paddedTo(std::int64_t alignment) const;

    /// @brief Exact `count`-fold Minkowski self-sum (binary doubling; `count <= 0` denotes {0}).
    [[nodiscard]] std::optional<RunSet> repeated(std::int64_t count) const;

    /// @brief Exact `union over k in [0, countMax]` of the k-fold self-sum; always contains 0.
    ///
    /// Huge `countMax` is handled in closed form: iteration proceeds only until the k-fold sums
    /// become dense single runs whose successive terms chain, after which the remaining union
    /// collapses to at most `stride` phase-family runs computed arithmetically. A set whose
    /// iterates never converge within budget yields `nullopt`.
    [[nodiscard]] std::optional<RunSet> repeatRange(std::int64_t countMax) const;
    /// @}

    /// @brief Materializes the concrete elements when `count() <= limit`; `nullopt` otherwise
    ///        (the caller decides how to surface the refusal — this is a size guard on the
    ///        OUTPUT, which is inherently proportional to cardinality).
    [[nodiscard]] std::optional<FlatSet<std::int64_t>> materialize(std::size_t limit) const;

    /// @brief Read access to the runs (sorted by start, pairwise set-disjoint).
    [[nodiscard]] const std::vector<Run>& runs() const
    {
        return runs_;
    }

    /// @brief Audits the representation invariants (test support; O(#runs^2) CRT checks).
    [[nodiscard]] bool valid() const;

private:
    RunSet() = default;

    /// @brief Inserts `run` preserving sortedness and set-disjointness, resolving overlaps
    ///        exactly by AP subtraction; charges `budget`.
    [[nodiscard]] bool insertRun(Run run, std::size_t& budget);

    /// @brief Greedy merge of structurally adjacent runs (same stride and phase, contiguous);
    ///        keeps the representation compact. Purely a compaction — never changes the set.
    void coalesce();

    std::vector<Run> runs_;
};

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SEMANTICS_RUNSET_H
