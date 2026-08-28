//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// APIs for rendering helper binding snippets in supported target languages.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_HELPER_BINDING_RENDER_H
#define LLVMDSDL_CODEGEN_HELPER_BINDING_RENDER_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace llvmdsdl
{
struct ScalarHelperDescriptor;
struct SectionHelperBindingPlan;

/// @file
/// @brief Render helpers for language-specific helper-binding snippets.

/// @brief Target language for helper-binding snippet rendering.
enum class HelperBindingRenderLanguage
{

    /// @brief C++ target.
    Cpp,

    /// @brief Rust target.
    Rust,

    /// @brief Go target.
    Go,

    /// @brief TypeScript target.
    TypeScript,

    /// @brief Python target.
    Python,
};

/// @brief Direction for scalar helper snippet rendering.
enum class ScalarBindingRenderDirection
{

    /// @brief Serialize helper wrapper.
    Serialize,

    /// @brief Deserialize helper wrapper.
    Deserialize,
};

/// @brief Renders the all-ones literal for a @p bits-wide field.
/// @param[in] language Target language.
/// @param[in] bits Wire width; 0 and widths at or above 64 have their own spellings.
/// @return The literal.
std::string renderU64MaskLiteral(HelperBindingRenderLanguage language, std::uint32_t bits);

/// @brief Renders capacity-check helper binding code.
/// @param[in] language Target language.
/// @param[in] helperName Helper symbol.
/// @param[in] requiredBits Required capacity in bits.
/// @return Rendered source lines.
std::vector<std::string> renderCapacityCheckBinding(HelperBindingRenderLanguage language,
                                                    const std::string&          helperName,
                                                    std::int64_t                requiredBits);

/// @brief Renders union-tag mask helper binding code.
/// @param[in] language Target language.
/// @param[in] helperName Helper symbol.
/// @param[in] bits Union-tag width in bits.
/// @return Rendered source lines.
std::vector<std::string> renderUnionTagMaskBinding(HelperBindingRenderLanguage language,
                                                   const std::string&          helperName,
                                                   std::uint32_t               bits);

/// @brief Renders union-tag validation helper binding code.
/// @param[in] language Target language.
/// @param[in] helperName Helper symbol.
/// @param[in] allowedTags Allowed union tags.
/// @return Rendered source lines.
std::vector<std::string> renderUnionTagValidateBinding(HelperBindingRenderLanguage      language,
                                                       const std::string&               helperName,
                                                       const std::vector<std::int64_t>& allowedTags);

/// @brief Renders delimiter validation helper binding code.
/// @param[in] language Target language.
/// @param[in] helperName Helper symbol.
/// @return Rendered source lines.
std::vector<std::string> renderDelimiterValidateBinding(HelperBindingRenderLanguage language,
                                                        const std::string&          helperName);

/// @brief Renders scalar helper binding code.
/// @param[in] language Target language.
/// @param[in] direction Serialize or deserialize.
/// @param[in] helperName Helper symbol.
/// @param[in] descriptor Scalar helper descriptor.
/// @return Rendered source lines.
std::vector<std::string> renderScalarBinding(HelperBindingRenderLanguage   language,
                                             ScalarBindingRenderDirection  direction,
                                             const std::string&            helperName,
                                             const ScalarHelperDescriptor& descriptor);

/// @brief Renders array-prefix helper binding code.
/// @param[in] language Target language.
/// @param[in] helperName Helper symbol.
/// @param[in] bits Prefix width in bits.
/// @return Rendered source lines.
std::vector<std::string> renderArrayPrefixBinding(HelperBindingRenderLanguage language,
                                                  const std::string&          helperName,
                                                  std::uint32_t               bits);

/// @brief Renders array-length validation helper binding code.
/// @param[in] language Target language.
/// @param[in] helperName Helper symbol.
/// @param[in] capacity Maximum allowed element count.
/// @return Rendered source lines.
std::vector<std::string> renderArrayValidateBinding(HelperBindingRenderLanguage language,
                                                    const std::string&          helperName,
                                                    std::int64_t                capacity);

/// @brief Renders complete helper-binding block for a section.
/// @param[in] plan Helper-binding plan.
/// @param[in] language Target language.
/// @param[in] scalarDirection Scalar helper direction.
/// @param[in] helperNameResolver Symbol-to-identifier resolver.
/// @param[in] emitCapacityCheck Controls capacity-check emission.
/// @return Rendered source lines.
std::vector<std::string> renderSectionHelperBindings(
    const SectionHelperBindingPlan&                       plan,
    HelperBindingRenderLanguage                           language,
    ScalarBindingRenderDirection                          scalarDirection,
    const std::function<std::string(const std::string&)>& helperNameResolver,
    bool                                                  emitCapacityCheck);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_HELPER_BINDING_RENDER_H
