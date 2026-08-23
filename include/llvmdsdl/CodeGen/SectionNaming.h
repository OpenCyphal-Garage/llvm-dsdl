//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared identifier scopes for one section's attributes.
///
/// The emitters and the naming manifest both name a section's attributes through these, which is
/// what makes the manifest a report of the identifiers a backend writes.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_SECTION_NAMING_H
#define LLVMDSDL_CODEGEN_SECTION_NAMING_H

#include <cstdint>
#include <string>

#include "llvm/ADT/StringRef.h"

#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/NamingPolicy.h"

namespace llvmdsdl
{

/// @brief Which of the two constants a variable- or fixed-length array field contributes.
enum class ArrayMetadataKind : std::uint8_t
{
    Capacity,
    IsVariableLength,
};

/// @brief The name an array field's generated metadata constant is declared under in a section scope.
///
/// C and C++ emit `<FIELD>_ARRAY_CAPACITY` and `<FIELD>_ARRAY_IS_VARIABLE_LENGTH` beside the DSDL
/// constants, so those names have to be allocated from the same scope or nothing keeps them apart:
/// two array fields whose macro projections are equal (`fooBar` and `FooBar`) would otherwise declare
/// one constant twice, and a DSDL constant named `foo_array_capacity` would collide with the metadata
/// of an array field named `foo`.
///
/// The name is composed from the DSDL field name rather than from its projection, so that the scope
/// sees the collision: equal projections of the field name give equal projections here. It carries
/// the trailing `_` that C puts on a generated macro and C++ does not, because the scope compares
/// what is emitted -- without it, C would report a collision between a metadata macro and a DSDL
/// constant that the trailing `_` keeps apart.
/// @param[in] language Naming language.
/// @param[in] fieldName DSDL field name.
/// @param[in] kind Which constant.
/// @return The scope key, to be declared and read back under @ref IdentifierRole::MacroName.
[[nodiscard]] std::string arrayMetadataName(CodegenNamingLanguage language,
                                            llvm::StringRef       fieldName,
                                            ArrayMetadataKind     kind);

/// @brief Builds the field-name scope for @p section in @p language.
///
/// Fields are declared in DSDL order, which makes the assignment reproducible; padding fields carry
/// no name and are skipped. Where the language declares constants into the same region as fields the
/// constants are declared here too, so the two cannot collide -- see @ref makeSectionConstantScope.
/// @param[in] language Naming language.
/// @param[in] section The section whose fields are being named.
/// @return A scope with every field declared.
[[nodiscard]] NamingScope makeSectionFieldScope(CodegenNamingLanguage language, const SemanticSection& section);

/// @brief Builds the constant-name scope for @p section in @p language.
///
/// A C++ struct body holds the fields and the constants, so a field and a constant that project onto
/// one identifier are a redeclaration; for C++ this returns the same scope as
/// @ref makeSectionFieldScope. The other five put constants somewhere a field cannot reach -- outside
/// the type, or behind a macro prefix -- and get a scope of their own.
/// @param[in] language Naming language.
/// @param[in] section The section whose constants are being named.
/// @return A scope with every constant declared.
[[nodiscard]] NamingScope makeSectionConstantScope(CodegenNamingLanguage language, const SemanticSection& section);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_SECTION_NAMING_H
