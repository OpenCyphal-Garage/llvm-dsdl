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
/// Elements model bit counts: every value supplied to a constructor MUST be non-negative.
/// The class does not validate this precondition; behavior for negative elements is
/// unspecified (rounding, `min()`/`max()`, and `modulo()` are known to disagree with the
/// mathematical definitions above in that regime).
///
/// All arithmetic is unchecked `std::int64_t`. Callers MUST ensure that no derivable value —
/// including intermediate sums `max(a) + max(b)`, products `max(x) * k`, and alignment
/// round-ups — exceeds `INT64_MAX`; violation is signed-overflow undefined behavior.
///
/// ## Invariants
///
///   - I1 (non-empty): S is never empty. The default constructor and the coercion of an empty
///     input set both yield {0}. Consequently `min()` and `max()` are always defined.
///   - I2 (ordered bounds): `min() <= max()`, and both are elements of S (exactness of the
///     symbolic bounds; holds on the specified non-negative domain).
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
///       * when truncation occurs, WHICH subset is returned is unspecified (it is NOT
///         guaranteed to be the smallest `limit` elements), and no error is reported;
///       * `limit` MUST be >= 1; `expand(0)` is a precondition violation (see D3 in the
///         defect log: it can currently return an empty set).
///   - `modulo(d)` is defined as the exact residue set of S; because the current
///     implementation derives it from `expand()` with the default limit, completeness is only
///     guaranteed under the same conditions as `expand()` exactness. Callers performing
///     alignment proofs MUST NOT rely on `modulo()` completeness for sets that may exceed the
///     default expansion limit (tracked as defect BLS-D1).
///
/// ## Complexity and robustness caveats (as implemented)
///
///   - Construction operations (`+`, `|`, `padToAlignment`, `repeat`, `repeatRange`) are O(1):
///     they allocate one node and share children.
///   - `min()`, `max()`, `expand()`, and `str()` recurse over the expression graph as a TREE:
///     shared subexpressions are re-visited once per path (no memoization), so heavily shared
///     graphs (e.g. `s = s + s` applied n times) cost O(2^n) (BLS-D9). Recursion depth is
///     proportional to expression depth; extremely deep chains (hundreds of thousands of
///     `+` applications) overflow the stack, including at destruction (BLS-D10).
///   - `repeat(k)`/`repeatRange(k)` expansion loops execute Theta(k) iterations even after the
///     result has converged or saturated at `limit`; user-controlled large `k` is a
///     compile-time DoS vector (BLS-D8).
///
/// ## Concurrency
///
/// After construction, a `BitLengthSet` is deeply immutable; all const member functions are
/// safe to call concurrently on the same or structure-sharing objects. The usual rules for
/// the object itself apply (do not assign to an object while another thread reads it).
///
/// ## Move semantics
///
/// A moved-from `BitLengthSet` holds no state and MUST NOT be used except to destroy or
/// assign to it; any other member call dereferences a null root (undefined behavior,
/// currently a crash — BLS-D7).
///
class BitLengthSet final
{
public:
    /// @brief Constructs the singleton set {0}.
    ///
    /// Note this denotes "the entity serializes to exactly zero bits", not "no information":
    /// the denoted set is never empty (invariant I1), and {0} is the identity of `operator+`.
    BitLengthSet();

    /// @brief Constructs a singleton set {value}.
    /// @param[in] value Single bit-length value.
    /// @pre `value >= 0` (not validated; see class-level "Value domain").
    explicit BitLengthSet(std::int64_t value);

    /// @brief Constructs a concrete set from expanded values.
    /// @param[in] values Explicit value set; an empty set is coerced to {0} (invariant I1).
    /// @pre Every element is `>= 0` (not validated; see class-level "Value domain").
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

    /// @brief Computes the residues of the denoted set modulo `divisor`.
    /// @param[in] divisor Modulo divisor; values `< 1` yield `{0}` (silent sentinel).
    /// @return `{ v mod d : v in S }` — subject to the completeness caveat below.
    /// @warning Completeness is NOT guaranteed for sets whose expansion exceeds the default
    ///          `expand()` limit: the result is then the residue set of an unspecified subset
    ///          of S and may silently omit residues (defect BLS-D1). It is complete whenever
    ///          `expand()` is exact for this expression at the default limit.
    /// @note Intended for alignment reasoning (e.g. "can this offset be misaligned?"),
    ///       mirroring pydsdl's `BitLengthSet.__mod__`.
    [[nodiscard]] std::set<std::int64_t> modulo(std::int64_t divisor) const;

    /// @brief Materializes the denoted set as concrete values.
    /// @param[in] limit Expansion safety limit; MUST be `>= 1`.
    /// @return A subset of S (sound under-approximation), never empty for `limit >= 1`:
    ///         exactly S when every intermediate subexpression's cardinality is `<= limit`;
    ///         otherwise an unspecified subset (no error is reported, and the subset is not
    ///         guaranteed to contain the smallest or the largest elements — in particular
    ///         `max()` of the expansion may be less than `max()` of the set).
    /// @note The result size does not exceed `limit`, except that a single leaf constructed
    ///       with more than `limit` explicit values is returned whole (BLS-D4).
    /// @warning Expansion cost is NOT bounded by `limit` alone; see class-level complexity
    ///          caveats (BLS-D8, BLS-D11).
    [[nodiscard]] std::set<std::int64_t> expand(std::size_t limit = 16384) const;

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
    /// @pre No derivable sum overflows `std::int64_t` (unchecked).
    friend BitLengthSet operator+(const BitLengthSet& lhs, const BitLengthSet& rhs);

    /// @brief Set union of two symbolic sets.
    ///
    /// Denotes `S(lhs) union S(rhs)` — the bit-length set of an entity that serializes as
    /// either alternative (e.g. tagged-union options). Commutative, associative, idempotent
    /// in value-set semantics. O(1): allocates one node, shares operand structure.
    friend BitLengthSet operator|(const BitLengthSet& lhs, const BitLengthSet& rhs);

private:
    /// @brief Internal persistent expression node.
    struct Node;

    /// @brief Constructs from internal node root.
    explicit BitLengthSet(std::shared_ptr<const Node> root);

    /// @brief Root of the persistent symbolic expression tree.
    ///
    /// Never null except in a moved-from object (see class-level "Move semantics").
    std::shared_ptr<const Node> root_;
};

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SEMANTICS_BITLENGTHSET_H
