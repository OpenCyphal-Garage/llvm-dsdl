//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Pins every row of the classification, and what holds across them.
///
/// Each row is rendered to one line and held against the line stated here, so a row changes only
/// where a test says it should. The relations between columns are asserted rather than restated:
/// that every language has its row, that a getter answering a view is one no caller can hand null,
/// and where today's output departs from what its language can express -- which is the work the
/// per-language phases of `CLEAN_CODE.md` exist to do, and which one of them ends by editing the
/// list here.
///
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <iostream>
#include <set>
#include <string>
#include <utility>

#include "llvm/ADT/StringRef.h"

#include "llvmdsdl/Support/BodyInterface.h"
#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/LanguageTraits.h"

#include "UnitTests.h"

namespace
{

using llvmdsdl::ConstantsScope;
using llvmdsdl::ErrorConvention;
using llvmdsdl::InternalLinkage;
using llvmdsdl::Language;
using llvmdsdl::LanguageTraits;
using llvmdsdl::MethodForm;
using llvmdsdl::ReservedUnderscores;

struct TestContext final
{
    bool ok{true};

    void expect(const bool condition, const std::string& what)
    {
        if (!condition)
        {
            ok = false;
            std::cerr << "LanguageTraits test failed: " << what << '\n';
        }
    }
};

/// @brief Every language, which the switch in @ref indexOf keeps complete.
constexpr Language kLanguages[] =
    {Language::C, Language::Cpp, Language::Rust, Language::Go, Language::TypeScript, Language::Python};

/// @brief The position a language's row must hold.
///
/// A switch with no default, so an enumerator added without a case here fails to compile under
/// `-Wswitch` rather than indexing past the table.
std::size_t indexOf(const Language language)
{
    switch (language)
    {
    case Language::C:
        return 0;
    case Language::Cpp:
        return 1;
    case Language::Rust:
        return 2;
    case Language::Go:
        return 3;
    case Language::TypeScript:
        return 4;
    case Language::Python:
        return 5;
    }
    return 6;
}

std::string render(const MethodForm value)
{
    switch (value)
    {
    case MethodForm::None:
        return "none";
    case MethodForm::Member:
        return "member";
    case MethodForm::Impl:
        return "impl";
    case MethodForm::Receiver:
        return "receiver";
    }
    return "?";
}

std::string render(const InternalLinkage value)
{
    switch (value)
    {
    case InternalLinkage::Static:
        return "static";
    case InternalLinkage::PrivateMember:
        return "private-member";
    case InternalLinkage::PrivateByDefault:
        return "private-by-default";
    case InternalLinkage::LowerCaseInitial:
        return "lower-case-initial";
    case InternalLinkage::UnderscorePrefix:
        return "underscore-prefix";
    case InternalLinkage::NotExported:
        return "not-exported";
    }
    return "?";
}

std::string render(const ErrorConvention value)
{
    switch (value)
    {
    case ErrorConvention::StatusCode:
        return "status-code";
    case ErrorConvention::Result:
        return "result";
    case ErrorConvention::ValueAndError:
        return "value-and-error";
    case ErrorConvention::Exception:
        return "exception";
    }
    return "?";
}

std::string render(const ConstantsScope value)
{
    switch (value)
    {
    case ConstantsScope::Enclosing:
        return "enclosing";
    case ConstantsScope::Type:
        return "type";
    case ConstantsScope::Package:
        return "package";
    case ConstantsScope::Module:
        return "module";
    }
    return "?";
}

std::string render(const ReservedUnderscores value)
{
    switch (value)
    {
    case ReservedUnderscores::None:
        return "none";
    case ReservedUnderscores::Leading:
        return "leading";
    case ReservedUnderscores::LeadingAndInterior:
        return "leading-and-interior";
    }
    return "?";
}

std::string render(const llvmdsdl::BoolArrayStorage value)
{
    switch (value)
    {
    case llvmdsdl::BoolArrayStorage::Packed:
        return "packed";
    case llvmdsdl::BoolArrayStorage::PackedWhenFixed:
        return "packed-when-fixed";
    case llvmdsdl::BoolArrayStorage::PerElement:
        return "per-element";
    }
    return "?";
}

std::string flag(const bool value)
{
    return value ? "1" : "0";
}

/// @brief One row as a line, every column named.
std::string render(const LanguageTraits& row)
{
    const auto& c = row.classification;
    const auto& b = row.body;
    const auto& d = row.composition;
    return row.name.str() + ": scopes=" + flag(c.scopes.namespaces) + flag(c.scopes.classes) + flag(c.scopes.modules) +
           " nested=" + flag(c.nestedTypes) + " methods=" + render(c.methods) +
           " internal=" + render(c.internalLinkage) + " errors=" + render(c.errors) +
           " constants=" + render(c.typeConstants) + " constants-share-fields=" + flag(c.constantsShareFieldNamespace) +
           " reserved=" + render(c.reservedUnderscores) + " | nullable=" + flag(b.nullability.objectPointer) +
           flag(b.nullability.rawPointer) + flag(b.nullability.accessorBuffer) +
           " views=" + flag(b.accessorsReturnViews) + " images=" + flag(b.objectsAreByteImages) +
           " nested-answers-size=" + flag(b.nestedCallsAnswerSize) + " bool-arrays=" + render(b.boolArrays) +
           " | namespace-join='" + d.definitionName.namespaceJoin.str() +
           "' version-in-name=" + flag(d.definitionName.versionInTypeName) +
           " name-reaches-type=" + flag(d.definitionName.typeNameReachesTheType) + " section-join='" +
           d.sectionJoin.str() + "' section-alone=" + flag(d.sectionNamedAlone) +
           " namespace-shared=" + flag(d.definitionsShareNamespaceScope) + " constants=" + render(d.constants) +
           " generated-suffix='" + d.generatedConstantSuffix.str() +
           "' array-metadata=" + flag(d.arrayMetadataConstants) +
           " deprecated-apart=" + flag(d.deprecatedTypeDeclaredApart);
}

}  // namespace

