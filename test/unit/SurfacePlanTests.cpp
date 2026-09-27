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
    return FieldParts{.name = name, .padding = false, .array = array, .unionOptionIndex = option};
}

DefinitionParts message(const std::string& shortName, SectionParts section)
{
    return DefinitionParts{.namespaceComponents = {"ns"},
                           .shortName           = shortName,
                           .majorVersion        = 1,
                           .minorVersion        = 0,
                           .fixedPortId         = std::nullopt,
                           .service             = false,
                           .request             = std::move(section),
                           .response            = std::nullopt};
}

SurfacePlan allocate(const Language language, const std::vector<DefinitionParts>& definitions)
{
    return llvmdsdl::allocateSurface(llvmdsdl::languageTraits(language),
                                     definitions,
                                     llvmdsdl::SurfaceOptions{.packageName = "pkg"});
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
            out += indent + "  " + plan.decls[item.index].name + " : " + className(plan.decls[item.index].nameClass) +
                   "\n";
        }
    }
}

std::string outline(const SurfacePlan& plan)
{
    std::string out;
    outline(plan, 0, "", out);
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
                "  module ns\n"
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
                "  module ns\n"
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
                                        "  module ns\n"
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
                                         "  module ns\n"
                                         "    module list_1_0\n"
                                         "      type Request\n"
                                         "        index : value\n"
                                         "      type Response\n"
                                         "        path : value\n",
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

    return ok;
}
