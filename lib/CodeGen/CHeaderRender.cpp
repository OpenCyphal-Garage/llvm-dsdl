//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements shared C header rendering helpers.
///
/// These helpers produce deterministic metadata macro and wrapper text used by
/// the C backend.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/CHeaderRender.h"
#include <cstdint>
#include <string>
#include <vector>

namespace llvmdsdl
{

std::vector<std::string> renderCTypeMetadataMacros(const CHeaderTypeMetadata& metadata)
{
    return {
        "#define " + metadata.typeName + "_FULL_NAME_ \"" + metadata.fullName + "\"",
        "#define " + metadata.typeName + "_FULL_NAME_AND_VERSION_ \"" + metadata.fullName + "." +
            std::to_string(metadata.majorVersion) + "." + std::to_string(metadata.minorVersion) + "\"",
        "#define " + metadata.typeName + "_EXTENT_BYTES_ " + std::to_string(metadata.extentBytes) + "UL",
        "#define " + metadata.typeName + "_SERIALIZATION_BUFFER_SIZE_BYTES_ " +
            std::to_string(metadata.serializationBufferSizeBytes) + "UL",
    };
}

std::vector<std::string> renderCServiceAliasIdentityMacros(const std::string&  baseTypeName,
                                                           const std::string&  fullName,
                                                           const std::uint32_t majorVersion,
                                                           const std::uint32_t minorVersion)
{
    return {
        "#define " + baseTypeName + "_FULL_NAME_ \"" + fullName + "\"",
        "#define " + baseTypeName + "_FULL_NAME_AND_VERSION_ \"" + fullName + "." + std::to_string(majorVersion) + "." +
            std::to_string(minorVersion) + "\"",
    };
}

std::vector<std::string> renderCServiceAliasBridgeLines(const std::string& baseTypeName,
                                                        const std::string& requestTypeName)
{
    return {
        "typedef " + requestTypeName + " " + baseTypeName + ";",
        "#define " + baseTypeName + "_EXTENT_BYTES_ " + requestTypeName + "_EXTENT_BYTES_",
        "#define " + baseTypeName + "_SERIALIZATION_BUFFER_SIZE_BYTES_ " + requestTypeName +
            "_SERIALIZATION_BUFFER_SIZE_BYTES_",
        "#define " + baseTypeName + "_ZOH_ALIAS_ELIGIBLE_ " + requestTypeName + "_ZOH_ALIAS_ELIGIBLE_",
        "#define " + baseTypeName + "_ZOH_ALIAS_REASON_ " + requestTypeName + "_ZOH_ALIAS_REASON_",
    };
}

std::vector<std::string> renderCServiceAliasWrapperLines(const std::string& baseTypeName,
                                                         const std::string& requestTypeName)
{
    return {
        "static inline int8_t " + baseTypeName + "__serialize_(const " + baseTypeName +
            "* const obj, uint8_t* const buffer, size_t* const inout_buffer_size_bytes)",
        "{",
        "  return " + requestTypeName + "__serialize_((const " + requestTypeName +
            "*)obj, buffer, inout_buffer_size_bytes);",
        "}",
        "static inline int8_t " + baseTypeName + "__deserialize_(" + baseTypeName +
            "* const out_obj, const uint8_t* buffer, size_t* const inout_buffer_size_bytes)",
        "{",
        "  return " + requestTypeName + "__deserialize_((" + requestTypeName +
            "*)out_obj, buffer, inout_buffer_size_bytes);",
        "}",
        "static inline int8_t " + baseTypeName +
            "__try_deserialize_view_(const uint8_t* const buffer, size_t* const inout_buffer_size_bytes, "
            "const uint8_t** const out_view_bytes)",
        "{",
        "  return " + requestTypeName + "__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);",
        "}",
        "static inline int8_t " + baseTypeName +
            "__try_serialize_view_(const uint8_t* const view_bytes, const size_t view_size_bytes, "
            "uint8_t* const buffer, size_t* const inout_buffer_size_bytes)",
        "{",
        "  return " + requestTypeName +
            "__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);",
        "}",
    };
}

}  // namespace llvmdsdl
