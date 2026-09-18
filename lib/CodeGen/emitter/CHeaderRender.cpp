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

#include "llvmdsdl/CodeGen/emitter/CHeaderRender.h"
#include "llvmdsdl/CodeGen/TypeMetadata.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace llvmdsdl::emitter::c
{

std::vector<std::string> renderTypeMetadataMacros(const std::string& typeName, const SectionMetadata& metadata)
{
    std::vector<std::string> lines = {
        "#define " + typeName + "_FULL_NAME_ \"" + metadata.fullName + "\"",
        "#define " + typeName + "_FULL_NAME_AND_VERSION_ \"" + metadata.fullName + "." +
            std::to_string(metadata.majorVersion) + "." + std::to_string(metadata.minorVersion) + "\"",
        "#define " + typeName + "_EXTENT_BYTES_ " + std::to_string(metadata.extentBytes) + "UL",
        "#define " + typeName + "_SERIALIZATION_BUFFER_SIZE_BYTES_ " +
            std::to_string(metadata.serializationBufferSizeBytes) + "UL",
        "#define " + typeName + "_WIRE_FLAT_ " + (metadata.wireFlat.holds ? "true" : "false"),
        "#define " + typeName + "_WIRE_FLAT_REASON_ \"" + metadata.wireFlat.reason + "\"",
        "#define " + typeName + "_HOST_IMAGE_ " + (metadata.hostImage.holds ? "true" : "false"),
        "#define " + typeName + "_HOST_IMAGE_REASON_ \"" + metadata.hostImage.reason + "\"",
        "#define " + typeName + "_IS_DEPRECATED_ " + (metadata.deprecated ? "true" : "false"),
    };
    if (metadata.declaresPortId)
    {
        lines.push_back("#define " + typeName + "_HAS_FIXED_PORT_ID_ " + (metadata.fixedPortId ? "true" : "false"));
        if (metadata.fixedPortId)
        {
            lines.push_back("#define " + typeName + "_FIXED_PORT_ID_ " + std::to_string(*metadata.fixedPortId) + "U");
        }
    }
    return lines;
}

std::vector<std::string> renderServiceAliasIdentityMacros(const std::string&                 baseTypeName,
                                                          const std::string&                 fullName,
                                                          const std::uint32_t                majorVersion,
                                                          const std::uint32_t                minorVersion,
                                                          const std::optional<std::uint32_t> fixedPortId)
{
    std::vector<std::string> lines = {
        "#define " + baseTypeName + "_FULL_NAME_ \"" + fullName + "\"",
        "#define " + baseTypeName + "_FULL_NAME_AND_VERSION_ \"" + fullName + "." + std::to_string(majorVersion) + "." +
            std::to_string(minorVersion) + "\"",
        "#define " + baseTypeName + "_HAS_FIXED_PORT_ID_ " + (fixedPortId ? "true" : "false"),
    };
    if (fixedPortId)
    {
        lines.push_back("#define " + baseTypeName + "_FIXED_PORT_ID_ " + std::to_string(*fixedPortId) + "U");
    }
    return lines;
}

std::vector<std::string> renderServiceAliasBridgeLines(const std::string& baseTypeName,
                                                       const std::string& requestTypeName,
                                                       const bool         deprecatedAttribute)
{
    return {
        "typedef " + renderCTagSpelling(requestTypeName) + " " + baseTypeName +
            (deprecatedAttribute ? " __attribute__((deprecated));" : ";"),
        "#define " + baseTypeName + "_EXTENT_BYTES_ " + requestTypeName + "_EXTENT_BYTES_",
        "#define " + baseTypeName + "_SERIALIZATION_BUFFER_SIZE_BYTES_ " + requestTypeName +
            "_SERIALIZATION_BUFFER_SIZE_BYTES_",
    };
}

std::vector<std::string> renderServiceAliasWrapperLines(const std::string& baseTypeName,
                                                        const std::string& requestTypeName)
{
    const std::string objectType = renderCTagSpelling(requestTypeName);
    return {
        "static inline int8_t " + baseTypeName + "__serialize_(const " + objectType +
            "* const obj, uint8_t* const buffer, size_t* const inout_buffer_size_bytes)",
        "{",
        "  return " + requestTypeName + "__serialize_(obj, buffer, inout_buffer_size_bytes);",
        "}",
        "static inline int8_t " + baseTypeName + "__deserialize_(" + objectType +
            "* const out_obj, const uint8_t* buffer, size_t* const inout_buffer_size_bytes)",
        "{",
        "  return " + requestTypeName + "__deserialize_(out_obj, buffer, inout_buffer_size_bytes);",
        "}",
        "static inline int8_t " + baseTypeName + "__initialize_(" + objectType + "* const out_obj)",
        "{",
        "  return " + requestTypeName + "__initialize_(out_obj);",
        "}",
    };
}

}  // namespace llvmdsdl::emitter::c
