//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "llvmdsdl/Frontend/Lexer.h"
#include "llvmdsdl/Frontend/Parser.h"
#include "llvmdsdl/Semantics/Evaluator.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Support/Rational.h"

#include "UnitTests.h"

namespace llvmdsdl
{
struct SourceLocation;
}  // namespace llvmdsdl

namespace
{

std::shared_ptr<llvmdsdl::ExprAST> parseAssertExpression(const std::string&          expression,
                                                         llvmdsdl::DiagnosticEngine& diag)
{
    const std::string text = "@assert " + expression + "\n@sealed\n";
    llvmdsdl::Lexer   lexer("evaluator_test.dsdl", text);
    auto              tokens = lexer.lex();
    llvmdsdl::Parser  parser("evaluator_test.dsdl", std::move(tokens), diag);
    auto              def = parser.parseDefinition();
    if (!def || def->statements.empty())
    {
        return nullptr;
    }
    const auto* directive = std::get_if<llvmdsdl::DirectiveAST>(def->statements.data());
    if (!directive)
    {
        return nullptr;
    }
    return directive->expression;
}

bool hasErrorContaining(const llvmdsdl::DiagnosticEngine& diag, std::string_view needle)
{
    for (const auto& d : diag.diagnostics())
    {
        if (d.level == llvmdsdl::DiagnosticLevel::Error && d.message.contains(needle))
        {
            return true;
        }
    }
    return false;
}

std::optional<llvmdsdl::Value> evaluateAssertExpression(const std::string&                     expression,
                                                        llvmdsdl::DiagnosticEngine&            diag,
                                                        const llvmdsdl::ValueEnv&              env      = {},
                                                        const llvmdsdl::TypeAttributeResolver* resolver = nullptr)
{
    auto expr = parseAssertExpression(expression, diag);
    if (!expr)
    {
        return std::nullopt;
    }
    return llvmdsdl::evaluateExpression(*expr, env, diag, expr->location, resolver);
}

bool expectRational(const std::optional<llvmdsdl::Value>& value, const llvmdsdl::Rational& expected)
{
    return value.has_value() && std::holds_alternative<llvmdsdl::Rational>(value->data) &&
           std::get<llvmdsdl::Rational>(value->data) == expected;
}

bool expectBool(const std::optional<llvmdsdl::Value>& value, const bool expected)
{
    return value.has_value() && std::holds_alternative<bool>(value->data) && std::get<bool>(value->data) == expected;
}

bool expectString(const std::optional<llvmdsdl::Value>& value, const std::string& expected)
{
    return value.has_value() && std::holds_alternative<std::string>(value->data) &&
           std::get<std::string>(value->data) == expected;
}

bool expectSet(const std::optional<llvmdsdl::Value>& value, const std::set<llvmdsdl::Rational>& expected)
{
    return value.has_value() && std::holds_alternative<llvmdsdl::Value::Set>(value->data) &&
           std::get<llvmdsdl::Value::Set>(value->data) == expected;
}

}  // namespace

