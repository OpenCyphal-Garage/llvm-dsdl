//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements shared semantic definition path/type projection helpers.
///
/// This component consolidates deterministic name and path projection used by
/// scripted emitters.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/DefinitionPathProjection.h"

#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include <cstdint>
#include <filesystem>
#include <llvm/ADT/StringRef.h>
#include <string>
#include <vector>

namespace llvmdsdl
{
namespace
{

std::string normalizedExtension(const llvm::StringRef extension)
{
    if (extension.empty())
    {
        return "";
    }
    if (extension.front() == '.')
    {
        return extension.str();
    }
    return "." + extension.str();
}

std::filesystem::path renderRelativeTypeFilePathImpl(const CodegenNamingLanguage     language,
                                                     const std::vector<std::string>& namespaceComponents,
                                                     const llvm::StringRef           shortName,
                                                     const std::uint32_t             majorVersion,
                                                     const std::uint32_t             minorVersion,
                                                     const llvm::StringRef           extension)
{
    auto path = renderNamespaceRelativePath(language, namespaceComponents);
    path /= renderVersionedFileStem(language, shortName, majorVersion, minorVersion) + normalizedExtension(extension);
    return path;
}

}  // namespace

std::string renderVersionedTypeName(const CodegenNamingLanguage language,
                                    const llvm::StringRef       shortName,
                                    const std::uint32_t         majorVersion,
                                    const std::uint32_t         minorVersion)
{
    return codegenProjectIdentifier(language, IdentifierRole::TypeName, shortName) + "_" +
           std::to_string(majorVersion) + "_" + std::to_string(minorVersion);
}

std::string renderVersionedFileStem(const CodegenNamingLanguage language,
                                    const llvm::StringRef       shortName,
                                    const std::uint32_t         majorVersion,
                                    const std::uint32_t         minorVersion)
{
    return renderDefinitionFileStem(language, shortName, majorVersion, minorVersion);
}

std::filesystem::path renderNamespaceRelativePath(const CodegenNamingLanguage     language,
                                                  const std::vector<std::string>& namespaceComponents)
{
    std::filesystem::path path;
    for (const auto& component : namespaceComponents)
    {
        path /= codegenProjectIdentifier(language, IdentifierRole::NamespaceName, component);
    }
    return path;
}

std::filesystem::path renderRelativeTypeFilePath(const CodegenNamingLanguage language,
                                                 const DiscoveredDefinition& info,
                                                 const llvm::StringRef       extension)
{
    return renderRelativeTypeFilePathImpl(language,
                                          info.namespaceComponents,
                                          info.shortName,
                                          info.majorVersion,
                                          info.minorVersion,
                                          extension);
}

std::filesystem::path renderRelativeTypeFilePath(const CodegenNamingLanguage language,
                                                 const SemanticTypeRef&      ref,
                                                 const llvm::StringRef       extension)
{
    return renderRelativeTypeFilePathImpl(language,
                                          ref.namespaceComponents,
                                          ref.shortName,
                                          ref.majorVersion,
                                          ref.minorVersion,
                                          extension);
}

}  // namespace llvmdsdl
