//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Abstract syntax tree declarations representing parsed DSDL definitions.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_FRONTEND_AST_H
#define LLVMDSDL_FRONTEND_AST_H

#include "llvmdsdl/Frontend/SourceLocation.h"
#include "llvmdsdl/Support/Rational.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace llvmdsdl
{

/// @file
/// @brief Frontend abstract syntax tree types for parsed DSDL definitions.

/// @brief Cast behaviour requested by scalar type expressions.
enum class CastMode
{

    /// @brief Saturating cast semantics.
    Saturated,

    /// @brief Truncating cast semantics.
    Truncated,
};

/// @brief Primitive scalar families supported by DSDL syntax.
enum class PrimitiveKind
{

    /// @brief Boolean primitive.
    Bool,

    /// @brief Byte primitive.
    Byte,

    /// @brief UTF-8 code unit primitive.
    Utf8,

    /// @brief Unsigned integer primitive family.
    UnsignedInt,

    /// @brief Signed integer primitive family.
    SignedInt,

    /// @brief Floating-point primitive family.
    Float,
};

/// @brief Array modifier attached to a scalar type expression.
enum class ArrayKind
{

    /// @brief Scalar (non-array) type.
    None,

    /// @brief Fixed-size array (`[N]`).
    Fixed,

    /// @brief Variable-size inclusive array (`[<=N]`).
    VariableInclusive,

    /// @brief Variable-size exclusive array (`[<N]`).
    VariableExclusive,
};

struct TypeExprAST;
struct ExprAST;

/// @brief One normalized source comment line attached to an AST node.
struct AttachedDocLine
{
    /// @brief Source location of the original comment token.
    SourceLocation location;

    /// @brief Normalized comment text without leading `#`.
    std::string text;
};

/// @brief Attached documentation comments for an AST node.
struct AttachedDoc
{
    /// @brief Attached comment lines in source order.
    std::vector<AttachedDocLine> lines;

    /// @brief Returns true when no comment lines are attached.
    /// @return True when @ref lines is empty.
    [[nodiscard]] bool empty() const;

    /// @brief Returns attached comment text joined by newlines.
    /// @return Joined normalized comment text.
    [[nodiscard]] std::string str() const;
};

/// @brief Primitive scalar type expression.
struct PrimitiveTypeExprAST
{
    /// @brief Primitive scalar family.
    PrimitiveKind kind{PrimitiveKind::Bool};

    /// @brief Requested cast mode.
    CastMode castMode{CastMode::Saturated};

    /// @brief Scalar bit length.
    std::uint32_t bitLength{1};
};

/// @brief Explicit padding type expression (`voidN`).
struct VoidTypeExprAST
{
    /// @brief Padding width in bits.
    std::uint32_t bitLength{1};
};

/// @brief Versioned reference to another DSDL type.
struct VersionedTypeExprAST
{
    /// @brief Qualified name components.
    std::vector<std::string> nameComponents;

    /// @brief Major version.
    std::uint32_t major{0};

    /// @brief Minor version.
    std::uint32_t minor{0};
};

/// @brief Parsed type expression.
struct TypeExprAST
{
    /// @brief Source location for the expression.
    SourceLocation location;

    /// @brief Scalar variant payload.
    std::variant<PrimitiveTypeExprAST, VoidTypeExprAST, VersionedTypeExprAST> scalar;

    /// @brief Array modifier.
    ArrayKind arrayKind{ArrayKind::None};

    /// @brief Optional array capacity expression.
    std::shared_ptr<ExprAST> arrayCapacity;

    /// @brief Returns true when this type is a void/padding expression.
    /// @return True for @ref VoidTypeExprAST scalar variant.
    [[nodiscard]] bool isVoid() const;

    /// @brief Returns a normalized textual representation.
    /// @return Human-readable type expression text.
    [[nodiscard]] std::string str() const;
};

/// @brief Unary operators in constant expressions.
enum class UnaryOp
{

    /// @brief Unary `+`.
    Plus,

    /// @brief Unary `-`.
    Minus,

    /// @brief Logical negation `!`.
    LogicalNot,
};

/// @brief Binary operators in constant expressions.
enum class BinaryOp
{

    /// @brief Attribute access operator (`.`).
    Attribute,

    /// @brief Exponentiation (`**`).
    Pow,

    /// @brief Multiplication (`*`).
    Mul,

    /// @brief Division (`/`).
    Div,

    /// @brief Modulus (`%`).
    Mod,

    /// @brief Addition (`+`).
    Add,

    /// @brief Subtraction (`-`).
    Sub,

    /// @brief Bitwise OR (`|`).
    BitOr,

    /// @brief Bitwise XOR (`^`).
    BitXor,

    /// @brief Bitwise AND (`&`).
    BitAnd,

    /// @brief Equality (`==`).
    Eq,

    /// @brief Inequality (`!=`).
    Ne,

    /// @brief Less-than-or-equal (`<=`).
    Le,

    /// @brief Greater-than-or-equal (`>=`).
    Ge,

    /// @brief Less-than (`<`).
    Lt,

    /// @brief Greater-than (`>`).
    Gt,

    /// @brief Logical OR (`||`).
    LogicalOr,

    /// @brief Logical AND (`&&`).
    LogicalAnd,
};

/// @brief Parsed constant-expression node.
struct ExprAST
{
    /// @brief Identifier expression payload.
    struct Identifier
    {
        /// @brief Symbol name.
        std::string name;
    };

    /// @brief Unary expression payload.
    struct Unary
    {
        /// @brief Unary operator.
        UnaryOp op;

        /// @brief Operand expression.
        std::shared_ptr<ExprAST> operand;
    };

    /// @brief Binary expression payload.
    struct Binary
    {
        /// @brief Binary operator.
        BinaryOp op;

        /// @brief Left-hand operand.
        std::shared_ptr<ExprAST> lhs;

        /// @brief Right-hand operand.
        std::shared_ptr<ExprAST> rhs;
    };

    /// @brief Set literal payload.
    struct SetLiteral
    {
        /// @brief Set elements.
        std::vector<std::shared_ptr<ExprAST>> elements;
    };

    /// @brief Type literal payload.
    struct TypeLiteral
    {
        /// @brief Embedded type expression.
        TypeExprAST type;
    };

    /// @brief Source location for this expression node.
    SourceLocation location;

    /// @brief Expression payload variant.
    std::variant<bool, Rational, std::string, Identifier, Unary, Binary, SetLiteral, TypeLiteral> value;

    /// @brief Returns a normalized textual representation.
    /// @return Human-readable expression text.
    [[nodiscard]] std::string str() const;
};

/// @brief Supported `@` directives.
enum class DirectiveKind
{

    /// @brief `@union`.
    Union,

    /// @brief `@extent`.
    Extent,

    /// @brief `@sealed`.
    Sealed,

    /// @brief `@deprecated`.
    Deprecated,

    /// @brief `@assert`.
    Assert,

    /// @brief `@print`.
    Print,

    /// @brief Unrecognized directive.
    Unknown,
};

/// @brief Constant declaration statement.
struct ConstantDeclAST
{
    /// @brief Statement source location.
    SourceLocation location;

    /// @brief Documentation attached to this declaration.
    AttachedDoc doc;

    /// @brief Declared type expression.
    TypeExprAST type;

    /// @brief Constant symbol name.
    std::string name;

    /// @brief Source location of constant symbol name.
    SourceLocation nameLocation;

    /// @brief Value expression.
    std::shared_ptr<ExprAST> value;
};

/// @brief Field declaration statement.
struct FieldDeclAST
{
    /// @brief Statement source location.
    SourceLocation location;

    /// @brief Documentation attached to this declaration.
    AttachedDoc doc;

    /// @brief Field type expression.
    TypeExprAST type;

    /// @brief Field name.
    std::string name;

    /// @brief Source location of field symbol name.
    SourceLocation nameLocation;

    /// @brief True when the field represents padding.
    bool isPadding{false};
};

/// @brief Directive statement.
struct DirectiveAST
{
    /// @brief Statement source location.
    SourceLocation location;

    /// @brief Documentation attached to this directive.
    AttachedDoc doc;

    /// @brief Parsed directive kind.
    DirectiveKind kind{DirectiveKind::Unknown};

    /// @brief Raw directive spelling (without `@`).
    std::string rawName;

    /// @brief Optional directive expression payload.
    std::shared_ptr<ExprAST> expression;
};

/// @brief Marker separating service request and response sections.
struct ServiceResponseMarkerAST
{
    /// @brief Marker source location.
    SourceLocation location;

    /// @brief Documentation attached to this marker.
    AttachedDoc doc;
};

/// @brief Union of all supported top-level statements.
using StatementAST = std::variant<ConstantDeclAST, FieldDeclAST, DirectiveAST, ServiceResponseMarkerAST>;

/// @brief Top-level parsed definition.
struct DefinitionAST
{
    /// @brief Definition source location.
    SourceLocation location;

    /// @brief Documentation attached to this definition.
    AttachedDoc doc;

    /// @brief Statements in source order.
    std::vector<StatementAST> statements;

    /// @brief Returns true when the definition is a service.
    /// @return True when a service response marker is present.
    [[nodiscard]] bool isService() const;
};

/// @brief Definition discovered on disk with resolved metadata.
struct DiscoveredDefinition
{
    /// @brief Source file path.
    std::string filePath;

    /// @brief Owning root namespace directory.
    std::string rootNamespacePath;

    /// @brief Fully qualified type name.
    std::string fullName;

    /// @brief Short type name.
    std::string shortName;

    /// @brief Namespace path components.
    std::vector<std::string> namespaceComponents;

    /// @brief Major version.
    std::uint32_t majorVersion{0};

    /// @brief Minor version.
    std::uint32_t minorVersion{0};

    /// @brief Optional fixed port-id.
    std::optional<std::uint32_t> fixedPortId;

    /// @brief True if this definition was explicitly targeted by the CLI.
    bool isExplicitTarget{false};

    /// @brief True if the CLI named this definition's file one at a time, rather than sweeping it in.
    ///
    /// A folder target makes @ref isExplicitTarget true for everything beneath it, so that flag
    /// cannot distinguish `dsdlc ns/` from `dsdlc ns/Foo.1.0.dsdl`. This one can, which is what lets
    /// a default that generates only the newest version of each type leave alone a version the user
    /// asked for by name.
    bool isNamedTarget{false};

    /// @brief Full source text.
    std::string text;
};

/// @brief Parsed definition bundle: discovery metadata plus AST.
struct ParsedDefinition
{
    /// @brief Discovery metadata.
    DiscoveredDefinition info;

    /// @brief Parsed AST.
    DefinitionAST ast;
};

/// @brief Collection of parsed definitions.
struct ASTModule
{
    /// @brief Parsed definitions in module scope.
    std::vector<ParsedDefinition> definitions;
};

}  // namespace llvmdsdl

#endif  // LLVMDSDL_FRONTEND_AST_H
