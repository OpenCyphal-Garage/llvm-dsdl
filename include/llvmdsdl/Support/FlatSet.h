//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// A sorted-vector set, standing in for `std::flat_set`.
///
/// `std::flat_set` is C++23 and reached libstdc++ only in GCC 15, which no
/// Ubuntu before 26.04 ships. Depending on it would pin released Linux packages
/// to a glibc floor that excludes 24.04 LTS, and the obvious escape -- building
/// against libc++, which does have it -- is closed: the distro `libLLVM` links
/// libstdc++, and putting two C++ runtimes either side of MLIR's `std::string`
/// boundary is an ABI hazard. So the container is provided here instead, and
/// used on every platform: the code that ships is the code that gets tested.
///
/// It implements the subset `BitLengthSet` uses and nothing more, so there is
/// less to get wrong. Like `std::flat_set` it keeps elements sorted and unique
/// in contiguous storage, so iteration order matches both `std::flat_set` and
/// `std::set` -- which is what keeps generated output byte-reproducible.
///
/// Performance matters here: `BitLengthSet` is CPU-intensive, and its Minkowski
/// sum inserts into a capped set inside a doubly-nested loop. Measured against
/// libc++'s `std::flat_set` on that pattern and on the union path, both land
/// within noise of parity (0.98x-1.08x). `std::flat_set` is itself a sorted
/// vector, so single-element insert is linear in both, and the range insert
/// below merges rather than re-sorting for the same reason the standard
/// specifies it that way. An earlier version of that method re-sorted the whole
/// container and was 7x slower on a 16k-element union.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SUPPORT_FLATSET_H
#define LLVMDSDL_SUPPORT_FLATSET_H

#include <algorithm>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <utility>
#include <vector>

namespace llvmdsdl
{

/// @brief Tag selecting the constructor that trusts its input to be sorted and unique.
/// @details Mirrors `std::sorted_unique_t`; kept separate so this header does not
///   depend on `<flat_set>` being present at all.
struct sorted_unique_t final
{
    explicit sorted_unique_t() = default;
};

inline constexpr sorted_unique_t sorted_unique{};

/// @brief An ordered set stored as a sorted, unique, contiguous sequence.
/// @tparam T Element type; must be totally ordered by @p Compare.
/// @tparam Compare Strict weak ordering used for both lookup and iteration order.
template <typename T, typename Compare = std::less<T>>
class FlatSet final
{
public:
    using value_type     = T;
    using key_type       = T;
    using key_compare    = Compare;
    using container_type = std::vector<T>;
    using size_type      = typename container_type::size_type;
    // Elements are keys, so they are immutable in place: mutating one through an
    // iterator could break the sorted invariant the whole class rests on.
    using const_iterator         = typename container_type::const_iterator;
    using iterator               = const_iterator;
    using const_reverse_iterator = typename container_type::const_reverse_iterator;
    using reverse_iterator       = const_reverse_iterator;

    FlatSet() = default;

    FlatSet(std::initializer_list<T> init)
    {
        data_.assign(init.begin(), init.end());
        sortAndUnique();
    }

    /// @brief Builds from a range already known to be sorted and free of duplicates.
    /// @details Skips the sort, so passing an unsorted or duplicated range is a
    ///   precondition violation that silently breaks lookup -- exactly as it would
    ///   with `std::flat_set`.
    template <typename InputIt>
    FlatSet(sorted_unique_t, InputIt first, InputIt last)
        : data_(first, last)
    {
    }

    /// @brief Adopts a vector's storage, sorting and de-duplicating it in place.
    explicit FlatSet(container_type&& values)
        : data_(std::move(values))
    {
        sortAndUnique();
    }

    [[nodiscard]] const_iterator begin() const noexcept
    {
        return data_.begin();
    }
    [[nodiscard]] const_iterator end() const noexcept
    {
        return data_.end();
    }
    [[nodiscard]] const_iterator cbegin() const noexcept
    {
        return data_.cbegin();
    }
    [[nodiscard]] const_iterator cend() const noexcept
    {
        return data_.cend();
    }
    [[nodiscard]] const_reverse_iterator rbegin() const noexcept
    {
        return data_.rbegin();
    }
    [[nodiscard]] const_reverse_iterator rend() const noexcept
    {
        return data_.rend();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return data_.empty();
    }
    [[nodiscard]] size_type size() const noexcept
    {
        return data_.size();
    }

    void clear() noexcept
    {
        data_.clear();
    }

    /// @brief Inserts @p value if absent.
    /// @return The position of the element, and whether this call inserted it.
    std::pair<iterator, bool> insert(const T& value)
    {
        auto pos = std::lower_bound(data_.begin(), data_.end(), value, Compare{});
        if (pos != data_.end() && !Compare{}(value, *pos))
        {
            return {const_iterator(pos), false};
        }
        const auto inserted = data_.insert(pos, value);
        return {const_iterator(inserted), true};
    }

    /// @brief Inserts every element of [@p first, @p last).
    /// @details Appends the new elements, sorts only that tail, then merges the two
    ///   sorted runs in place. Re-sorting the whole container instead would be
    ///   O((N+M) log(N+M)) and throw away the ordering the existing elements
    ///   already have -- measurably so: on a 16k-element union that costs about
    ///   7x the merge. std::flat_set's range insert is specified this way for the
    ///   same reason.
    template <typename InputIt>
    void insert(InputIt first, InputIt last)
    {
        if (first == last)
        {
            return;
        }
        // An index, not an iterator: appending may reallocate.
        const size_type pivot = data_.size();
        data_.insert(data_.end(), first, last);
        std::sort(data_.begin() + static_cast<std::ptrdiff_t>(pivot), data_.end(), Compare{});
        std::inplace_merge(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>(pivot), data_.end(), Compare{});
        eraseAdjacentDuplicates();
    }

    /// @brief Removes the element at @p pos.
    /// @return Iterator following the removed element.
    iterator erase(const_iterator pos)
    {
        // vector::erase takes a const_iterator and returns a mutable one; convert
        // back so callers keep seeing the immutable-element view.
        return const_iterator(data_.erase(pos));
    }

    [[nodiscard]] const_iterator find(const T& value) const
    {
        auto pos = std::lower_bound(data_.begin(), data_.end(), value, Compare{});
        if (pos != data_.end() && !Compare{}(value, *pos))
        {
            return pos;
        }
        return data_.end();
    }

    [[nodiscard]] bool contains(const T& value) const
    {
        return find(value) != data_.end();
    }

    /// @brief Number of elements equal to @p value; always 0 or 1, the keys being unique.
    [[nodiscard]] size_type count(const T& value) const
    {
        return contains(value) ? size_type{1} : size_type{0};
    }

    [[nodiscard]] friend bool operator==(const FlatSet& lhs, const FlatSet& rhs)
    {
        return lhs.data_ == rhs.data_;
    }

private:
    void sortAndUnique()
    {
        std::sort(data_.begin(), data_.end(), Compare{});
        eraseAdjacentDuplicates();
    }

    /// @brief Collapses runs of equivalent elements. Requires the data to be sorted.
    /// @details Equivalence is derived from @p Compare rather than `operator==`, so a
    ///   comparator that treats distinct values as equal keeps exactly one of them --
    ///   matching how the ordered standard containers define uniqueness.
    void eraseAdjacentDuplicates()
    {
        data_.erase(std::unique(data_.begin(),
                                data_.end(),
                                [](const T& a, const T& b) { return !Compare{}(a, b) && !Compare{}(b, a); }),
                    data_.end());
    }

    container_type data_;
};

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SUPPORT_FLATSET_H
