//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Semantics/BitLengthSet.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{

using llvmdsdl::BitLengthSet;
using llvmdsdl::FlatSet;

struct Case final
{
    std::string  recipe;
    BitLengthSet value;
};

std::string render(const FlatSet<std::int64_t>& values)
{
    std::ostringstream out;
    out << '{';
    bool first = true;
    for (const auto value : values)
    {
        if (!first)
        {
            out << ',';
        }
        out << value;
        first = false;
    }
    out << '}';
    return out.str();
}

Case leaf(std::mt19937& random)
{
    constexpr std::int64_t          maximum = std::numeric_limits<std::int64_t>::max();
    const std::vector<std::int64_t> domain{0, 1, 2, 7, 8, 9, maximum - 9, maximum - 2, maximum - 1, maximum};
    std::set<std::int64_t>          values;
    const std::size_t               count = 1 + (random() % 3);
    for (std::size_t i = 0; i < count; ++i)
    {
        values.insert(domain[random() % domain.size()]);
    }
    std::ostringstream recipe;
    recipe << "L{";
    bool first = true;
    for (const auto value : values)
    {
        if (!first)
        {
            recipe << ',';
        }
        recipe << value;
        first = false;
    }
    recipe << '}';
    return Case{recipe.str(), BitLengthSet(values)};
}

Case randomCase(std::uint32_t seed)
{
    std::mt19937      random(seed);
    Case              result     = leaf(random);
    const std::size_t operations = 2 + (random() % 5);
    for (std::size_t operation = 0; operation < operations; ++operation)
    {
        switch (random() % 5)
        {
        case 0: {
            Case const rhs = leaf(random);
            result.recipe  = result.recipe + ' ' + rhs.recipe + " A";
            result.value   = result.value + rhs.value;
            break;
        }
        case 1: {
            Case const rhs = leaf(random);
            result.recipe  = result.recipe + ' ' + rhs.recipe + " U";
            result.value   = result.value | rhs.value;
            break;
        }
        case 2: {
            const std::int64_t alignment = 1 + (random() % 16);
            result.recipe += " P" + std::to_string(alignment);
            result.value = result.value.padToAlignment(alignment);
            break;
        }
        case 3: {
            const std::int64_t count = random() % 5;
            result.recipe += " R" + std::to_string(count);
            result.value = result.value.repeat(count);
            break;
        }
        default: {
            const std::int64_t count = random() % 5;
            result.recipe += " Q" + std::to_string(count);
            result.value = result.value.repeatRange(count);
            break;
        }
        }
    }
    return result;
}

std::vector<Case> directedCases()
{
    const std::int64_t maximum = std::numeric_limits<std::int64_t>::max();
    return {
        {"L{9223372036854775807} L{9223372036854775806} A", BitLengthSet(maximum) + BitLengthSet(maximum - 1)},
        {"L{9223372036854775807} P8", BitLengthSet(maximum).padToAlignment(8)},
        {"L{9223372036854775807} R2", BitLengthSet(maximum).repeat(2)},
        {"L{9223372036854775807} Q2", BitLengthSet(maximum).repeatRange(2)},
        {"L{1,9223372036854775807} R2", BitLengthSet(std::set<std::int64_t>{1, maximum}).repeat(2)},
        {"L{9223372036854775805,9223372036854775807} P16",
         BitLengthSet(std::set<std::int64_t>{maximum - 2, maximum}).padToAlignment(16)},
    };
}

void emit(std::size_t id, const Case& testCase)
{
    std::cout << "CASE " << id << " | " << testCase.recipe << " | min=" << testCase.value.min()
              << " max=" << testCase.value.max() << " fixed=" << (testCase.value.fixed() ? 1 : 0) << " |";
    for (const std::int64_t divisor : {3, 5, 8, 16})
    {
        std::cout << " mod" << divisor << '=';
        if (const auto residues = testCase.value.modulo(divisor))
        {
            std::cout << render(*residues);
        }
        else
        {
            std::cout << "REFUSED";
        }
    }
    const auto expansion = testCase.value.expandChecked(65536);
    std::cout << " | values=" << (expansion.exact ? render(expansion.values) : "REFUSED") << '\n';
}

}  // namespace

int main()
{
    std::size_t id = 0;
    for (const auto& testCase : directedCases())
    {
        emit(id++, testCase);
    }
    for (std::uint32_t seed = 0; seed < 500; ++seed)
    {
        emit(id++, randomCase(seed));
    }
    return 0;
}
