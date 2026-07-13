//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared render template for the union section prologue (P2 Phase 2).
///
/// The canonical serialize/deserialize union step order
/// (docs/plans/P2_canonical_emit_order.md, proven safe by
/// spec/dafny/CyphalSerdes.dfy) lives in exactly one place:
/// @ref buildUnionSectionSteps. Every string backend supplies a
/// @ref UnionSectionSpelling that renders each abstract step in its own idiom
/// (match/switch/if-elif, Result/(rc,0)/negative-int/throw/raise, mask folded
/// into the write argument or spelled as its own statement) and records the
/// corresponding EmitTraceOp events at the spelling site, so the emit-order
/// verifier proves the template preserved behavior.
///
/// A backend can no longer reorder the union prologue on its own: the order is
/// produced by construction here, and the verifier pins it independently.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_EMIT_STEP_H
#define LLVMDSDL_CODEGEN_EMIT_STEP_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "llvmdsdl/CodeGen/EmitTrace.h"

namespace llvmdsdl
{

/// @brief One union option arm: its wire tag value plus the backend-rendered body.
///
/// The body closure renders option alignment plus the option field's
/// serialize/deserialize ops (and any backend-specific guards, e.g. the
/// scripted backends' option-missing checks). It is opaque to the template:
/// Phase 2 shares the *prologue*; field rendering remains per backend until 2d.
struct UnionCaseRender final
{
    std::int64_t          optionIndex;
    std::function<void()> renderBody;
};

/// @brief Per-backend spelling of the abstract union-prologue steps.
///
/// Each method renders one canonical step in the backend's idiom and records
/// the abstract EmitTraceOp events for exactly what it emits. Methods are
/// invoked by @ref renderUnionSection in canonical order only — a spelling
/// cannot change the step order, just the surface text.
class UnionSectionSpelling
{
public:
    virtual ~UnionSectionSpelling() = default;

    // ---- serialize-only steps ----

    /// @brief VALIDATE_TAG: validate the caller-set tag; error-branch on failure.
    virtual void spellSerializeValidateTag() = 0;

    /// @brief MASK_TAG + WRITE_TAG: write the masked tag (mask may be folded into the
    ///        write argument or spelled as its own statement — D1 spelling freedom).
    virtual void spellSerializeWriteMaskedTag() = 0;

    // ---- deserialize-only steps ----

    /// @brief READ_TAG + MASK_TAG + STORE_TAG: read raw tag bits, mask, store.
    virtual void spellDeserializeReadMaskStoreTag() = 0;

    /// @brief VALIDATE_TAG: validate the stored tag; error-branch on failure.
    virtual void spellDeserializeValidateTag() = 0;

    // ---- direction-shared steps ----

    /// @brief ADVANCE(tagBits): advance the bit cursor past the tag.
    virtual void spellAdvanceTag() = 0;

    /// @brief SWITCH: open the dispatch construct (match / switch / if-chain).
    virtual void spellBeginDispatch() = 0;

    /// @brief CASE(optionIndex): open one case arm. @p firstCase supports if/elif chains.
    virtual void spellBeginCase(std::int64_t optionIndex, bool firstCase) = 0;

    /// @brief Close one case arm (may be empty for keyword-cased languages).
    virtual void spellEndCase() = 0;

    /// @brief DEFAULT_BAD_TAG: the unreachable-tag arm -> BAD_UNION_TAG error.
    virtual void spellBadTagDefault() = 0;

    /// @brief Close the dispatch construct (may be empty).
    virtual void spellEndDispatch() = 0;
};

/// @brief Abstract union-prologue step kinds, in the vocabulary of the canonical spec.
enum class UnionStepKind
{
    SerializeValidateTag,
    SerializeWriteMaskedTag,
    DeserializeReadMaskStoreTag,
    DeserializeValidateTag,
    AdvanceTag,
    BeginDispatch,
    BeginCase,   ///< caseOrdinal identifies the option arm.
    CaseBody,    ///< caseOrdinal identifies the option arm.
    EndCase,     ///< caseOrdinal identifies the option arm.
    BadTagDefault,
    EndDispatch,
};

/// @brief One reified emit step; caseOrdinal indexes the case list (-1 when N/A).
struct UnionEmitStep final
{
    UnionStepKind kind;
    std::int32_t  caseOrdinal;
};

/// @brief Builds the canonical ordered step list for a union section.
///
/// THE single in-code statement of the canonical union prologue order:
///   serialize:   VALIDATE_TAG, MASK+WRITE_TAG, ADVANCE, SWITCH,
///                (CASE, body, end-case)*, DEFAULT_BAD_TAG, end-switch
///   deserialize: READ+MASK+STORE_TAG, VALIDATE_TAG, ADVANCE, SWITCH,
///                (CASE, body, end-case)*, DEFAULT_BAD_TAG, end-switch
///
/// @param[in] direction Serialize or deserialize.
/// @param[in] caseCount Number of union option arms.
/// @return The ordered steps @ref renderUnionSection walks.
std::vector<UnionEmitStep> buildUnionSectionSteps(EmitTraceDirection direction, std::size_t caseCount);

/// @brief Renders a union section prologue + dispatch through a backend spelling.
///
/// Walks @ref buildUnionSectionSteps and dispatches each step to the spelling
/// (case bodies to the corresponding @ref UnionCaseRender). All five string
/// backends' union emission funnels through here.
///
/// @param[in] direction Serialize or deserialize.
/// @param[in] cases Union option arms in lowered order.
/// @param[in,out] spelling Backend spelling that renders each step.
void renderUnionSection(EmitTraceDirection                 direction,
                        const std::vector<UnionCaseRender>& cases,
                        UnionSectionSpelling&               spelling);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_EMIT_STEP_H
