//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Semantics/BitLengthSet.h"
#include "llvmdsdl/Semantics/RunSet.h"
#include "llvmdsdl/Support/FlatSet.h"

#include <cstddef>
#include <llvm/Support/raw_ostream.h>
#include <exception>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <initializer_list>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{

using llvmdsdl::BitLengthSet;
using llvmdsdl::FlatSet;
using llvmdsdl::RunSet;
using Values = std::set<std::int64_t>;

struct Tests final
{
    int failures{0};

    void expect(bool condition, const std::string& description)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "BLS exactness FAIL: " << description << '\n';
        }
    }

    template <typename Range>
    void expectValues(const Range& actual, const Values& expected, const std::string& description)
    {
        expect(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()), description);
    }

    void expectRun(const std::optional<RunSet>& actual, const Values& expected, const std::string& description)
    {
        expect(actual.has_value(), description + " returns an exact result");
        if (!actual)
        {
            return;
        }
        const bool valid = actual->valid();
        expect(valid, description + " returns a valid RunSet");
        if (!valid)
        {
            return;
        }
        const auto values = actual->materialize(1024);
        expect(values.has_value(), description + " materializes within the test bound");
        if (values)
        {
            expectValues(*values, expected, description + " matches enumeration");
        }
    }

    void expectRunOrRefusal(const std::optional<RunSet>& actual,
                            const std::optional<Values>& expected,
                            const std::string&           description)
    {
        if (!expected)
        {
            expect(!actual, description + " refuses an unrepresentable result");
            return;
        }
        if (!actual)
        {
            return;  // refusal is contract-permitted; batteries pin a non-vacuity floor below
        }
        ++verifiedExactResults;
        expectRun(actual, *expected, description);
    }

    /// Count of expectRunOrRefusal calls that verified an exact result rather than tolerating a
    /// refusal, so a battery can assert it did not pass vacuously against blanket refusals.
    std::size_t verifiedExactResults{0};
};

std::string renderValues(const Values& values)
{
    std::string out = "{";
    bool        first{true};
    for (const auto value : values)
    {
        out += (first ? "" : ",") + std::to_string(value);
        first = false;
    }
    return out + "}";
}

FlatSet<std::int64_t> toFlatSet(const Values& values)
{
    FlatSet<std::int64_t> out;
    out.insert(values.begin(), values.end());
    return out;
}

Values sum(const Values& lhs, const Values& rhs)
{
    Values out;
    for (const auto x : lhs)
    {
        for (const auto y : rhs)
        {
            const __int128 value = static_cast<__int128>(x) + y;
            if (value >= std::numeric_limits<std::int64_t>::min() && value <= std::numeric_limits<std::int64_t>::max())
            {
                out.insert(static_cast<std::int64_t>(value));
            }
        }
    }
    return out;
}

std::optional<Values> checkedSum(const Values& lhs, const Values& rhs)
{
    Values out;
    for (const auto x : lhs)
    {
        for (const auto y : rhs)
        {
            const __int128 value = static_cast<__int128>(x) + y;
            if (value < std::numeric_limits<std::int64_t>::min() || value > std::numeric_limits<std::int64_t>::max())
            {
                return std::nullopt;
            }
            out.insert(static_cast<std::int64_t>(value));
        }
    }
    return out;
}

Values repeated(const Values& values, std::int64_t count)
{
    Values out{0};
    for (std::int64_t i = 0; i < count; ++i)
    {
        out = sum(out, values);
    }
    return out;
}

Values repeatRange(const Values& values, std::int64_t countMax)
{
    Values out;
    for (std::int64_t count = 0; count <= countMax; ++count)
    {
        const auto term = repeated(values, count);
        out.insert(term.begin(), term.end());
    }
    return out;
}

std::optional<Values> checkedRepeated(const Values& values, std::int64_t count)
{
    Values out{0};
    for (std::int64_t i = 0; i < count; ++i)
    {
        const auto next = checkedSum(out, values);
        if (!next)
        {
            return std::nullopt;
        }
        out = *next;
    }
    return out;
}

