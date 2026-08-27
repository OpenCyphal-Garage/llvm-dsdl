//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Corpus generator + evaluator for the pydsdl differential test of the BitLengthSet algebra.
///
/// Emits a deterministic corpus of composed bit-length-set expressions as replayable RECIPES
/// together with this implementation's answers in a canonical text form. The companion script
/// (`bls_pydsdl_differential.py`) replays each recipe through pydsdl's `BitLengthSet` — the
/// peer implementation of the same algebra, which this project does not author — and diffs
/// every answer. Agreement corroborates that our denotational semantics (the C++ header's
/// table, modeled formally in spec/dafny/BitLengthSet.dfy) matches the ecosystem's reference
/// understanding of the DSDL length algebra; a mismatch is an investigation, adjudicated by
/// the Cyphal Specification, that neither side wins by default.
///
/// Output line format (one per case; fields `|`-separated, sets brace-enclosed and sorted):
///
///   CASE <id> | <recipe> | min=<n> max=<n> fixed=<0|1> | mod3={..} mod5={..} mod8={..}
///        | values={..}            (when the exact set materializes within kValuesLimit)
///        | values=BIG count=<n>   (exact cardinality known but set too large to exchange)
///        | values=BIG             (cardinality itself beyond exact evaluation)
///
/// The recipe is an RPN program over a stack of sets: `L{v,v,...}` pushes a leaf, `A` adds
/// (Minkowski), `U` unites, `P<a>` pads, `R<k>` repeats, `Q<k>` repeat-ranges. Determinism:
/// raw mt19937 outputs are reduced with `%` (std::uniform_int_distribution is
/// implementation-defined and would make the corpus differ across standard libraries).
///
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "llvmdsdl/Semantics/BitLengthSet.h"

