//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Symbolic bit-length set declarations used by semantic analysis and layout reasoning.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SEMANTICS_BITLENGTHSET_H
#define LLVMDSDL_SEMANTICS_BITLENGTHSET_H

#include <cstdint>
#include <flat_set>
#include <memory>
#include <set>
#include <string>
#include <cstddef>

namespace llvmdsdl
{

/// @file
/// @brief Symbolic bit-length set algebra used by semantic analysis.

//===----------------------------------------------------------------------===//
// SPECIFICATION
//===----------------------------------------------------------------------===//
///
/// @brief Persistent symbolic set of possible serialized bit lengths.
///
/// ## Purpose and model
///
/// A `BitLengthSet` denotes a finite, non-empty set of integers `S`, where each element is a
/// possible length, in bits, of the serialized representation of a DSDL entity (a field, a
/// section, or a whole definition). It is the C++ analogue of pydsdl's `BitLengthSet` and
/// implements the length algebra required by the OpenCyphal Specification's serialization
/// rules (composition of fields, tagged unions, arrays, and alignment padding).
///
/// The representation is *symbolic*: operations do not materialize value sets, they build an
/// immutable expression graph (leaves hold explicit value sets; interior nodes denote sum,
/// union, padding, and repetition). `min()`, `max()`, and `fixed()` are answered from the
/// symbolic form without enumeration; `expand()` and `modulo()` materialize concrete values
/// subject to an expansion limit (see "Exactness model" below).
///
/// Denotationally, with `S(x)` the set denoted by object `x`:
///
///   - `BitLengthSet()`                 : S = {0}          (zero-length entity; NOT the empty set)
///   - `BitLengthSet(v)`                : S = {v}
///   - `BitLengthSet(values)`           : S = values, or {0} when `values` is empty
///   - `a + b`                          : S = { x + y : x in S(a), y in S(b) }   (Minkowski sum)
///   - `a | b`                          : S = S(a) union S(b)
///   - `x.padToAlignment(a)`            : S = { ceil(v / a) * a : v in S(x) }
///   - `x.repeat(k)`                    : S = { v1 + ... + vk : vi in S(x) },  k <= 0 gives {0}
///   - `x.repeatRange(k)`               : S = union of x.repeat(i) for i in [0, k]
///   - `x.modulo(d)`                    : { v mod d : v in S(x) },             d <= 0 gives {0}
///
/// ## Value domain (precondition)
///
/// Elements model bit counts: every value supplied to a constructor SHOULD be non-negative. A
/// negative element is a caller precondition violation; rather than leave behavior undefined, the
/// constructors clamp any negative value to 0, keeping the set in-domain so `min()`, `max()`, and
/// `modulo()` stay well-defined. Realistic inputs never trigger this.
///
/// Arithmetic is `std::int64_t` but SATURATES at `INT64_MAX` instead of overflowing: any derivable
/// value that would exceed the range — an intermediate sum `max(a) + max(b)`, a product
/// `max(x) * k`, or an alignment round-up — clamps to `INT64_MAX` rather than invoking
/// signed-overflow undefined behavior. Bit lengths of real definitions are far below the ceiling,
/// so saturation is a safety net, not an expected result; when it does engage, a saturated `max()`
/// simply reads as "astronomically large" to callers (e.g. extent checks).
///
/// The out-of-domain clamps throughout the API — negative element -> 0, `alignment < 1` -> 1,
/// `count < 0` -> 0, `divisor <= 0` -> `{0}` — are INTENTIONAL, defined recovery. This layer has no
/// diagnostic channel; validating inputs against DSDL limits is the caller's responsibility (e.g.
/// the analyzer), and the clamps guarantee this class stays well-defined regardless.
///
/// ## Invariants
///
///   - I1 (non-empty): S is never empty. The default constructor and the coercion of an empty
///     input set both yield {0}. Consequently `min()` and `max()` are always defined.
///   - I2 (ordered bounds): `min() <= max()` is guaranteed unconditionally, and on the
///     non-negative value domain both are true elements of S (the symbolic bounds are exact). In
///     the saturation regime (unreached by real inputs) a bound may read `INT64_MAX` without being
///     a true element of S.
///   - I3 (immutability / persistence): objects are immutable values. Every operation returns
///     a new object and never observes or mutates its operands afterwards. Copies are O(1)
///     and share structure safely.
///   - I4 (no hidden expansion): `min()`, `max()`, `fixed()`, and `str()` never enumerate S;
///     only `expand()` and `modulo()` do.
///
/// ## Algebraic laws (value-set semantics)
///
/// Where `==` compares denoted sets (e.g. via exact `expand()`):
///
///   - `+` is commutative and associative; `BitLengthSet(0)` (or `BitLengthSet()`) is its
///     identity: `a + BitLengthSet(0) == a`.
///   - `|` is commutative, associative, and idempotent: `a | a == a`.
///   - `+` distributes over `|`: `a + (b | c) == (a + b) | (a + c)`.
///   - `x.repeat(0) == BitLengthSet(0)`; `x.repeat(1) == x`;
///     `x.repeat(k) == x + x + ... + x` (k addends).
///   - `x.repeatRange(k)` always contains 0; `x.repeatRange(0) == BitLengthSet(0)`.
///   - `padToAlignment` is idempotent (`x.padToAlignment(a).padToAlignment(a) ==
///     x.padToAlignment(a)`), is the identity for `a == 1` and for already-aligned sets, and
///     every element of the result is a multiple of `a`.
///
/// ## Exactness model
///
///   - `min()` / `max()` / `fixed()` are EXACT (no truncation), for any set size.
///   - `expand(limit)` returns a subset of S ("sound under-approximation"):
///       * exact (== S) whenever the cardinality of every intermediate subexpression's set is
///         <= `limit`; in particular exact whenever |S| <= `limit` holds for every node of
///         the expression;
///       * when truncation occurs it keeps (at most) `limit` of the smaller elements; WHICH
///         subset survives across a truncating `Add`/`Repeat` is not fully specified, but the
///         result never exceeds `limit` elements and is never empty;
///       * `limit` is clamped to `>= 1` internally, so `expand(0)` returns a one-element sound
///         subset rather than the empty set (invariant I1).
///     `expand()` reports no completeness signal; use `expandChecked()` when the caller must
///     know whether the result equals S (e.g. before evaluating an assertion over the values).
///   - `expandChecked(limit)` returns the same values plus an `exact` flag that is true iff the
///     returned set equals S (no truncation occurred anywhere in the expression). This is the
///     exactness signal callers need to avoid silently reasoning over an incomplete set.
///   - `modulo(d)` returns the EXACT residue set of S for any set size. It is computed by
///     symbolic per-node residue propagation (each intermediate set is a subset of Z/d, so it
///     never truncates), NOT by expanding S. The sole exception is a defensive internal cap on
///     the working modulus (only ever widened by padding with an alignment coprime to `d`,
///     which does not occur for the power-of-two alignments used in practice): beyond that cap
///     the result degrades to the `expand()`-based approximation and may be incomplete.
///
/// ## Complexity and robustness caveats (as implemented)
///
///   - Construction operations (`+`, `|`, `padToAlignment`, `repeat`, `repeatRange`) are O(1):
///     they allocate one node and share children.
///   - `min()`, `max()`, `expand()`, `modulo()`, and `str()` evaluate ITERATIVELY and MEMOIZED:
///     each distinct node (for `modulo()`, each distinct `(node, modulus)`) is computed once, so a
///     heavily shared graph (e.g. `s = s + s` applied n times) costs O(nodes), not O(2^n). No
///     call-stack recursion is used, so arbitrarily deep expressions do not overflow the stack —
///     and neither does destruction, which is likewise iterative. `str()` renders the tree, so its
///     OUTPUT length is unbounded for a shared graph, though it uses bounded stack.
///   - `repeat(k)`/`repeatRange(k)` expansion is bounded to O(convergence) rounds — the truncated
///     shifted sumset reaches a fixpoint (and `repeatRange` also stops once later terms move past
///     the kept window) — so a large `k` does not drive the loop count. Cost depends on `limit`
///     and the item values, not on `k`. A single truncating `Add` can still cost up to O(limit^2)
///     in the rare case where both children have ~limit/2 elements forming a minimal sumset (the
///     full sumset must be visited); the common truncating case is O(limit).
///
/// ## Concurrency
///
/// After construction, a `BitLengthSet` is deeply immutable; all const member functions are
/// safe to call concurrently on the same or structure-sharing objects. The usual rules for
/// the object itself apply (do not assign to an object while another thread reads it).
///
/// ## Move semantics
///
/// A moved-from `BitLengthSet` is left denoting `{0}` — a valid, usable state (invariant I1),
/// not a null/unspecified one. Every member call on it is well-defined; `root_` is never null.
/// Moves remain O(1): the source is reset to a shared zero-leaf singleton, no allocation.
///
class BitLengthSet final
{
public:
    /// @brief Constructs the singleton set {0}.
    ///
    /// Note this denotes "the entity serializes to exactly zero bits", not "no information":
    /// the denoted set is never empty (invariant I1), and {0} is the identity of `operator+`.
    BitLengthSet();