std::optional<Values> checkedRepeatRange(const Values& values, std::int64_t countMax)
{
    Values out;
    for (std::int64_t count = 0; count <= countMax; ++count)
    {
        const auto term = checkedRepeated(values, count);
        if (!term)
        {
            return std::nullopt;
        }
        out.insert(term->begin(), term->end());
    }
    return out;
}

std::int64_t pad(std::int64_t value, std::int64_t alignment)
{
    const std::int64_t remainder = value % alignment;
    if (remainder == 0)
    {
        return value;
    }
    return value > 0 ? value + alignment - remainder : value - remainder;
}

void testSaturation(Tests& tests)
{
    const std::int64_t maximum        = std::numeric_limits<std::int64_t>::max();
    const auto         expectResidues = [&](const std::optional<FlatSet<std::int64_t>>& actual,
                                            const Values&                               expected,
                                            const std::string&                          description) {
        tests.expect(actual.has_value(), description + " returns an exact result");
        if (actual)
        {
            tests.expectValues(*actual, expected, description);
        }
    };

    expectResidues((BitLengthSet(maximum) + BitLengthSet(maximum - 1)).modulo(8), {7}, "saturating add residues");
    expectResidues(BitLengthSet(maximum).padToAlignment(8).modulo(8), {7}, "saturating pad residues");
    expectResidues(BitLengthSet(maximum).repeat(2).modulo(8), {7}, "saturating repeat residues");
    expectResidues(BitLengthSet(maximum).repeatRange(2).modulo(8), {0, 7}, "saturating repeat-range residues");
    tests.expect(BitLengthSet(maximum).padToAlignment(8).is_aligned_at(8) == std::optional<bool>{false},
                 "saturating alignment is false");

    const BitLengthSet wide = BitLengthSet(Values{2, 5, 12}).repeat(4097);
    tests.expect(wide.equalsExact(wide) == std::optional<bool>{true}, "large self-equality is true");
    tests.expect(wide.isSubsetOfExact(wide) == std::optional<bool>{true}, "large self-subset is true");
    tests.expect(!wide.equalsExact(BitLengthSet(Values{2, 5, 12}).repeat(4097)).has_value(),
                 "undecidable equality refuses");
}

