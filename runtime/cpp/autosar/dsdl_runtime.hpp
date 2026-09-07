//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// AUTOSAR-oriented C++ wrappers for the shared C runtime used by generated DSDL bindings.
///
/// This header re-exports the C runtime and provides deterministic bounded
/// variable-array storage used by generated AUTOSAR C++14 bindings.
///
//===----------------------------------------------------------------------===//

#ifndef LLVMDSDL_CPP_RUNTIME_HPP
#define LLVMDSDL_CPP_RUNTIME_HPP

#include <algorithm>
#include <array>
#include <cstddef>

#if defined(__cplusplus) && (__cplusplus >= 201703L)
#    if defined(__has_cpp_attribute)
#        if __has_cpp_attribute(nodiscard)
#            define LLVMDSDL_NODISCARD [[nodiscard]]
#        else
#            define LLVMDSDL_NODISCARD
#        endif
#    else
#        define LLVMDSDL_NODISCARD [[nodiscard]]
#    endif
#else
#    define LLVMDSDL_NODISCARD
#endif

extern "C"
{
#include "dsdl_runtime.h"
}

namespace llvmdsdl
{
namespace cpp
{
namespace autosar
{

/// @brief Deterministic fixed-capacity vector with logical size tracking.
/// @tparam T Element type.
/// @tparam Capacity Maximum number of materialised elements.
template <typename T, std::size_t Capacity>
class BoundedVector final
{
public:
    using value_type         = T;
    using size_type          = std::size_t;
    using reference          = value_type&;
    using const_reference    = const value_type&;
    using pointer            = value_type*;
    using const_pointer      = const value_type*;
    using iterator           = pointer;
    using const_iterator     = const_pointer;
    using storage_array_type = std::array<value_type, Capacity>;

    BoundedVector() = default;

    size_type size() const noexcept
    {
        return logical_size_;
    }

    static constexpr size_type capacity() noexcept
    {
        return Capacity;
    }

    bool empty() const noexcept
    {
        return logical_size_ == 0U;
    }

    void clear() noexcept
    {
        logical_size_ = 0U;
    }

    void resize(const size_type count) noexcept
    {
        logical_size_ = count;
    }

    void resize(const size_type count, const value_type& value) noexcept
    {
        const size_type previous = materialized_size();
        logical_size_            = count;
        const size_type current  = materialized_size();
        for (size_type i = previous; i < current; ++i)
        {
            storage_[i] = value;
        }
    }

    reference operator[](const size_type index) noexcept
    {
        return storage_[index];
    }

    const_reference operator[](const size_type index) const noexcept
    {
        return storage_[index];
    }

    pointer data() noexcept
    {
        return storage_.data();
    }

    const_pointer data() const noexcept
    {
        return storage_.data();
    }

    iterator begin() noexcept
    {
        return storage_.data();
    }

    const_iterator begin() const noexcept
    {
        return storage_.data();
    }

    const_iterator cbegin() const noexcept
    {
        return storage_.data();
    }

    iterator end() noexcept
    {
        return storage_.data() + materialized_size();
    }

    const_iterator end() const noexcept
    {
        return storage_.data() + materialized_size();
    }

    const_iterator cend() const noexcept
    {
        return storage_.data() + materialized_size();
    }

private:
    size_type materialized_size() const noexcept
    {
        return std::min(logical_size_, Capacity);
    }

    storage_array_type storage_{};
    size_type          logical_size_{0U};
};

}  // namespace autosar
}  // namespace cpp
}  // namespace llvmdsdl

#endif  // LLVMDSDL_CPP_RUNTIME_HPP