    /// @name Special members
    /// Copies share structure (O(1)). Moves are O(1) and leave the source denoting `{0}` — a
    /// valid, usable state rather than a null one (see "Move semantics").
    /// @{
    BitLengthSet(const BitLengthSet&)            = default;
    BitLengthSet& operator=(const BitLengthSet&) = default;
    BitLengthSet(BitLengthSet&& other) noexcept;
    BitLengthSet& operator=(BitLengthSet&& other) noexcept;
    ~BitLengthSet() = default;
    /// @}

    /// @brief Constructs a singleton set {value}.
    /// @param[in] value Single bit-length value; a negative value is clamped to 0 (see "Value
    ///            domain").
    explicit BitLengthSet(std::int64_t value);

    /// @brief Constructs a concrete set from expanded values.
    /// @param[in] values Explicit value set; an empty set is coerced to {0} (invariant I1) and any
    ///            negative element is clamped to 0 (see "Value domain").
    explicit BitLengthSet(std::set<std::int64_t> values);

    /// @brief Returns the exact minimum of the denoted set.
    /// @return `min(S)`; always defined because S is non-empty (I1).
    /// @post Result is an element of S and `min() <= max()` (on the specified domain).
    /// @note Symbolic: never enumerates S (I4). Cost: see class-level complexity caveats.
    [[nodiscard]] std::int64_t min() const;

