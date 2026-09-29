//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Renders and reads the symbols the IR names a definition and its functions by.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Support/PlanSymbol.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"

namespace llvmdsdl
{
namespace
{

constexpr llvm::StringLiteral kHelperMarker{"plan"};

llvm::StringRef directionWord(const PlanHelperDirection direction)
{
    switch (direction)
    {
    case PlanHelperDirection::None:
        return "";
    case PlanHelperDirection::Serialize:
        return "ser";
    case PlanHelperDirection::Deserialize:
        return "deser";
    }
    return "";
}

bool allDigits(const llvm::StringRef text)
{
    return !text.empty() && llvm::all_of(text, llvm::isDigit);
}

std::optional<std::uint32_t> readVersion(const llvm::StringRef text)
{
    std::uint64_t value = 0;
    if (!allDigits(text) || text.getAsInteger(10, value) || (value > std::numeric_limits<std::uint32_t>::max()))
    {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

/// @brief Splits @p symbol into its schema and the components after it.
std::optional<SchemaSymbol> splitSchema(const llvm::StringRef symbol, llvm::SmallVectorImpl<llvm::StringRef>& rest)
{
    llvm::SmallVector<llvm::StringRef, 8> parts;
    symbol.split(parts, '.');
    std::size_t major = 0;
    while ((major < parts.size()) && !allDigits(parts[major]))
    {
        ++major;
    }
    // A full name has a namespace and a short name; the version follows it.
    if ((major < 2) || ((major + 1) >= parts.size()))
    {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < major; ++i)
    {
        if (parts[i].empty())
        {
            return std::nullopt;
        }
    }
    const auto majorVersion = readVersion(parts[major]);
    const auto minorVersion = readVersion(parts[major + 1]);
    if (!majorVersion || !minorVersion)
    {
        return std::nullopt;
    }
    rest.assign(parts.begin() + static_cast<std::ptrdiff_t>(major + 2), parts.end());
    return SchemaSymbol{llvm::join(llvm::ArrayRef(parts).take_front(major), "."), *majorVersion, *minorVersion};
}

}  // namespace

llvm::StringRef planFunctionWord(const PlanFunction function)
{
    switch (function)
    {
    case PlanFunction::Serialize:
        return "serialize";
    case PlanFunction::Deserialize:
        return "deserialize";
    case PlanFunction::Initialize:
        return "initialize";
    case PlanFunction::AppendWireImage:
        return "append_wire_image";
    case PlanFunction::WireImage:
        return "wire_image";
    case PlanFunction::ReadWireImage:
        return "read_wire_image";
    case PlanFunction::Get:
        return "get";
    case PlanFunction::Set:
        return "set";
    case PlanFunction::Helper:
        return kHelperMarker;
    }
    return "";
}

std::string renderSchemaSymbol(const SchemaSymbol& schema)
{
    return schema.fullName + "." + std::to_string(schema.major) + "." + std::to_string(schema.minor);
}

std::string renderPlanSymbol(const PlanSymbol& symbol)
{
    std::string out = renderSchemaSymbol(symbol.schema);
    if (!symbol.section.empty())
    {
        out += "." + symbol.section;
    }
    out += "." + planFunctionWord(symbol.function).str();
    switch (symbol.function)
    {
    case PlanFunction::Get:
    case PlanFunction::Set:
        out += "." + symbol.member;
        break;
    case PlanFunction::Helper:
        out += "." + symbol.helperKind;
        if (symbol.step)
        {
            out += "." + std::to_string(*symbol.step);
        }
        if (symbol.direction != PlanHelperDirection::None)
        {
            out += "." + directionWord(symbol.direction).str();
        }
        break;
    case PlanFunction::Serialize:
    case PlanFunction::Deserialize:
    case PlanFunction::Initialize:
    case PlanFunction::AppendWireImage:
    case PlanFunction::WireImage:
    case PlanFunction::ReadWireImage:
        break;
    }
    return out;
}

std::optional<SchemaSymbol> parseSchemaSymbol(const llvm::StringRef symbol)
{
    llvm::SmallVector<llvm::StringRef, 4> rest;
    auto                                  schema = splitSchema(symbol, rest);
    if (!schema || !rest.empty())
    {
        return std::nullopt;
    }
    return schema;
}

std::optional<PlanSymbol> parsePlanSymbol(const llvm::StringRef symbol)
{
    llvm::SmallVector<llvm::StringRef, 4> rest;
    auto                                  schema = splitSchema(symbol, rest);
    if (!schema || rest.empty())
    {
        return std::nullopt;
    }

    PlanSymbol  out;
    std::size_t at = 0;
    out.schema     = std::move(*schema);
    if ((rest[at] == "request") || (rest[at] == "response"))
    {
        out.section = rest[at].str();
        ++at;
    }
    if (at >= rest.size())
    {
        return std::nullopt;
    }
    const llvm::StringRef word      = rest[at++];
    const std::size_t     remaining = rest.size() - at;
    for (const PlanFunction function : {PlanFunction::Serialize,
                                        PlanFunction::Deserialize,
                                        PlanFunction::Initialize,
                                        PlanFunction::AppendWireImage,
                                        PlanFunction::WireImage,
                                        PlanFunction::ReadWireImage})
    {
        if (word == planFunctionWord(function))
        {
            if (remaining != 0)
            {
                return std::nullopt;
            }
            out.function = function;
            return out;
        }
    }
    if ((word == "get") || (word == "set"))
    {
        if ((remaining != 1) || rest[at].empty())
        {
            return std::nullopt;
        }
        out.function = (word == "get") ? PlanFunction::Get : PlanFunction::Set;
        out.member   = rest[at].str();
        return out;
    }
    if ((word != kHelperMarker) || (remaining == 0) || rest[at].empty())
    {
        return std::nullopt;
    }
    out.function   = PlanFunction::Helper;
    out.helperKind = rest[at++].str();
    if ((at < rest.size()) && allDigits(rest[at]))
    {
        std::int64_t step = 0;
        if (rest[at].getAsInteger(10, step))
        {
            return std::nullopt;
        }
        out.step = step;
        ++at;
    }
    if (at < rest.size())
    {
        if (rest[at] == "ser")
        {
            out.direction = PlanHelperDirection::Serialize;
        }
        else if (rest[at] == "deser")
        {
            out.direction = PlanHelperDirection::Deserialize;
        }
        else
        {
            return std::nullopt;
        }
        ++at;
    }
    return (at == rest.size()) ? std::optional<PlanSymbol>(std::move(out)) : std::nullopt;
}

}  // namespace llvmdsdl
