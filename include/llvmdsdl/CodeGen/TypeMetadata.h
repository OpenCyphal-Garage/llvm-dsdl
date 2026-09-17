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
/// is deprecated, the aliasing verdict, its fixed port-ID, and for a union its options. Each one
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

    /// @brief The zero-overhead alias verdict.
    AliasVerdict alias;

    /// @brief True when the section is a tagged union.
    bool isUnion{false};

    /// @brief The union's options in tag order; empty for a structure.
    std::vector<UnionOption> unionOptions;

    /// @brief The definition's fixed port-ID, where it has one.
    ///
    /// A service's port-ID is its service-ID and a message's is its subject-ID. It belongs to the
    /// definition rather than to one of its sections, and is reported on both so that a caller
    /// holding either section's metadata can reach it.
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
