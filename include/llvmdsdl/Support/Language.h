//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The languages dsdlc generates.
///
/// Nothing else in the compiler enumerates them. What a language can express, what its generated
/// interface hands a body, and how the output composes its declarations are the language's row in
/// `LanguageTraits.h`, and code that needs one of those reads the row rather than comparing the
/// language. A spelling table -- how a language writes a name, a literal or a type -- is keyed on
/// the language, as is each backend's own code.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SUPPORT_LANGUAGE_H
#define LLVMDSDL_SUPPORT_LANGUAGE_H

namespace llvmdsdl
{

/// @brief A language dsdlc generates.
enum class Language
{
    /// @brief C.
    C,

    /// @brief C++.
    Cpp,

    /// @brief Rust.
    Rust,

    /// @brief Go.
    Go,

    /// @brief TypeScript.
    TypeScript,

    /// @brief Python.
    Python,
};

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SUPPORT_LANGUAGE_H
