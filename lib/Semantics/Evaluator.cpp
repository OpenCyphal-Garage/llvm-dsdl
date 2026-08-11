//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements constant-expression evaluation for semantic analysis.
///
/// Expression evaluators produce typed values and diagnostics for compile-time computations in DSDL definitions.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Semantics/Evaluator.h"

#include <optional>
#include <sstream>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "llvmdsdl/Support/Diagnostics.h"

namespace llvmdsdl
{
namespace
{

/// Materialization ceiling for symbolic set values (`_offset_`). This is a pure resource guard
/// against pathological definitions (compile-time DoS), NOT a correctness knob: an expression
/// whose exact value set cannot be materialized within this many elements FAILS with a
/// diagnostic instead of being evaluated against a truncated set. Queries answered symbolically
/// (`.min`, `.max`, `% k`, singleton comparisons) never consult this limit and are exact at any
/// cardinality.
constexpr std::size_t kExactMaterializationLimit = 16384;

Value::Set toRationalSet(const FlatSet<std::int64_t>& values)
{
    Value::Set out;
    for (const auto v : values)
    {
        out.insert(Rational(v, 1));
    }
    return out;
}

/// Materializes the exact value set denoted by a symbolic set, or fails with a hard error.
/// Never returns a truncated set: inexactness here is a diagnosed evaluation failure, so
/// approximate values cannot leak into expression results.
///
/// The RunSet path is tried first: it has no intermediate-truncation cliff, so it succeeds
/// whenever the FINAL set fits the ceiling even if intermediate subexpressions were huge.
/// `expandChecked` remains as the fallback for structures RunSet refuses.
std::optional<Value::Set> materializeExact(const BitLengthSet&   bls,
                                           DiagnosticEngine&     diagnostics,
                                           const SourceLocation& location)
{
    if (const auto rs = bls.runSet())
    {
        if (const auto values = rs->materialize(kExactMaterializationLimit))
        {
            return toRationalSet(*values);
        }
        // The exact set is known but exceeds the output ceiling: refuse below (the output of an
        // elementwise operation is proportional to cardinality, so the bound is inherent).
    }
    else
    {
        const auto expansion = bls.expandChecked(kExactMaterializationLimit);
        if (expansion.exact)
        {
            return toRationalSet(expansion.values);
        }
    }
    diagnostics.error(location,
                      "this expression requires the full contents of '_offset_', which cannot be materialized "
                      "exactly within the evaluator's capacity of " +
                          std::to_string(kExactMaterializationLimit) +
                          " values; rewrite using '_offset_.min', '_offset_.max', '_offset_.count', "
                          "'_offset_ % <divisor>', or a set comparison, which are computed exactly for any set size");
    return std::nullopt;
}

/// Converts a set-literal element to the int64 domain of a symbolic set; nullopt when the
/// element cannot possibly be a member (non-integer, or outside int64).
std::optional<std::int64_t> literalElementAsInt(const Rational& r)
{
    if (!r.isInteger())
    {
        return std::nullopt;
    }
    const auto wide = r.asWideInteger();
    if (!wide || *wide < std::numeric_limits<std::int64_t>::min() || *wide > std::numeric_limits<std::int64_t>::max())
    {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(*wide);
}

/// Decides `S == literal` exactly, where S is symbolic. Returns true with the verdict in
/// `equal`, or false after diagnosing a genuinely undecidable comparison (never guesses):
///   - `min()`/`max()` are exact at any cardinality, so a bounds mismatch disproves equality
///     without materializing anything;
///   - a materialization that completes is compared exactly;
///   - a truncated materialization is still a sound subset of S, so any element outside the
///     literal — or more elements than the literal holds — also disproves equality exactly.
bool decideSetEquality(const BitLengthSet&   bls,
                       const Value::Set&     literal,
                       bool&                 equal,
                       DiagnosticEngine&     diagnostics,
                       const SourceLocation& location)
{
    if (literal.empty())
    {
        equal = false;  // S is never empty (invariant I1).
        return true;
    }
    if (*literal.begin() != Rational(bls.min(), 1) || *literal.rbegin() != Rational(bls.max(), 1))
    {
        equal = false;
        return true;
    }
    // Exact path, any cardinality: S == literal iff |S| == |literal| and every literal element
    // is a member of S — both closed-form on the RunSet.
    if (const auto rs = bls.runSet())
    {
        const auto n = rs->count();
        if (n)
        {
            if (static_cast<std::size_t>(*n) != literal.size())
            {
                equal = false;
                return true;
            }
            equal = std::all_of(literal.begin(), literal.end(), [&](const Rational& r) {
                const auto v = literalElementAsInt(r);
                return v && rs->contains(*v);
            });
            return true;
        }
    }
    const auto expansion = bls.expandChecked(kExactMaterializationLimit);
    const auto values    = toRationalSet(expansion.values);
    if (expansion.exact)
    {
        equal = (values == literal);
        return true;
    }
    if (values.size() > literal.size())
    {
        equal = false;
        return true;
    }
    for (const auto& v : values)
    {
        if (!literal.contains(v))
        {
            equal = false;
            return true;
        }
    }
    diagnostics.error(location,
                      "cannot decide this '_offset_' set comparison exactly within the evaluator's capacity of " +
                          std::to_string(kExactMaterializationLimit) +
                          " values; compare '_offset_.min'/'_offset_.max' or '_offset_ % <divisor>' instead");
    return false;
}

bool asBool(const Value& v, bool& out)
{
    if (auto p = std::get_if<bool>(&v.data))
    {
        out = *p;
        return true;
    }
    return false;
}

bool asRational(const Value& v, Rational& out)
{
    if (auto p = std::get_if<Rational>(&v.data))
    {
        out = *p;
        return true;
    }
    return false;
}

bool isInteger(const Rational& r)
{
    return r.isInteger();
}

std::optional<Rational> intPow(const Rational& base, const Rational& exp)
{
    if (!exp.isInteger())
    {
        return std::nullopt;
    }
    const auto e = exp.asWideInteger().value_or(0);
    if (e < 0)
    {
        return std::nullopt;
    }
    // Special-case the bases whose powers never overflow, so a huge exponent cannot spin the loop
    // (a denial-of-service vector): 1**e == 1, 0**e is 0 (or 1 for e == 0), and (-1)**e alternates.
    if (base == Rational(1, 1))
    {
        return Rational(1, 1);
    }
    if (base == Rational(0, 1))
    {
        return (e == 0) ? Rational(1, 1) : Rational(0, 1);
    }
    if (base == Rational(-1, 1))
    {
        return ((e % 2) == 0) ? Rational(1, 1) : Rational(-1, 1);
    }
    // Every other base has |value| != 1, so the running product leaves 64-bit range within a few
    // dozen iterations; bail as soon as it overflows, which also bounds the loop.
    Rational out(1, 1);
    for (__int128 i = 0; i < e; ++i)
    {
        out = out * base;
        if (out.overflowed())
        {
            return std::nullopt;
        }
    }
    return out;
}

std::optional<Value> applyBinaryRational(BinaryOp op, const Rational& lhs, const Rational& rhs)
{
    switch (op)
    {
    case BinaryOp::Pow: {
        auto p = intPow(lhs, rhs);
        if (!p)
        {
            return std::nullopt;
        }
        return Value{*p};
    }
    case BinaryOp::Mul: {
        const Rational r = lhs * rhs;
        if (r.overflowed())
        {
            return std::nullopt;
        }
        return Value{r};
    }
    case BinaryOp::Div: {
        if (rhs == Rational(0, 1))
        {
            return std::nullopt;
        }
        const Rational r = lhs / rhs;
        if (r.overflowed())
        {
            return std::nullopt;
        }
        return Value{r};
    }
    case BinaryOp::Mod: {
        if (!isInteger(lhs) || !isInteger(rhs) || rhs == Rational(0, 1))
        {
            return std::nullopt;
        }
        const auto li = lhs.asWideInteger().value();
        const auto ri = rhs.asWideInteger().value();
        // `x % -1` is 0; special-cased so a most-negative dividend cannot form an unrepresentable
        // quotient (undefined behaviour) in the modulo below.
        if (ri == -1)
        {
            return Value{Rational(0, 1)};
        }
        return Value{Rational(li % ri, 1)};
    }
    case BinaryOp::Add: {
        const Rational r = lhs + rhs;
        if (r.overflowed())
        {
            return std::nullopt;
        }
        return Value{r};
    }
    case BinaryOp::Sub: {
        const Rational r = lhs - rhs;
        if (r.overflowed())
        {
            return std::nullopt;
        }
        return Value{r};
    }
    case BinaryOp::BitOr:
    case BinaryOp::BitXor:
    case BinaryOp::BitAnd: {
        if (!isInteger(lhs) || !isInteger(rhs))
        {
            return std::nullopt;
        }
        const auto li = lhs.asWideInteger().value();
        const auto ri = rhs.asWideInteger().value();
        if (op == BinaryOp::BitOr)
        {
            return Value{Rational(li | ri, 1)};
        }
        if (op == BinaryOp::BitXor)
        {
            return Value{Rational(li ^ ri, 1)};
        }
        return Value{Rational(li & ri, 1)};
    }
    case BinaryOp::Eq:
        return Value{lhs == rhs};
    case BinaryOp::Ne:
        return Value{lhs != rhs};
    case BinaryOp::Le:
        return Value{lhs <= rhs};
    case BinaryOp::Ge:
        return Value{lhs >= rhs};
    case BinaryOp::Lt:
        return Value{lhs < rhs};
    case BinaryOp::Gt:
        return Value{lhs > rhs};
    default:
        return std::nullopt;
    }
}

Value::Set applySetBinary(BinaryOp op, const Value::Set& lhs, const Value::Set& rhs)
{
    Value::Set out;
    if (op == BinaryOp::BitOr)
    {
        out = lhs;
        out.insert(rhs.begin(), rhs.end());
    }
    else if (op == BinaryOp::BitAnd)
    {
        for (const auto& x : lhs)
        {
            if (rhs.contains(x))
            {
                out.insert(x);
            }
        }
    }
    else if (op == BinaryOp::BitXor)
    {
        for (const auto& x : lhs)
        {
            if (!rhs.contains(x))
            {
                out.insert(x);
            }
        }
        for (const auto& x : rhs)
        {
            if (!lhs.contains(x))
            {
                out.insert(x);
            }
        }
    }
    return out;
}

std::optional<Value::Set> applySetElementwise(BinaryOp op, const Value::Set& lhs, const Value::Set& rhs)
{
    Value::Set out;
    for (const auto& a : lhs)
    {
        for (const auto& b : rhs)
        {
            auto v = applyBinaryRational(op, a, b);
            if (!v)
            {
                return std::nullopt;
            }
            if (auto p = std::get_if<Rational>(&v->data))
            {
                out.insert(*p);
            }
            else
            {
                return std::nullopt;
            }
        }
    }
    return out;
}

std::optional<Value> evaluate(const ExprAST&               expr,
                              const ValueEnv&              env,
                              DiagnosticEngine&            diagnostics,
                              const TypeAttributeResolver* resolver);

std::optional<Value> evaluateBinary(const ExprAST::Binary&       b,
                                    const SourceLocation&        location,
                                    const ValueEnv&              env,
                                    DiagnosticEngine&            diagnostics,
                                    const TypeAttributeResolver* resolver)
{
    if (b.op == BinaryOp::Attribute)
    {
        auto lhs = evaluate(*b.lhs, env, diagnostics, resolver);
        if (!lhs)
        {
            return std::nullopt;
        }

        const auto* rhsId = std::get_if<ExprAST::Identifier>(&b.rhs->value);
        if (!rhsId)
        {
            diagnostics.error(location, "attribute operator expects identifier on RHS");
            return std::nullopt;
        }

        if (auto bls = std::get_if<BitLengthSet>(&lhs->data))
        {
            // Symbolic set: min/max are exact at any cardinality and never enumerate the set.
            if (rhsId->name == "min")
            {
                return Value{Rational(bls->min(), 1)};
            }
            if (rhsId->name == "max")
            {
                return Value{Rational(bls->max(), 1)};
            }
            if (rhsId->name == "count")
            {
                if (bls->fixed())
                {
                    return Value{Rational(1, 1)};
                }
                // Exact cardinality at ANY size via the run representation — no enumeration.
                if (const auto rs = bls->runSet())
                {
                    if (const auto n = rs->count())
                    {
                        return Value{Rational(*n, 1)};
                    }
                }
                auto set = materializeExact(*bls, diagnostics, location);
                if (!set)
                {
                    return std::nullopt;
                }
                return Value{Rational(static_cast<std::int64_t>(set->size()), 1)};
            }
        }

        if (auto set = std::get_if<Value::Set>(&lhs->data))
        {
            if (rhsId->name == "count")
            {
                return Value{Rational(static_cast<std::int64_t>(set->size()), 1)};
            }
            if (set->empty())
            {
                diagnostics.error(location, "cannot access set min/max on an empty set literal");
                return std::nullopt;
            }
            if (rhsId->name == "min")
            {
                return Value{*set->begin()};
            }
            if (rhsId->name == "max")
            {
                return Value{*set->rbegin()};
            }
        }

        if (auto t = std::get_if<TypeExprAST>(&lhs->data))
        {
            if (resolver)
            {
                return (*resolver)(*t, rhsId->name, location);
            }
            if (rhsId->name == "_extent_")
            {
                return Value{Rational(0, 1)};
            }
            diagnostics.error(location, "unsupported metaserializable attribute: " + rhsId->name);
            return std::nullopt;
        }

        diagnostics.error(location, "attribute operator is not defined on " + lhs->typeName());
        return std::nullopt;
    }

    auto lhs = evaluate(*b.lhs, env, diagnostics, resolver);
    auto rhs = evaluate(*b.rhs, env, diagnostics, resolver);
    if (!lhs || !rhs)
    {
        return std::nullopt;
    }

    // Symbolic set operands (`_offset_`): answer exactly from the symbolic form where possible;
    // whatever remains materializes its exact value set below or fails — an expression is never
    // evaluated against a truncated set.
    if (std::holds_alternative<BitLengthSet>(lhs->data) || std::holds_alternative<BitLengthSet>(rhs->data))
    {
        // `_offset_ % k`: exact residues at any cardinality via modulo() (exact-or-refuse).
        // Offsets are non-negative, so remainder by a negative divisor equals remainder by its
        // magnitude (elementwise `%` truncates toward zero). A refusal — like a divisor beyond
        // int64 — falls THROUGH to generic materialization, which is itself exact-or-error.
        if (b.op == BinaryOp::Mod)
        {
            const auto* lbls = std::get_if<BitLengthSet>(&lhs->data);
            const auto* r    = std::get_if<Rational>(&rhs->data);
            if (lbls != nullptr && r != nullptr && r->isInteger())
            {
                const __int128 d         = r->asWideInteger().value();
                const __int128 magnitude = (d < 0) ? -d : d;
                if (d == 0)
                {
                    diagnostics.error(location, "invalid elementwise set operation");
                    return std::nullopt;
                }
                if (magnitude <= std::numeric_limits<std::int64_t>::max())
                {
                    if (auto residues = lbls->modulo(static_cast<std::int64_t>(magnitude)))
                    {
                        return Value{toRationalSet(*residues)};
                    }
                }
            }
        }

        // Equality against a concrete set: decidable exactly at any cardinality in all but
        // genuinely pathological cases (see decideSetEquality).
        if (b.op == BinaryOp::Eq || b.op == BinaryOp::Ne)
        {
            const auto* lbls = std::get_if<BitLengthSet>(&lhs->data);
            const auto* rbls = std::get_if<BitLengthSet>(&rhs->data);
            const auto* lset = std::get_if<Value::Set>(&lhs->data);
            const auto* rset = std::get_if<Value::Set>(&rhs->data);
            const auto* bls  = (lbls != nullptr) ? lbls : rbls;
            const auto* set  = (lbls != nullptr) ? rset : lset;
            if (bls != nullptr && set != nullptr)
            {
                bool equal = false;
                if (!decideSetEquality(*bls, *set, equal, diagnostics, location))
                {
                    return std::nullopt;
                }
                return Value{(b.op == BinaryOp::Eq) ? equal : !equal};
            }
        }

        // Ordered comparisons against a concrete set are subset relations (mirroring the
        // concrete Set-Set semantics below): decidable exactly at any cardinality via the run
        // representation — membership of each literal element is closed-form, and the symbolic
        // set can only be a subset of a small literal if it is itself small.
        if (b.op == BinaryOp::Le || b.op == BinaryOp::Lt || b.op == BinaryOp::Ge || b.op == BinaryOp::Gt)
        {
            const auto* lbls = std::get_if<BitLengthSet>(&lhs->data);
            const auto* rbls = std::get_if<BitLengthSet>(&rhs->data);
            const auto* lset = std::get_if<Value::Set>(&lhs->data);
            const auto* rset = std::get_if<Value::Set>(&rhs->data);
            const auto* bls  = (lbls != nullptr) ? lbls : rbls;
            const auto* set  = (lbls != nullptr) ? rset : lset;
            if (bls != nullptr && set != nullptr)
            {
                if (const auto rs = bls->runSet())
                {
                    if (const auto n = rs->count())
                    {
                        // S subset-of literal: impossible when |S| exceeds the literal;
                        // otherwise S is small enough to materialize and check directly.
                        bool sInLit = false;
                        if (static_cast<std::size_t>(*n) <= set->size())
                        {
                            if (const auto values = rs->materialize(set->size()))
                            {
                                sInLit = std::all_of(values->begin(), values->end(), [&](std::int64_t v) {
                                    return set->contains(Rational(v, 1));
                                });
                            }
                        }
                        // literal subset-of S: closed-form membership per element.
                        const bool litInS = std::all_of(set->begin(), set->end(), [&](const Rational& r) {
                            const auto v = literalElementAsInt(r);
                            return v && rs->contains(*v);
                        });
                        const bool subsetLR   = (lbls != nullptr) ? sInLit : litInS;  // lhs subset rhs
                        const bool supersetLR = (lbls != nullptr) ? litInS : sInLit;  // rhs subset lhs
                        const bool sameCard   = static_cast<std::size_t>(*n) == set->size();
                        switch (b.op)
                        {
                        case BinaryOp::Le:
                            return Value{subsetLR};
                        case BinaryOp::Lt:
                            return Value{subsetLR && !sameCard};
                        case BinaryOp::Ge:
                            return Value{supersetLR};
                        default:
                            return Value{supersetLR && !sameCard};
                        }
                    }
                }
            }
        }

        // Everything else: materialize the exact set (or fail) and dispatch through the
        // ordinary concrete-set paths below.
        if (auto lbls = std::get_if<BitLengthSet>(&lhs->data))
        {
            auto set = materializeExact(*lbls, diagnostics, location);
            if (!set)
            {
                return std::nullopt;
            }
            lhs->data = std::move(*set);
        }
        if (auto rbls = std::get_if<BitLengthSet>(&rhs->data))
        {
            auto set = materializeExact(*rbls, diagnostics, location);
            if (!set)
            {
                return std::nullopt;
            }
            rhs->data = std::move(*set);
        }
    }

    if (auto l = std::get_if<Rational>(&lhs->data))
    {
        if (auto r = std::get_if<Rational>(&rhs->data))
        {
            auto v = applyBinaryRational(b.op, *l, *r);
            if (!v)
            {
                diagnostics.error(location, "invalid rational operation");
            }
            return v;
        }
    }

    if (auto l = std::get_if<bool>(&lhs->data))
    {
        if (auto r = std::get_if<bool>(&rhs->data))
        {
            switch (b.op)
            {
            case BinaryOp::LogicalOr:
                return Value{*l || *r};
            case BinaryOp::LogicalAnd:
                return Value{*l && *r};
            case BinaryOp::Eq:
                return Value{*l == *r};
            case BinaryOp::Ne:
                return Value{*l != *r};
            default:
                break;
            }
        }
    }

    if (auto l = std::get_if<std::string>(&lhs->data))
    {
        if (auto r = std::get_if<std::string>(&rhs->data))
        {
            switch (b.op)
            {
            case BinaryOp::Add:
                return Value{*l + *r};
            case BinaryOp::Eq:
                return Value{*l == *r};
            case BinaryOp::Ne:
                return Value{*l != *r};
            default:
                break;
            }
        }
    }

    if (auto ls = std::get_if<Value::Set>(&lhs->data))
    {
        if (auto rs = std::get_if<Value::Set>(&rhs->data))
        {
            if (b.op == BinaryOp::Eq)
            {
                return Value{*ls == *rs};
            }
            if (b.op == BinaryOp::Ne)
            {
                return Value{*ls != *rs};
            }
            if (b.op == BinaryOp::Le || b.op == BinaryOp::Lt || b.op == BinaryOp::Ge || b.op == BinaryOp::Gt)
            {
                const auto subset = [&]() {
                    return std::all_of(ls->begin(), ls->end(), [&](const Rational& x) { return rs->contains(x); });
                }();
                const auto superset = [&]() {
                    return std::all_of(rs->begin(), rs->end(), [&](const Rational& x) { return ls->contains(x); });
                }();
                if (b.op == BinaryOp::Le)
                {
                    return Value{subset};
                }
                if (b.op == BinaryOp::Lt)
                {
                    return Value{subset && (*ls != *rs)};
                }
                if (b.op == BinaryOp::Ge)
                {
                    return Value{superset};
                }
                return Value{superset && (*ls != *rs)};
            }
            if (b.op == BinaryOp::BitOr || b.op == BinaryOp::BitAnd || b.op == BinaryOp::BitXor)
            {
                return Value{applySetBinary(b.op, *ls, *rs)};
            }
            if (b.op == BinaryOp::Pow || b.op == BinaryOp::Mul || b.op == BinaryOp::Div || b.op == BinaryOp::Mod ||
                b.op == BinaryOp::Add || b.op == BinaryOp::Sub)
            {
                auto out = applySetElementwise(b.op, *ls, *rs);
                if (!out)
                {
                    diagnostics.error(location, "invalid elementwise set operation");
                    return std::nullopt;
                }
                return Value{*out};
            }
        }

        if (auto rr = std::get_if<Rational>(&rhs->data))
        {
            Value::Set rs{*rr};
            if (b.op == BinaryOp::Pow || b.op == BinaryOp::Mul || b.op == BinaryOp::Div || b.op == BinaryOp::Mod ||
                b.op == BinaryOp::Add || b.op == BinaryOp::Sub)
            {
                auto out = applySetElementwise(b.op, *ls, rs);
                if (!out)
                {
                    diagnostics.error(location, "invalid elementwise set operation");
                    return std::nullopt;
                }
                return Value{*out};
            }
        }
    }

    if (auto lr = std::get_if<Rational>(&lhs->data))
    {
        if (auto rs = std::get_if<Value::Set>(&rhs->data))
        {
            Value::Set ls{*lr};
            if (b.op == BinaryOp::Pow || b.op == BinaryOp::Mul || b.op == BinaryOp::Div || b.op == BinaryOp::Mod ||
                b.op == BinaryOp::Add || b.op == BinaryOp::Sub)
            {
                auto out = applySetElementwise(b.op, ls, *rs);
                if (!out)
                {
                    diagnostics.error(location, "invalid elementwise set operation");
                    return std::nullopt;
                }
                return Value{*out};
            }
        }
    }

    diagnostics.error(location, "unsupported operand types: " + lhs->typeName() + " and " + rhs->typeName());
    return std::nullopt;
}

std::optional<Value> evaluate(const ExprAST&               expr,
                              const ValueEnv&              env,
                              DiagnosticEngine&            diagnostics,
                              const TypeAttributeResolver* resolver)
{
    if (auto p = std::get_if<bool>(&expr.value))
    {
        return Value{*p};
    }
    if (auto p = std::get_if<Rational>(&expr.value))
    {
        return Value{*p};
    }
    if (auto p = std::get_if<std::string>(&expr.value))
    {
        return Value{*p};
    }
    if (auto p = std::get_if<ExprAST::Identifier>(&expr.value))
    {
        const auto it = env.find(p->name);
        if (it == env.end())
        {
            diagnostics.error(expr.location, "undefined identifier: " + p->name);
            return std::nullopt;
        }
        return it->second;
    }
    if (auto p = std::get_if<ExprAST::Unary>(&expr.value))
    {
        auto operand = evaluate(*p->operand, env, diagnostics, resolver);
        if (!operand)
        {
            return std::nullopt;
        }

        if (p->op == UnaryOp::LogicalNot)
        {
            bool b = false;
            if (!asBool(*operand, b))
            {
                diagnostics.error(expr.location, "logical not requires boolean operand");
                return std::nullopt;
            }
            return Value{!b};
        }

        Rational r;
        if (!asRational(*operand, r))
        {
            diagnostics.error(expr.location, "unary +/- requires rational operand");
            return std::nullopt;
        }
        if (p->op == UnaryOp::Minus)
        {
            return Value{Rational(-r.numerator(), r.denominator())};
        }
        return Value{r};
    }
    if (auto p = std::get_if<ExprAST::Binary>(&expr.value))
    {
        return evaluateBinary(*p, expr.location, env, diagnostics, resolver);
    }
    if (auto p = std::get_if<ExprAST::SetLiteral>(&expr.value))
    {
        Value::Set set;
        for (const auto& elem : p->elements)
        {
            auto value = evaluate(*elem, env, diagnostics, resolver);
            if (!value)
            {
                return std::nullopt;
            }
            auto rv = std::get_if<Rational>(&value->data);
            if (!rv)
            {
                diagnostics.error(elem->location, "set literal elements must evaluate to rational");
                return std::nullopt;
            }
            set.insert(*rv);
        }
        return Value{set};
    }
    if (auto p = std::get_if<ExprAST::TypeLiteral>(&expr.value))
    {
        return Value{p->type};
    }

    diagnostics.error(expr.location, "unsupported expression kind");
    return std::nullopt;
}

}  // namespace

std::string Value::typeName() const
{
    if (std::holds_alternative<bool>(data))
    {
        return "bool";
    }
    if (std::holds_alternative<Rational>(data))
    {
        return "rational";
    }
    if (std::holds_alternative<std::string>(data))
    {
        return "string";
    }
    if (std::holds_alternative<Set>(data))
    {
        return "set<rational>";
    }
    if (std::holds_alternative<BitLengthSet>(data))
    {
        // The symbolic representation is an implementation detail; to the DSDL author
        // `_offset_` is a set of rationals.
        return "set<rational>";
    }
    return "metaserializable";
}

std::string Value::str() const
{
    std::ostringstream out;
    if (auto p = std::get_if<bool>(&data))
    {
        out << (*p ? "true" : "false");
    }
    else if (auto p = std::get_if<Rational>(&data))
    {
        out << p->str();
    }
    else if (auto p = std::get_if<std::string>(&data))
    {
        out << '\'' << *p << '\'';
    }
    else if (auto p = std::get_if<Set>(&data))
    {
        out << '{';
        bool first = true;
        for (const auto& v : *p)
        {
            if (!first)
            {
                out << ", ";
            }
            out << v.str();
            first = false;
        }
        out << '}';
    }
    else if (auto p = std::get_if<TypeExprAST>(&data))
    {
        out << p->str();
    }
    else if (auto p = std::get_if<BitLengthSet>(&data))
    {
        // Concrete rendering when the exact set materializes; otherwise the symbolic expression
        // — never a silently truncated set.
        const auto expansion = p->expandChecked(kExactMaterializationLimit);
        if (expansion.exact)
        {
            out << '{';
            bool first = true;
            for (const auto v : expansion.values)
            {
                if (!first)
                {
                    out << ", ";
                }
                out << v;
                first = false;
            }
            out << '}';
        }
        else
        {
            out << p->str();
        }
    }
    return out.str();
}

std::optional<Value> evaluateExpression(const ExprAST&               expr,
                                        const ValueEnv&              env,
                                        DiagnosticEngine&            diagnostics,
                                        const SourceLocation&        location,
                                        const TypeAttributeResolver* resolver)
{
    const auto beforeDiagnostics = diagnostics.diagnostics().size();
    auto       v                 = evaluate(expr, env, diagnostics, resolver);
    if (!v && diagnostics.diagnostics().size() == beforeDiagnostics)
    {
        diagnostics.error(location, "failed to evaluate expression");
    }
    return v;
}

}  // namespace llvmdsdl
