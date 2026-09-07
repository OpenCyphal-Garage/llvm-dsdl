//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Core semantic model declarations produced from parsed DSDL AST definitions.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SEMANTICS_MODEL_H
#define LLVMDSDL_SEMANTICS_MODEL_H

#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/BitLengthSet.h"
#include "llvmdsdl/Semantics/Evaluator.h"

#include <optional>
#include <string>
#include <vector>

namespace llvmdsdl
{

/// @file
/// @brief Semantic model produced from parsed DSDL AST.

/// @brief Resolved scalar category for a semantic field type.
enum class SemanticScalarCategory
{

    /// @brief Boolean scalar.
    Bool,

    /// @brief Byte scalar.
    Byte,

    /// @brief UTF-8 code unit scalar.
    Utf8,

    /// @brief Unsigned integer scalar.
    UnsignedInt,

    /// @brief Signed integer scalar.
    SignedInt,

    /// @brief Floating-point scalar.
    Float,

    /// @brief Padding/void scalar.
    Void,

    /// @brief Composite reference scalar.
    Composite,
};

/// @brief Canonical reference to a resolved composite DSDL type.
struct SemanticTypeRef final
{
    /// @brief Fully qualified type name.
    std::string fullName;

    /// @brief Namespace path components.
    std::vector<std::string> namespaceComponents;

    /// @brief Short type name.
    std::string shortName;

    /// @brief Major version.
    std::uint32_t majorVersion{0};

    /// @brief Minor version.
    std::uint32_t minorVersion{0};
};

/// @brief Fully resolved type information used by lowering and codegen.
struct SemanticFieldType final
{
    /// @brief Scalar category.
    SemanticScalarCategory scalarCategory{SemanticScalarCategory::Void};

    /// @brief Cast mode semantics for integer-like types.
    CastMode castMode{CastMode::Saturated};

    /// @brief Scalar bit width for non-composite types.
    std::uint32_t bitLength{0};

    /// @brief Array qualifier.
    ArrayKind arrayKind{ArrayKind::None};

    /// @brief Resolved maximum array element count.
    std::int64_t arrayCapacity{0};

    /// @brief Variable-array length-prefix width in bits.
    std::int64_t arrayLengthPrefixBits{0};

    /// @brief Required alignment in bits.
    std::int64_t alignmentBits{1};

    /// @brief Possible serialised bit-length set.
    BitLengthSet bitLengthSet;

    /// @brief Target composite type for composite fields.
    std::optional<SemanticTypeRef> compositeType;

    /// @brief True for sealed composites, false for appendable composites.
    bool compositeSealed{true};

    /// @brief Composite payload extent in bits for appendable types.
    std::int64_t compositeExtentBits{0};
};

/// @brief Resolved field declaration.
struct SemanticField final
{
    /// @brief Field name.
    std::string name;

    /// @brief Attached documentation comments.
    AttachedDoc doc;

    /// @brief Original parsed type expression.
    TypeExprAST type;

    /// @brief True when this field represents padding.
    bool isPadding{false};

    /// @brief Resolved type metadata.
    SemanticFieldType resolvedType;

    /// @brief Logical section name for diagnostics.
    std::string sectionName;

    /// @brief Union option index when section is a union.
    std::uint32_t unionOptionIndex{0};

    /// @brief Union tag width in bits.
    std::uint32_t unionTagBits{0};
};

/// @brief Resolved constant declaration.
struct SemanticConstant final
{
    /// @brief Constant symbol name.
    std::string name;

    /// @brief Attached documentation comments.
    AttachedDoc doc;

    /// @brief Original parsed type expression.
    TypeExprAST type;

    /// @brief Evaluated constant value.
    Value value;
};

/// @brief Semantic representation of one serialisation section.
struct SemanticSection final
{
    /// @brief True when the section is a union.
    bool isUnion{false};

    /// @brief True when the section is sealed.
    bool sealed{false};

    /// @brief True when the section is deprecated.
    bool deprecated{false};

    /// @brief Optional explicit extent in bits.
    std::optional<std::int64_t> extentBits;

    /// @brief Section fields in declaration order.
    std::vector<SemanticField> fields;

    /// @brief Section constants.
    std::vector<SemanticConstant> constants;

    /// @brief Possible final offsets at section end.
    BitLengthSet offsetAtEnd;

    /// @brief Minimum serialised bit length.
    std::int64_t minBitLength{0};

    /// @brief Maximum serialised bit length.
    std::int64_t maxBitLength{0};

    /// @brief True when serialised bit length is invariant.
    bool fixedSize{true};

    /// @brief Required serialisation buffer size in bits.
    std::int64_t serializationBufferSizeBits{0};
};

/// @brief Fully resolved definition including optional service response.
struct SemanticDefinition final
{
    /// @brief Discovery metadata.
    DiscoveredDefinition info;

    /// @brief Attached documentation comments.
    AttachedDoc doc;

    /// @brief True when this definition is a service.
    bool isService{false};

    /// @brief Request section (or message body for non-services).
    SemanticSection request;

    /// @brief Optional response section for services.
    std::optional<SemanticSection> response;
};

/// @brief Semantic compilation unit.
struct SemanticModule final
{
    /// @brief All resolved definitions.
    std::vector<SemanticDefinition> definitions;
};

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SEMANTICS_MODEL_H
