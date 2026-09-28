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

using llvmdsdl::AccessorNaming;
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

std::string render(const AccessorNaming value)
{
    switch (value)
    {
    case AccessorNaming::None:
        return "none";
    case AccessorNaming::Joined:
        return "joined";
    case AccessorNaming::Concatenated:
        return "concatenated";
    case AccessorNaming::VerbFirst:
        return "verb-first";
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

std::string render(const llvmdsdl::NamespaceForm value)
{
    switch (value)
    {
    case llvmdsdl::NamespaceForm::Joined:
        return "joined";
    case llvmdsdl::NamespaceForm::Namespace:
        return "namespace";
    case llvmdsdl::NamespaceForm::Module:
        return "module";
    case llvmdsdl::NamespaceForm::Package:
        return "package";
    }
    return "?";
}

std::string render(const llvmdsdl::HelperNaming value)
{
    switch (value)
    {
    case llvmdsdl::HelperNaming::LinkName:
        return "link-name";
    case llvmdsdl::HelperNaming::Binding:
        return "binding";
    case llvmdsdl::HelperNaming::Package:
        return "package";
    case llvmdsdl::HelperNaming::Module:
        return "module";
    }
    return "?";
}

std::string render(const llvmdsdl::ImportNaming value)
{
    switch (value)
    {
    case llvmdsdl::ImportNaming::None:
        return "none";
    case llvmdsdl::ImportNaming::Package:
        return "package";
    case llvmdsdl::ImportNaming::Type:
        return "type";
    case llvmdsdl::ImportNaming::TypeAndFunctions:
        return "type-and-functions";
    }
    return "?";
}

std::string render(const llvmdsdl::MemberReach value)
{
    switch (value)
    {
    case llvmdsdl::MemberReach::None:
        return "none";
    case llvmdsdl::MemberReach::Bare:
        return "bare";
    case llvmdsdl::MemberReach::SelfType:
        return "self-type";
    case llvmdsdl::MemberReach::Instance:
        return "instance";
    case llvmdsdl::MemberReach::TypeName:
        return "type-name";
    }
    return "?";
}

std::string render(const llvmdsdl::Qualification value)
{
    switch (value)
    {
    case llvmdsdl::Qualification::Shortest:
        return "shortest";
    case llvmdsdl::Qualification::Rooted:
        return "rooted";
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
           " constants=" + render(c.typeConstants) + " reserved=" + render(c.reservedUnderscores) +
           " classes=" + flag(c.nameClasses.typesApartFromValues) + flag(c.nameClasses.modulesAmongTypes) +
           flag(c.nameClasses.tags) + flag(c.nameClasses.macros) + flag(c.nameClasses.fieldsApart) + " lookup='" +
           c.lookup.separator.str() + "','" + c.lookup.rootPrefix.str() + "'," +
           flag(c.lookup.rootPrefixOnlyWhenShadowed) + flag(c.lookup.enclosingNamespaces) + "," +
           render(c.lookup.ownMembers) + ",'" + c.lookup.selfType.str() + "','" + c.lookup.selfInstance.str() + "','" +
           c.lookup.selfClass.str() + "'" + " | nullable=" + flag(b.nullability.objectPointer) +
           flag(b.nullability.rawPointer) + flag(b.nullability.accessorBuffer) +
           " views=" + flag(b.accessorsReturnViews) + " images=" + flag(b.objectsAreByteImages) +
           " answers-size=" + flag(b.bodiesAnswerSize) + " bool-arrays=" + render(b.boolArrays) +
           " | namespace-join='" + d.definitionName.namespaceJoin.str() +
           "' version-in-name=" + flag(d.definitionName.versionInTypeName) +
           " name-reaches-type=" + flag(d.definitionName.typeNameReachesTheType) + " section-join='" +
           d.sectionJoin.str() + "' section-alone=" + flag(d.sectionNamedAlone) +
           " namespace-shared=" + flag(d.definitionsShareNamespaceScope) + " namespaces=" + render(d.namespaces) +
           " extension='" + d.fileExtension.str() + "' directories-projected=" + flag(d.directoriesProjected) +
           " source='" + d.sourceDirectory.str() + "' package-directory=" + flag(d.packageDirectory) +
           " namespace-file='" + d.namespaceFile.str() + "' root-file='" + d.rootFile.str() +
           "' imports=" + render(d.imports) + " helpers=" + render(d.helpers) +
           " qualification=" + render(d.qualification) +
           " file-directory-module=" + flag(d.fileAndDirectoryAreOneModule) +
           " namespace-type-scope=" + flag(d.namespaceAndTypeShareScope) + " constants=" + render(d.constants) +
           " constant-macros=" + flag(d.constantsAreMacros) + " generated-suffix='" + d.generatedConstantSuffix.str() +
           "' array-metadata=" + flag(d.arrayMetadataConstants) +
           " deprecated-apart=" + flag(d.deprecatedTypeDeclaredApart) + " free-entry-join='" +
           d.freeFunctions.entryPointJoin.str() + "' free-init=" + flag(d.freeFunctions.initializer) +
           " free-accessors=" + render(d.freeFunctions.accessors) +
           " free-union-options=" + flag(d.freeFunctions.unionOptionFunctions) + " lowered-body-suffix='" +
           d.freeFunctions.loweredBodySuffix.str() + "'";
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
         "reserved=leading classes=00111 lookup='','',00,none,'','','' | nullable=111 views=0 "
         "images=1 answers-size=0 "
         "bool-arrays=packed | "
         "namespace-join='__' "
         "version-in-name=1 name-reaches-type=0 section-join='__' section-alone=0 namespace-shared=1 namespaces=joined "
         "extension='.h' directories-projected=0 source='' package-directory=0 namespace-file='' root-file='' "
         "imports=none "
         "helpers=link-name qualification=shortest "
         "file-directory-module=0 namespace-type-scope=0 constants=enclosing constant-macros=1 generated-suffix='_' "
         "array-metadata=1 "
         "deprecated-apart=0 free-entry-join='__' free-init=1 free-accessors=joined free-union-options=1 "
         "lowered-body-suffix='ir_'"},
        {Language::Cpp,
         "cpp: scopes=110 nested=1 methods=member internal=private-member errors=status-code constants=type "
         "reserved=leading-and-interior classes=01010 lookup='::','::',11,bare,'','','' | "
         "nullable=110 views=1 images=1 "
         "answers-size=0 "
         "bool-arrays=packed-when-fixed "
         "| "
         "namespace-join='' version-in-name=1 name-reaches-type=0 section-join='_' section-alone=0 "
         "namespace-shared=1 namespaces=namespace extension='.hpp' directories-projected=0 source='' "
         "package-directory=0 namespace-file='' root-file='' imports=none helpers=binding "
         "qualification=rooted file-directory-module=0 "
         "namespace-type-scope=1 "
         "constants=type "
         "constant-macros=0 generated-suffix='' "
         "array-metadata=1 "
         "deprecated-apart=1 free-entry-join='_' free-init=0 free-accessors=none free-union-options=0 "
         "lowered-body-suffix=''"},
        {Language::Rust,
         "rust: scopes=001 nested=0 methods=impl internal=private-by-default errors=result constants=type "
         "reserved=none classes=11001 lookup='::','crate::',00,self-type,'Self','','' | "
         "nullable=000 views=1 images=1 answers-size=1 "
         "bool-arrays=per-element | "
         "namespace-join='' "
         "version-in-name=0 name-reaches-type=1 section-join='' section-alone=1 namespace-shared=0 namespaces=module "
         "extension='.rs' directories-projected=1 source='src/' package-directory=0 namespace-file='mod.rs' "
         "root-file='lib.rs' imports=type "
         "helpers=module qualification=shortest "
         "file-directory-module=1 namespace-type-scope=0 constants=type constant-macros=0 generated-suffix='' "
         "array-metadata=0 "
         "deprecated-apart=1 free-entry-join='' free-init=0 free-accessors=none free-union-options=0 "
         "lowered-body-suffix=''"},
        {Language::Go,
         "go: scopes=000 nested=0 methods=receiver internal=lower-case-initial errors=value-and-error "
         "constants=package reserved=none classes=00000 lookup='.','',00,none,'','','' | "
         "nullable=100 views=1 images=1 "
         "answers-size=1 bool-arrays=per-element | "
         "namespace-join='' version-in-name=1 name-reaches-type=1 section-join='' section-alone=0 "
         "namespace-shared=1 namespaces=package extension='.go' directories-projected=1 source='' package-directory=0 "
         "namespace-file='' root-file='' imports=package helpers=package "
         "qualification=shortest file-directory-module=0 "
         "namespace-type-scope=0 "
         "constants=package "
         "constant-macros=0 generated-suffix='' "
         "array-metadata=0 "
         "deprecated-apart=0 free-entry-join='' free-init=0 free-accessors=concatenated free-union-options=0 "
         "lowered-body-suffix=''"},
        {Language::TypeScript,
         "ts: scopes=110 nested=1 methods=member internal=not-exported errors=exception constants=type "
         "reserved=none classes=10001 lookup='.','',00,type-name,'','','' | nullable=100 "
         "views=1 images=0 answers-size=1 "
         "bool-arrays=per-element | "
         "namespace-join='' "
         "version-in-name=1 name-reaches-type=1 section-join='' section-alone=0 namespace-shared=0 namespaces=module "
         "extension='.ts' directories-projected=1 source='' package-directory=0 namespace-file='' root-file='index.ts' "
         "imports=type-and-functions "
         "helpers=module qualification=shortest "
         "file-directory-module=0 namespace-type-scope=0 constants=module constant-macros=0 generated-suffix='' "
         "array-metadata=0 "
         "deprecated-apart=0 free-entry-join='' free-init=0 free-accessors=verb-first free-union-options=0 "
         "lowered-body-suffix=''"},
        {Language::Python,
         "python: scopes=010 nested=1 methods=member internal=underscore-prefix errors=exception constants=type "
         "reserved=none classes=01000 lookup='.','',00,instance,'','self','cls' | nullable=100 "
         "views=1 images=0 answers-size=1 "
         "bool-arrays=per-element | "
         "namespace-join='' "
         "version-in-name=1 name-reaches-type=1 section-join='' section-alone=0 namespace-shared=0 namespaces=module "
         "extension='.py' directories-projected=1 source='' package-directory=1 namespace-file='__init__.py' "
         "root-file='__init__.py' imports=type "
         "helpers=module qualification=shortest "
         "file-directory-module=1 namespace-type-scope=0 constants=module constant-macros=0 generated-suffix='' "
         "array-metadata=0 "
         "deprecated-apart=0 free-entry-join='' free-init=0 free-accessors=none free-union-options=0 "
         "lowered-body-suffix=''"},
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

    // A helper is named by its link name exactly where the bodies are compiled apart.
    for (const LanguageTraits& row : llvmdsdl::allLanguageTraits())
    {
        t.expect((row.composition.helpers == llvmdsdl::HelperNaming::LinkName) ==
                     !row.composition.freeFunctions.loweredBodySuffix.empty(),
                 row.name.str() + ": a helper takes a link name where, and only where, the bodies are apart");
    }

    // A namespace is joined into each identifier exactly where the row names a separator to join it.
    for (const LanguageTraits& row : llvmdsdl::allLanguageTraits())
    {
        t.expect((row.composition.namespaces == llvmdsdl::NamespaceForm::Joined) ==
                     !row.composition.definitionName.namespaceJoin.empty(),
                 row.name.str() + ": a joined namespace has a separator, and only a joined one");
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
