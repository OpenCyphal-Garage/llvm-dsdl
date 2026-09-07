//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared C header rendering helpers for generated type metadata and wrappers.
///
/// This utility centralises C metadata macro and service alias wrapper text used
/// by the C backend emitter.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_EMITTER_C_HEADER_RENDER_H
#define LLVMDSDL_CODEGEN_EMITTER_C_HEADER_RENDER_H

#include <cstdint>
#include <string>
#include <vector>

namespace llvmdsdl::emitter::c
{

/// @brief Metadata macro inputs for one generated C type.
struct HeaderTypeMetadata final
{
    /// @brief Generated C type name stem.
    std::string typeName;

    /// @brief Fully-qualified DSDL full name.
    std::string fullName;

    /// @brief DSDL major version.
    std::uint32_t majorVersion{0};

    /// @brief DSDL minor version.
    std::uint32_t minorVersion{0};

    /// @brief Type extent in bytes.
    std::uint64_t extentBytes{0};

    /// @brief Type serialisation buffer size in bytes.
    std::uint64_t serializationBufferSizeBytes{0};
};

/// @brief Renders C metadata macros for one generated type.
/// @param[in] metadata Type metadata for macro rendering.
/// @return Ordered macro lines.
std::vector<std::string> renderTypeMetadataMacros(const HeaderTypeMetadata& metadata);

/// @brief Renders service alias identity metadata macro lines.
/// @param[in] baseTypeName Alias base type name.
/// @param[in] fullName Service full DSDL name.
/// @param[in] majorVersion DSDL major version.
/// @param[in] minorVersion DSDL minor version.
/// @return Ordered macro lines.
std::vector<std::string> renderServiceAliasIdentityMacros(const std::string& baseTypeName,
                                                          const std::string& fullName,
                                                          std::uint32_t      majorVersion,
                                                          std::uint32_t      minorVersion);

/// @brief Renders service alias bridge lines after request type declaration.
/// @param[in] baseTypeName Alias base type name.
/// @param[in] requestTypeName Request section generated type name.
/// @param[in] deprecatedAttribute Whether the alias typedef carries `__attribute__((deprecated))`.
/// @return Ordered typedef/bridge macro lines.
std::vector<std::string> renderServiceAliasBridgeLines(const std::string& baseTypeName,
                                                       const std::string& requestTypeName,
                                                       bool               deprecatedAttribute);

/// @brief Renders service alias serialise/deserialize inline wrappers.
/// @param[in] baseTypeName Alias base type name.
/// @param[in] requestTypeName Request section generated type name.
/// @return Ordered wrapper lines.
std::vector<std::string> renderServiceAliasWrapperLines(const std::string& baseTypeName,
                                                        const std::string& requestTypeName);

}  // namespace llvmdsdl::emitter::c

#endif  // LLVMDSDL_CODEGEN_EMITTER_C_HEADER_RENDER_H
