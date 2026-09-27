//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The symbols the IR names a definition and its functions by.
///
/// A symbol is the definition's DSDL identity, `uavcan.node.Heartbeat.1.0`, and a function's is that
/// identity with dotted suffixes naming the section and what the function does:
///
/// | symbol | names |
/// |---|---|
/// | `ns.Msg.1.0` | the schema |
/// | `ns.Msg.1.0.serialize` | a body: `serialize`, `deserialize` or `initialize` |
/// | `ns.Svc.1.0.request.serialize` | a service section's body |
/// | `ns.Msg.1.0.get.speed` | an accessor of a member: `get` or `set` |
/// | `ns.Msg.1.0.plan.scalar_unsigned.2.ser` | a helper: its kind, then the plan step and the direction it serves,
/// where it serves one |
///
/// No DSDL name component begins with a digit, so the first all-digit component is the major
/// version and the grammar reads back unambiguously. A backend that cannot declare these spells
/// them its own way from what @ref parsePlanSymbol reads back; the IR carries no backend's spelling.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SUPPORT_PLAN_SYMBOL_H
#define LLVMDSDL_SUPPORT_PLAN_SYMBOL_H

#include <cstdint>
#include <optional>
#include <string>

#include "llvm/ADT/StringRef.h"

namespace llvmdsdl
{

/// @brief The member a plan names a union's tag by, which no DSDL field can be named.
inline constexpr llvm::StringLiteral kPlanUnionTagMember{"_tag_"};

/// @brief What an IR function does for the section it belongs to.
enum class PlanFunction : std::uint8_t
{
    /// @brief Serialises an object.
    Serialize,

    /// @brief Deserialises an object.
    Deserialize,

    /// @brief Initialises an object.
    Initialize,

    /// @brief Reads one member from a buffer.
    Get,

    /// @brief Writes one member into a buffer.
    Set,

    /// @brief Serves the bodies: a check or a conversion they call.
    Helper,
};

/// @brief The direction a helper serves, where it serves one.
enum class PlanHelperDirection : std::uint8_t
{
    /// @brief Either, or none.
    None,

    /// @brief Serialisation, spelt `ser`.
    Serialize,

    /// @brief Deserialisation, spelt `deser`.
    Deserialize,
};

/// @brief A definition's identity: its full name and version.
struct SchemaSymbol final
{
    /// @brief Dot-separated DSDL full name.
    std::string fullName;

    /// @brief Major version.
    std::uint32_t major{};

    /// @brief Minor version.
    std::uint32_t minor{};
};

/// @brief What an IR function's symbol says about it.
struct PlanSymbol final
{
    /// @brief For a helper that serves one plan step, that step's index.
    std::optional<std::int64_t> step;

    /// @brief The section it belongs to: `request`, `response`, or empty for a message.
    std::string section;

    /// @brief For an accessor, the member as the plan names it: a DSDL field's name, or `_tag_`.
    std::string member;

    /// @brief For a helper, its kind: `capacity_check`, `scalar_unsigned` and so on.
    std::string helperKind;

    /// @brief The definition it belongs to.
    SchemaSymbol schema;

    /// @brief What it does.
    PlanFunction function{};

    /// @brief For a helper that serves one direction, that direction.
    PlanHelperDirection direction{};
};

/// @brief Renders a definition's symbol: `ns.Msg.1.0`.
/// @param[in] schema The definition.
/// @return The symbol.
[[nodiscard]] std::string renderSchemaSymbol(const SchemaSymbol& schema);

/// @brief Renders a function's symbol.
/// @param[in] symbol What the function is.
/// @return The symbol.
[[nodiscard]] std::string renderPlanSymbol(const PlanSymbol& symbol);

/// @brief Reads a definition's symbol back.
/// @param[in] symbol A symbol @ref renderSchemaSymbol rendered.
/// @return The definition, or nothing where @p symbol is not one.
[[nodiscard]] std::optional<SchemaSymbol> parseSchemaSymbol(llvm::StringRef symbol);

/// @brief Reads a function's symbol back.
/// @param[in] symbol A symbol @ref renderPlanSymbol rendered.
/// @return What the function is, or nothing where @p symbol names something else, as a runtime
///         function's does.
[[nodiscard]] std::optional<PlanSymbol> parsePlanSymbol(llvm::StringRef symbol);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SUPPORT_PLAN_SYMBOL_H
