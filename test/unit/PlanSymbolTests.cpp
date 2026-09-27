//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <optional>
#include <string>

#include "llvmdsdl/Support/PlanSymbol.h"

#include "UnitTests.h"

namespace
{

using llvmdsdl::parsePlanSymbol;
using llvmdsdl::parseSchemaSymbol;
using llvmdsdl::PlanFunction;
using llvmdsdl::PlanHelperDirection;
using llvmdsdl::PlanSymbol;
using llvmdsdl::renderPlanSymbol;
using llvmdsdl::renderSchemaSymbol;
using llvmdsdl::SchemaSymbol;

struct TestContext final
{
    bool ok{true};

    void expect(const bool condition, const std::string& what)
    {
        if (!condition)
        {
            std::cerr << "PlanSymbol test failed: " << what << '\n';
            ok = false;
        }
    }
};

bool sameSymbol(const PlanSymbol& a, const PlanSymbol& b)
{
    return (a.schema.fullName == b.schema.fullName) && (a.schema.major == b.schema.major) &&
           (a.schema.minor == b.schema.minor) && (a.section == b.section) && (a.function == b.function) &&
           (a.member == b.member) && (a.helperKind == b.helperKind) && (a.step == b.step) &&
           (a.direction == b.direction);
}

PlanSymbol function(const std::string&  fullName,
                    const std::string&  section,
                    const PlanFunction  what,
                    const std::string&  member = {},
                    const std::string&  kind   = {},
                    std::optional<long> step   = std::nullopt,
                    PlanHelperDirection way    = PlanHelperDirection::None)
{
    PlanSymbol symbol;
    symbol.schema     = SchemaSymbol{fullName, 1, 0};
    symbol.section    = section;
    symbol.function   = what;
    symbol.member     = member;
    symbol.helperKind = kind;
    symbol.step       = step;
    symbol.direction  = way;
    return symbol;
}

}  // namespace

bool runPlanSymbolTests()
{
    TestContext t;

    // The spellings the IR carries.
    t.expect(renderSchemaSymbol(SchemaSymbol{"ns.A_B", 1, 0}) == "ns.A_B.1.0", "schema symbol");
    t.expect(renderPlanSymbol(function("ns.Msg", "", PlanFunction::Serialize)) == "ns.Msg.1.0.serialize",
             "message body");
    t.expect(renderPlanSymbol(function("ns.Svc", "request", PlanFunction::Deserialize)) ==
                 "ns.Svc.1.0.request.deserialize",
             "service section body");
    t.expect(renderPlanSymbol(function("ns.Msg", "", PlanFunction::Get, "speed")) == "ns.Msg.1.0.get.speed",
             "accessor");
    t.expect(renderPlanSymbol(function("ns.Msg",
                                       "",
                                       PlanFunction::Helper,
                                       {},
                                       "scalar_unsigned",
                                       2,
                                       PlanHelperDirection::Serialize)) == "ns.Msg.1.0.plan.scalar_unsigned.2.ser",
             "helper with a step and a direction");
    t.expect(renderPlanSymbol(function("ns.Svc", "response", PlanFunction::Helper, {}, "capacity_check")) ==
                 "ns.Svc.1.0.response.plan.capacity_check",
             "helper of neither");

    // Every shape reads back as it was written, including names that are the grammar's own words.
    const PlanSymbol shapes[] = {
        function("ns.Msg", "", PlanFunction::Serialize),
        function("ns.Msg", "", PlanFunction::Initialize),
        function("ns.Svc", "request", PlanFunction::Serialize),
        function("ns.Msg", "", PlanFunction::Set, "_tag_"),
        function("ns.plan.get.request", "", PlanFunction::Get, "serialize"),
        function("ns.Msg",
                 "response",
                 PlanFunction::Helper,
                 {},
                 "union_tag",
                 std::nullopt,
                 PlanHelperDirection::Deserialize),
        function("ns.Msg", "", PlanFunction::Helper, {}, "validate_array_length", 7),
    };
    for (const PlanSymbol& shape : shapes)
    {
        const std::string rendered = renderPlanSymbol(shape);
        const auto        parsed   = parsePlanSymbol(rendered);
        t.expect(parsed && sameSymbol(*parsed, shape), "round trip of " + rendered);
    }

    // The pair a symbol spelled for C folded into one.
    t.expect(renderSchemaSymbol(SchemaSymbol{"ns.A_B", 1, 0}) != renderSchemaSymbol(SchemaSymbol{"ns.A.B", 1, 0}),
             "ns.A_B and ns.A.B are two symbols");
    const auto aB = parseSchemaSymbol("ns.A.B.1.0");
    t.expect(aB && (aB->fullName == "ns.A.B") && (aB->major == 1) && (aB->minor == 0), "ns.A.B.1.0 reads back");

    // What names nothing the grammar writes.
    t.expect(!parsePlanSymbol("ns.Msg.1.0"), "a schema symbol is no function");
    t.expect(!parseSchemaSymbol("ns.Msg.1.0.serialize"), "a function symbol is no schema");
    t.expect(!parsePlanSymbol("dsdl_runtime_get_u8"), "a runtime function is no plan function");
    t.expect(!parsePlanSymbol("Msg.1.0.serialize"), "a full name has a namespace");
    t.expect(!parsePlanSymbol("ns.Msg.1.0.get"), "an accessor names its member");
    t.expect(!parsePlanSymbol("ns.Msg.1.0.plan.kind.1.sideways"), "a helper's direction is ser or deser");

    return t.ok;
}
