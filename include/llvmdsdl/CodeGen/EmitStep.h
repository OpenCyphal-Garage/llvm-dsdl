//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared render template for section bodies (P2 Phase 2).
///
/// The canonical serialize/deserialize step order
/// (docs/reference/codegen/emit-order.md, proven safe by
/// spec/dafny/CyphalSerdes.dfy) lives in exactly one place per construct:
/// @ref buildUnionSectionSteps for the union prologue/dispatch, and
/// @ref buildFieldEmitSteps + @ref renderFieldSteps for the recursive field
/// bodies (scalars, arrays including their element recursion, composites,
/// padding). Every string backend supplies spelling objects that render each
/// abstract step in the backend's own idiom and record the corresponding
/// EmitTraceOp events at the spelling site, so the emit-order verifier proves
/// the template preserved behavior.
///
/// A backend can no longer reorder emission on its own: the order is produced
/// by construction here, and the verifier pins it independently. The two
/// accepted structural deviations are explicit interface points, not scattered
/// conditionals: D2 (fixed-array length guard may be type-system-subsumed) is
/// @ref FieldStepSpelling::spellFixedArrayLenCheck, and D3 (the C++ fixed-bool
/// bulk copy) is @ref FieldStepSpelling::trySpellArrayBulkFastPath.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_EMIT_STEP_H
#define LLVMDSDL_CODEGEN_EMIT_STEP_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "llvmdsdl/CodeGen/EmitTrace.h"
#include "llvmdsdl/CodeGen/SerDesHelperDescriptors.h"
#include "llvmdsdl/Semantics/Model.h"

namespace llvmdsdl
{
enum class HelperBindingDirection;
struct LoweredFieldFacts;

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

//===----------------------------------------------------------------------===//
// Recursive field-body render template (P2 step 2d)
//===----------------------------------------------------------------------===//

/// @brief Abstract field-body step kinds, mirroring the canonical spec vocabulary.
enum class FieldStepKind
{
    Pad,            ///< void padding: PAD + ADVANCE (payload = bits).
    ScalarBool,     ///< 1-bit bool: WRITE/READ_SCALAR_BOOL + ADVANCE.
    ScalarUint,     ///< unsigned/byte/utf8: helper + WRITE/READ_SCALAR_UINT + ADVANCE.
    ScalarSint,     ///< signed: helper + WRITE/READ_SCALAR_SINT + ADVANCE.
    ScalarFloat,    ///< float16/32/64: width-matched helper + WRITE/READ_SCALAR_FLOAT + ADVANCE.
    FixedArray,     ///< LEN_CHECK (D2) then ELEM_LOOP { child }; D3 bulk hook may replace the loop.
    VariableArray,  ///< serialize LEN_VALIDATE+LEN_WRITE+ADVANCE / deserialize LEN_READ+ADVANCE+
                    ///< LEN_VALIDATE then ELEM_LOOP { child }.
    Composite,      ///< COMPOSITE_INLINE (sealed) or COMPOSITE_DELIM_HEADER (delimited) nested call.
};

/// @brief One node of the recursive field-body step tree.
///
/// Built once per (field, direction) by @ref buildFieldEmitSteps from
/// backend-neutral facts (semantic type, lowered facts, shared helper-symbol
/// resolution, shared array wire planning). Array nodes own exactly one child:
/// the element's step tree — this is where nesting/recursion is shared rather
/// than re-written per backend.
struct FieldEmitStep final
{
    FieldStepKind kind{FieldStepKind::Pad};

    /// @brief Resolved semantic type at this node (element type inside array children).
    SemanticFieldType type;

    /// @brief Scalar bit width / pad bits (kind-dependent).
    std::int64_t bits{0};

    /// @brief Array capacity (Fixed/VariableArray).
    std::int64_t capacity{0};

    /// @brief Variable-array length-prefix width in bits.
    std::uint32_t prefixBits{0};

    /// @brief Direction-resolved scalar normalize/mask/saturate helper symbol (scalar kinds).
    std::string scalarHelperSymbol;

    /// @brief Variable-array length helper descriptors (validate + prefix normalize).
    std::optional<ArrayLengthHelperDescriptor> arrayHelpers;

    /// @brief Delimited-composite header validate helper symbol (Composite, non-sealed).
    std::string delimiterValidateSymbol;

    /// @brief Element step tree for array kinds (exactly one entry).
    std::vector<FieldEmitStep> children;
};

/// @brief Per-backend spelling of the abstract field-body steps.
///
/// Methods are invoked by @ref renderFieldSteps in canonical order only. Each
/// method renders one bundled step group in the backend's idiom (statement
/// shapes, temp names, error channel) and records the abstract trace ops for
/// exactly what it emits; the *cross-group* order is renderer-owned. Spellings
/// track their own indentation across loop begin/end.
///
/// @warning Temp-name allocation order is load-bearing. `nextName` counters are
/// shared and order-sensitive, so the sequence of `nextName` calls inside a
/// spelling method determines the generated identifier suffixes for the whole
/// section. Backends legitimately differ — C++ allocates `count_raw` before
/// `count`, Rust/Go the reverse — and each preserves the allocation order its
/// generated output had before the sequencing was shared. Reordering these
/// calls is not a no-op refactor: it renames identifiers across the entire
/// corpus and will churn the golden/lit snapshots even though the emit-order
/// trace is unchanged.
class FieldStepSpelling
{
public:
    virtual ~FieldStepSpelling() = default;

