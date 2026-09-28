//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/PlanSymbol.h"
#include "llvmdsdl/Support/BodyNaming.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/SectionScopes.h"
#include "llvmdsdl/Support/SurfacePlan.h"

#include "UnitTests.h"

namespace
{

using llvmdsdl::DefinitionParts;
using llvmdsdl::FieldParts;
using llvmdsdl::Language;
using llvmdsdl::SectionParts;
using llvmdsdl::SurfacePlan;

FieldParts field(const std::string& name, const bool array = false, const std::uint32_t option = 0)
{
    return FieldParts{.name             = name,
                      .padding          = false,
                      .array            = array,
                      .unionOptionIndex = option,
                      .composite        = std::nullopt,
                      .view             = false};
}

llvmdsdl::DefinitionRef ref(const std::string& space, const std::string& shortName)
{
    return llvmdsdl::DefinitionRef{.namespaceComponents = {space},
                                   .shortName           = shortName,
                                   .majorVersion        = 1,
                                   .minorVersion        = 0};
}

/// @brief A field holding the definition @p held.
FieldParts holding(const std::string& name, llvmdsdl::DefinitionRef held, const bool view = false)
{
    FieldParts out = field(name);
    out.composite  = std::move(held);
    out.view       = view;
    return out;
}

DefinitionParts message(const std::string& shortName, SectionParts section, const std::string& space = "ns")
{
    return DefinitionParts{.ref         = ref(space, shortName),
                           .fixedPortId = std::nullopt,
                           .service     = false,
                           .deprecated  = false,
                           .request     = std::move(section),
                           .response    = std::nullopt,
                           .bodies      = {}};
}

SurfacePlan allocate(const Language                      language,
                     const std::vector<DefinitionParts>& definitions,
                     const std::string&                  packageName = "pkg")
{
    return llvmdsdl::allocateSurface(llvmdsdl::languageTraits(language),
                                     definitions,
                                     llvmdsdl::SurfaceOptions{.packageName = packageName});
}

const char* kindName(const llvmdsdl::SurfaceScopeKind kind)
{
    switch (kind)
    {
    case llvmdsdl::SurfaceScopeKind::Root:
        return "root";
    case llvmdsdl::SurfaceScopeKind::Namespace:
        return "namespace";
    case llvmdsdl::SurfaceScopeKind::Module:
        return "module";
    case llvmdsdl::SurfaceScopeKind::Package:
        return "package";
    case llvmdsdl::SurfaceScopeKind::File:
        return "file";
    case llvmdsdl::SurfaceScopeKind::Type:
        return "type";
    }
    return "?";
}

const char* className(const llvmdsdl::NameClass nameClass)
{
    switch (nameClass)
    {
    case llvmdsdl::NameClass::Value:
        return "value";
    case llvmdsdl::NameClass::Type:
        return "type";
    case llvmdsdl::NameClass::Module:
        return "module";
    case llvmdsdl::NameClass::Tag:
        return "tag";
    case llvmdsdl::NameClass::Macro:
        return "macro";
    }
    return "?";
}

/// @brief The plan as an indented outline: a scope's kind and name, and each declaration's name and
///        class.
void outline(const SurfacePlan& plan, const std::size_t scope, const std::string& indent, std::string& out)
{
    out += indent + kindName(plan.scopes[scope].kind) + " " + plan.scopes[scope].name + "\n";
    for (const llvmdsdl::SurfaceItem& item : plan.scopes[scope].items)
    {
        if (item.scope)
        {
            outline(plan, item.index, indent + "  ", out);
        }
        else
        {
            const llvmdsdl::SurfaceDecl& decl = plan.decls[item.index];
            out += indent + "  " + decl.name + " : " + className(decl.nameClass) +
                   ((decl.visibility == llvmdsdl::SurfaceVisibility::Private) ? " private" : "") + "\n";
        }
    }
}

std::string outline(const SurfacePlan& plan)
{
    std::string out;
    outline(plan, 0, "", out);
    return out;
}

/// @brief Each scope the plan places in a file, and the file.
std::string placements(const SurfacePlan& plan)
{
    std::string out;
    for (const llvmdsdl::SurfaceScope& scope : plan.scopes)
    {
        if (!scope.path.empty())
        {
            out += std::string(kindName(scope.kind)) + " " + scope.name + ": " + scope.path + "\n";
        }
    }
    return out;
}

/// @brief The imports the first definition's file makes: the local name, and what it names.
std::string imports(const SurfacePlan& plan)
{
    std::string out;
    for (const llvmdsdl::SurfaceItem& item : plan.scopes[plan.definitions.front().fileScope].items)
    {
        if (item.scope)
        {
            continue;
        }
        const llvmdsdl::SurfaceDecl& decl = plan.decls[item.index];
        if (decl.kind == llvmdsdl::SurfaceDeclKind::Import)
        {
            out += decl.name + " " + decl.of->schema + "\n";
        }
    }
    return out;
}

/// @brief The declared name of @p definition's constant @p constant, in its first section.
std::string constantName(const SurfacePlan& plan, const std::string& constant)
{
    const auto found = plan.definitions.front().sections.front().constants.find(constant);
    return (found == plan.definitions.front().sections.front().constants.end()) ? "<none>"
                                                                                : plan.decls[found->second].name;
}

std::string optionName(const SurfacePlan& plan, const std::string& option)
{
    const auto found = plan.definitions.front().sections.front().options.find(option);
    return (found == plan.definitions.front().sections.front().options.end()) ? "<none>"
                                                                              : plan.decls[found->second.decl].name;
}

/// @brief A lowered function of `ns.Msg.1.0`, from its symbol.
llvmdsdl::BodyParts body(const std::string& symbol, const bool unreferenced = false)
{
    return llvmdsdl::BodyParts{.symbol       = symbol,
                               .plan         = llvmdsdl::parsePlanSymbol(symbol).value_or(llvmdsdl::PlanSymbol{}),
                               .unreferenced = unreferenced};
}

bool expect(const std::string& got, const std::string& want, const std::string& what)
{
    if (got != want)
    {
        std::cerr << "surface plan: " << what << "\n--- got\n" << got << "\n--- want\n" << want << "\n";
        return false;
    }
    return true;
}

}  // namespace

