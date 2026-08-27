//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements pretty-printing for frontend AST nodes.
///
/// This file provides readable textual rendering of expressions and declarations for debugging and diagnostics.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Frontend/ASTPrinter.h"

#include <algorithm>
#include <sstream>
#include <cstddef>
#include <memory>  // IWYU pragma: keep -- libstdc++ reaches this transitively; libc++ needs it named.
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "llvmdsdl/Frontend/AST.h"

namespace llvmdsdl
{
class Rational;

namespace
{

std::string binaryOpToString(BinaryOp op)
{
    switch (op)
    {
    case BinaryOp::Attribute:
        return ".";
    case BinaryOp::Pow:
        return "**";
    case BinaryOp::Mul:
        return "*";
    case BinaryOp::Div:
        return "/";
    case BinaryOp::Mod:
        return "%";
    case BinaryOp::Add:
        return "+";
    case BinaryOp::Sub:
        return "-";
    case BinaryOp::BitOr:
        return "|";
    case BinaryOp::BitXor:
        return "^";
    case BinaryOp::BitAnd:
        return "&";
    case BinaryOp::Eq:
        return "==";
    case BinaryOp::Ne:
        return "!=";
    case BinaryOp::Le:
        return "<=";
    case BinaryOp::Ge:
        return ">=";
    case BinaryOp::Lt:
        return "<";
    case BinaryOp::Gt:
        return ">";
    case BinaryOp::LogicalOr:
        return "||";
    case BinaryOp::LogicalAnd:
        return "&&";
    }
    return "?";
}

std::string unaryOpToString(UnaryOp op)
{
    switch (op)
    {
    case UnaryOp::Plus:
        return "+";
    case UnaryOp::Minus:
        return "-";
    case UnaryOp::LogicalNot:
        return "!";
    }
    return "?";
}

std::string directiveToString(DirectiveKind kind)
{
    switch (kind)
    {
    case DirectiveKind::Union:
        return "union";
    case DirectiveKind::Extent:
        return "extent";
    case DirectiveKind::Sealed:
        return "sealed";
    case DirectiveKind::Deprecated:
        return "deprecated";
    case DirectiveKind::Assert:
        return "assert";
    case DirectiveKind::Print:
        return "print";
    case DirectiveKind::Unknown:
        return "unknown";
    }
    return "unknown";
}

}  // namespace

bool AttachedDoc::empty() const
{
    return lines.empty();
}

std::string AttachedDoc::str() const
{
    std::ostringstream out;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        if (i > 0)
        {
            out << '\n';
        }
        out << lines[i].text;
    }
    return out.str();
}

bool TypeExprAST::isVoid() const
{
    return std::holds_alternative<VoidTypeExprAST>(scalar);
}

std::string TypeExprAST::str() const
{
    std::ostringstream out;
    std::visit(
        [&](const auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, PrimitiveTypeExprAST>)
            {
                const bool showCast = node.kind != PrimitiveKind::Bool && node.kind != PrimitiveKind::Byte &&
                                      node.kind != PrimitiveKind::Utf8;
                if (showCast)
                {
                    out << (node.castMode == CastMode::Truncated ? "truncated " : "saturated ");
                }
                switch (node.kind)
                {
                case PrimitiveKind::Bool:
                    out << "bool";
                    break;
                case PrimitiveKind::Byte:
                    out << "byte";
                    break;
                case PrimitiveKind::Utf8:
                    out << "utf8";
                    break;
                case PrimitiveKind::UnsignedInt:
                    out << "uint" << node.bitLength;
                    break;
                case PrimitiveKind::SignedInt:
                    out << "int" << node.bitLength;
                    break;
                case PrimitiveKind::Float:
                    out << "float" << node.bitLength;
                    break;
                }
            }
            else if constexpr (std::is_same_v<T, VoidTypeExprAST>)
            {
                out << "void" << node.bitLength;
            }
            else if constexpr (std::is_same_v<T, VersionedTypeExprAST>)
            {
                for (std::size_t i = 0; i < node.nameComponents.size(); ++i)
                {
                    if (i > 0)
                    {
                        out << '.';
                    }
                    out << node.nameComponents[i];
                }
                out << '.' << node.major << '.' << node.minor;
            }
        },
        scalar);

    if (arrayKind != ArrayKind::None)
    {
        out << '[';
        if (arrayKind == ArrayKind::VariableExclusive)
        {
            out << '<';
        }
        else if (arrayKind == ArrayKind::VariableInclusive)
        {
            out << "<=";
        }
        out << (arrayCapacity ? arrayCapacity->str() : "?") << ']';
    }

    return out.str();
}

