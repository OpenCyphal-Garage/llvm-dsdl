//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/ImportNameScope.h"
#include "llvmdsdl/Support/Language.h"

#include "UnitTests.h"

namespace
{

llvmdsdl::DefinitionRef refTo(const std::vector<std::string>& namespaceComponents,
                              const std::string&              shortName,
                              const std::uint32_t             minor = 0)
{
    return llvmdsdl::DefinitionRef{.namespaceComponents = namespaceComponents,
                                   .shortName           = shortName,
                                   .majorVersion        = 1,
                                   .minorVersion        = minor};
}

bool expect(const std::string& got, const std::string& want, const char* what)
{
    if (got != want)
    {
        std::cerr << "import name scope: " << what << " is " << got << ", want " << want << "\n";
        return false;
    }
    return true;
}

}  // namespace

bool runImportNameScopeTests()
{
    using llvmdsdl::ImportNameScope;
    using llvmdsdl::Language;
    bool ok = true;

    // A dependency whose short name the file declares is the one that moves.
    {
        ImportNameScope scope(Language::Rust);
        scope.reserve("Owner");
        const auto inner = refTo({"adv", "shadow", "inner"}, "Owner");
        ok = expect(scope.claim(inner, "Owner", false), "InnerOwner", "a dependency meeting a declaration") && ok;
        ok = expect(scope.claim(inner, "Owner", false), "InnerOwner", "a second claim of one definition") && ok;
        ok = expect(scope.localName(inner, "Owner"), "InnerOwner", "a claimed local name") && ok;
    }

    // Two dependencies of one short name: the first keeps it, the second takes its namespace.
    {
        ImportNameScope scope(Language::Rust);
        const auto      velocity = refTo({"uavcan", "si", "unit", "velocity"}, "Vector3");
        const auto      angular  = refTo({"uavcan", "si", "unit", "angular_velocity"}, "Vector3");
        ok                       = expect(scope.claim(velocity, "Vector3", false), "Vector3", "the first of two") && ok;
        ok = expect(scope.claim(angular, "Vector3", false), "AngularVelocityVector3", "the second of two") && ok;
        ok = expect(scope.localName(refTo({"ns"}, "Other"), "Other"), "Other", "an unclaimed definition") && ok;
    }

    // The namespace runs out before three versions of one type do, and an ordinal follows.
    {
        ImportNameScope scope(Language::Rust);
        ok = expect(scope.claim(refTo({"ns"}, "Ver", 0), "Ver", false), "Ver", "the first version") && ok;
        ok = expect(scope.claim(refTo({"ns"}, "Ver", 1), "Ver", false), "NsVer", "the second version") && ok;
        ok = expect(scope.claim(refTo({"ns"}, "Ver", 2), "Ver", false), "Ver2", "the third version") && ok;
    }

    // A type that brings functions named after it clashes on any of them: `serializeFooInto` is the
    // body function of an imported `Foo` and the entry point of a declared `FooInto`.
    {
        ImportNameScope scope(Language::TypeScript, [](const std::string& type) {
            return std::vector<std::string>{type, "make" + type, "serialize" + type + "Into"};
        });
        scope.reserve("serializeFooInto");
        ok = expect(scope.claim(refTo({"ns"}, "Foo"), "Foo", false), "NsFoo", "a clash on a brought name") && ok;
    }
    return ok;
}
