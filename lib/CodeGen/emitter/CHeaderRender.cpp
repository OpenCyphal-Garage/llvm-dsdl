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
#include "llvmdsdl/CodeGen/emitter/CIncludes.h"
#include "llvmdsdl/CodeGen/TypeMetadata.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/GeneratedFact.h"
#include <cstdint>
#include <llvm/ADT/StringRef.h>
#include <optional>
#include <string>
#include <vector>

namespace llvmdsdl::emitter::c
{
namespace
{

/// @brief Opens the definition of the macro @p name.
std::string define(const std::string& name)
{
    return "#define " + name + " ";
}

}  // namespace

std::vector<std::string> renderTypeMetadataMacros(const NameOfFact&      named,
                                                  const SectionMetadata& metadata,
                                                  const CFileNames&      file)
{
    const auto               boolean = [&](const bool value) { return file.standard(value ? "true" : "false"); };
    const auto               macro   = [&](const GeneratedFact fact) { return define(named(fact)); };
    std::vector<std::string> lines   = {
        macro(GeneratedFact::FullName) + "\"" + metadata.fullName + "\"",
        macro(GeneratedFact::FullNameAndVersion) + "\"" + metadata.fullName + "." +
            std::to_string(metadata.majorVersion) + "." + std::to_string(metadata.minorVersion) + "\"",
        macro(GeneratedFact::ExtentBytes) + std::to_string(metadata.extentBytes) + "UL",
        macro(GeneratedFact::SerializationBufferSizeBytes) + std::to_string(metadata.serializationBufferSizeBytes) +
            "UL",
        macro(GeneratedFact::WireFlat) + boolean(metadata.wireFlat.holds),
        macro(GeneratedFact::WireFlatReason) + "\"" + metadata.wireFlat.reason + "\"",
        macro(GeneratedFact::HostImage) + boolean(metadata.hostImage.holds),
        macro(GeneratedFact::HostImageReason) + "\"" + metadata.hostImage.reason + "\"",
        macro(GeneratedFact::IsDeprecated) + boolean(metadata.deprecated),
    };
    if (metadata.declaresPortId)
    {
        lines.push_back(macro(GeneratedFact::HasFixedPortId) + boolean(metadata.fixedPortId.has_value()));
        if (metadata.fixedPortId)
        {
            lines.push_back(macro(GeneratedFact::FixedPortId) + std::to_string(*metadata.fixedPortId) + "U");
        }
    }
    return lines;
}

std::vector<std::string> renderServiceAliasIdentityMacros(const NameOfFact&                  named,
                                                          const std::string&                 fullName,
                                                          const std::uint32_t                majorVersion,
                                                          const std::uint32_t                minorVersion,
                                                          const std::optional<std::uint32_t> fixedPortId,
                                                          const CFileNames&                  file)
{
    const auto               macro = [&](const GeneratedFact fact) { return define(named(fact)); };
    std::vector<std::string> lines = {
        macro(GeneratedFact::FullName) + "\"" + fullName + "\"",
        macro(GeneratedFact::FullNameAndVersion) + "\"" + fullName + "." + std::to_string(majorVersion) + "." +
            std::to_string(minorVersion) + "\"",
        macro(GeneratedFact::HasFixedPortId) + file.standard(fixedPortId ? "true" : "false"),
    };
    if (fixedPortId)
    {
        lines.push_back(macro(GeneratedFact::FixedPortId) + std::to_string(*fixedPortId) + "U");
    }
    return lines;
}

std::vector<std::string> renderLittleEndianGuardLines(const std::string& typeName)
{
    const std::string moves = typeName + ": its serialisation moves the object as the wire's bytes, which holds ";
    const std::string fix   = " Regenerate with --target-triple naming this target.\"";
    // The targets MSVC compiles for, which it states no byte order for.
    const std::string msvc = std::string("defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64)") +
                             " || defined(_M_ARM) || defined(_M_ARM64) || defined(_M_ARM64EC))";
    return {
        "#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)",
        "#  if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__",
        "#    error \"" + moves + "only on a little-endian host." + fix,
        "#  endif",
        "#elif !(" + msvc + ")",
        "#  error \"" + moves + "only on a little-endian host, and this compiler states no byte order." + fix,
        "#endif",
    };
}

std::vector<std::string> renderServiceAliasBridgeLines(const std::string& aliasName,
                                                       const std::string& requestTag,
                                                       const NameOfFact&  alias,
                                                       const NameOfFact&  request,
                                                       const bool         deprecatedAttribute)
{
    return {
        "typedef " + requestTag + " " + aliasName + (deprecatedAttribute ? " __attribute__((deprecated));" : ";"),
        define(alias(GeneratedFact::ExtentBytes)) + request(GeneratedFact::ExtentBytes),
        define(alias(GeneratedFact::SerializationBufferSizeBytes)) +
            request(GeneratedFact::SerializationBufferSizeBytes),
    };
}

std::vector<std::string> renderServiceAliasWrapperLines(const NameOfEntryPoint& service,
                                                        const NameOfEntryPoint& request,
                                                        const std::string&      requestTag,
                                                        const CFileNames&       file)
{
    const std::string status = file.standard("int8_t");
    const std::string byte   = file.standard("uint8_t");
    const std::string size   = file.standard("size_t");
    return {
        "static inline " + status + " " + service(EntryPoint::Serialize) + "(const " + requestTag + "* const obj, " +
            byte + "* const buffer, " + size + "* const inout_buffer_size_bytes)",
        "{",
        "  return " + request(EntryPoint::Serialize) + "(obj, buffer, inout_buffer_size_bytes);",
        "}",
        "static inline " + status + " " + service(EntryPoint::Deserialize) + "(" + requestTag +
            "* const out_obj, const " + byte + "* const buffer, " + size + "* const inout_buffer_size_bytes)",
        "{",
        "  return " + request(EntryPoint::Deserialize) + "(out_obj, buffer, inout_buffer_size_bytes);",
        "}",
        "static inline " + status + " " + service(EntryPoint::Initialize) + "(" + requestTag + "* const out_obj)",
        "{",
        "  return " + request(EntryPoint::Initialize) + "(out_obj);",
        "}",
    };
}

}  // namespace llvmdsdl::emitter::c
