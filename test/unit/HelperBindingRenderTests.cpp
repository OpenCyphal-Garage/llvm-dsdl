//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <optional>

#include "llvmdsdl/CodeGen/HelperBindingRender.h"
#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"
#include "llvmdsdl/CodeGen/SerDesHelperDescriptors.h"
#include "llvmdsdl/Frontend/AST.h"

#include "UnitTests.h"

namespace
{

bool hasSubstring(const std::vector<std::string>& lines, const std::string& needle)
{
    return std::ranges::any_of(lines, [&](const auto& line) { return line.contains(needle); });
}

}  // namespace

bool runHelperBindingRenderTests()
{
    {
        const auto lines =
            llvmdsdl::renderUnionTagValidateBinding(llvmdsdl::HelperBindingRenderLanguage::Cpp, "union_check", {0, 2});
        if (!hasSubstring(lines, "(tag_value == 0LL) || (tag_value == 2LL)"))
        {
            std::cerr << "cpp union validate render mismatch\n";
            return false;
        }
    }

    {
        const auto lines =
            llvmdsdl::renderDelimiterValidateBinding(llvmdsdl::HelperBindingRenderLanguage::Rust, "delim_check");
        if (!hasSubstring(lines, "-crate::dsdl_runtime::DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_DELIMITER_HEADER"))
        {
            std::cerr << "rust delimiter validate render mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::ScalarHelperDescriptor descriptor;
        descriptor.kind      = llvmdsdl::ScalarHelperKind::Unsigned;
        descriptor.bitLength = 3;
        descriptor.castMode  = llvmdsdl::CastMode::Saturated;
        const auto lines     = llvmdsdl::renderScalarBinding(llvmdsdl::HelperBindingRenderLanguage::Cpp,
                                                             llvmdsdl::ScalarBindingRenderDirection::Serialize,
                                                             "sat_u3",
                                                             descriptor);
        if (!hasSubstring(lines, "value > 7ULL"))
        {
            std::cerr << "cpp saturated unsigned scalar render mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::ScalarHelperDescriptor descriptor;
        descriptor.kind      = llvmdsdl::ScalarHelperKind::Signed;
        descriptor.bitLength = 3;
        descriptor.castMode  = llvmdsdl::CastMode::Truncated;
        const auto lines     = llvmdsdl::renderScalarBinding(llvmdsdl::HelperBindingRenderLanguage::Rust,
                                                             llvmdsdl::ScalarBindingRenderDirection::Deserialize,
                                                             "des_i3",
                                                             descriptor);
        if (!hasSubstring(lines, "(raw & 4u64) != 0u64") || !hasSubstring(lines, "(raw | (!7u64)) as i64"))
        {
            std::cerr << "rust signed deserialize scalar render mismatch\n";
            return false;
        }
    }

    {
        const auto lines =
            llvmdsdl::renderArrayPrefixBinding(llvmdsdl::HelperBindingRenderLanguage::Cpp, "arr_prefix", 8);
        if (!hasSubstring(lines, "value & 255ULL"))
        {
            std::cerr << "cpp array prefix render mismatch\n";
            return false;
        }
    }

    {
        const auto lines = llvmdsdl::renderUnionTagValidateBinding(llvmdsdl::HelperBindingRenderLanguage::Go,
                                                                   "union_check_go",
                                                                   {0, 2, 7});
        if (!hasSubstring(lines, "func(tagValue int64) int8") ||
            !hasSubstring(lines, "(tagValue == 0) || (tagValue == 2) || (tagValue == 7)") ||
            !hasSubstring(lines, "-dsdlruntime.DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG"))
        {
            std::cerr << "go union validate render mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::ScalarHelperDescriptor descriptor;
        descriptor.kind      = llvmdsdl::ScalarHelperKind::Signed;
        descriptor.bitLength = 5;
        descriptor.castMode  = llvmdsdl::CastMode::Truncated;
        const auto lines     = llvmdsdl::renderScalarBinding(llvmdsdl::HelperBindingRenderLanguage::Go,
                                                             llvmdsdl::ScalarBindingRenderDirection::Deserialize,
                                                             "des_i5_go",
                                                             descriptor);
        if (!hasSubstring(lines, "raw := uint64(value) & uint64(31)") ||
            !hasSubstring(lines, "return int64(raw | (^uint64(31)))"))
        {
            std::cerr << "go signed deserialize scalar render mismatch\n";
            return false;
        }
    }

    {
        const auto lines =
            llvmdsdl::renderCapacityCheckBinding(llvmdsdl::HelperBindingRenderLanguage::TypeScript, "cap_ts", 128);
        if (!hasSubstring(lines, "const cap_ts = (capacityBits: number): boolean => 128 <= capacityBits;"))
        {
            std::cerr << "typescript capacity render mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::ScalarHelperDescriptor descriptor;
        descriptor.kind      = llvmdsdl::ScalarHelperKind::Signed;
        descriptor.bitLength = 5;
        descriptor.castMode  = llvmdsdl::CastMode::Truncated;
        const auto lines     = llvmdsdl::renderScalarBinding(llvmdsdl::HelperBindingRenderLanguage::TypeScript,
                                                             llvmdsdl::ScalarBindingRenderDirection::Deserialize,
                                                             "des_i5_ts",
                                                             descriptor);
        if (!hasSubstring(lines, "(raw & 16n) !== 0n") || !hasSubstring(lines, "raw | (~31n)"))
        {
            std::cerr << "typescript signed deserialize scalar render mismatch\n";
            return false;
        }
    }

    {
        const auto lines =
            llvmdsdl::renderArrayValidateBinding(llvmdsdl::HelperBindingRenderLanguage::TypeScript, "arr_check_ts", 7);
        if (!hasSubstring(lines, "const arr_check_ts = (value: number): boolean => (value >= 0) && (value <= 7);"))
        {
            std::cerr << "typescript array validate render mismatch\n";
            return false;
        }
    }

    {
        const auto lines = llvmdsdl::renderUnionTagValidateBinding(llvmdsdl::HelperBindingRenderLanguage::Python,
                                                                   "union_check_py",
                                                                   {1});
        if (!hasSubstring(lines, "def union_check_py(tag_value: int) -> bool:") ||
            !hasSubstring(lines, "return (tag_value == 1)"))
        {
            std::cerr << "python union validate render mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::ScalarHelperDescriptor descriptor;
        descriptor.kind      = llvmdsdl::ScalarHelperKind::Unsigned;
        descriptor.bitLength = 4;
        descriptor.castMode  = llvmdsdl::CastMode::Saturated;
        const auto lines     = llvmdsdl::renderScalarBinding(llvmdsdl::HelperBindingRenderLanguage::Python,
                                                             llvmdsdl::ScalarBindingRenderDirection::Serialize,
                                                             "sat_u4_py",
                                                             descriptor);
        if (!hasSubstring(lines, "if raw > 15:") || !hasSubstring(lines, "return 15"))
        {
            std::cerr << "python saturated unsigned scalar render mismatch\n";
            return false;
        }
    }

    {
        const auto lines =
            llvmdsdl::renderDelimiterValidateBinding(llvmdsdl::HelperBindingRenderLanguage::Python, "delim_check_py");
        if (!hasSubstring(lines, "def delim_check_py(payload_bytes: int, remaining_bytes: int) -> bool:") ||
            !hasSubstring(lines, "payload_bytes <= remaining_bytes"))
        {
            std::cerr << "python delimiter validate render mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::SectionHelperBindingPlan plan;
        plan.capacityCheck    = llvmdsdl::CapacityCheckHelperDescriptor{"cap", 16};
        plan.unionTagValidate = llvmdsdl::UnionTagValidateHelperDescriptor{"validate_tag", {0, 1}};
        plan.unionTagMask     = llvmdsdl::UnionTagMaskBindingDescriptor{"mask_tag", 8};
        plan.delimiterValidateBindings.push_back(llvmdsdl::DelimiterValidateBindingDescriptor{"delim"});

        const auto lines = llvmdsdl::renderSectionHelperBindings(
            plan,
            llvmdsdl::HelperBindingRenderLanguage::Rust,
            llvmdsdl::ScalarBindingRenderDirection::Serialize,
            [](const std::string& symbol) { return "mlir_" + symbol; },
            /*emitCapacityCheck=*/true);
        if (!hasSubstring(lines, "let mlir_cap = |capacity_bits: i64| -> i8 {") ||
            !hasSubstring(lines, "let mlir_validate_tag = |tag_value: i64| -> i8 {") ||
            !hasSubstring(lines, "let mlir_mask_tag = |value: u64| -> u64 {") ||
            !hasSubstring(lines, "let mlir_delim = |payload_bytes: i64, remaining_bytes: i64| -> i8 {"))
        {
            std::cerr << "section helper binding render mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::SectionHelperBindingPlan plan;
        plan.capacityCheck    = llvmdsdl::CapacityCheckHelperDescriptor{"cap", 16};
        plan.unionTagValidate = llvmdsdl::UnionTagValidateHelperDescriptor{"validate_tag", {0, 1}};
        plan.unionTagMask     = llvmdsdl::UnionTagMaskBindingDescriptor{"mask_tag", 8};
        plan.delimiterValidateBindings.push_back(llvmdsdl::DelimiterValidateBindingDescriptor{"delim"});
        plan.arrayPrefixBindings.push_back(llvmdsdl::ArrayPrefixBindingDescriptor{"arr_prefix", 8});
        plan.arrayValidateBindings.push_back(llvmdsdl::ArrayValidateBindingDescriptor{"arr_validate", 32});

        const auto lines = llvmdsdl::renderSectionHelperBindings(
            plan,
            llvmdsdl::HelperBindingRenderLanguage::Go,
            llvmdsdl::ScalarBindingRenderDirection::Serialize,
            [](const std::string& symbol) { return "mlir_" + symbol; },
            /*emitCapacityCheck=*/true);
        if (!hasSubstring(lines, "mlir_cap := func(capacityBits int64) int8 {") ||
            !hasSubstring(lines, "mlir_validate_tag := func(tagValue int64) int8 {") ||
            !hasSubstring(lines, "mlir_mask_tag := func(value uint64) uint64 {") ||
            !hasSubstring(lines, "mlir_delim := func(payloadBytes int64, remainingBytes int64) int8 {") ||
            !hasSubstring(lines, "mlir_arr_prefix := func(value uint64) uint64 {") ||
            !hasSubstring(lines, "mlir_arr_validate := func(value int64) int8 {"))
        {
            std::cerr << "go section helper binding render mismatch\n";
            return false;
        }
    }

    return true;
}