void testSignedRunSets(Tests& tests)
{
    std::vector<Values>             sets;
    const std::vector<std::int64_t> domain{-3, -2, -1, 0, 1, 2, 3};
    for (std::uint32_t mask = 1; mask < (std::uint32_t{1} << domain.size()); ++mask)
    {
        Values values;
        for (std::size_t bit = 0; bit < domain.size(); ++bit)
        {
            if ((mask & (std::uint32_t{1} << bit)) != 0)
            {
                values.insert(domain[bit]);
            }
        }
        sets.push_back(std::move(values));
    }

    std::vector<RunSet> runs;
    runs.reserve(sets.size());
    for (const auto& values : sets)
    {
        RunSet run = RunSet::fromValues(toFlatSet(values));
        tests.expect(run.valid(), "signed source decomposition is valid");
        const auto materialized = run.materialize(32);
        tests.expect(materialized.has_value(), "signed source decomposition materializes");
        if (materialized)
        {
            tests.expectValues(*materialized, values, "signed source decomposition matches enumeration");
        }

        for (std::int64_t alignment = 1; alignment <= 5; ++alignment)
        {
            Values expected;
            for (const auto value : values)
            {
                expected.insert(pad(value, alignment));
            }
            tests.expectRun(run.paddedTo(alignment), expected, "signed padding");
        }
        for (std::int64_t count = 0; count <= 4; ++count)
        {
            tests.expectRun(run.repeated(count), repeated(values, count), "signed repetition");
            tests.expectRun(run.repeatRange(count), repeatRange(values, count), "signed repetition range");
        }
        runs.push_back(std::move(run));
    }

    for (std::size_t i = 0; i < sets.size(); ++i)
    {
        for (std::size_t j = 0; j < sets.size(); ++j)
        {
            Values united = sets[i];
            united.insert(sets[j].begin(), sets[j].end());
            tests.expectRun(RunSet::unite(runs[i], runs[j]), united, "signed union");
            tests.expectRun(RunSet::sum(runs[i], runs[j]), sum(sets[i], sets[j]), "signed sum");
            tests.expect(runs[i].isSubsetOf(runs[j]) ==
                             std::includes(sets[j].begin(), sets[j].end(), sets[i].begin(), sets[i].end()),
                         "signed subset matches enumeration");
            tests.expect(runs[i].equals(runs[j]) == (sets[i] == sets[j]), "signed equality matches enumeration");
        }
    }

    const auto negative = RunSet(-3).repeatRange(3);
    tests.expectRun(negative, {-9, -6, -3, 0}, "negative singleton repetition range");

    FlatSet<std::int64_t> extremes;
    extremes.insert(std::numeric_limits<std::int64_t>::min());
    extremes.insert(1);
    const auto extremeRange = RunSet::fromValues(extremes).repeatRange(2);
    tests.expect(!extremeRange || extremeRange->valid(), "signed extreme repetition range is valid or refuses");

    const std::int64_t maximum = std::numeric_limits<std::int64_t>::max();
    const auto         full    = RunSet(1).repeatRange(maximum - 1);
    tests.expect(full.has_value(), "maximum-cardinality run is representable");
    if (full)
    {
        const auto oversized = RunSet::unite(*full, RunSet(maximum));
        tests.expect(oversized.has_value() && !oversized->count().has_value(),
                     "oversized split RunSet is representable");
        if (oversized)
        {
            tests.expect(oversized->valid(), "oversized split RunSet is valid");
            tests.expect(oversized->equals(*oversized), "oversized split RunSet equals itself");
        }
    }
}