namespace
{

using llvmdsdl::BitLengthSet;
using llvmdsdl::FlatSet;

/// Materialization ceiling for exchanging concrete value sets with the Python side. Beyond it
/// the differential still compares min/max/fixed and residues (which stay small), plus exact
/// cardinality when this side can compute it — pydsdl cannot enumerate huge sets cheaply, so
/// the concrete-values comparison is bounded by construction, never truncated.
constexpr std::size_t kValuesLimit = 4096;

struct Case final
{
    std::string  recipe;
    BitLengthSet set;
    bool         pyEnumerable{true};
};

/// Estimated cost of ENUMERATING the expression on the pydsdl side. pydsdl's min/max/fixed
/// and `%` are lazy and cheap for any composition, but iterating a set expands
/// k-multicombinations, which is combinatorial in REPEAT COUNTS even when the final set is
/// tiny (repeat(2000) of five elements times out for a 2251-value result — precisely the
/// regime this project's RunSet closed forms exist for). Value-set parity is therefore gated
/// by this estimate, while extrema/residue parity runs on every case including the huge ones.
constexpr double kPyEnumCostCap = 200000.0;

double multicombinations(double n, std::int64_t k)
{
    double c = 1.0;
    for (std::int64_t i = 1; i <= k; ++i)
    {
        c = c * (n + static_cast<double>(i) - 1.0) / static_cast<double>(i);
        if (c > kPyEnumCostCap)
        {
            return kPyEnumCostCap + 1.0;
        }
    }
    return c;
}

std::string renderSet(const FlatSet<std::int64_t>& values)
{
    std::ostringstream out;
    out << '{';
    bool first = true;
    for (const auto v : values)
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

/// Builds one random composed expression, returning both the replayable recipe and the set.
/// The magnitudes are sized so pydsdl's lazy evaluation stays cheap for the comparisons the
/// script actually performs (min/max/fixed/mod always; concrete values only when small).
Case makeRandomCase(std::mt19937& rng)
{
    std::ostringstream        recipe;
    std::vector<BitLengthSet> stack;
    std::vector<double>       cost;  // parallel pydsdl-enumeration cost estimate per stack slot

    const auto pushLeaf = [&]() {
        const std::size_t      n = 1 + rng() % 4;
        std::set<std::int64_t> values;
        for (std::size_t i = 0; i < n; ++i)
        {
            values.insert(static_cast<std::int64_t>(rng() % 64));
        }
        recipe << " L{";
        bool first = true;
        for (const auto v : values)
        {
            if (!first)
            {
                recipe << ',';
            }
            recipe << v;
            first = false;
        }
        recipe << '}';
        stack.push_back(BitLengthSet(values));
        cost.push_back(static_cast<double>(values.size()));
    };

    pushLeaf();
    const std::size_t operations = 2 + rng() % 5;
    for (std::size_t i = 0; i < operations; ++i)
    {
        switch (rng() % 5)
        {
        case 0: {
            pushLeaf();
            recipe << " A";
            const auto rhs = stack.back();
            stack.pop_back();
            stack.back()         = stack.back() + rhs;
            const double rhsCost = cost.back();
            cost.pop_back();
            cost.back() = std::min(kPyEnumCostCap + 1.0, cost.back() * rhsCost);  // cartesian product
            break;
        }
        case 1: {
            pushLeaf();
            recipe << " U";
            const auto rhs = stack.back();
            stack.pop_back();
            stack.back()         = stack.back() | rhs;
            const double rhsCost = cost.back();
            cost.pop_back();
            cost.back() = cost.back() + rhsCost;
            break;
        }
        case 2: {
            const std::int64_t a = 1 + static_cast<std::int64_t>(rng() % 8);
            recipe << " P" << a;
            stack.back() = stack.back().padToAlignment(a);
            break;
        }
        case 3: {
            const std::int64_t k = static_cast<std::int64_t>(rng() % 5);
            recipe << " R" << k;
            stack.back() = stack.back().repeat(k);
            cost.back()  = multicombinations(cost.back(), k);
            break;
        }
        default: {
            const std::int64_t k = static_cast<std::int64_t>(rng() % 5);
            recipe << " Q" << k;
            stack.back() = stack.back().repeatRange(k);
            cost.back() =
                std::min(kPyEnumCostCap + 1.0, static_cast<double>(k + 1) * multicombinations(cost.back(), k));
            break;
        }
        }
    }
    return Case{recipe.str().substr(1), stack.back(), cost.back() <= kPyEnumCostCap};
}

/// Directed cases: the closed-form regime where this implementation's RunSet does real work
/// and pydsdl answers lazily — huge repeat counts, the variable-array offset shape, and the
/// exact-residue queries that motivated the modulo hardening.
std::vector<Case> directedCases()
{
    std::vector<Case> cases;
    const auto        add = [&](std::string recipe, BitLengthSet set, bool pyEnumerable) {
        cases.push_back(Case{std::move(recipe), std::move(set), pyEnumerable});
    };
    // Huge-count cases are extrema/residue parity only: pydsdl cannot ENUMERATE them cheaply
    // (its expansion is combinatorial in repeat counts), but its lazy min/max/fixed/% answer
    // instantly — which is exactly the comparison that matters in the closed-form regime.
    add("L{8} Q9000 L{16} A P8", (BitLengthSet(8).repeatRange(9000) + BitLengthSet(16)).padToAlignment(8), false);
    add("L{8} Q20000 L{16} A", BitLengthSet(8).repeatRange(20000) + BitLengthSet(16), false);
    add("L{8} R100000", BitLengthSet(8).repeat(100000), false);
    add("L{8,24} Q50000", BitLengthSet(std::set<std::int64_t>{8, 24}).repeatRange(50000), false);
    add("L{0,1,7,8,9} R2000 P8",
        BitLengthSet(std::set<std::int64_t>{0, 1, 7, 8, 9}).repeat(2000).padToAlignment(8),
        false);
    add("L{3,9} Q777", BitLengthSet(std::set<std::int64_t>{3, 9}).repeatRange(777), false);
    add("L{0} R0", BitLengthSet(0).repeat(0), true);
    add("L{5,10} P1", BitLengthSet(std::set<std::int64_t>{5, 10}).padToAlignment(1), true);
    return cases;
}

void emitCase(std::size_t id, const Case& c)
{
    std::cout << "CASE " << id << " | " << c.recipe << " | min=" << c.set.min() << " max=" << c.set.max()
              << " fixed=" << (c.set.fixed() ? 1 : 0) << " pyenum=" << (c.pyEnumerable ? 1 : 0) << " |";
    for (const std::int64_t d : {3, 5, 8})
    {
        const auto residues = c.set.modulo(d);
        std::cout << " mod" << d << '=';
        if (residues)
        {
            std::cout << renderSet(*residues);
        }
        else
        {
            std::cout << "REFUSED";  // exact-or-refuse: never an approximate residue set
        }
    }
    std::cout << " | values=";
    if (const auto rs = c.set.runSet())
    {
        if (const auto values = rs->materialize(kValuesLimit))
        {
            std::cout << renderSet(*values);
        }
        else if (const auto n = rs->count())
        {
            std::cout << "BIG count=" << *n;
        }
        else
        {
            std::cout << "BIG";
        }
    }
    else
    {
        const auto expansion = c.set.expandChecked(kValuesLimit);
        if (expansion.exact)
        {
            std::cout << renderSet(expansion.values);
        }
        else
        {
            std::cout << "BIG";
        }
    }
    std::cout << '\n';
}

}  // namespace

int main(int argc, char** argv)
{
    std::size_t randomCases = 500;
    if (argc > 1)
    {
        randomCases = static_cast<std::size_t>(std::stoul(argv[1]));
    }

    std::size_t id = 0;
    for (const auto& c : directedCases())
    {
        emitCase(id++, c);
    }
    for (std::size_t seed = 0; seed < randomCases; ++seed)
    {
        std::mt19937 rng(static_cast<std::uint32_t>(seed));
        emitCase(id++, makeRandomCase(rng));
    }
    return 0;
}
