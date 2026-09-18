//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The facts a backend declares beside a section's generated type.
///
/// Every backend emits the same set: the type's identity, how much of a buffer it needs, whether it
/// is deprecated, its two layout verdicts, its fixed port-ID, and for a union its options. Each one
/// spells them in its own syntax, and each one used to derive them itself. This is where they are
/// derived, once, so that the six spellings are six renderings of one answer.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_TYPE_METADATA_H
#define LLVMDSDL_CODEGEN_TYPE_METADATA_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <llvm/ADT/StringRef.h>

#include "llvmdsdl/CodeGen/SchemaLookup.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/Semantics/Model.h"

namespace llvmdsdl
{

/// @brief One option of a union: the DSDL name that selects it and the tag value that means it.
struct UnionOption final
{
    /// @brief The option's DSDL field name, to be projected by the reading backend.
    std::string name;

    /// @brief The tag value that selects this option.
    std::int64_t tag{0};
};

/// @brief What a backend declares beside one section's type.
struct SectionMetadata final
{
    /// @brief The section's fully-qualified name, without the version; a service section carries
    /// `.Request` or `.Response`.
    std::string fullName;

    /// @brief DSDL major version.
    std::uint32_t majorVersion{0};

    /// @brief DSDL minor version.
    std::uint32_t minorVersion{0};

    /// @brief Extent in bytes: the buffer a receiver allocates.
    std::uint64_t extentBytes{0};

    /// @brief Largest serialised representation in bytes: the buffer a sender allocates.
    std::uint64_t serializationBufferSizeBytes{0};

    /// @brief True when the definition carries `@deprecated`.
    bool deprecated{false};

    /// @brief Whether the serialised form is a contiguous byte image.
    AliasVerdict wireFlat;

    /// @brief Whether the generated structure is that same byte image.
    AliasVerdict hostImage;

    /// @brief The image's members in declaration order with the byte offset each has; empty unless
    ///        @ref hostImage holds.
    std::vector<HostImageMember> hostImageMembers;

    /// @brief True when the section is a tagged union.
    bool isUnion{false};

    /// @brief The union's options in tag order; empty for a structure.
    std::vector<UnionOption> unionOptions;

    /// @brief The width of the tag the union's plan writes, in bits.
    ///
    /// The option tag constants are declared at this width so that assigning one to the tag member
    /// is not a narrowing conversion.
    std::uint32_t unionTagBits{8};

    /// @brief True when this section's type is the one that stands for the definition.
    ///
    /// The definition's port-ID is declared on that type and nowhere else. A message has one
    /// section and it is that type; a service is reached through an alias, and its request and
    /// response are two types under it. Asking a request for the service's port-ID is a question
    /// about the service, so the request does not answer it.
    bool declaresPortId{true};

    /// @brief The fixed port-ID, where the definition has one; read only when @ref declaresPortId.
    ///
    /// A message's is its subject-ID and a service's is its service-ID.
    std::optional<std::uint32_t> fixedPortId;
};

/// @brief Reads what @p section declares, from the model and from its lowered schema.
///
/// The union options come from the schema's `dsdl.field` ops, which is where an option's name and
/// its tag value are carried together. `dsdl.schema` verifies those indices against the plan steps
/// that serialise them, so reading either gives the same answer.
///
/// @param[in] info The definition's discovered identity.
/// @param[in] section The section being declared.
/// @param[in] schema The definition's lowered schema; a null schema yields default wire facts.
/// @param[in] sectionName "" for a message, "request" or "response" for a service.
/// @return The facts the section declares.
[[nodiscard]] SectionMetadata sectionMetadata(const DiscoveredDefinition& info,
                                              const SemanticSection&      section,
                                              mlir::dsdl::SchemaOp        schema,
                                              llvm::StringRef             sectionName);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_TYPE_METADATA_H
