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
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "llvmdsdl/CodeGen/TypeMetadata.h"
#include "llvmdsdl/CodeGen/emitter/CIncludes.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/GeneratedFact.h"

namespace llvmdsdl::emitter::c
{

/// @brief The name a declaration stating a fact is declared under.
using NameOfFact = std::function<std::string(GeneratedFact)>;

/// @brief The name an entry point is declared under.
using NameOfEntryPoint = std::function<std::string(EntryPoint)>;

/// @brief Renders C metadata macros for one generated type.
/// @param[in] named The name of each macro, by the fact it states.
/// @param[in] metadata The section's facts.
/// @param[in] file How the header names what it takes from other headers.
/// @return Ordered macro lines.
std::vector<std::string> renderTypeMetadataMacros(const NameOfFact&      named,
                                                  const SectionMetadata& metadata,
                                                  const CFileNames&      file);

/// @brief Renders the guard a folded body carries, refusing a host that does not order bytes as
///        the wire does.
///
/// It refuses a host it cannot place as well as one it places as big-endian: a compiler stating no
/// byte order says nothing about the target, and the fold is only correct where the two orders
/// agree. MSVC states none and every target it compiles for is little-endian, so it is named.
/// C and C++ carry the same guard, which is why it is rendered once.
/// @param[in] typeName Generated type name, which the refusal names.
/// @return Ordered preprocessor lines.
std::vector<std::string> renderLittleEndianGuardLines(const std::string& typeName);

/// @brief Renders service alias identity metadata macro lines.
/// @param[in] named The name of each of the service's macros, by the fact it states.
/// @param[in] fullName Service full DSDL name.
/// @param[in] majorVersion DSDL major version.
/// @param[in] minorVersion DSDL minor version.
/// @param[in] fixedPortId The service's fixed service-ID, where it has one.
/// @param[in] file How the header names what it takes from other headers.
/// @return Ordered macro lines.
std::vector<std::string> renderServiceAliasIdentityMacros(const NameOfFact&            named,
                                                          const std::string&           fullName,
                                                          std::uint32_t                majorVersion,
                                                          std::uint32_t                minorVersion,
                                                          std::optional<std::uint32_t> fixedPortId,
                                                          const CFileNames&            file);

/// @brief Renders service alias bridge lines after request type declaration.
/// @param[in] aliasName The name the service's typedef declares.
/// @param[in] requestTag The request's structure tag, as C spells a use of it.
/// @param[in] alias The name of each of the service's macros, by the fact it states.
/// @param[in] request The name of each of the request's macros, by the fact it states.
/// @param[in] deprecatedAttribute Whether the alias typedef carries `__attribute__((deprecated))`.
/// @return Ordered typedef/bridge macro lines.
std::vector<std::string> renderServiceAliasBridgeLines(const std::string& aliasName,
                                                       const std::string& requestTag,
                                                       const NameOfFact&  alias,
                                                       const NameOfFact&  request,
                                                       bool               deprecatedAttribute);

/// @brief Renders service alias serialise/deserialize inline wrappers.
/// @param[in] service The name of each of the service's entry points.
/// @param[in] request The name of each of the request's entry points, which the service's call.
/// @param[in] requestTag The request's structure tag, as C spells a use of it.
/// @param[in] file How the header names what it takes from other headers.
/// @return Ordered wrapper lines.
std::vector<std::string> renderServiceAliasWrapperLines(const NameOfEntryPoint& service,
                                                        const NameOfEntryPoint& request,
                                                        const std::string&      requestTag,
                                                        const CFileNames&       file);

}  // namespace llvmdsdl::emitter::c

#endif  // LLVMDSDL_CODEGEN_EMITTER_C_HEADER_RENDER_H
