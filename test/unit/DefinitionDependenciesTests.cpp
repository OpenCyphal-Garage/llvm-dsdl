//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <vector>

#include "llvmdsdl/CodeGen/DefinitionDependencies.h"

#include "UnitTests.h"

namespace
{

llvmdsdl::SemanticTypeRef makeRef(const std::string&              fullName,
                                  const std::vector<std::string>& namespaceComponents,
                                  const std::string&              shortName,
                                  const std::uint32_t             majorVersion,
                                  const std::uint32_t             minorVersion)
{
    llvmdsdl::SemanticTypeRef ref;
    ref.fullName            = fullName;
    ref.namespaceComponents = namespaceComponents;
    ref.shortName           = shortName;
    ref.majorVersion        = majorVersion;
    ref.minorVersion        = minorVersion;
    return ref;
}

llvmdsdl::SemanticField makeCompositeField(const std::string& name, const llvmdsdl::SemanticTypeRef& ref)
{
    llvmdsdl::SemanticField field;
    field.name                               = name;
    field.resolvedType.scalarCategory        = llvmdsdl::SemanticScalarCategory::Composite;
    field.resolvedType.compositeType         = ref;
    field.resolvedType.alignmentBits         = 8;
    field.resolvedType.bitLength             = 8;
    field.resolvedType.arrayKind             = llvmdsdl::ArrayKind::None;
    field.resolvedType.arrayCapacity         = 0;
    field.resolvedType.arrayLengthPrefixBits = 0;
    return field;
}

}  // namespace

bool runDefinitionDependenciesTests()
{
    const auto a = makeRef("alpha.beta.TypeA", {"alpha", "beta"}, "TypeA", 1, 0);
    const auto b = makeRef("alpha.beta.TypeB", {"alpha", "beta"}, "TypeB", 2, 1);
    const auto c = makeRef("alpha.gamma.TypeC", {"alpha", "gamma"}, "TypeC", 1, 3);

    llvmdsdl::SemanticSection section;
    section.fields.push_back(makeCompositeField("a0", a));
    section.fields.push_back(makeCompositeField("b0", b));
    section.fields.push_back(makeCompositeField("a1", a));
    section.fields.push_back(makeCompositeField("c0", c));

    const auto sectionDeps = llvmdsdl::collectSectionCompositeDependencies(section);
    if (sectionDeps.size() != 3U)
    {
        std::cerr << "collectSectionCompositeDependencies expected 3 deps\n";
        return false;
    }
    if (sectionDeps[0].fullName != "alpha.beta.TypeA" || sectionDeps[1].fullName != "alpha.beta.TypeB" ||
        sectionDeps[2].fullName != "alpha.gamma.TypeC")
    {
        std::cerr << "collectSectionCompositeDependencies ordering mismatch\n";
        return false;
    }

    llvmdsdl::SemanticDefinition def;
    def.request = section;
    llvmdsdl::SemanticSection response;
    response.fields.push_back(makeCompositeField("b1", b));
    response.fields.push_back(makeCompositeField("c1", c));
    def.response = response;

    const auto defDeps = llvmdsdl::collectDefinitionCompositeDependencies(def);
    if (defDeps.size() != 3U)
    {
        std::cerr << "collectDefinitionCompositeDependencies expected 3 deps\n";
        return false;
    }
    if (llvmdsdl::renderDefinitionDependencyKey(defDeps[1]) != "alpha.beta.TypeB:2:1")
    {
        std::cerr << "renderDefinitionDependencyKey mismatch\n";
        return false;
    }

    return true;
}
