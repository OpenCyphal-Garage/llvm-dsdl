//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared semantic definition path/type projection helpers.
///
/// This utility centralizes deterministic versioned type naming and relative
/// source-file path projection used by scripted emitters.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_DEFINITION_PATH_PROJECTION_H
#define LLVMDSDL_CODEGEN_DEFINITION_PATH_PROJECTION_H

#include <filesystem>
#include <string>
#include <vector>

#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvm/ADT/StringRef.h"

namespace llvmdsdl
{

/// @brief Renders a versioned type identifier from semantic name parts.
/// @param[in] language Naming policy language.
/// @param[in] shortName Unqualified semantic type name.
/// @param[in] majorVersion Major version component.
/// @param[in] minorVersion Minor version component.
/// @return Language-projected versioned type name.
std::string renderVersionedTypeName(CodegenNamingLanguage language,
                                    llvm::StringRef       shortName,
                                    std::uint32_t         majorVersion,
                                    std::uint32_t         minorVersion);

/// @brief Renders a versioned file stem from semantic name parts.
/// @param[in] language Naming policy language.
/// @param[in] shortName Unqualified semantic type name.
/// @param[in] majorVersion Major version component.
/// @param[in] minorVersion Minor version component.
/// @return Language-projected versioned file stem.
std::string renderVersionedFileStem(CodegenNamingLanguage language,
                                    llvm::StringRef       shortName,
                                    std::uint32_t         majorVersion,
                                    std::uint32_t         minorVersion);

/// @brief Renders namespace components as a relative path.
/// @param[in] language Naming policy language.
/// @param[in] namespaceComponents Semantic namespace components.
/// @return Relative namespace path.
std::filesystem::path renderNamespaceRelativePath(CodegenNamingLanguage           language,
                                                  const std::vector<std::string>& namespaceComponents);

/// @brief Renders relative file path for one discovered definition.
/// @param[in] language Naming policy language.
/// @param[in] info Discovered definition metadata.
/// @param[in] extension File extension, with or without leading dot.
/// @return Relative file path.
std::filesystem::path renderRelativeTypeFilePath(CodegenNamingLanguage       language,
                                                 const DiscoveredDefinition& info,
                                                 llvm::StringRef             extension);

/// @brief Renders relative file path for one semantic type reference.
/// @param[in] language Naming policy language.
/// @param[in] ref Referenced type metadata.
/// @param[in] extension File extension, with or without leading dot.
/// @return Relative file path.
std::filesystem::path renderRelativeTypeFilePath(CodegenNamingLanguage  language,
                                                 const SemanticTypeRef& ref,
                                                 llvm::StringRef        extension);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_DEFINITION_PATH_PROJECTION_H