bool runLanguageTraitsTests()
{
    TestContext t;

    // Every language has its row, at the position the lookup reads it from.
    t.expect(llvmdsdl::allLanguageTraits().size() == std::size(kLanguages), "one row per language");
    for (const Language language : kLanguages)
    {
        const LanguageTraits& row = llvmdsdl::languageTraits(language);
        t.expect(row.language == language, row.name.str() + " is the row of its own language");
        t.expect(&llvmdsdl::allLanguageTraits()[indexOf(language)] == &row, row.name.str() + " is at its position");
        t.expect(llvmdsdl::languageTraitsNamed(row.name) == &row, row.name.str() + " is found by its name");
    }
    t.expect(llvmdsdl::languageTraitsNamed("obj") == nullptr, "obj is a backend of C, not a language");
    t.expect(llvmdsdl::languageTraitsNamed("definitely-not-a-language") == nullptr, "an unknown name has no row");

    // Every row, pinned.
    const std::pair<Language, std::string> kExpected[] = {
        {Language::C,
         "c: scopes=000 nested=0 methods=none internal=static errors=status-code constants=enclosing "
         "constants-share-fields=0 reserved=leading | nullable=111 views=0 images=1 nested-answers-size=0 "
         "bool-arrays=packed | "
         "namespace-join='__' "
         "version-in-name=1 name-reaches-type=0 section-join='__' section-alone=0 namespace-shared=1 "
         "constants=enclosing generated-suffix='_' array-metadata=1 deprecated-apart=0"},
        {Language::Cpp,
         "cpp: scopes=110 nested=1 methods=member internal=private-member errors=status-code constants=type "
         "constants-share-fields=1 reserved=leading-and-interior | nullable=110 views=1 images=1 nested-answers-size=0 "
         "bool-arrays=packed-when-fixed "
         "| "
         "namespace-join='' version-in-name=1 name-reaches-type=0 section-join='_' section-alone=0 "
         "namespace-shared=1 constants=type generated-suffix='' array-metadata=1 deprecated-apart=1"},
        {Language::Rust,
         "rust: scopes=001 nested=0 methods=impl internal=private-by-default errors=result constants=type "
         "constants-share-fields=0 reserved=none | nullable=000 views=1 images=1 nested-answers-size=1 "
         "bool-arrays=per-element | "
         "namespace-join='' "
         "version-in-name=0 name-reaches-type=1 section-join='' section-alone=1 namespace-shared=0 "
         "constants=type generated-suffix='' array-metadata=0 deprecated-apart=0"},
        {Language::Go,
         "go: scopes=000 nested=0 methods=receiver internal=lower-case-initial errors=value-and-error "
         "constants=package constants-share-fields=0 reserved=none | nullable=100 views=1 images=1 "
         "nested-answers-size=1 bool-arrays=per-element | "
         "namespace-join='' version-in-name=1 name-reaches-type=1 section-join='' section-alone=0 "
         "namespace-shared=1 constants=package generated-suffix='' array-metadata=0 deprecated-apart=0"},
        {Language::TypeScript,
         "ts: scopes=110 nested=1 methods=member internal=not-exported errors=exception constants=type "
         "constants-share-fields=0 reserved=none | nullable=100 views=1 images=0 nested-answers-size=1 "
         "bool-arrays=per-element | "
         "namespace-join='' "
         "version-in-name=1 name-reaches-type=1 section-join='' section-alone=0 namespace-shared=0 "
         "constants=module generated-suffix='' array-metadata=0 deprecated-apart=0"},
        {Language::Python,
         "python: scopes=010 nested=1 methods=member internal=underscore-prefix errors=exception constants=type "
         "constants-share-fields=1 reserved=none | nullable=100 views=1 images=0 nested-answers-size=1 "
         "bool-arrays=per-element | "
         "namespace-join='' "
         "version-in-name=1 name-reaches-type=1 section-join='' section-alone=0 namespace-shared=0 "
         "constants=module generated-suffix='' array-metadata=0 deprecated-apart=0"},
    };
    for (const auto& [language, expected] : kExpected)
    {
        const std::string actual = render(llvmdsdl::languageTraits(language));
        std::string       what   = "row changed without its test:\n  expected ";
        what += expected;
        what += "\n  actual   ";
        what += actual;
        t.expect(actual == expected, what);
    }

    // A getter that answers a view takes one, and a view is never null; a getter that answers a
    // pointer takes a pointer. A buffer no body can be handed null is never null to a getter either.
    for (const LanguageTraits& row : llvmdsdl::allLanguageTraits())
    {
        t.expect(row.body.accessorsReturnViews == !row.body.nullability.accessorBuffer,
                 row.name.str() + ": a getter's buffer is null only where it answers a pointer");
        t.expect(row.body.nullability.rawPointer || !row.body.nullability.accessorBuffer,
                 row.name.str() + ": a buffer no body sees null is not null to a getter");
    }

    // Where the output declares a type's constants somewhere other than its language would. The
    // phase that takes each of these languages removes it from this list.
    std::set<std::string> departures;
    for (const LanguageTraits& row : llvmdsdl::allLanguageTraits())
    {
        if (row.composition.constants != row.classification.typeConstants)
        {
            departures.insert(row.name.str());
        }
    }
    t.expect(departures == std::set<std::string>{"python", "ts"},
             "the languages whose constants are not yet where the language declares them");

    return t.ok;
}
