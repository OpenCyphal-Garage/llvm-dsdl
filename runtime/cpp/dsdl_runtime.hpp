//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// C++ convenience wrappers for the shared C runtime used by generated DSDL bindings.
///
/// This header re-exports the C runtime and provides common helpers plus an
/// optional PMR-oriented API used by generated std/pmr C++ bindings.
///
//===----------------------------------------------------------------------===//

#ifndef LLVMDSDL_CPP_RUNTIME_HPP
#define LLVMDSDL_CPP_RUNTIME_HPP

#include <cstddef>
#include <cstdint>

#if defined(__cplusplus) && (__cplusplus >= 201703L) && defined(__has_include)
#    if __has_include(<memory_resource>)
#        include <memory_resource>
#        define LLVMDSDL_CPP_HAS_MEMORY_RESOURCE 1
#    else
#        define LLVMDSDL_CPP_HAS_MEMORY_RESOURCE 0
#    endif
#else
#    define LLVMDSDL_CPP_HAS_MEMORY_RESOURCE 0
#endif

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

/// @brief The source a runtime read primitive takes for a field accessor's buffer: its bytes, or a
///        byte that exists where it holds none.
///
/// The primitives take a non-null source even where they copy nothing, and an empty span may
/// hold a null pointer.
/// @param[in] bytes The span's bytes.
/// @return @p bytes, or a pointer to a byte where @p bytes is null.
inline const std::uint8_t* readable_bytes(const std::uint8_t* const bytes) noexcept
{
    static constexpr std::uint8_t STAND_IN{0U};
    return (bytes != nullptr) ? bytes : &STAND_IN;
}

/// @brief Sets the bit at @p bit_offset of @p buffer to @p value, leaving the rest of its byte.
///
/// One element of a bool array stored a bool per element, moved to the wire.
/// @param[out] buffer The wire buffer.
/// @param[in] bit_offset Where the bit goes, in bits from the start of @p buffer.
/// @param[in] value The element.
inline void set_bit(std::uint8_t* const buffer, const std::size_t bit_offset, const bool value) noexcept
{
    const std::uint8_t bit = value ? 1U : 0U;
    dsdl_runtime_copy_bits(buffer, bit_offset, 1U, &bit, 0U);
}

#if LLVMDSDL_CPP_HAS_MEMORY_RESOURCE
/// @brief Alias for the polymorphic memory-resource abstraction.
using MemoryResource = std::pmr::memory_resource;

/// @brief Returns the process-default polymorphic memory resource.
/// @return Pointer to the current default memory resource.
inline MemoryResource* default_memory_resource() noexcept
{
    return std::pmr::get_default_resource();
}

/// @brief Returns a null memory-resource pointer.
/// @return Always `nullptr`.
inline constexpr MemoryResource* null_memory_resource() noexcept
{
    return nullptr;
}
#endif

}  // namespace cpp
}  // namespace llvmdsdl

#endif  // LLVMDSDL_CPP_RUNTIME_HPP