bool runEvaluatorTests()
{
    {
        llvmdsdl::DiagnosticEngine diag;
        llvmdsdl::ValueEnv const   env;
        auto                       value = evaluateAssertExpression("Foo.1.0.MAX", diag, env, nullptr);
        if (value)
        {
            std::cerr << "expected evaluation without resolver to fail\n";
            return false;
        }
        if (!hasErrorContaining(diag, "unsupported metaserializable attribute: MAX"))
        {
            std::cerr << "missing unsupported-attribute diagnostic\n";
            return false;
        }
    }

    {
        llvmdsdl::DiagnosticEngine            diag;
        bool                                  resolverCalled = false;
        llvmdsdl::TypeAttributeResolver const resolver =
            [&](const llvmdsdl::TypeExprAST& type,
                const std::string&           attribute,
                const llvmdsdl::SourceLocation&) -> std::optional<llvmdsdl::Value> {
            resolverCalled        = true;
            const auto* versioned = std::get_if<llvmdsdl::VersionedTypeExprAST>(&type.scalar);
            if (!versioned || attribute != "MAX")
            {
                return std::nullopt;
            }
            return llvmdsdl::Value{llvmdsdl::Rational(42, 1)};
        };

        llvmdsdl::ValueEnv const env;
        auto                     value = evaluateAssertExpression("Foo.1.0.MAX", diag, env, &resolver);
        if (!expectRational(value, llvmdsdl::Rational(42, 1)))
        {
            std::cerr << "resolver-based evaluation produced unexpected result\n";
            return false;
        }
        if (!resolverCalled)
        {
            std::cerr << "resolver was not invoked for type attribute\n";
            return false;
        }
    }

    {
        llvmdsdl::DiagnosticEngine            diag;
        bool                                  resolverCalled = false;
        llvmdsdl::TypeAttributeResolver const resolver =
            [&](const llvmdsdl::TypeExprAST&, const std::string&, const llvmdsdl::SourceLocation&) {
                resolverCalled = true;
                return std::optional<llvmdsdl::Value>{};
            };

        llvmdsdl::ValueEnv const env;
        auto                     value = evaluateAssertExpression("Foo.1.0.MAX", diag, env, &resolver);
        if (value)
        {
            std::cerr << "expected resolver nullopt response to fail evaluation\n";
            return false;
        }
        if (!resolverCalled)
        {
            std::cerr << "resolver was not invoked for nullopt fallback path\n";
            return false;
        }
        if (!hasErrorContaining(diag, "failed to evaluate expression"))
        {
            std::cerr << "expected fallback evaluation failure diagnostic\n";
            return false;
        }
    }

    {
        // Symbolic/concrete subset relations use closed-form cardinality and membership. This
        // set is larger than the evaluator's ordinary exact materialisation capacity.
        constexpr std::int64_t     countMax = 70000;
        llvmdsdl::DiagnosticEngine diag;
        llvmdsdl::ValueEnv         env;
        llvmdsdl::Value::Set       exactOffsets;
        for (std::int64_t i = 0; i <= countMax; ++i)
        {
            exactOffsets.emplace(i * 8, 1);
        }
        env.insert_or_assign("_offset_", llvmdsdl::Value{llvmdsdl::BitLengthSet(8).repeatRange(countMax)});
        env.insert_or_assign("exact_offsets", llvmdsdl::Value{std::move(exactOffsets)});

        const auto subset = evaluateAssertExpression("_offset_ <= exact_offsets", diag, env, nullptr);
        if (!expectBool(subset, true) || diag.hasErrors())
        {
            std::cerr << "symbolic set subset of a large concrete set was not decided exactly\n";
            return false;
        }

        const auto superset = evaluateAssertExpression("exact_offsets >= _offset_", diag, env, nullptr);
        if (!expectBool(superset, true) || diag.hasErrors())
        {
            std::cerr << "large concrete set superset relation was not decided exactly\n";
            return false;
        }
    }

    {
        // A negative divisor whose magnitude is outside positive int64 bypasses the symbolic
        // residue path and is evaluated exactly after materialising the small offset set.
        llvmdsdl::DiagnosticEngine diag;
        llvmdsdl::ValueEnv         env;
        env.insert_or_assign("offsets", llvmdsdl::Value{llvmdsdl::BitLengthSet(std::set<std::int64_t>{0, 8, 16})});
        env.insert_or_assign("divisor", llvmdsdl::Value{llvmdsdl::Rational(INT64_MIN, 1)});
        const auto residues = evaluateAssertExpression("offsets % divisor", diag, env, nullptr);
        if (!expectSet(residues, {llvmdsdl::Rational(0, 1), llvmdsdl::Rational(8, 1), llvmdsdl::Rational(16, 1)}) ||
            diag.hasErrors())
        {
            std::cerr << "wide negative symbolic modulo divisor was not evaluated exactly\n";
            return false;
        }
    }

    {
        llvmdsdl::DiagnosticEngine diag;
        llvmdsdl::ValueEnv         env;
        const auto                 values = std::set<std::int64_t>{2, 5, 12};
        env.insert_or_assign("lhs", llvmdsdl::Value{llvmdsdl::BitLengthSet(values).repeat(4097)});
        env.insert_or_assign("rhs", llvmdsdl::Value{llvmdsdl::BitLengthSet(values).repeat(4097)});

        auto selfEqual = evaluateAssertExpression("lhs == lhs", diag, env, nullptr);
        if (!expectBool(selfEqual, true))
        {
            std::cerr << "symbolic self-equality beyond operation budgets was not decided exactly\n";
            return false;
        }

        auto undecidable = evaluateAssertExpression("lhs == rhs", diag, env, nullptr);
        if (undecidable || !hasErrorContaining(diag, "cannot decide this '_offset_' set comparison exactly"))
        {
            std::cerr << "undecidable symbolic equality did not produce an exactness diagnostic\n";
            return false;
        }
    }

    {
        llvmdsdl::DiagnosticEngine diag;
        llvmdsdl::ValueEnv         env;
        const auto                 maximum = std::numeric_limits<std::int64_t>::max();
        env.insert_or_assign("saturated",
                             llvmdsdl::Value{llvmdsdl::BitLengthSet(maximum) + llvmdsdl::BitLengthSet(maximum - 1)});
        auto residues = evaluateAssertExpression("saturated % 8", diag, env, nullptr);
        if (!expectSet(residues, {llvmdsdl::Rational(7, 1)}) || diag.hasErrors())
        {
            std::cerr << "saturated symbolic modulo did not use an exact fallback\n";
            return false;
        }
    }

    {
        llvmdsdl::DiagnosticEngine diag;
        llvmdsdl::ValueEnv const   env;
        auto                       value = evaluateAssertExpression("Foo.1.0._extent_", diag, env, nullptr);
        if (!expectRational(value, llvmdsdl::Rational(0, 1)))
        {
            std::cerr << "default _extent_ evaluator path produced unexpected result\n";
            return false;
        }
    }

    {
        llvmdsdl::DiagnosticEngine diag;

        llvmdsdl::TypeAttributeResolver const resolver =
            [&](const llvmdsdl::TypeExprAST&,
                const std::string& attribute,
                const llvmdsdl::SourceLocation&) -> std::optional<llvmdsdl::Value> {
            if (attribute == "_extent_")
            {
                return llvmdsdl::Value{llvmdsdl::Rational(128, 1)};
            }
            return std::nullopt;
        };

        llvmdsdl::ValueEnv const env;
        auto                     value = evaluateAssertExpression("Foo.1.0._extent_", diag, env, &resolver);
        if (!expectRational(value, llvmdsdl::Rational(128, 1)))
        {
            std::cerr << "_extent_ resolver evaluation produced unexpected result\n";
            return false;
        }
    }

    {
        llvmdsdl::DiagnosticEngine diag;
        llvmdsdl::ValueEnv const   env;
        auto                       product = evaluateAssertExpression("2 + 3 * 4", diag, env, nullptr);
        if (!expectRational(product, llvmdsdl::Rational(14, 1)))
        {
            std::cerr << "arithmetic precedence evaluation produced unexpected result\n";
            return false;
        }
        auto quotient = evaluateAssertExpression("5 / 2", diag, env, nullptr);
        if (!expectRational(quotient, llvmdsdl::Rational(5, 2)))
        {
            std::cerr << "rational division evaluation produced unexpected result\n";
            return false;
        }
        auto modulus = evaluateAssertExpression("7 % 4", diag, env, nullptr);
        if (!expectRational(modulus, llvmdsdl::Rational(3, 1)))
        {
            std::cerr << "modulus evaluation produced unexpected result\n";
            return false;
        }
        auto power = evaluateAssertExpression("2 ** 3", diag, env, nullptr);
        if (!expectRational(power, llvmdsdl::Rational(8, 1)))
        {
            std::cerr << "integer power evaluation produced unexpected result\n";
            return false;
        }

        auto invalidPower = evaluateAssertExpression("2 ** -1", diag, env, nullptr);
        if (invalidPower || !hasErrorContaining(diag, "invalid rational operation"))
        {
            std::cerr << "expected invalid rational operation for negative exponent\n";
            return false;
        }
    }

    {
        llvmdsdl::DiagnosticEngine diag;
        llvmdsdl::ValueEnv const   env;
        auto                       conjunction = evaluateAssertExpression("true && false", diag, env, nullptr);
        if (!expectBool(conjunction, false))
        {
            std::cerr << "logical and evaluation produced unexpected result\n";
            return false;
        }
        auto disjunction = evaluateAssertExpression("true || false", diag, env, nullptr);
        if (!expectBool(disjunction, true))
        {
            std::cerr << "logical or evaluation produced unexpected result\n";
            return false;
        }
        auto inequality = evaluateAssertExpression("true != false", diag, env, nullptr);
        if (!expectBool(inequality, true))
        {
            std::cerr << "boolean inequality evaluation produced unexpected result\n";
            return false;
        }
        auto logicalNot = evaluateAssertExpression("!false", diag, env, nullptr);
        if (!expectBool(logicalNot, true))
        {
            std::cerr << "logical not evaluation produced unexpected result\n";
            return false;
        }

        auto invalidNot = evaluateAssertExpression("!1", diag, env, nullptr);
        if (invalidNot || !hasErrorContaining(diag, "logical not requires boolean operand"))
        {
            std::cerr << "expected logical-not operand diagnostic\n";
            return false;
        }
    }

    {
        llvmdsdl::DiagnosticEngine diag;
        llvmdsdl::ValueEnv const   env;
        auto                       concat = evaluateAssertExpression(R"("ab" + "cd")", diag, env, nullptr);
        if (!expectString(concat, "abcd"))
        {
            std::cerr << "string concatenation produced unexpected result\n";
            return false;
        }
        auto equal = evaluateAssertExpression(R"("ab" == "ab")", diag, env, nullptr);
        if (!expectBool(equal, true))
        {
            std::cerr << "string equality produced unexpected result\n";
            return false;
        }

        auto mismatch = evaluateAssertExpression("\"ab\" + 1", diag, env, nullptr);
        if (mismatch || !hasErrorContaining(diag, "unsupported operand types: string and rational"))
        {
            std::cerr << "expected unsupported operand type diagnostic for string+rational\n";
            return false;
        }
    }

    {
        llvmdsdl::DiagnosticEngine diag;
        llvmdsdl::ValueEnv const   env;
        auto                       setUnion = evaluateAssertExpression("{1, 2} | {2, 3}", diag, env, nullptr);
        if (!expectSet(setUnion, {llvmdsdl::Rational(1, 1), llvmdsdl::Rational(2, 1), llvmdsdl::Rational(3, 1)}))
        {
            std::cerr << "set union evaluation produced unexpected result\n";
            return false;
        }
        auto setIntersection = evaluateAssertExpression("{1, 2} & {2, 3}", diag, env, nullptr);
        if (!expectSet(setIntersection, {llvmdsdl::Rational(2, 1)}))
        {
            std::cerr << "set intersection evaluation produced unexpected result\n";
            return false;
        }
        auto setSymDiff = evaluateAssertExpression("{1, 2} ^ {2, 3}", diag, env, nullptr);
        if (!expectSet(setSymDiff, {llvmdsdl::Rational(1, 1), llvmdsdl::Rational(3, 1)}))
        {
            std::cerr << "set symmetric difference evaluation produced unexpected result\n";
            return false;
        }
        auto setAdd = evaluateAssertExpression("{1, 2} + {10}", diag, env, nullptr);
        if (!expectSet(setAdd, {llvmdsdl::Rational(11, 1), llvmdsdl::Rational(12, 1)}))
        {
            std::cerr << "set elementwise add evaluation produced unexpected result\n";
            return false;
        }
        auto setPlusScalar = evaluateAssertExpression("{1, 2} + 3", diag, env, nullptr);
        if (!expectSet(setPlusScalar, {llvmdsdl::Rational(4, 1), llvmdsdl::Rational(5, 1)}))
        {
            std::cerr << "set plus scalar evaluation produced unexpected result\n";
            return false;
        }
        auto scalarPlusSet = evaluateAssertExpression("3 + {1, 2}", diag, env, nullptr);
        if (!expectSet(scalarPlusSet, {llvmdsdl::Rational(4, 1), llvmdsdl::Rational(5, 1)}))
        {
            std::cerr << "scalar plus set evaluation produced unexpected result\n";
            return false;
        }
        auto strictSubset = evaluateAssertExpression("{1, 2} < {1, 2, 3}", diag, env, nullptr);
        if (!expectBool(strictSubset, true))
        {
            std::cerr << "strict subset relation evaluation produced unexpected result\n";
            return false;
        }
        auto strictSuperset = evaluateAssertExpression("{1, 2, 3} > {1, 2}", diag, env, nullptr);
        if (!expectBool(strictSuperset, true))
        {
            std::cerr << "strict superset relation evaluation produced unexpected result\n";
            return false;
        }

        auto invalidSetDivide = evaluateAssertExpression("{1} / {0}", diag, env, nullptr);
        if (invalidSetDivide || !hasErrorContaining(diag, "invalid elementwise set operation"))
        {
            std::cerr << "expected invalid elementwise set operation diagnostic\n";
            return false;
        }
    }

    {
        llvmdsdl::DiagnosticEngine diag;
        llvmdsdl::ValueEnv const   env;
        auto                       setCount = evaluateAssertExpression("{1, 4, 2}.count", diag, env, nullptr);
        if (!expectRational(setCount, llvmdsdl::Rational(3, 1)))
        {
            std::cerr << "set .count attribute produced unexpected result\n";
            return false;
        }
        auto setMin = evaluateAssertExpression("{1, 4, 2}.min", diag, env, nullptr);
        if (!expectRational(setMin, llvmdsdl::Rational(1, 1)))
        {
            std::cerr << "set .min attribute produced unexpected result\n";
            return false;
        }
        auto setMax = evaluateAssertExpression("{1, 4, 2}.max", diag, env, nullptr);
        if (!expectRational(setMax, llvmdsdl::Rational(4, 1)))
        {
            std::cerr << "set .max attribute produced unexpected result\n";
            return false;
        }

        auto emptySetMin = evaluateAssertExpression("{}.min", diag, env, nullptr);
        if (emptySetMin || !hasErrorContaining(diag, "cannot access set min/max on an empty set literal"))
        {
            std::cerr << "expected empty-set min/max diagnostic\n";
            return false;
        }
        auto unknownSetAttribute = evaluateAssertExpression("{1}.unknown", diag, env, nullptr);
        if (unknownSetAttribute ||
            (!hasErrorContaining(diag, "attribute operator is not defined on set<rational>") && !diag.hasErrors()))
        {
            std::cerr << "expected unknown set attribute diagnostic\n";
            return false;
        }
        const llvmdsdl::SourceLocation loc{"evaluator_attr_test.dsdl", 1, 1};

        auto lhsBool      = std::make_shared<llvmdsdl::ExprAST>();
        lhsBool->location = loc;
        lhsBool->value    = true;

        auto rhsIdentifier      = std::make_shared<llvmdsdl::ExprAST>();
        rhsIdentifier->location = loc;
        rhsIdentifier->value    = llvmdsdl::ExprAST::Identifier{"count"};

        llvmdsdl::ExprAST wrongTargetExpr;
        wrongTargetExpr.location = loc;
        wrongTargetExpr.value    = llvmdsdl::ExprAST::Binary{llvmdsdl::BinaryOp::Attribute, lhsBool, rhsIdentifier};

        auto wrongAttributeTarget = llvmdsdl::evaluateExpression(wrongTargetExpr, env, diag, loc, nullptr);
        if (wrongAttributeTarget || !hasErrorContaining(diag, "attribute operator is not defined on bool"))
        {
            std::cerr << "expected unsupported attribute target diagnostic\n";
            return false;
        }

        auto rhsRational      = std::make_shared<llvmdsdl::ExprAST>();
        rhsRational->location = loc;
        rhsRational->value    = llvmdsdl::Rational(1, 1);

        llvmdsdl::ExprAST wrongRhsExpr;
        wrongRhsExpr.location = loc;
        wrongRhsExpr.value    = llvmdsdl::ExprAST::Binary{llvmdsdl::BinaryOp::Attribute, lhsBool, rhsRational};

        auto wrongAttributeRhs = llvmdsdl::evaluateExpression(wrongRhsExpr, env, diag, loc, nullptr);
        if (wrongAttributeRhs || !hasErrorContaining(diag, "attribute operator expects identifier on RHS"))
        {
            std::cerr << "expected attribute RHS identifier diagnostic\n";
            return false;
        }
    }

    {
        llvmdsdl::DiagnosticEngine diag;
        llvmdsdl::ValueEnv         env;
        env.insert_or_assign("answer", llvmdsdl::Value{llvmdsdl::Rational(41, 1)});

        auto withEnv = evaluateAssertExpression("answer + 1", diag, env, nullptr);
        if (!expectRational(withEnv, llvmdsdl::Rational(42, 1)))
        {
            std::cerr << "identifier lookup from environment produced unexpected result\n";
            return false;
        }

        auto undefinedIdentifier = evaluateAssertExpression("missing_symbol + 1", diag, env, nullptr);
        if (undefinedIdentifier || !hasErrorContaining(diag, "undefined identifier: missing_symbol"))
        {
            std::cerr << "expected undefined identifier diagnostic\n";
            return false;
        }
    }

    {
        llvmdsdl::DiagnosticEngine diag;
        llvmdsdl::ValueEnv         env;
        env.insert_or_assign("_offset_", llvmdsdl::Value{llvmdsdl::BitLengthSet(8).repeatRange(20000)});
        env.insert_or_assign("narrow_offset", llvmdsdl::Value{llvmdsdl::BitLengthSet(8).repeatRange(10000)});

        auto equal = evaluateAssertExpression("_offset_ == _offset_", diag, env, nullptr);
        if (!expectBool(equal, true))
        {
            std::cerr << "symbolic set equality materialized instead of using RunSet equality\n";
            return false;
        }

        auto notEqual = evaluateAssertExpression("_offset_ != _offset_", diag, env, nullptr);
        if (!expectBool(notEqual, false))
        {
            std::cerr << "symbolic set inequality produced unexpected result\n";
            return false;
        }

        auto subset = evaluateAssertExpression("narrow_offset <= _offset_", diag, env, nullptr);
        if (!expectBool(subset, true))
        {
            std::cerr << "symbolic set subset relation produced unexpected result\n";
            return false;
        }

        auto strictSubset = evaluateAssertExpression("narrow_offset < _offset_", diag, env, nullptr);
        if (!expectBool(strictSubset, true))
        {
            std::cerr << "symbolic set strict subset relation produced unexpected result\n";
            return false;
        }

        auto notSubset = evaluateAssertExpression("_offset_ <= narrow_offset", diag, env, nullptr);
        if (!expectBool(notSubset, false))
        {
            std::cerr << "symbolic set negative subset relation produced unexpected result\n";
            return false;
        }

        if (diag.hasErrors())
        {
            std::cerr << "symbolic set comparisons diagnosed a materialization failure\n";
            return false;
        }
    }

    {
        llvmdsdl::DiagnosticEngine diag;
        llvmdsdl::ValueEnv const   env;

        auto unaryTypeError = evaluateAssertExpression("-true", diag, env, nullptr);
        if (unaryTypeError || !hasErrorContaining(diag, "unary +/- requires rational operand"))
        {
            std::cerr << "expected unary rational operand diagnostic\n";
            return false;
        }

        const llvmdsdl::SourceLocation setLoc{"evaluator_set_literal_test.dsdl", 1, 1};
        auto                           rationalElem = std::make_shared<llvmdsdl::ExprAST>();
        rationalElem->location                      = setLoc;
        rationalElem->value                         = llvmdsdl::Rational(1, 1);
        auto boolElem                               = std::make_shared<llvmdsdl::ExprAST>();
        boolElem->location                          = setLoc;
        boolElem->value                             = true;

        llvmdsdl::ExprAST invalidSetExpr;
        invalidSetExpr.location = setLoc;
        invalidSetExpr.value    = llvmdsdl::ExprAST::SetLiteral{{rationalElem, boolElem}};

        auto invalidSetLiteral = llvmdsdl::evaluateExpression(invalidSetExpr, env, diag, setLoc, nullptr);
        if (invalidSetLiteral || !hasErrorContaining(diag, "set literal elements must evaluate to rational"))
        {
            std::cerr << "expected set-literal element type diagnostic\n";
            return false;
        }
    }

    {
        llvmdsdl::DiagnosticEngine diag;
        llvmdsdl::ValueEnv const   env;
        auto                       boolValue     = evaluateAssertExpression("true", diag, env, nullptr);
        auto                       rationalValue = evaluateAssertExpression("5", diag, env, nullptr);
        auto                       stringValue   = evaluateAssertExpression("\"text\"", diag, env, nullptr);
        auto                       setValue      = evaluateAssertExpression("{2, 1}", diag, env, nullptr);
        auto                       typeValue     = evaluateAssertExpression("Foo.1.0", diag, env, nullptr);
        if (!boolValue || !rationalValue || !stringValue || !setValue || !typeValue)
        {
            std::cerr << "failed to evaluate values for string/type formatting coverage\n";
            return false;
        }

        if (boolValue->typeName() != "bool" || boolValue->str() != "true")
        {
            std::cerr << "bool value formatting/typeName mismatch\n";
            return false;
        }
        if (rationalValue->typeName() != "rational" || rationalValue->str() != "5")
        {
            std::cerr << "rational value formatting/typeName mismatch\n";
            return false;
        }
        if (stringValue->typeName() != "string" || stringValue->str() != "'text'")
        {
            std::cerr << "string value formatting/typeName mismatch\n";
            return false;
        }
        if (setValue->typeName() != "set<rational>" || setValue->str() != "{1, 2}")
        {
            std::cerr << "set value formatting/typeName mismatch\n";
            return false;
        }
        if (typeValue->typeName() != "metaserializable" || typeValue->str() != "Foo.1.0")
        {
            std::cerr << "type-literal value formatting/typeName mismatch\n";
            return false;
        }
    }

    // --- Overflow / DoS hardening (P1 #1) -------------------------------------------------------
    {
        // A product that leaves 128-bit range must yield a diagnostic, not undefined behaviour.
        // (`(2^64-1)^2` ~ 2^128 overflows the 128-bit constant value type; per-type range checking of
        // in-range values is the analyzer's job, not the evaluator's.)
        llvmdsdl::DiagnosticEngine diag;
        auto value = evaluateAssertExpression("18446744073709551615 * 18446744073709551615", diag);
        if (value || !hasErrorContaining(diag, "invalid rational operation"))
        {
            std::cerr << "overflowing multiply should fail with a diagnostic\n";
            return false;
        }
    }
    {
        // Exponentiation that overflows must bail (also bounding the loop) rather than run forever.
        llvmdsdl::DiagnosticEngine diag;
        auto                       value = evaluateAssertExpression("2 ** 1000000000000", diag);
        if (value || !hasErrorContaining(diag, "invalid rational operation"))
        {
            std::cerr << "overflowing exponentiation should fail with a diagnostic\n";
            return false;
        }
    }
    {
        // Trivial bases must be special-cased so a huge exponent cannot spin the loop (DoS). If this
        // were unbounded the test would hang rather than fail.
        llvmdsdl::DiagnosticEngine diag;
        auto                       one = evaluateAssertExpression("1 ** 1000000000000", diag);
        if (!expectRational(one, llvmdsdl::Rational(1, 1)))
        {
            std::cerr << "1 ** huge should evaluate to 1 without looping\n";
            return false;
        }
    }
    {
        // 128-bit storage: every value in [INT64_MIN, UINT64_MAX] is exact and unflagged; only a
        // genuine 128-bit overflow poisons.
        const llvmdsdl::Rational big = llvmdsdl::Rational(INT64_MAX, 1) * llvmdsdl::Rational(2, 1);
        if (big.overflowed() || big.asWideInteger().value() != static_cast<__int128>(INT64_MAX) * 2)
        {
            std::cerr << "INT64_MAX * 2 fits 128-bit and must be exact, not overflowed\n";
            return false;
        }
        // A full-width uint64 value is representable: exact via asWideInteger, absent via asInteger.
        const __int128           uint64Max = (static_cast<__int128>(1) << 64) - 1;
        const llvmdsdl::Rational umax(uint64Max, 1);
        if (umax.overflowed() || umax.asInteger().has_value() || umax.asWideInteger().value() != uint64Max)
        {
            std::cerr << "UINT64_MAX should be exact in 128-bit but not fit int64\n";
            return false;
        }
        // A real 128-bit overflow still poisons (bounds the DoS surface for constant expressions).
        const __int128           huge  = static_cast<__int128>(1) << 96;
        const llvmdsdl::Rational spill = llvmdsdl::Rational(huge, 1) * llvmdsdl::Rational(huge, 1);
        if (!spill.overflowed())
        {
            std::cerr << "2^96 * 2^96 should set the 128-bit overflow flag\n";
            return false;
        }
        if (!(llvmdsdl::Rational(INT64_MIN, 1) < llvmdsdl::Rational(INT64_MAX, 1)))
        {
            std::cerr << "INT64_MIN should compare less than INT64_MAX\n";
            return false;
        }
        if (llvmdsdl::Rational(INT64_MAX, 1) < llvmdsdl::Rational(INT64_MAX, 1))
        {
            std::cerr << "INT64_MAX should not compare less than itself\n";
            return false;
        }
        // INT64_MIN must be constructible/normalizable without UB (negation guard).
        const llvmdsdl::Rational neg = llvmdsdl::Rational(INT64_MIN, 1);
        if (neg.numerator() != INT64_MIN || neg.denominator() != 1)
        {
            std::cerr << "INT64_MIN/1 should normalize to itself\n";
            return false;
        }
    }

    return true;
}