std::string ExprAST::str() const
{
    std::ostringstream out;
    std::visit(
        [&](const auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, bool>)
            {
                out << (node ? "true" : "false");
            }
            else if constexpr (std::is_same_v<T, Rational>)
            {
                out << node.str();
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                out << '\'' << node << '\'';
            }
            else if constexpr (std::is_same_v<T, Identifier>)
            {
                out << node.name;
            }
            else if constexpr (std::is_same_v<T, Unary>)
            {
                out << '(' << unaryOpToString(node.op) << (node.operand ? node.operand->str() : "?") << ')';
            }
            else if constexpr (std::is_same_v<T, Binary>)
            {
                out << '(' << (node.lhs ? node.lhs->str() : "?") << ' ' << binaryOpToString(node.op) << ' '
                    << (node.rhs ? node.rhs->str() : "?") << ')';
            }
            else if constexpr (std::is_same_v<T, SetLiteral>)
            {
                out << '{';
                for (std::size_t i = 0; i < node.elements.size(); ++i)
                {
                    if (i > 0)
                    {
                        out << ", ";
                    }
                    out << (node.elements[i] ? node.elements[i]->str() : "?");
                }
                out << '}';
            }
            else if constexpr (std::is_same_v<T, TypeLiteral>)
            {
                out << node.type.str();
            }
        },
        value);
    return out.str();
}

bool DefinitionAST::isService() const
{
    return std::ranges::any_of(statements,
                               [](const auto& stmt) { return std::holds_alternative<ServiceResponseMarkerAST>(stmt); });
}

std::string printAST(const ASTModule& module)
{
    std::ostringstream out;
    const auto         emitDoc = [&](const AttachedDoc& doc, const int indent) {
        for (const auto& line : doc.lines)
        {
            out << std::string(static_cast<std::size_t>(indent) * 2U, ' ') << "#";
            if (!line.text.empty())
            {
                out << ' ' << line.text;
            }
            out << "\n";
        }
    };

    out << "module {\n";
    for (const auto& def : module.definitions)
    {
        emitDoc(def.ast.doc, 1);
        out << "  definition \"" << def.info.fullName << '.' << def.info.majorVersion << '.' << def.info.minorVersion
            << "\"";
        if (def.ast.isService())
        {
            out << " service";
        }
        out << " {\n";

        for (const auto& stmt : def.ast.statements)
        {
            std::visit(
                [&](const auto& s) {
                    using T = std::decay_t<decltype(s)>;
                    if constexpr (std::is_same_v<T, FieldDeclAST>)
                    {
                        emitDoc(s.doc, 2);
                        out << "    field " << s.type.str();
                        if (!s.isPadding)
                        {
                            out << ' ' << s.name;
                        }
                        out << "\n";
                    }
                    else if constexpr (std::is_same_v<T, ConstantDeclAST>)
                    {
                        emitDoc(s.doc, 2);
                        out << "    const " << s.type.str() << ' ' << s.name << " = "
                            << (s.value ? s.value->str() : "?") << "\n";
                    }
                    else if constexpr (std::is_same_v<T, DirectiveAST>)
                    {
                        emitDoc(s.doc, 2);
                        out << "    @" << directiveToString(s.kind);
                        if (s.expression)
                        {
                            out << ' ' << s.expression->str();
                        }
                        out << "\n";
                    }
                    else if constexpr (std::is_same_v<T, ServiceResponseMarkerAST>)
                    {
                        emitDoc(s.doc, 2);
                        out << "    ---\n";
                    }
                },
                stmt);
        }

        out << "  }\n";
    }
    out << "}\n";
    return out.str();
}

}  // namespace llvmdsdl
