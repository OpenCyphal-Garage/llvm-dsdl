//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Renders helper binding snippets for generated language backends.
///
/// The rendering utilities centralize helper-call formatting so backend emitters share identical helper semantics.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/HelperBindingRender.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <optional>

#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"
#include "llvmdsdl/CodeGen/SerDesHelperDescriptors.h"
#include "llvmdsdl/Frontend/AST.h"

namespace llvmdsdl
{

namespace
{

}  // namespace

std::string renderU64MaskLiteral(const HelperBindingRenderLanguage language, const std::uint32_t bits)
{
    switch (language)
    {
    case HelperBindingRenderLanguage::Cpp:
        if (bits == 0U)
        {
            return "0ULL";
        }
        if (bits >= 64U)
        {
            return "18446744073709551615ULL";
        }
        return std::to_string((1ULL << bits) - 1ULL) + "ULL";
    case HelperBindingRenderLanguage::Rust:
        if (bits == 0U)
        {
            return "0u64";
        }
        if (bits >= 64U)
        {
            return "u64::MAX";
        }
        return std::to_string((1ULL << bits) - 1ULL) + "u64";
    case HelperBindingRenderLanguage::Go:
        if (bits == 0U)
        {
            return "uint64(0)";
        }
        if (bits >= 64U)
        {
            return "^uint64(0)";
        }
        return "uint64(" + std::to_string((1ULL << bits) - 1ULL) + ")";
    case HelperBindingRenderLanguage::TypeScript:
        if (bits == 0U)
        {
            return "0n";
        }
        if (bits >= 64U)
        {
            return "18446744073709551615n";
        }
        return std::to_string((1ULL << bits) - 1ULL) + "n";
    case HelperBindingRenderLanguage::Python:
        if (bits == 0U)
        {
            return "0";
        }
        if (bits >= 64U)
        {
            return "18446744073709551615";
        }
        return std::to_string((1ULL << bits) - 1ULL);
    }
    return "0";
}

namespace
{

void appendLines(std::vector<std::string>& out, const std::vector<std::string>& in)
{
    out.insert(out.end(), in.begin(), in.end());
}

}  // namespace

std::vector<std::string> renderCapacityCheckBinding(const HelperBindingRenderLanguage language,
                                                    const std::string&                helperName,
                                                    const std::int64_t                requiredBits)
{
    switch (language)
    {
    case HelperBindingRenderLanguage::Cpp:
        return {
            "const auto " + helperName + " = [](const std::int64_t capacity_bits) -> std::int8_t {",
            "return (" + std::to_string(requiredBits) +
                "LL > capacity_bits) ? static_cast<std::int8_t>(-DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL) : "
                "static_cast<std::int8_t>(DSDL_RUNTIME_SUCCESS);",
            "};",
        };
    case HelperBindingRenderLanguage::Rust:
        return {
            "let " + helperName + " = |capacity_bits: i64| -> i8 {",
            "if " + std::to_string(requiredBits) + "i64 > capacity_bits {",
            "-crate::dsdl_runtime::DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL",
            "} else {",
            "crate::dsdl_runtime::DSDL_RUNTIME_SUCCESS",
            "}",
            "};",
        };
    case HelperBindingRenderLanguage::Go:
        return {
            helperName + " := func(capacityBits int64) int8 {",
            "if " + std::to_string(requiredBits) + " > capacityBits {",
            "return -dsdlruntime.DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL",
            "}",
            "return dsdlruntime.DSDL_RUNTIME_SUCCESS",
            "}",
        };
    case HelperBindingRenderLanguage::TypeScript:
        return {
            "const " + helperName + " = (capacityBits: number): boolean => " + std::to_string(requiredBits) +
                " <= capacityBits;",
        };
    case HelperBindingRenderLanguage::Python:
        return {
            "def " + helperName + "(capacity_bits: int) -> bool:",
            "    return " + std::to_string(requiredBits) + " <= capacity_bits",
        };
    }
    return {};
}

std::vector<std::string> renderUnionTagMaskBinding(const HelperBindingRenderLanguage language,
                                                   const std::string&                helperName,
                                                   const std::uint32_t               bits)
{
    const auto bitMaskLiteral = renderU64MaskLiteral(language, bits);
    switch (language)
    {
    case HelperBindingRenderLanguage::Cpp:
        return {
            "const auto " + helperName + " = [](const std::uint64_t value) -> std::uint64_t { return value & " +
                bitMaskLiteral + "; };",
        };
    case HelperBindingRenderLanguage::Rust:
        return {
            "let " + helperName + " = |value: u64| -> u64 { value & " + bitMaskLiteral + " };",
        };
    case HelperBindingRenderLanguage::Go:
        return {
            helperName + " := func(value uint64) uint64 { return value & " + bitMaskLiteral + " }",
        };
    case HelperBindingRenderLanguage::TypeScript:
        return {
            "const " + helperName + " = (value: number | bigint): number | bigint => {",
            "const raw = (typeof value === \"bigint\") ? value : BigInt(Math.trunc(value));",
            "const masked = raw & " + bitMaskLiteral + ";",
            "return (typeof value === \"bigint\") ? masked : Number(masked);",
            "};",
        };
    case HelperBindingRenderLanguage::Python:
        return {
            "def " + helperName + "(value: int) -> int:",
            "    return int(value) & " + bitMaskLiteral,
        };
    }
    return {};
}

std::vector<std::string> renderUnionTagValidateBinding(const HelperBindingRenderLanguage language,
                                                       const std::string&                helperName,
                                                       const std::vector<std::int64_t>&  allowedTags)
{
    switch (language)
    {
    case HelperBindingRenderLanguage::Cpp: {
        std::string condition;
        for (const auto tag : allowedTags)
        {
            if (!condition.empty())
            {
                condition += " || ";
            }
            condition += "(tag_value == " + std::to_string(tag) + "LL)";
        }
        if (condition.empty())
        {
            return {
                "const auto " + helperName + " = [](const std::int64_t tag_value) -> std::int8_t {",
                "return static_cast<std::int8_t>(-DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG);",
                "};",
            };
        }
        return {
            "const auto " + helperName + " = [](const std::int64_t tag_value) -> std::int8_t {",
            "return (" + condition +
                ") ? static_cast<std::int8_t>(DSDL_RUNTIME_SUCCESS) : "
                "static_cast<std::int8_t>(-DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG);",
            "};",
        };
    }
    case HelperBindingRenderLanguage::Rust: {
        std::string condition;
        for (const auto tag : allowedTags)
        {
            if (!condition.empty())
            {
                condition += " || ";
            }
            condition += "(tag_value == " + std::to_string(tag) + "i64)";
        }
        if (condition.empty())
        {
            return {
                "let " + helperName + " = |tag_value: i64| -> i8 {",
                "-crate::dsdl_runtime::DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG",
                "};",
            };
        }
        return {
            "let " + helperName + " = |tag_value: i64| -> i8 {",
            "if " + condition + " {",
            "crate::dsdl_runtime::DSDL_RUNTIME_SUCCESS",
            "} else {",
            "-crate::dsdl_runtime::DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG",
            "}",
            "};",
        };
    }
    case HelperBindingRenderLanguage::Go: {
        std::string condition;
        for (const auto tag : allowedTags)
        {
            if (!condition.empty())
            {
                condition += " || ";
            }
            condition += "(tagValue == " + std::to_string(tag) + ")";
        }
        if (condition.empty())
        {
            return {
                helperName + " := func(tagValue int64) int8 {",
                "return -dsdlruntime.DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG",
                "}",
            };
        }
        return {
            helperName + " := func(tagValue int64) int8 {",
            "if " + condition + " {",
            "return dsdlruntime.DSDL_RUNTIME_SUCCESS",
            "}",
            "return -dsdlruntime.DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG",
            "}",
        };
    }
    case HelperBindingRenderLanguage::TypeScript: {
        std::string condition;
        for (const auto tag : allowedTags)
        {
            if (!condition.empty())
            {
                condition += " || ";
            }
            condition += "(tagValue === " + std::to_string(tag) + ")";
        }
        if (condition.empty())
        {
            return {
                "const " + helperName + " = (_tagValue: number): boolean => false;",
            };
        }
        return {
            "const " + helperName + " = (tagValue: number): boolean => " + condition + ";",
        };
    }
    case HelperBindingRenderLanguage::Python: {
        std::string condition;
        for (const auto tag : allowedTags)
        {
            if (!condition.empty())
            {
                condition += " or ";
            }
            condition += "(tag_value == " + std::to_string(tag) + ")";
        }
        if (condition.empty())
        {
            return {
                "def " + helperName + "(tag_value: int) -> bool:",
                "    return False",
            };
        }
        return {
            "def " + helperName + "(tag_value: int) -> bool:",
            "    return " + condition,
        };
    }
    }
    return {};
}

std::vector<std::string> renderDelimiterValidateBinding(const HelperBindingRenderLanguage language,
                                                        const std::string&                helperName)
{
    switch (language)
    {
    case HelperBindingRenderLanguage::Cpp:
        return {
            "const auto " + helperName +
                " = [](const std::int64_t payload_bytes, const std::int64_t remaining_bytes) -> std::int8_t {",
            "return ((payload_bytes < 0LL) || (payload_bytes > remaining_bytes)) ? "
            "static_cast<std::int8_t>(-DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_DELIMITER_HEADER) : "
            "static_cast<std::int8_t>(DSDL_RUNTIME_SUCCESS);",
            "};",
        };
    case HelperBindingRenderLanguage::Rust:
        return {
            "let " + helperName + " = |payload_bytes: i64, remaining_bytes: i64| -> i8 {",
            "if (payload_bytes < 0i64) || (payload_bytes > remaining_bytes) {",
            "-crate::dsdl_runtime::DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_DELIMITER_HEADER",
            "} else {",
            "crate::dsdl_runtime::DSDL_RUNTIME_SUCCESS",
            "}",
            "};",
        };
    case HelperBindingRenderLanguage::Go:
        return {
            helperName + " := func(payloadBytes int64, remainingBytes int64) int8 {",
            "if (payloadBytes < 0) || (payloadBytes > remainingBytes) {",
            "return -dsdlruntime.DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_DELIMITER_HEADER",
            "}",
            "return dsdlruntime.DSDL_RUNTIME_SUCCESS",
            "}",
        };
    case HelperBindingRenderLanguage::TypeScript:
        return {
            "const " + helperName +
                " = (payloadBytes: number, remainingBytes: number): boolean => (payloadBytes >= 0) && "
                "(payloadBytes <= remainingBytes);",
        };
    case HelperBindingRenderLanguage::Python:
        return {
            "def " + helperName + "(payload_bytes: int, remaining_bytes: int) -> bool:",
            "    return (payload_bytes >= 0) and (payload_bytes <= remaining_bytes)",
        };
    }
    return {};
}

std::vector<std::string> renderScalarBinding(const HelperBindingRenderLanguage  language,
                                             const ScalarBindingRenderDirection direction,
                                             const std::string&                 helperName,
                                             const ScalarHelperDescriptor&      descriptor)
{
    const auto maskLiteral = renderU64MaskLiteral(language, descriptor.bitLength);

    if (language == HelperBindingRenderLanguage::Cpp)
    {
        switch (descriptor.kind)
        {
        case ScalarHelperKind::Unsigned: {
            if (direction == ScalarBindingRenderDirection::Serialize && descriptor.castMode == CastMode::Saturated &&
                descriptor.bitLength < 64U)
            {
                return {
                    "const auto " + helperName + " = [](const std::uint64_t value) -> std::uint64_t {",
                    "return (value > " + maskLiteral + ") ? " + maskLiteral + " : value;",
                    "};",
                };
            }
            if (descriptor.bitLength < 64U)
            {
                return {
                    "const auto " + helperName + " = [](const std::uint64_t value) -> std::uint64_t {",
                    "return value & " + maskLiteral + ";",
                    "};",
                };
            }
            return {
                "const auto " + helperName + " = [](const std::uint64_t value) -> std::uint64_t {",
                "return value;",
                "};",
            };
        }
        case ScalarHelperKind::Signed: {
            if (direction == ScalarBindingRenderDirection::Serialize && descriptor.castMode == CastMode::Saturated &&
                descriptor.bitLength > 0U && descriptor.bitLength < 64U)
            {
                const auto minVal = -(1LL << (descriptor.bitLength - 1U));
                const auto maxVal = (1LL << (descriptor.bitLength - 1U)) - 1LL;
                return {
                    "const auto " + helperName + " = [](const std::int64_t value) -> std::int64_t {",
                    "if (value < " + std::to_string(minVal) + "LL) { return " + std::to_string(minVal) + "LL; }",
                    "if (value > " + std::to_string(maxVal) + "LL) { return " + std::to_string(maxVal) + "LL; }",
                    "return value;",
                    "};",
                };
            }
            if (direction == ScalarBindingRenderDirection::Deserialize && descriptor.bitLength > 0U &&
                descriptor.bitLength < 64U)
            {
                const auto signMask = std::to_string((1ULL << (descriptor.bitLength - 1U))) + "ULL";
                return {
                    "const auto " + helperName + " = [](const std::int64_t value) -> std::int64_t {",
                    "const std::uint64_t raw = static_cast<std::uint64_t>(value) & " + maskLiteral + ";",
                    "if ((raw & " + signMask + ") != 0ULL) {",
                    "return static_cast<std::int64_t>(raw | (~" + maskLiteral + "));",
                    "}",
                    "return static_cast<std::int64_t>(raw);",
                    "};",
                };
            }
            return {
                "const auto " + helperName + " = [](const std::int64_t value) -> std::int64_t {",
                "return value;",
                "};",
            };
        }
        case ScalarHelperKind::Float: {
            const std::string floatType = descriptor.bitLength == 64U ? "double" : "float";
            return {
                "const auto " + helperName + " = [](const " + floatType + " value) -> " + floatType +
                    " { return value; };",
            };
        }
        }
    }

    if (language == HelperBindingRenderLanguage::Rust)
    {
        switch (descriptor.kind)
        {
        case ScalarHelperKind::Unsigned: {
            if (direction == ScalarBindingRenderDirection::Serialize && descriptor.castMode == CastMode::Saturated &&
                descriptor.bitLength < 64U)
            {
                return {
                    "let " + helperName + " = |value: u64| -> u64 {",
                    "if value > " + maskLiteral + " { " + maskLiteral + " } else { value }",
                    "};",
                };
            }
            if (descriptor.bitLength < 64U)
            {
                return {
                    "let " + helperName + " = |value: u64| -> u64 {",
                    "value & " + maskLiteral,
                    "};",
                };
            }
            return {
                "let " + helperName + " = |value: u64| -> u64 {",
                "value",
                "};",
            };
        }
        case ScalarHelperKind::Signed: {
            if (direction == ScalarBindingRenderDirection::Serialize && descriptor.castMode == CastMode::Saturated &&
                descriptor.bitLength > 0U && descriptor.bitLength < 64U)
            {
                const auto minVal = -(1LL << (descriptor.bitLength - 1U));
                const auto maxVal = (1LL << (descriptor.bitLength - 1U)) - 1LL;
                return {
                    "let " + helperName + " = |value: i64| -> i64 {",
                    "if value < " + std::to_string(minVal) + "i64 {",
                    std::to_string(minVal) + "i64",
                    "} else if value > " + std::to_string(maxVal) + "i64 {",
                    std::to_string(maxVal) + "i64",
                    "} else {",
                    "value",
                    "}",
                    "};",
                };
            }
            if (direction == ScalarBindingRenderDirection::Deserialize && descriptor.bitLength > 0U &&
                descriptor.bitLength < 64U)
            {
                const auto signMask = std::to_string((1ULL << (descriptor.bitLength - 1U))) + "u64";
                return {
                    "let " + helperName + " = |value: i64| -> i64 {",
                    "let raw = (value as u64) & " + maskLiteral + ";",
                    "if (raw & " + signMask + ") != 0u64 {",
                    "(raw | (!" + maskLiteral + ")) as i64",
                    "} else {",
                    "raw as i64",
                    "}",
                    "};",
                };
            }
            return {
                "let " + helperName + " = |value: i64| -> i64 {",
                "value",
                "};",
            };
        }
        case ScalarHelperKind::Float: {
            const std::string floatType = descriptor.bitLength == 64U ? "f64" : "f32";
            return {
                "let " + helperName + " = |value: " + floatType + "| -> " + floatType + " { value };",
            };
        }
        }
    }

    if (language == HelperBindingRenderLanguage::TypeScript)
    {
        switch (descriptor.kind)
        {
        case ScalarHelperKind::Unsigned: {
            if (direction == ScalarBindingRenderDirection::Serialize && descriptor.castMode == CastMode::Saturated &&
                descriptor.bitLength < 64U)
            {
                return {
                    "const " + helperName + " = (value: number | bigint): number | bigint => {",
                    "const raw = (typeof value === \"bigint\") ? value : BigInt(Math.trunc(value));",
                    "const clamped = raw < 0n ? 0n : (raw > " + maskLiteral + " ? " + maskLiteral + " : raw);",
                    "return (typeof value === \"bigint\") ? clamped : Number(clamped);",
                    "};",
                };
            }
            if (descriptor.bitLength < 64U)
            {
                return {
                    "const " + helperName + " = (value: number | bigint): number | bigint => {",
                    "const raw = (typeof value === \"bigint\") ? value : BigInt(Math.trunc(value));",
                    "const masked = raw & " + maskLiteral + ";",
                    "return (typeof value === \"bigint\") ? masked : Number(masked);",
                    "};",
                };
            }
            return {
                "const " + helperName + " = (value: number | bigint): number | bigint => value;",
            };
        }
        case ScalarHelperKind::Signed: {
            if (direction == ScalarBindingRenderDirection::Serialize && descriptor.castMode == CastMode::Saturated &&
                descriptor.bitLength > 0U && descriptor.bitLength < 64U)
            {
                const auto minVal = -(1LL << (descriptor.bitLength - 1U));
                const auto maxVal = (1LL << (descriptor.bitLength - 1U)) - 1LL;
                return {
                    "const " + helperName + " = (value: number | bigint): number | bigint => {",
                    "const raw = (typeof value === \"bigint\") ? value : BigInt(Math.trunc(value));",
                    "const clamped = raw < " + std::to_string(minVal) + "n ? " + std::to_string(minVal) +
                        "n : (raw > " + std::to_string(maxVal) + "n ? " + std::to_string(maxVal) + "n : raw);",
                    "return (typeof value === \"bigint\") ? clamped : Number(clamped);",
                    "};",
                };
            }
            if (direction == ScalarBindingRenderDirection::Deserialize && descriptor.bitLength > 0U &&
                descriptor.bitLength < 64U)
            {
                const auto signMask = std::to_string((1ULL << (descriptor.bitLength - 1U))) + "n";
                return {
                    "const " + helperName + " = (value: number | bigint): number | bigint => {",
                    "const raw = ((typeof value === \"bigint\") ? value : BigInt(Math.trunc(value))) & " + maskLiteral +
                        ";",
                    "const signed = (raw & " + signMask + ") !== 0n ? (raw | (~" + maskLiteral + ")) : raw;",
                    "return (typeof value === \"bigint\") ? signed : Number(signed);",
                    "};",
                };
            }
            return {
                "const " + helperName + " = (value: number | bigint): number | bigint => value;",
            };
        }
        case ScalarHelperKind::Float:
            return {
                "const " + helperName + " = (value: number): number => value;",
            };
        }
    }

    if (language == HelperBindingRenderLanguage::Python)
    {
        switch (descriptor.kind)
        {
        case ScalarHelperKind::Unsigned: {
            if (direction == ScalarBindingRenderDirection::Serialize && descriptor.castMode == CastMode::Saturated &&
                descriptor.bitLength < 64U)
            {
                return {
                    "def " + helperName + "(value: int) -> int:",
                    "    raw = int(value)",
                    "    if raw < 0:",
                    "        return 0",
                    "    if raw > " + maskLiteral + ":",
                    "        return " + maskLiteral,
                    "    return raw",
                };
            }
            if (descriptor.bitLength < 64U)
            {
                return {
                    "def " + helperName + "(value: int) -> int:",
                    "    return int(value) & " + maskLiteral,
                };
            }
            return {
                "def " + helperName + "(value: int) -> int:",
                "    return int(value)",
            };
        }
        case ScalarHelperKind::Signed: {
            if (direction == ScalarBindingRenderDirection::Serialize && descriptor.castMode == CastMode::Saturated &&
                descriptor.bitLength > 0U && descriptor.bitLength < 64U)
            {
                const auto minVal = -(1LL << (descriptor.bitLength - 1U));
                const auto maxVal = (1LL << (descriptor.bitLength - 1U)) - 1LL;
                return {
                    "def " + helperName + "(value: int) -> int:",
                    "    raw = int(value)",
                    "    if raw < " + std::to_string(minVal) + ":",
                    "        return " + std::to_string(minVal),
                    "    if raw > " + std::to_string(maxVal) + ":",
                    "        return " + std::to_string(maxVal),
                    "    return raw",
                };
            }
            if (direction == ScalarBindingRenderDirection::Deserialize && descriptor.bitLength > 0U &&
                descriptor.bitLength < 64U)
            {
                const auto signMask = std::to_string(1ULL << (descriptor.bitLength - 1U));
                return {
                    "def " + helperName + "(value: int) -> int:",
                    "    raw = int(value) & " + maskLiteral,
                    "    if (raw & " + signMask + ") != 0:",
                    "        return raw | (~" + maskLiteral + ")",
                    "    return raw",
                };
            }
            return {
                "def " + helperName + "(value: int) -> int:",
                "    return int(value)",
            };
        }
        case ScalarHelperKind::Float:
            return {
                "def " + helperName + "(value: float) -> float:",
                "    return float(value)",
            };
        }
    }

    switch (descriptor.kind)
    {
    case ScalarHelperKind::Unsigned: {
        if (direction == ScalarBindingRenderDirection::Serialize && descriptor.castMode == CastMode::Saturated &&
            descriptor.bitLength < 64U)
        {
            return {
                helperName + " := func(value uint64) uint64 {",
                "if value > " + maskLiteral + " {",
                "return " + maskLiteral,
                "}",
                "return value",
                "}",
            };
        }
        if (descriptor.bitLength < 64U)
        {
            return {
                helperName + " := func(value uint64) uint64 {",
                "return value & " + maskLiteral,
                "}",
            };
        }
        return {
            helperName + " := func(value uint64) uint64 {",
            "return value",
            "}",
        };
    }
    case ScalarHelperKind::Signed: {
        if (direction == ScalarBindingRenderDirection::Serialize && descriptor.castMode == CastMode::Saturated &&
            descriptor.bitLength > 0U && descriptor.bitLength < 64U)
        {
            const auto minVal = -(1LL << (descriptor.bitLength - 1U));
            const auto maxVal = (1LL << (descriptor.bitLength - 1U)) - 1LL;
            return {
                helperName + " := func(value int64) int64 {",
                "if value < " + std::to_string(minVal) + " {",
                "return " + std::to_string(minVal),
                "}",
                "if value > " + std::to_string(maxVal) + " {",
                "return " + std::to_string(maxVal),
                "}",
                "return value",
                "}",
            };
        }
        if (direction == ScalarBindingRenderDirection::Deserialize && descriptor.bitLength > 0U &&
            descriptor.bitLength < 64U)
        {
            const auto signMask = "uint64(" + std::to_string(1ULL << (descriptor.bitLength - 1U)) + ")";
            return {
                helperName + " := func(value int64) int64 {",
                "raw := uint64(value) & " + maskLiteral,
                "if (raw & " + signMask + ") != 0 {",
                "return int64(raw | (^" + maskLiteral + "))",
                "}",
                "return int64(raw)",
                "}",
            };
        }
        return {
            helperName + " := func(value int64) int64 {",
            "return value",
            "}",
        };
    }
    case ScalarHelperKind::Float: {
        const std::string floatType = descriptor.bitLength == 64U ? "float64" : "float32";
        return {
            helperName + " := func(value " + floatType + ") " + floatType + " { return value }",
        };
    }
    }
    return {};
}

std::vector<std::string> renderArrayPrefixBinding(const HelperBindingRenderLanguage language,
                                                  const std::string&                helperName,
                                                  const std::uint32_t               bits)
{
    return renderUnionTagMaskBinding(language, helperName, bits);
}

std::vector<std::string> renderArrayValidateBinding(const HelperBindingRenderLanguage language,
                                                    const std::string&                helperName,
                                                    const std::int64_t                capacity)
{
    switch (language)
    {
    case HelperBindingRenderLanguage::Cpp:
        return {
            "const auto " + helperName + " = [](const std::int64_t value) -> std::int8_t {",
            "return ((value < 0LL) || (value > " + std::to_string(capacity) +
                "LL)) ? static_cast<std::int8_t>(-DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH) : "
                "static_cast<std::int8_t>(DSDL_RUNTIME_SUCCESS);",
            "};",
        };
    case HelperBindingRenderLanguage::Rust:
        return {
            "let " + helperName + " = |value: i64| -> i8 {",
            "if (value < 0i64) || (value > " + std::to_string(capacity) + "i64) {",
            "-crate::dsdl_runtime::DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH",
            "} else {",
            "crate::dsdl_runtime::DSDL_RUNTIME_SUCCESS",
            "}",
            "};",
        };
    case HelperBindingRenderLanguage::Go:
        return {
            helperName + " := func(value int64) int8 {",
            "if (value < 0) || (value > " + std::to_string(capacity) + ") {",
            "return -dsdlruntime.DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH",
            "}",
            "return dsdlruntime.DSDL_RUNTIME_SUCCESS",
            "}",
        };
    case HelperBindingRenderLanguage::TypeScript:
        return {
            "const " + helperName +
                " = (value: number): boolean => (value >= 0) && (value <= " + std::to_string(capacity) + ");",
        };
    case HelperBindingRenderLanguage::Python:
        return {
            "def " + helperName + "(value: int) -> bool:",
            "    return (value >= 0) and (value <= " + std::to_string(capacity) + ")",
        };
    }
    return {};
}

std::vector<std::string> renderSectionHelperBindings(
    const SectionHelperBindingPlan&                       plan,
    const HelperBindingRenderLanguage                     language,
    const ScalarBindingRenderDirection                    scalarDirection,
    const std::function<std::string(const std::string&)>& helperNameResolver,
    const bool                                            emitCapacityCheck)
{
    std::vector<std::string> out;

    if (emitCapacityCheck && plan.capacityCheck)
    {
        appendLines(out,
                    renderCapacityCheckBinding(language,
                                               helperNameResolver(plan.capacityCheck->symbol),
                                               plan.capacityCheck->requiredBits));
    }
    if (plan.unionTagValidate)
    {
        appendLines(out,
                    renderUnionTagValidateBinding(language,
                                                  helperNameResolver(plan.unionTagValidate->symbol),
                                                  plan.unionTagValidate->allowedTags));
    }
    if (plan.unionTagMask)
    {
        appendLines(out,
                    renderUnionTagMaskBinding(language,
                                              helperNameResolver(plan.unionTagMask->symbol),
                                              plan.unionTagMask->bits));
    }
    for (const auto& binding : plan.scalarBindings)
    {
        appendLines(out,
                    renderScalarBinding(language,
                                        scalarDirection,
                                        helperNameResolver(binding.symbol),
                                        binding.descriptor));
    }
    for (const auto& binding : plan.delimiterValidateBindings)
    {
        appendLines(out, renderDelimiterValidateBinding(language, helperNameResolver(binding.symbol)));
    }
    for (const auto& binding : plan.arrayPrefixBindings)
    {
        appendLines(out, renderArrayPrefixBinding(language, helperNameResolver(binding.symbol), binding.bits));
    }
    for (const auto& binding : plan.arrayValidateBindings)
    {
        appendLines(out, renderArrayValidateBinding(language, helperNameResolver(binding.symbol), binding.capacity));
    }

    return out;
}

}  // namespace llvmdsdl
