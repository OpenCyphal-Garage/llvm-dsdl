//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Abstract emit-order trace vocabulary shared by the string backends.
///
/// The emit-order verifier (tools/convergence/emit_order_verifier.py) asserts that every string backend
/// (Rust/Go/C++/TypeScript/Python) performs the same ordered sequence of
/// *abstract* serialize/deserialize operations for a given type, independent of
/// surface spelling. Each backend records these ops into an @ref EmitTraceSink
/// at the point it emits the corresponding text; a null sink is the default and
/// costs nothing. The canonical order these ops must follow is specified in
/// docs/design/canonical-emit-order.md.
///
/// This header is intentionally dependency-free: it is the shared contract
/// between the five emitters and the verifier, and pulls in no MLIR/LLVM types.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_EMIT_TRACE_H
#define LLVMDSDL_CODEGEN_EMIT_TRACE_H

#include <cstdint>
#include <string>
#include <vector>

namespace llvmdsdl
{

/// @brief Serialize vs deserialize direction for a recorded trace.
enum class EmitTraceDirection
{
    Serialize,
    Deserialize,
};

/// @brief One abstract emit operation. Names mirror docs/design/canonical-emit-order.md.
///
/// These are *abstract* ops: the verifier compares the ordered sequence of them
/// across backends. Surface spelling (statement vs sub-expression, match vs
/// switch, error channel, identifiers, indentation) is deliberately NOT captured
/// — two backends that differ only in spelling must produce identical traces.
enum class EmitTraceOp
{
    // ---- Union tag (see canonical spec: Union serialize/deserialize) ----
    ValidateTag,       ///< unionTagValidate(tag) + error branch.
    MaskTag,           ///< unionTagMask(tag).
    WriteTag,          ///< write tag bits (payload = tagBits).
    ReadTag,           ///< read tag bits (payload = tagBits).
    StoreTag,          ///< assign masked tag into the object.
    Switch,            ///< begin switch/match on the tag.
    Case,              ///< begin a case (payload = union option index).
    DefaultBadTag,     ///< default arm -> BAD_UNION_TAG error.

    // ---- Alignment / padding ----
    Align,             ///< align to a bit boundary (payload = alignment bits).
    Pad,               ///< write/skip zeroed padding bits (payload = pad bits).

    // ---- Scalar values (payload = bit width) ----
    WriteScalarBool,
    WriteScalarUint,
    WriteScalarSint,
    WriteScalarFloat,
    ReadScalarBool,
    ReadScalarUint,
    ReadScalarSint,
    ReadScalarFloat,

    // ---- Arrays (see canonical spec: Array serialize/deserialize) ----
    LenCheck,          ///< fixed-array exact-length guard (payload = capacity).
    LenValidate,       ///< variable-array length validate.
    LenWrite,          ///< write length prefix (payload = prefix bits).
    LenRead,           ///< read length prefix (payload = prefix bits).
    ElemLoop,          ///< begin the element loop.
    BulkCopy,          ///< bulk bit-copy replacing an element loop (payload = total bits).
                       ///< C++-only fixed-bool-array fast path (known difference D3): the
                       ///< comparator models it as equivalent to ELEM_LOOP + one 1-bit
                       ///< bool scalar op — an accepted optimization, explicitly declared.

    // ---- Composites ----
    CompositeInline,       ///< sealed: inline nested (de)serialize at current offset.
    CompositeDelimHeader,  ///< delimited: 32-bit delimiter/extent header + nested payload.

    // ---- Cursor ----
    Advance,           ///< offset_bits += N (payload = N).

    // ---- Stream structure (not a wire op) ----
    SectionStart,      ///< begin of one (type, direction) trace segment; payload = direction
                       ///< (0 = serialize, 1 = deserialize), label = canonical DSDL name
                       ///< ("full.type.Name[.Request|.Response].major.minor"). The comparator
                       ///< keys segments by this label so failures localize to a type and
                       ///< direction and misalignment cannot cancel across type boundaries.
};

/// @brief One recorded abstract op with its optional payload.
struct EmitTraceEvent final
{
    EmitTraceOp  op;
    std::int64_t payload;  ///< Bit width / option index / capacity; -1 when not applicable.
    std::string  label;    ///< SectionStart only: canonical DSDL section name. Empty otherwise.
};

/// @brief Ordered collector of abstract emit ops for one (type, direction).
///
/// A backend holds a pointer to a sink (nullable). When null — the default in
/// production codegen — every @ref EmitTraceSink::record call site is a no-op via
/// @ref emitTrace, so instrumentation is zero-cost off the verifier path.
class EmitTraceSink final
{
public:
    /// @brief Appends one abstract emit op to the end of the recorded sequence.
    /// @param[in] op Abstract op observed at an emitter spelling site.
    /// @param[in] payload Optional payload (bit width / union option index / capacity); -1 when not applicable.
    void record(const EmitTraceOp op, const std::int64_t payload = -1)
    {
        events_.push_back(EmitTraceEvent{op, payload, {}});
    }