    /// @brief PAD + ADVANCE for a void field.
    virtual void spellPad(const FieldEmitStep& step) = 0;

    /// @brief Scalar serialize: cast/helper + WRITE_SCALAR_* + error branch + ADVANCE.
    virtual void spellScalarSerialize(const FieldEmitStep& step, const std::string& valueExpr) = 0;

    /// @brief Scalar deserialize: READ_SCALAR_* + helper + store + ADVANCE.
    virtual void spellScalarDeserialize(const FieldEmitStep& step, const std::string& targetExpr) = 0;

    /// @brief D2 point — fixed-array exact-length guard (LEN_CHECK).
    ///
    /// Backends whose fixed arrays are compile-time sized (Go `[N]T`,
    /// C++ `std::array`) implement this as a documented no-op: the guard is
    /// type-system-subsumed, not skipped ad hoc.
    virtual void spellFixedArrayLenCheck(const FieldEmitStep& step, const std::string& expr) = 0;

    /// @brief D3 point — optional bulk fast path replacing the element loop (BULK_COPY).
    ///
    /// Invoked after the length-handling group in both directions. Return true
    /// to declare the loop replaced (the renderer then skips ELEM_LOOP and the
    /// element subtree). Only C++ implements it, for fixed bool arrays.
    virtual bool trySpellArrayBulkFastPath(const FieldEmitStep&    step,
                                           const std::string&      expr,
                                           HelperBindingDirection  direction)
    {
        (void) step;
        (void) expr;
        (void) direction;
        return false;
    }

    /// @brief Variable-array serialize length group: LEN_VALIDATE + LEN_WRITE + ADVANCE.
    virtual void spellVariableArrayLenSerialize(const FieldEmitStep& step, const std::string& expr) = 0;

    /// @brief Variable-array deserialize length group: LEN_READ + ADVANCE + normalize +
    ///        LEN_VALIDATE + storage prep (clear/reserve/resize).
    /// @return The element-count expression the loop bounds on.
    virtual std::string spellVariableArrayLenDeserialize(const FieldEmitStep& step, const std::string& expr) = 0;

    /// @brief Fixed-array deserialize storage prep (may emit nothing).
    /// @return The element-count expression the loop bounds on.
    virtual std::string spellFixedArrayCountDeserialize(const FieldEmitStep& step, const std::string& expr) = 0;

    /// @brief ELEM_LOOP open (serialize). @return The element value expression for the body.
    virtual std::string spellBeginElemLoopSerialize(const FieldEmitStep& step, const std::string& expr) = 0;

    /// @brief ELEM_LOOP open (deserialize). @return The element target expression for the body.
    virtual std::string spellBeginElemLoopDeserialize(const FieldEmitStep& step,
                                                      const std::string&   expr,
                                                      const std::string&   countExpr) = 0;

    /// @brief Close the serialize element loop.
    virtual void spellEndElemLoopSerialize(const FieldEmitStep& step, const std::string& expr) = 0;

    /// @brief Close the deserialize element loop (append/push if the backend needs it).
    virtual void spellEndElemLoopDeserialize(const FieldEmitStep& step, const std::string& expr) = 0;

    /// @brief Composite serialize: COMPOSITE_INLINE/COMPOSITE_DELIM_HEADER + nested call
    ///        (+ delimiter backpatch) + ADVANCE.
    virtual void spellCompositeSerialize(const FieldEmitStep& step, const std::string& expr) = 0;

    /// @brief Composite deserialize: header read/validate (delimited) + nested call + ADVANCE.
    virtual void spellCompositeDeserialize(const FieldEmitStep& step, const std::string& expr) = 0;
};

/// @brief Builds the recursive step tree for one field body from shared facts.
///
/// THE single in-code statement of canonical field-body structure: scalar
/// grouping, array length-group-then-loop ordering, and element recursion are
/// decided here for every backend.
///
/// @param[in] type Resolved field type.
/// @param[in] fieldFacts Lowered field facts (helper symbols, prefix widths).
/// @param[in] prefixBitsOverride Optional lowered array-prefix override.
/// @param[in] direction Serialize or deserialize (helper symbols differ).
/// @return Root of the field's step tree.
FieldEmitStep buildFieldEmitSteps(const SemanticFieldType&     type,
                                  const LoweredFieldFacts*     fieldFacts,
                                  std::optional<std::uint32_t> prefixBitsOverride,
                                  HelperBindingDirection       direction);

/// @brief Renders one field's step tree through a backend spelling.
///
/// Owns every cross-group ordering decision, recursively: fixed arrays render
/// LEN_CHECK (D2 hook) then the D3 bulk hook then ELEM_LOOP { element subtree };
/// variable arrays render their direction's length group then the loop; scalars,
/// padding, and composites dispatch to the corresponding spelling method.
///
/// @param[in] step Field step tree (from @ref buildFieldEmitSteps).
/// @param[in] expr Backend value/target expression for this node.
/// @param[in] direction Serialize or deserialize.
/// @param[in,out] spelling Backend spelling.
void renderFieldSteps(const FieldEmitStep&   step,
                      const std::string&     expr,
                      HelperBindingDirection direction,
                      FieldStepSpelling&     spelling);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_EMIT_STEP_H
