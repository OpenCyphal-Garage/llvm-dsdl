//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <string>
#include <vector>

#include "llvmdsdl/CodeGen/CompositeImportGraph.h"
#include "llvmdsdl/Semantics/Model.h"

#include "UnitTests.h"

namespace
{

llvmdsdl::SemanticTypeRef makeRef(const std::string&       fullName,
                                  std::vector<std::string> namespaceComponents,
                                  const std::string&       shortName,
                                  const std::uint32_t      majorVersion,
                                  const std::uint32_t      minorVersion)
{
    llvmdsdl::SemanticTypeRef ref;
    ref.fullName            = fullName;
    ref.namespaceComponents = std::move(namespaceComponents);
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
    field.resolvedType.bitLength             = 32;
    field.resolvedType.alignmentBits         = 8;
    field.resolvedType.arrayKind             = llvmdsdl::ArrayKind::None;
    field.resolvedType.arrayCapacity         = 0;
    field.resolvedType.arrayLengthPrefixBits = 0;
    return field;
}

}  // namespace

bool runCompositeImportGraphTests()
{
    llvmdsdl::DiscoveredDefinition owner;
    owner.fullName     = "civildrone.nav.State";
    owner.shortName    = "State";
    owner.majorVersion = 1;
    owner.minorVersion = 0;

    llvmdsdl::SemanticSection section;
    section.fields.push_back(
        makeCompositeField("self_ref", makeRef("civildrone.nav.State", {"civildrone", "nav"}, "State", 1, 0)));
    section.fields.push_back(
        makeCompositeField("camera",
                           makeRef("civildrone.vision.CameraFrame", {"civildrone", "vision"}, "CameraFrame", 1, 2)));
    section.fields.push_back(
        makeCompositeField("camera_duplicate",
                           makeRef("civildrone.vision.CameraFrame", {"civildrone", "vision"}, "CameraFrame", 1, 2)));
    section.fields.push_back(
        makeCompositeField("control", makeRef("civildrone.ctrl.Command", {"civildrone", "ctrl"}, "Command", 2, 0)));

    auto padding =
        makeCompositeField("padding", makeRef("civildrone.misc.Unused", {"civildrone", "misc"}, "Unused", 1, 0));
    padding.isPadding = true;
    section.fields.push_back(padding);

    const auto dependencies = llvmdsdl::collectCompositeDependencies(section, owner);
    if (dependencies.size() != 2U)
    {
        std::cerr << "collectCompositeDependencies expected 2 entries but got " << dependencies.size() << '\n';
        return false;
    }
    if (dependencies[0].fullName != "civildrone.ctrl.Command" ||
        dependencies[1].fullName != "civildrone.vision.CameraFrame")
    {
        std::cerr << "collectCompositeDependencies ordering mismatch\n";
        return false;
    }

    const auto imports = llvmdsdl::projectCompositeImports(
        dependencies,
        [](const llvmdsdl::SemanticTypeRef& ref) { return "pkg." + ref.namespaceComponents.back(); },
        [](const llvmdsdl::SemanticTypeRef& ref) {
            return ref.shortName + "_" + std::to_string(ref.majorVersion) + "_" + std::to_string(ref.minorVersion);
        });

    if (imports.size() != 2U)
    {
        std::cerr << "projectCompositeImports expected 2 entries but got " << imports.size() << '\n';
        return false;
    }
    if (imports[0].modulePath != "pkg.ctrl" || imports[0].typeName != "Command_2_0")
    {
        std::cerr << "projectCompositeImports first entry mismatch\n";
        return false;
    }
    if (imports[1].modulePath != "pkg.vision" || imports[1].typeName != "CameraFrame_1_2")
    {
        std::cerr << "projectCompositeImports second entry mismatch\n";
        return false;
    }

    return true;
}
