//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Tests for shared integer arithmetic helpers.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Support/IntegerMath.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

namespace
{

struct TestContext final
{
    bool ok{true};

    void expect(const bool condition, const std::string& what)
    {
        if (!condition)
        {
            ok = false;
            std::cerr << "IntegerMath test failed: " << what << '\n';
        }
    }

    void expectEq(const std::int64_t actual, const std::int64_t expected, const std::string& what)
    {
        if (actual != expected)
        {
            ok = false;
            std::cerr << "IntegerMath test failed: " << what << "\n  expected: " << expected
                      << "\n  actual:   " << actual << '\n';
        }
    }
};

void testCheckedArithmetic(TestContext& t)
{
    const std::int64_t max = std::numeric_limits<std::int64_t>::max();
    const std::int64_t min = std::numeric_limits<std::int64_t>::min();
    std::int64_t       out = 0;

    t.expect(llvmdsdl::checkedAdd(40, 2, out), "checkedAdd accepts in-range addition");
    t.expectEq(out, 42, "checkedAdd result");
    t.expect(!llvmdsdl::checkedAdd(max, 1, out), "checkedAdd rejects positive overflow");
    t.expect(!llvmdsdl::checkedAdd(min, -1, out), "checkedAdd rejects negative overflow");
    t.expect(llvmdsdl::checkedAdd(max, -1, out), "checkedAdd accepts mixed-sign addition");
    t.expectEq(out, max - 1, "checkedAdd mixed-sign result");

    t.expect(llvmdsdl::checkedMultiply(-7, 6, out), "checkedMultiply accepts in-range product");
    t.expectEq(out, -42, "checkedMultiply result");
    t.expect(!llvmdsdl::checkedMultiply(max, 2, out), "checkedMultiply rejects positive overflow");
    t.expect(!llvmdsdl::checkedMultiply(min, -1, out), "checkedMultiply rejects int64 min negation overflow");
    t.expect(llvmdsdl::checkedMultiply(max / 2, 2, out), "checkedMultiply accepts boundary product");
    t.expectEq(out, max - 1, "checkedMultiply boundary result");
}

void testSaturatingArithmetic(TestContext& t)
{
    const std::int64_t max = std::numeric_limits<std::int64_t>::max();

    t.expectEq(llvmdsdl::saturatingAddNonNegative(40, 2), 42, "saturatingAddNonNegative in range");
    t.expectEq(llvmdsdl::saturatingAddNonNegative(max - 5, 5), max, "saturatingAddNonNegative at ceiling");
    t.expectEq(llvmdsdl::saturatingAddNonNegative(max - 5, 6), max, "saturatingAddNonNegative clamps overflow");

    t.expectEq(llvmdsdl::saturatingMultiplyNonNegative(7, 6), 42, "saturatingMultiplyNonNegative in range");
    t.expectEq(llvmdsdl::saturatingMultiplyNonNegative(0, max), 0, "saturatingMultiplyNonNegative by zero");
    t.expectEq(llvmdsdl::saturatingMultiplyNonNegative(max / 2 + 1, 2),
               max,
               "saturatingMultiplyNonNegative clamps overflow");

    t.expectEq(llvmdsdl::saturatingRoundUpToMultipleNonNegative(0, 8), 0, "round up zero");
    t.expectEq(llvmdsdl::saturatingRoundUpToMultipleNonNegative(9, 8), 16, "round up in range");
    t.expectEq(llvmdsdl::saturatingRoundUpToMultipleNonNegative(max - 3, 8), max, "round up at ceiling");
    t.expectEq(llvmdsdl::saturatingRoundUpToMultipleNonNegative(max - 2, 8), max, "round up clamps overflow");
}

void testModuloAndLcm(TestContext& t)
{
    const std::int64_t max = std::numeric_limits<std::int64_t>::max();
    const std::int64_t min = std::numeric_limits<std::int64_t>::min();
    std::int64_t       out = 0;

    t.expectEq(llvmdsdl::euclideanModulo(5, 3), 2, "euclideanModulo positive dividend");
    t.expectEq(llvmdsdl::euclideanModulo(-1, 8), 7, "euclideanModulo negative dividend");
    t.expectEq(llvmdsdl::euclideanModulo(min, 8), 0, "euclideanModulo int64 min multiple");
    t.expectEq(llvmdsdl::euclideanModulo(min + 1, 8), 1, "euclideanModulo int64 min plus one");
    t.expectEq(llvmdsdl::euclideanModulo(-1, max), max - 1, "euclideanModulo max divisor");

    t.expect(llvmdsdl::lcmAtMost(12, 18, 100, out), "lcmAtMost accepts result under limit");
    t.expectEq(out, 36, "lcmAtMost result");
    t.expect(llvmdsdl::lcmAtMost(21, 6, 42, out), "lcmAtMost accepts result at limit");
    t.expectEq(out, 42, "lcmAtMost result at limit");
    t.expect(!llvmdsdl::lcmAtMost(12, 18, 35, out), "lcmAtMost rejects result above limit");
    t.expect(!llvmdsdl::lcmAtMost(0, 18, 100, out), "lcmAtMost rejects non-positive input");
}

}  // namespace

bool runIntegerMathTests()
{
    TestContext t;
    testCheckedArithmetic(t);
    testSaturatingArithmetic(t);
    testModuloAndLcm(t);
    return t.ok;
}