    /// @brief Returns the exact maximum of the denoted set.
    /// @return `max(S)`; always defined because S is non-empty (I1).
    /// @post Result is an element of S and `min() <= max()` (on the specified domain).
    /// @note Symbolic: never enumerates S (I4). Cost: see class-level complexity caveats.
    [[nodiscard]] std::int64_t max() const;

    /// @brief Returns true when the set contains exactly one value.
    /// @return `|S| == 1`, computed as `min() == max()` (exact on the specified domain).
    ///
    /// Used by layout analysis to classify fixed-size types; a fixed set means the entity
    /// serializes to the same number of bits in every case.
    [[nodiscard]] bool fixed() const;

    /// @brief Reports whether every possible length is a multiple of `alignment`.
    /// @param[in] alignment Alignment in bits; values `< 1` are treated as 1 (always aligned).
    /// @return True iff `modulo(alignment) == {0}` — i.e. the entity is `alignment`-aligned in
    ///         every case. Exact for any set size (built on the exact symbolic `modulo`).
    /// @note Mirrors pydsdl's `BitLengthSet.is_aligned_at`; the byte case is `is_aligned_at(8)`.
    [[nodiscard]] bool is_aligned_at(std::int64_t alignment) const;

    /// @brief Rounds each candidate length up to the nearest multiple of `alignment`.
    /// @param[in] alignment Alignment in bits; values `< 1` are clamped to 1 (identity map).
    /// @return Set denoting `{ ceil(v / alignment) * alignment : v in S }`.
    /// @post Every element of the result is a multiple of `alignment`; the operation is
    ///       idempotent; `min()`/`max()` of the result equal the padded `min()`/`max()` of
    ///       the input (rounding is monotone).
    /// @note Models DSDL alignment padding (e.g. byte alignment of composites and unions).
    ///       The clamp of non-positive alignments is silent — callers get no diagnostic.
    [[nodiscard]] BitLengthSet padToAlignment(std::int64_t alignment) const;