    /// @brief Marks the start of one (type, direction) trace segment.
    /// @param[in] name Canonical DSDL section name ("full.type.Name[.Request|.Response].major.minor").
    ///                 Must be backend-independent so the comparator can align segments across backends.
    /// @param[in] direction Serialize or deserialize.
    void beginSection(const std::string& name, const EmitTraceDirection direction)
    {
        events_.push_back(
            EmitTraceEvent{EmitTraceOp::SectionStart,
                           direction == EmitTraceDirection::Serialize ? std::int64_t{0} : std::int64_t{1},
                           name});
    }

    /// @brief Returns the recorded ops in emission order.
    /// @return Ordered events for this (type, direction); the sequence the emit-order verifier compares across backends.
    const std::vector<EmitTraceEvent>& events() const { return events_; }

    /// @brief Discards all recorded ops so the sink can be reused for the next (type, direction).
    void clear() { events_.clear(); }

private:
    std::vector<EmitTraceEvent> events_;
};

/// @brief Null-safe record helper used at emitter spelling sites.
/// @param[in] sink Nullable sink; when null this is a no-op (zero cost).
/// @param[in] op Abstract op to record.
/// @param[in] payload Optional payload (bit width / index / capacity).
inline void emitTrace(EmitTraceSink* const sink, const EmitTraceOp op, const std::int64_t payload = -1)
{
    if (sink != nullptr)
    {
        sink->record(op, payload);
    }
}

/// @brief Null-safe section-start helper used at emitter (type, direction) entry points.
/// @param[in] sink Nullable sink; when null this is a no-op (zero cost).
/// @param[in] name Canonical DSDL section name (backend-independent).
/// @param[in] direction Serialize or deserialize.
inline void emitTraceSection(EmitTraceSink* const     sink,
                             const std::string&       name,
                             const EmitTraceDirection direction)
{
    if (sink != nullptr)
    {
        sink->beginSection(name, direction);
    }
}

/// @brief Stable canonical name for an abstract op, for golden-trace comparison and diagnostics.
/// @param[in] op Abstract op.
/// @return A stable, spelling-independent token (never null).
inline const char* emitTraceOpName(const EmitTraceOp op)
{
    switch (op)
    {
    case EmitTraceOp::ValidateTag:          return "VALIDATE_TAG";
    case EmitTraceOp::MaskTag:              return "MASK_TAG";
    case EmitTraceOp::WriteTag:             return "WRITE_TAG";
    case EmitTraceOp::ReadTag:              return "READ_TAG";
    case EmitTraceOp::StoreTag:             return "STORE_TAG";
    case EmitTraceOp::Switch:               return "SWITCH";
    case EmitTraceOp::Case:                 return "CASE";
    case EmitTraceOp::DefaultBadTag:        return "DEFAULT_BAD_TAG";
    case EmitTraceOp::Align:                return "ALIGN";
    case EmitTraceOp::Pad:                  return "PAD";
    case EmitTraceOp::WriteScalarBool:      return "WRITE_SCALAR_BOOL";
    case EmitTraceOp::WriteScalarUint:      return "WRITE_SCALAR_UINT";
    case EmitTraceOp::WriteScalarSint:      return "WRITE_SCALAR_SINT";
    case EmitTraceOp::WriteScalarFloat:     return "WRITE_SCALAR_FLOAT";
    case EmitTraceOp::ReadScalarBool:       return "READ_SCALAR_BOOL";
    case EmitTraceOp::ReadScalarUint:       return "READ_SCALAR_UINT";
    case EmitTraceOp::ReadScalarSint:       return "READ_SCALAR_SINT";
    case EmitTraceOp::ReadScalarFloat:      return "READ_SCALAR_FLOAT";
    case EmitTraceOp::LenCheck:             return "LEN_CHECK";
    case EmitTraceOp::LenValidate:          return "LEN_VALIDATE";
    case EmitTraceOp::LenWrite:             return "LEN_WRITE";
    case EmitTraceOp::LenRead:              return "LEN_READ";
    case EmitTraceOp::ElemLoop:             return "ELEM_LOOP";
    case EmitTraceOp::BulkCopy:             return "BULK_COPY";
    case EmitTraceOp::CompositeInline:      return "COMPOSITE_INLINE";
    case EmitTraceOp::CompositeDelimHeader: return "COMPOSITE_DELIM_HEADER";
    case EmitTraceOp::Advance:              return "ADVANCE";
    case EmitTraceOp::SectionStart:         return "SECTION";
    }
    return "UNKNOWN";
}

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_EMIT_TRACE_H