bool runSurfacePlanTests()
{
    bool ok = true;

    const SectionParts limited{.fields = {field("value")}, .constants = {"LIMIT"}, .isUnion = false};

    // Each language opens the scopes its output does: C joins the namespace into each name and
    // declares a type's constants as macros beside it; C++ opens a namespace; Rust, TypeScript and
    // Python make each definition a module; Go makes the namespace a package its files share.
    ok = expect(outline(allocate(Language::C, {message("Msg", limited)})),
                "root pkg\n"
                "  file Msg_1_0\n"
                "    type ns__Msg\n"
                "      value : value\n"
                "    ns__Msg_LIMIT : macro\n",
                "C's plan") &&
         ok;
    ok = expect(outline(allocate(Language::Cpp, {message("Msg", limited)})),
                "root pkg\n"
                "  namespace ns\n"
                "    file Msg_1_0\n"
                "      type Msg\n"
                "        value : value\n"
                "        LIMIT : value\n",
                "C++'s plan") &&
         ok;
    ok = expect(outline(allocate(Language::Rust, {message("Msg", limited)})),
                "root pkg\n"
                "  namespace ns\n"
                "    module msg_1_0\n"
                "      type Msg\n"
                "        value : value\n"
                "        LIMIT : value\n",
                "Rust's plan") &&
         ok;
    ok = expect(outline(allocate(Language::Go, {message("Msg", limited)})),
                "root pkg\n"
                "  package ns\n"
                "    file msg_1_0\n"
                "      type Msg\n"
                "        Value : value\n"
                "      MsgLimit : value\n",
                "Go's plan") &&
         ok;
    ok = expect(outline(allocate(Language::TypeScript, {message("Msg", limited)})),
                "root pkg\n"
                "  namespace ns\n"
                "    module msg_1_0\n"
                "      type Msg\n"
                "        value : value\n"
                "      MSG_LIMIT : value\n",
                "TypeScript's plan") &&
         ok;

    // A namespace is opened once, by the first definition in it.
    {
        const SurfacePlan plan = allocate(Language::Rust, {message("A", SectionParts{}), message("B", SectionParts{})});
        ok                     = expect(outline(plan),
                                        "root pkg\n"
                                        "  namespace ns\n"
                                        "    module a_1_0\n"
                                        "      type A\n"
                                        "    module b_1_0\n"
                                        "      type B\n",
                                        "two definitions of one namespace") &&
                                 ok;
    }

    // A service's sections are each a type, which the index reports under the section's name.
    {
        DefinitionParts service = message("List", SectionParts{.fields = {field("index")}, .constants = {}});
        service.service         = true;
        service.response        = SectionParts{.fields = {field("path")}, .constants = {}};
        const SurfacePlan plan  = allocate(Language::Rust, {service});
        ok                      = expect(outline(plan),
                                         "root pkg\n"
                                         "  namespace ns\n"
                                         "    module list_1_0\n"
                                         "      type Request\n"
                                         "        index : value\n"
                                         "      type Response\n"
                                         "        path : value\n"
                                         "      List : type\n",
                                         "a service") &&
                                  ok;
        const llvmdsdl::DefinitionNames& names = plan.definitions.front();
        ok                                     = expect(names.key, "ns.List.1.0", "the definition's key") && ok;
        ok                     = expect(names.sections.back().section + " " + names.sections.back().typeName,
                                        "response Response",
                                        "the response section") &&
                                 ok;
        const std::size_t path = names.sections.back().fields.find("path")->second;
        ok =
            expect(plan.decls[path].of->schema + " " + plan.decls[path].of->section + " " + plan.decls[path].of->member,
                   "ns.List.1.0 response path",
                   "a field's entity") &&
            ok;
    }

    // A deprecated type is declared under a name of its own where the language does that, and its
    // public name is an alias of it.
    {
        DefinitionParts old = message("Msg", SectionParts{.fields = {field("value")}, .constants = {}});
        old.deprecated      = true;
        ok                  = expect(outline(allocate(Language::Rust, {old})),
                                     "root pkg\n"
                                     "  namespace ns\n"
                                     "    module msg_1_0\n"
                                     "      type Msg_\n"
                                     "        value : value\n"
                                     "      Msg : type\n",
                                     "a deprecated Rust type") &&
                              ok;
        ok                  = expect(outline(allocate(Language::C, {old})),
                                     "root pkg\n"
                                     "  file Msg_1_0\n"
                                     "    type ns__Msg\n"
                                     "      value : value\n",
                                     "a deprecated C type") &&
                              ok;
    }

    // Band 4, the lowered functions. A language that compiles the bodies apart links every one, a
    // helper nothing calls included; the others name each helper a body calls, in the pool the
    // language allocates helpers in.
    {
        DefinitionParts owner = message("Msg", SectionParts{.fields = {}, .constants = {}});
        owner.bodies          = {body("ns.Msg.1.0.plan.capacity_check"),
                                 body("ns.Msg.1.0.plan.scalar_unsigned.0.ser"),
                                 body("ns.Msg.1.0.serialize"),
                                 body("ns.Msg.1.0.get.speed"),
                                 body("ns.Msg.1.0.plan.scalar_unsigned.1.ser", true)};
        ok                    = expect(outline(allocate(Language::C, {owner})),
                                       "root pkg\n"
                                       "  file Msg_1_0\n"
                                       "    type ns__Msg\n"
                                       "    llvmdsdl_plan_capacity_check__ns__Msg_1_0 : value\n"
                                       "    llvmdsdl_plan_scalar_unsigned__ns__Msg_1_0__0__ser : value\n"
                                       "    ns__Msg_1_0__serialize_ir_ : value\n"
                                       "    ns__Msg_1_0__get_speed_ir_ : value\n"
                                       "    llvmdsdl_plan_scalar_unsigned__ns__Msg_1_0__1__ser : value\n",
                                       "C's lowered functions") &&
                                ok;
        ok                    = expect(outline(allocate(Language::Cpp, {owner})),
                                       "root pkg\n"
                                       "  namespace ns\n"
                                       "    file Msg_1_0\n"
                                       "      type Msg\n"
                                       "      mlir_llvmdsdl_plan_capacity_check_ns_Msg_1_0 : value\n"
                                       "      mlir_llvmdsdl_plan_scalar_unsigned_ns_Msg_1_0_0_ser : value\n",
                                       "C++'s helpers") &&
                                ok;
        ok                    = expect(outline(allocate(Language::Rust, {owner})),
                                       "root pkg\n"
                                       "  namespace ns\n"
                                       "    module msg_1_0\n"
                                       "      type Msg\n"
                                       "      capacity_check : value private\n"
                                       "      scalar_unsigned_0_ser : value private\n",
                                       "Rust's helpers") &&
                                ok;
        ok                    = expect(outline(allocate(Language::Go, {owner})),
                                       "root pkg\n"
                                       "  package ns\n"
                                       "    file msg_1_0\n"
                                       "      type Msg\n"
                                       "      msgCapacityCheck : value private\n"
                                       "      msgScalarUnsigned0Ser : value private\n",
                                       "Go's helpers") &&
                                ok;
        ok                    = expect(outline(allocate(Language::TypeScript, {owner})),
                                       "root pkg\n"
                                       "  namespace ns\n"
                                       "    module msg_1_0\n"
                                       "      type Msg\n"
                                       "      capacityCheck : value private\n"
                                       "      scalarUnsigned0Ser : value private\n",
                                       "TypeScript's helpers") &&
                                ok;
        ok                    = expect(outline(allocate(Language::Python, {owner})),
                                       "root pkg\n"
                                       "  namespace ns\n"
                                       "    module msg_1_0\n"
                                       "      type Msg\n"
                                       "      _capacity_check : value private\n"
                                       "      _scalar_unsigned_0_ser : value private\n",
                                       "Python's helpers") &&
                                ok;

        // Two helpers that project onto one name meet in the pool, and the later one moves.
        DefinitionParts           twins  = message("Msg", SectionParts{.fields = {}, .constants = {}});
        const llvmdsdl::BodyParts first  = body("ns.Msg.1.0.plan.capacity_check");
        llvmdsdl::BodyParts       second = first;
        second.symbol                    = "ns.Msg.1.0.plan.capacity__check";
        second.plan.helperKind           = "capacity__check";
        twins.bodies                     = {first, second};
        ok                               = expect(outline(allocate(Language::Rust, {twins})),
                                                  "root pkg\n"
                                                  "  namespace ns\n"
                                                  "    module msg_1_0\n"
                                                  "      type Msg\n"
                                                  "      capacity_check : value private\n"
                                                  "      capacity_check_2 : value private\n",
                                                  "two helpers of one name") &&
                                           ok;
    }

    // A service's own name is an alias of its request, where no section's type already has it.
    {
        DefinitionParts named = message("Request", SectionParts{.fields = {}, .constants = {}});
        named.service         = true;
        named.response        = SectionParts{.fields = {}, .constants = {}};
        ok                    = expect(outline(allocate(Language::Rust, {named})),
                                       "root pkg\n"
                                       "  namespace ns\n"
                                       "    module request_1_0\n"
                                       "      type Request\n"
                                       "      type Response\n",
                                       "a service named for its request section") &&
                                ok;
        ok                    = expect(outline(allocate(Language::C, {named})),
                                       "root pkg\n"
                                       "  file Request_1_0\n"
                                       "    type ns__Request__Request\n"
                                       "    type ns__Request__Response\n"
                                       "    ns__Request : type\n",
                                       "a C service") &&
                                ok;
    }

    // Band 1, the language's reservations: a constant that reaches a name the generator writes at
    // module scope moves, and so does one the projection's table claims.
    {
        const SectionParts claimed{.fields = {}, .constants = {"FULL_NAME"}};
        ok = expect(constantName(allocate(Language::TypeScript, {message("DSDL", claimed)}), "FULL_NAME"),
                    "DSDL_FULL_NAME_2",
                    "a TypeScript constant that reaches the module's metadata") &&
             ok;
        ok = expect(constantName(allocate(Language::Python, {message("DSDL", claimed)}), "FULL_NAME"),
                    "DSDL_FULL_NAME_2",
                    "a Python constant that reaches the module's metadata") &&
             ok;
        ok = expect(constantName(allocate(Language::Rust, {message("Msg", claimed)}), "FULL_NAME"),
                    "FULL_NAME_",
                    "a Rust constant the projection's table claims") &&
             ok;
    }

    // Band 2, the generator's own names: Go declares a type's metadata constants before any DSDL one.
    ok = expect(constantName(allocate(Language::Go,
                                      {message("Claimed", SectionParts{.fields = {}, .constants = {"FULL_NAME"}})}),
                             "FULL_NAME"),
                "ClaimedFullName2",
                "a Go constant that meets a generated one") &&
         ok;

    // Band 3, the declarations in order: array metadata, then option tags, then constants. The
    // earlier name keeps its projection and the later one moves.
    {
        const SectionParts arrays{.fields = {field("foo", true)}, .constants = {"FOO_ARRAY_CAPACITY"}};
        ok = expect(constantName(allocate(Language::Cpp, {message("Msg", arrays)}), "FOO_ARRAY_CAPACITY"),
                    "FOO_ARRAY_CAPACITY_2",
                    "a C++ constant that meets an array's metadata") &&
             ok;

        const SectionParts options{.fields    = {field("small", false, 0), field("large", false, 1)},
                                   .constants = {"SMALL_OPTION_TAG"},
                                   .isUnion   = true};
        for (const Language language : {Language::Cpp, Language::Rust, Language::TypeScript})
        {
            const SurfacePlan plan = allocate(language, {message("Pick", options)});
            const std::string row  = llvmdsdl::languageTraits(language).name.str();
            ok = expect(constantName(plan, "SMALL_OPTION_TAG"),
                        (language == Language::TypeScript) ? "PICK_SMALL_OPTION_TAG_2" : "SMALL_OPTION_TAG_2",
                        row + ": a constant that meets an option tag") &&
                 ok;
            ok = expect(optionName(plan, "small"),
                        (language == Language::TypeScript) ? "PICK_SMALL_OPTION_TAG" : "SMALL_OPTION_TAG",
                        row + ": the option tag") &&
                 ok;
            ok = expect(std::to_string(plan.definitions.front().sections.front().options.find("large")->second.tag),
                        "1",
                        row + ": an option's tag value") &&
                 ok;
        }
    }

    // Each scope a language writes to a file of its own is placed there: a module-per-definition
    // language's files are under its source directory, and a namespace's own declarations in the
    // namespace's file.
    ok = expect(placements(allocate(Language::Rust, {message("Msg", limited)})),
                "root pkg: src/lib.rs\n"
                "namespace ns: src/ns/mod.rs\n"
                "module msg_1_0: src/ns/msg_1_0.rs\n",
                "Rust's files") &&
         ok;
    ok = expect(placements(allocate(Language::Python, {message("Msg", limited)}, "top.pkg")),
                "root top.pkg: top/pkg/__init__.py\n"
                "namespace ns: top/pkg/ns/__init__.py\n"
                "module msg_1_0: top/pkg/ns/msg_1_0.py\n",
                "Python's files, under the package's directories") &&
         ok;
    ok = expect(placements(allocate(Language::C, {message("Msg", limited)})),
                "file Msg_1_0: ns/Msg_1_0.h\n",
                "C's files") &&
         ok;

    // Band 5, the imports. A file imports each definition its fields hold, in the order of the
    // definitions' names, and a field holding a view names no type. What the file declares is
    // reserved first, so an import that meets it takes as much of its namespace as tells it apart.
    {
        const std::vector<DefinitionParts>
            definitions{message("Holder",
                                SectionParts{.fields    = {holding("mine", ref("other", "Holder")),
                                                           holding("near", ref("b", "Scalar")),
                                                           holding("far", ref("a", "Scalar")),
                                                           holding("seen", ref("ns", "Viewed"), true),
                                                           holding("elsewhere", ref("absent", "Thing"))},
                                             .constants = {}}),
                        message("Holder", SectionParts{}, "other"),
                        message("Scalar", SectionParts{}, "a"),
                        message("Scalar", SectionParts{}, "b"),
                        message("Viewed", SectionParts{})};
        ok = expect(imports(allocate(Language::Rust, definitions)),
                    "Scalar a.Scalar.1.0\n"
                    "BScalar b.Scalar.1.0\n"
                    "OtherHolder other.Holder.1.0\n",
                    "Rust's imports") &&
             ok;
        ok = expect(imports(allocate(Language::Python, definitions)),
                    "Scalar a.Scalar.1.0\n"
                    "BScalar b.Scalar.1.0\n"
                    "OtherHolder other.Holder.1.0\n",
                    "Python's imports") &&
             ok;
        ok = expect(imports(allocate(Language::Cpp, definitions)), "", "C++ includes rather than imports") && ok;

        // A deprecated definition is imported under the name it is declared under.
        std::vector<DefinitionParts> deprecated{message("Holder",
                                                        SectionParts{.fields    = {holding("old", ref("ns", "Old"))},
                                                                     .constants = {}}),
                                                message("Old", SectionParts{})};
        deprecated.back().deprecated = true;
        ok = expect(imports(allocate(Language::Rust, deprecated)), "Old_ ns.Old.1.0\n", "a deprecated import") && ok;
    }

    return ok;
}