    /// @brief Denotes the sum of exactly `count` independent draws from this set.
    /// @param[in] count Repeat count; values `< 0` are clamped to 0.
    /// @return Set denoting `{ v1 + ... + v_count : vi in S }`; `{0}` when `count <= 0`.
    /// @post `repeat(k).min() == k * min()` and `repeat(k).max() == k * max()` (k >= 0);
    ///       `repeat(1)` denotes the same set as `*this`; equivalent to `k`-fold `operator+`.
    /// @note Models a fixed-length array of `count` elements whose element type has this
    ///       bit-length set.
    [[nodiscard]] BitLengthSet repeat(std::int64_t count) const;

    /// @brief Denotes the union of `repeat(k)` for all `k` in `[0, countMax]`.
    /// @param[in] countMax Maximum repeat count; values `< 0` are clamped to 0.
    /// @return Set denoting `union over k in [0, countMax] of repeat(k)`; always contains 0.
    /// @post `repeatRange(k).min() == 0`; `repeatRange(k).max() == k * max()` (on the
    ///       non-negative domain); `repeatRange(0)` denotes `{0}`.
    /// @note Models the payload of a variable-length array with capacity `countMax`
    ///       (the length-prefix field is accounted for separately by the caller).
    [[nodiscard]] BitLengthSet repeatRange(std::int64_t countMax) const;

    /// @brief Computes the exact residues of the denoted set modulo `divisor`.
    /// @param[in] divisor Modulo divisor; values `< 1` yield `{0}` (silent sentinel).
    /// @return `{ v mod d : v in S }`, complete for any set size.
    /// @note Computed by symbolic per-node residue propagation (each intermediate residue set is
    ///       a subset of Z/divisor), so it does NOT depend on `expand()` and never silently
    ///       omits residues. The only non-exact case is a defensive internal modulus cap that a
    ///       realistic (power-of-two-alignment) query never reaches; see the class-level
    ///       "Exactness model".
    /// @note Intended for alignment reasoning (e.g. "can this offset be misaligned?"),
    ///       mirroring pydsdl's `BitLengthSet.__mod__`.
    [[nodiscard]] std::flat_set<std::int64_t> modulo(std::int64_t divisor) const;

    /// @brief An expansion together with a completeness signal.
    struct Expansion final
    {
        /// @brief A subset of S; equals S exactly iff `exact` is true. Never empty, never larger
        ///        than the requested (clamped-to-`>= 1`) limit. A `std::flat_set` (sorted, contiguous)
        ///        — these sets are built once and iterated, so the flat layout beats a node-based set.
        std::flat_set<std::int64_t> values;

        /// @brief True iff `values == S` — i.e. no node in the expression was truncated.
        bool exact{true};
    };

    /// @brief Materializes the denoted set as concrete values.
    /// @param[in] limit Expansion safety limit; values `< 1` are clamped to 1.
    /// @return A subset of S (sound under-approximation), never empty and never larger than the
    ///         clamped limit: exactly S when every intermediate subexpression's cardinality is
    ///         `<= limit`; otherwise a truncated subset (favouring the smaller elements — in
    ///         particular `max()` of the expansion may be less than `max()` of the set). No
    ///         completeness signal is reported; use `expandChecked()` when that matters.
    /// @warning Expansion cost is NOT bounded by `limit` alone; see class-level complexity caveats.
    [[nodiscard]] std::flat_set<std::int64_t> expand(std::size_t limit = 16384) const;