void testSignedBoundaries(Tests& tests)
{
    const std::size_t  verifiedBefore = tests.verifiedExactResults;
    const std::int64_t minimum        = std::numeric_limits<std::int64_t>::min();
    const std::int64_t maximum        = std::numeric_limits<std::int64_t>::max();
    const std::vector<std::int64_t>
        domain{minimum, minimum + 1, minimum + 7, -9, -1, 0, 1, 9, maximum - 7, maximum - 1, maximum};

    std::vector<Values> sets;
    for (std::size_t i = 0; i < domain.size(); ++i)
    {
        sets.push_back({domain[i]});
        for (std::size_t j = i + 1; j < domain.size(); ++j)
        {
            sets.push_back({domain[i], domain[j]});
        }
    }

    std::vector<RunSet> runs;
    runs.reserve(sets.size());
    for (const auto& values : sets)
    {
        RunSet run = RunSet::fromValues(toFlatSet(values));
        tests.expect(run.valid(), "boundary source decomposition is valid");
        const auto materialized = run.materialize(4);
        tests.expect(materialized.has_value(), "boundary source decomposition materializes");
        if (materialized)
        {
            tests.expectValues(*materialized, values, "boundary source decomposition matches enumeration");
        }

        for (const std::int64_t delta : std::initializer_list<std::int64_t>{minimum, -1, 0, 1, maximum})
        {
            Values expected;
            bool   representable = true;
            for (const auto value : values)
            {
                const __int128 shifted = static_cast<__int128>(value) + delta;
                if (shifted < minimum || shifted > maximum)
                {
                    representable = false;
                    break;
                }
                expected.insert(static_cast<std::int64_t>(shifted));
            }
            tests.expectRunOrRefusal(run.shifted(delta),
                                     representable ? std::optional<Values>{expected} : std::nullopt,
                                     "boundary shift");
        }

        for (const std::int64_t alignment : std::initializer_list<std::int64_t>{1, 2, 8, maximum})
        {
            Values expected;
            bool   representable = true;
            for (const auto value : values)
            {
                const std::int64_t remainder = value % alignment;
                auto               padded    = static_cast<__int128>(value);
                if (remainder != 0)
                {
                    padded += (value > 0) ? (alignment - remainder) : -remainder;
                }
                if (padded < minimum || padded > maximum)
                {
                    representable = false;
                    break;
                }
                expected.insert(static_cast<std::int64_t>(padded));
            }
            tests.expectRunOrRefusal(run.paddedTo(alignment),
                                     representable ? std::optional<Values>{expected} : std::nullopt,
                                     "boundary padding");
        }

        for (std::int64_t count = 0; count <= 3; ++count)
        {
            tests.expectRunOrRefusal(run.repeated(count),
                                     checkedRepeated(values, count),
                                     "boundary repetition " + renderValues(values) + " count " + std::to_string(count));
            tests.expectRunOrRefusal(run.repeatRange(count),
                                     checkedRepeatRange(values, count),
                                     "boundary repetition range " + renderValues(values) + " count " +
                                         std::to_string(count));
        }

        for (const std::int64_t divisor : std::initializer_list<std::int64_t>{1, 2, 8, maximum})
        {
            Values expected;
            for (const auto value : values)
            {
                const std::int64_t remainder = value % divisor;
                expected.insert(remainder < 0 ? remainder + divisor : remainder);
            }
            const auto actual = run.residues(divisor);
            tests.expect(actual.has_value(), "boundary residues return an exact result");
            if (actual)
            {
                tests.expectValues(*actual, expected, "boundary residues match arbitrary-precision arithmetic");
            }
        }
        runs.push_back(std::move(run));
    }

    for (std::size_t i = 0; i < sets.size(); ++i)
    {
        for (std::size_t j = 0; j < sets.size(); ++j)
        {
            Values united = sets[i];
            united.insert(sets[j].begin(), sets[j].end());
            tests.expectRunOrRefusal(RunSet::unite(runs[i], runs[j]),
                                     united,
                                     "boundary union " + renderValues(sets[i]) + " and " + renderValues(sets[j]));
            tests.expectRunOrRefusal(RunSet::sum(runs[i], runs[j]),
                                     checkedSum(sets[i], sets[j]),
                                     "boundary sum " + renderValues(sets[i]) + " and " + renderValues(sets[j]));
        }
    }

    // Non-vacuity floor: the battery exists to pin exact boundary behaviour, so the representable
    // cases must actually have produced results — a kernel regression that starts refusing them
    // would otherwise satisfy every expectRunOrRefusal above vacuously. 6625 exact results were
    // observed at the time of writing; the floor leaves ~10% slack for legitimate budget
    // tightening while still catching a kernel whose boundary cases collapse into refusals.
    tests.expect(tests.verifiedExactResults - verifiedBefore >= 6000,
                 "signed-boundary battery verified its representable cases (" +
                     std::to_string(tests.verifiedExactResults - verifiedBefore) + " exact results)");
}

}  // namespace

namespace
{
int runBlsExactness()
{
    Tests tests;
    testSaturation(tests);
    testSignedRunSets(tests);
    testSignedBoundaries(tests);
    if (tests.failures == 0)
    {
        std::cout << "BLS exactness tests passed (" << tests.verifiedExactResults << " exact)\n";
        return 0;
    }
    std::cerr << tests.failures << " BLS exactness test(s) failed\n";
    return 1;
}
}  // namespace

/// @brief Turns an escaping exception into a diagnostic and a failure status.
///
/// Without this the exception would leave `main` and reach std::terminate, which prints nothing a
/// user can act on.
int main()
{
    try
    {
        return runBlsExactness();
    } catch (const std::exception& e)
    {
        llvm::errs() << "bls-exactness: unhandled exception: " << e.what() << "\n";
        return 1;
    } catch (...)
    {
        llvm::errs() << "bls-exactness: unhandled exception of unknown type\n";
        return 1;
    }
}
