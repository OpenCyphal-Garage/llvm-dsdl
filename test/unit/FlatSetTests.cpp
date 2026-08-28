//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Tests for the sorted-vector set that stands in for `std::flat_set`.
///
/// BitLengthSet exercises this container heavily, but only through the shapes it
/// happens to use, and a container that is subtly wrong in an unused corner is a
/// trap for the next caller. The cases here are the ones where a sorted-vector
/// implementation goes wrong: the range insert (which sorts only the appended
/// tail and merges, rather than re-sorting everything), duplicates that straddle
/// the merge boundary, and the `sorted_unique` constructor that trusts its input.
///
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "llvmdsdl/Support/FlatSet.h"

#include "UnitTests.h"

namespace
{

using llvmdsdl::FlatSet;
using llvmdsdl::sorted_unique;

struct TestContext final
{
    bool ok{true};

    void expect(const bool condition, const std::string& what)
    {
        if (!condition)
        {
            ok = false;
            std::cerr << "FlatSet test failed: " << what << '\n';
        }
    }

    void expectContents(const FlatSet<std::int64_t>&     set,
                        const std::vector<std::int64_t>& expected,
                        const std::string&               what)
    {
        const std::vector<std::int64_t> actual(set.begin(), set.end());
        if (actual != expected)
        {
            ok = false;
            std::cerr << "FlatSet test failed: " << what << "\n  expected:";
            for (const auto v : expected)
            {
                std::cerr << ' ' << v;
            }
            std::cerr << "\n  actual:  ";
            for (const auto v : actual)
            {
                std::cerr << ' ' << v;
            }
            std::cerr << '\n';
        }
    }
};

void testConstruction(TestContext& t)
{
    t.expectContents(FlatSet<std::int64_t>{}, {}, "default construction is empty");
    t.expect(FlatSet<std::int64_t>{}.empty(), "default construction reports empty");

    // Initializer lists are neither sorted nor unique in general.
    t.expectContents(FlatSet<std::int64_t>{5, 1, 5, 3, 1}, {1, 3, 5}, "initializer list is sorted and de-duplicated");

    std::vector<std::int64_t> unsorted{9, 2, 9, 4};
    t.expectContents(FlatSet<std::int64_t>(std::move(unsorted)),
                     {2, 4, 9},
                     "vector constructor sorts and de-duplicates");

    const std::vector<std::int64_t> already{1, 2, 3};
    t.expectContents(FlatSet<std::int64_t>(sorted_unique, already.begin(), already.end()),
                     {1, 2, 3},
                     "sorted_unique constructor preserves a conforming range");
}

void testSingleInsert(TestContext& t)
{
    FlatSet<std::int64_t> s;
    t.expect(s.insert(10).second, "inserting a new element reports insertion");
    t.expect(!s.insert(10).second, "inserting a duplicate reports no insertion");
    s.insert(5);
    s.insert(20);
    t.expectContents(s, {5, 10, 20}, "single inserts keep sorted order regardless of arrival order");
    t.expect(s.size() == 3, "size counts unique elements");
    t.expect(*s.insert(7).first == 7, "insert returns an iterator to the element");
}

void testRangeInsert(TestContext& t)
{
    // Into an empty set: the merge has nothing on the left-hand side.
    FlatSet<std::int64_t>           empty;
    const std::vector<std::int64_t> src{3, 1, 2};
    empty.insert(src.begin(), src.end());
    t.expectContents(empty, {1, 2, 3}, "range insert into an empty set sorts the input");

    // Interleaved, so a merge that mishandled either run would be visible.
    FlatSet<std::int64_t>           interleaved{1, 3, 5, 7};
    const std::vector<std::int64_t> evens{2, 4, 6, 8};
    interleaved.insert(evens.begin(), evens.end());
    t.expectContents(interleaved, {1, 2, 3, 4, 5, 6, 7, 8}, "range insert interleaves correctly");

    // Duplicates on both sides of the boundary, and within the incoming range.
    FlatSet<std::int64_t>           overlapping{10, 20, 30};
    const std::vector<std::int64_t> dupes{30, 20, 20, 40};
    overlapping.insert(dupes.begin(), dupes.end());
    t.expectContents(overlapping, {10, 20, 30, 40}, "range insert de-duplicates across the merge boundary");

    // Entirely below the existing contents: the appended tail sorts ahead of
    // everything, which a naive merge that assumed append-order would get wrong.
    FlatSet<std::int64_t>           below{100, 200};
    const std::vector<std::int64_t> small{3, 1, 2};
    below.insert(small.begin(), small.end());
    t.expectContents(below, {1, 2, 3, 100, 200}, "range insert wholly below existing contents");

    FlatSet<std::int64_t>           untouched{1, 2};
    const std::vector<std::int64_t> nothing;
    untouched.insert(nothing.begin(), nothing.end());
    t.expectContents(untouched, {1, 2}, "empty range insert is a no-op");
}

void testEraseAndLookup(TestContext& t)
{
    FlatSet<std::int64_t> s{1, 2, 3, 4};
    // BitLengthSet's truncating paths erase the largest element repeatedly.
    s.erase(std::prev(s.end()));
    t.expectContents(s, {1, 2, 3}, "erasing the last element drops the maximum");
    t.expect(*s.rbegin() == 3, "rbegin observes the new maximum");

    t.expect(s.contains(2), "contains finds a present element");
    t.expect(!s.contains(99), "contains rejects an absent element");
    // These name count() and find() on purpose: this suite is what says those two work, so
    // readability-container-contains must not fold them into the contains() checks above.
    // NOLINTBEGIN(readability-container-contains)
    t.expect(s.count(2) == 1, "count is 1 for a present element");
    t.expect(s.count(99) == 0, "count is 0 for an absent element");
    t.expect(s.find(3) != s.end(), "find locates a present element");
    t.expect(s.find(99) == s.end(), "find returns end for an absent element");
    // NOLINTEND(readability-container-contains)

    s.clear();
    t.expect(s.empty(), "clear empties the set");
}

}  // namespace

bool runFlatSetTests()
{
    TestContext t;
    testConstruction(t);
    testSingleInsert(t);
    testRangeInsert(t);
    testEraseAndLookup(t);
    return t.ok;
}