    /// @brief Like `expand()`, but also reports whether the result is complete (`== S`).
    /// @param[in] limit Expansion safety limit; values `< 1` are clamped to 1.
    /// @return `{ values, exact }` where `values` is the same sound subset `expand()` returns and
    ///         `exact` is true iff no truncation occurred anywhere in the expression (so
    ///         `values == S`). Callers that reason over the value set (e.g. evaluating an
    ///         assertion) MUST consult `exact` before trusting completeness — an `exact == false`
    ///         result means some values are missing and any all/any/count reasoning is unsound.
    [[nodiscard]] Expansion expandChecked(std::size_t limit = 16384) const;

    /// @brief Returns a compact textual rendering of the symbolic expression (diagnostics only).
    /// @return String over the grammar:
    ///         `set   := '{' int (',' int)* '}'`
    ///         `expr  := set | 'concat(' expr ',' expr ')' | 'union(' expr ',' expr ')'
    ///                 | 'pad(' expr ',' int ')' | 'repeat(' expr ',' int ')'
    ///                 | 'repeat_range(' expr ',' int ')'`
    ///         where leaf values print in ascending order and parameters are the
    ///         post-clamping values.
    /// @note The format is a debugging aid, not a stable serialization; it renders the
    ///       expression structure, not the expanded value set.
    [[nodiscard]] std::string str() const;

    /// @brief Pointwise additive combination (Minkowski sum) of two sets.
    ///
    /// Denotes `{ x + y : x in S(lhs), y in S(rhs) }` — the bit-length set of two entities
    /// serialized back-to-back. Commutative and associative in value-set semantics;
    /// `BitLengthSet(0)` is the identity. O(1): allocates one node, shares operand structure.
    /// @note Derived sums saturate at `INT64_MAX` rather than overflowing (see "Value domain").
    friend BitLengthSet operator+(const BitLengthSet& lhs, const BitLengthSet& rhs);

    /// @brief Set union of two symbolic sets.
    ///
    /// Denotes `S(lhs) union S(rhs)` — the bit-length set of an entity that serializes as
    /// either alternative (e.g. tagged-union options). Commutative, associative, idempotent
    /// in value-set semantics. O(1): allocates one node, shares operand structure.
    friend BitLengthSet operator|(const BitLengthSet& lhs, const BitLengthSet& rhs);

    /// @brief Value-set equality of two symbolic sets (mirrors pydsdl's `BitLengthSet.__eq__`).
    ///
    /// True iff the two objects PROVABLY denote the same set: their `min()`/`max()` agree and both
    /// expand exactly (at the default limit) to the same values. This is definitive for every set
    /// that fits the expansion limit — the realistic case. For a set too large to expand exactly it
    /// is conservative: it may return `false` for two sets that are in fact equal (a false
    /// negative), but never `true` for two that differ (no false positive).
    friend bool operator==(const BitLengthSet& lhs, const BitLengthSet& rhs);
    friend bool operator!=(const BitLengthSet& lhs, const BitLengthSet& rhs);

private:
    /// @brief Internal persistent expression node.
    struct Node;

    /// @brief Constructs from internal node root.
    explicit BitLengthSet(std::shared_ptr<const Node> root);

    /// @brief Process-wide shared leaf denoting `{0}`; a moved-from object is reset to it so its
    ///        root is never null and it keeps denoting `{0}`.
    /// @return A stable, non-null root shared by all `{0}`-valued moved-from sources.
    static const std::shared_ptr<const Node>& zeroLeaf();

    /// @brief Root of the persistent symbolic expression tree. Never null: constructed objects hold
    ///        a real node, and moves reset the source to `zeroLeaf()`.
    std::shared_ptr<const Node> root_;
};

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SEMANTICS_BITLENGTHSET_H
