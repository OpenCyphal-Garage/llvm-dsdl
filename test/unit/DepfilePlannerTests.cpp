//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "llvmdsdl/Frontend/DepfilePlanner.h"
#include "llvmdsdl/Semantics/Model.h"

namespace
{

std::vector<std::string> splitComponents(const std::string_view fullName)
{
    std::vector<std::string> components;
    std::size_t              begin = 0U;
    while (begin < fullName.size())
    {
        const std::size_t end = fullName.find('.', begin);
        if (end == std::string_view::npos)
        {
            components.emplace_back(fullName.substr(begin));
            break;
        }
        components.emplace_back(fullName.substr(begin, end - begin));
        begin = end + 1U;
    }
    return components;
}

llvmdsdl::SemanticTypeRef makeTypeRef(const std::string& fullName, const std::uint32_t major, const std::uint32_t minor)
{
    llvmdsdl::SemanticTypeRef ref;
    ref.fullName            = fullName;
    ref.namespaceComponents = splitComponents(fullName);
    ref.shortName           = ref.namespaceComponents.empty() ? fullName : ref.namespaceComponents.back();
    if (!ref.namespaceComponents.empty())
    {
        ref.namespaceComponents.pop_back();
    }
    ref.majorVersion = major;
    ref.minorVersion = minor;
    return ref;
}

void appendCompositeField(llvmdsdl::SemanticSection& section, const llvmdsdl::SemanticTypeRef& ref)
{
    llvmdsdl::SemanticField field;
    field.name                        = "dep";
    field.sectionName                 = "request";
    field.resolvedType.scalarCategory = llvmdsdl::SemanticScalarCategory::Composite;
    field.resolvedType.compositeType  = ref;
    section.fields.push_back(std::move(field));
}

llvmdsdl::SemanticDefinition makeDefinition(const std::string&                            fullName,
                                            const std::uint32_t                           major,
                                            const std::uint32_t                           minor,
                                            const std::string&                            filePath,
                                            const std::vector<llvmdsdl::SemanticTypeRef>& requestDeps  = {},
                                            const std::vector<llvmdsdl::SemanticTypeRef>& responseDeps = {})
{
    llvmdsdl::SemanticDefinition def;
    def.info.fullName            = fullName;
    def.info.filePath            = filePath;
    def.info.majorVersion        = major;
    def.info.minorVersion        = minor;
    def.info.namespaceComponents = splitComponents(fullName);
    def.info.shortName = def.info.namespaceComponents.empty() ? fullName : def.info.namespaceComponents.back();
    if (!def.info.namespaceComponents.empty())
    {
        def.info.namespaceComponents.pop_back();
    }

    for (const auto& dep : requestDeps)
    {
        appendCompositeField(def.request, dep);
    }
    if (!responseDeps.empty())
    {
        def.isService = true;
        def.response.emplace();
        for (const auto& dep : responseDeps)
        {
            appendCompositeField(*def.response, dep);
        }
    }
    return def;
}

bool containsSuffix(const std::vector<std::string>& paths, const std::string_view suffix)
{
    return std::any_of(paths.begin(), paths.end(), [&](const std::string& path) {
        return path.size() >= suffix.size() && std::string_view(path).substr(path.size() - suffix.size()) == suffix;
    });
}

bool isSortedAndUnique(const std::vector<std::string>& values)
{
    if (!std::is_sorted(values.begin(), values.end()))
    {
        return false;
    }
    return std::adjacent_find(values.begin(), values.end()) == values.end();
}

}  // namespace

bool runDepfilePlannerTests()
{
    llvmdsdl::SemanticModule module;
    module.definitions.push_back(makeDefinition("ns.A",
                                                1U,
                                                0U,
                                                "/tmp/depfile_planner/ns/A.1.0.dsdl",
                                                {makeTypeRef("ns.B", 1U, 0U),
                                                 makeTypeRef("uavcan.node.Heartbeat", 1U, 0U),
                                                 makeTypeRef("ns.B", 1U, 0U)}));
    module.definitions.push_back(
        makeDefinition("ns.B", 1U, 0U, "/tmp/depfile_planner/ns/B.1.0.dsdl", {}, {makeTypeRef("ns.D", 1U, 0U)}));
    module.definitions.push_back(makeDefinition("ns.D", 1U, 0U, "/tmp/depfile_planner/ns/D.1.0.dsdl"));
    module.definitions.push_back(
        makeDefinition("uavcan.node.Heartbeat", 1U, 0U, "<embedded-uavcan>:uavcan.node.Heartbeat.1.0.dsdl"));

    llvmdsdl::DepfilePlanner planner(module);

    const auto& depsForA = planner.depsForTypeKey("ns.A:1:0");
    if (!isSortedAndUnique(depsForA))
    {
        std::cerr << "depsForTypeKey(ns.A:1:0) must return sorted unique paths\n";
        return false;
    }
    if (!containsSuffix(depsForA, "/A.1.0.dsdl") || !containsSuffix(depsForA, "/B.1.0.dsdl") ||
        !containsSuffix(depsForA, "/D.1.0.dsdl"))
    {
        std::cerr << "depsForTypeKey(ns.A:1:0) missing transitive closure paths\n";
        return false;
    }
    if (containsSuffix(depsForA, "Heartbeat.1.0.dsdl"))
    {
        std::cerr << "depsForTypeKey(ns.A:1:0) must not include embedded synthetic dependencies\n";
        return false;
    }

    const auto& depsForASecondLookup = planner.depsForTypeKey("ns.A:1:0");
    if (&depsForASecondLookup != &depsForA)
    {
        std::cerr << "depsForTypeKey(ns.A:1:0) should be cached and return stable reference\n";
        return false;
    }

    const auto& depsForB = planner.depsForTypeKey("ns.B:1:0");
    if (!containsSuffix(depsForB, "/B.1.0.dsdl") || !containsSuffix(depsForB, "/D.1.0.dsdl"))
    {
        std::cerr << "depsForTypeKey(ns.B:1:0) missing service response dependency closure\n";
        return false;
    }
    if (containsSuffix(depsForB, "/A.1.0.dsdl"))
    {
        std::cerr << "depsForTypeKey(ns.B:1:0) should not include unrelated types\n";
        return false;
    }

    const auto& mergedAThenB = planner.depsForRequiredTypeKeys({"ns.B:1:0", "ns.A:1:0", "ns.B:1:0"});
    const auto& mergedBThenA = planner.depsForRequiredTypeKeys({"ns.A:1:0", "ns.B:1:0"});
    if (&mergedAThenB != &mergedBThenA)
    {
        std::cerr << "depsForRequiredTypeKeys cache should treat reordered equivalent key-sets identically\n";
        return false;
    }
    if (mergedAThenB != mergedBThenA || !isSortedAndUnique(mergedAThenB))
    {
        std::cerr << "depsForRequiredTypeKeys should be deterministic for equivalent key-sets\n";
        return false;
    }

    const auto& emptyRequiredSet = planner.depsForRequiredTypeKeys({});
    if (!emptyRequiredSet.empty())
    {
        std::cerr << "depsForRequiredTypeKeys({}) should be empty\n";
        return false;
    }

    const auto& unknownTypeKey = planner.depsForTypeKey("does.not.exist:1:0");
    if (!unknownTypeKey.empty())
    {
        std::cerr << "depsForTypeKey should be empty for unknown type keys\n";
        return false;
    }

    // With no stamp configured, a type supplied entirely by the embedded catalog has nothing to
    // depend on. That is the defect the stamp exists to fix: the rule would never rebuild.
    const auto& embeddedOnlyWithoutStamp = planner.depsForTypeKey("uavcan.node.Heartbeat:1:0");
    if (!embeddedOnlyWithoutStamp.empty())
    {
        std::cerr << "depsForTypeKey for an embedded type should be empty when no stamp is configured\n";
        return false;
    }

    llvmdsdl::DepfilePlanner stamped(module, "/tmp/depfile_planner/bin/dsdlc");

    // The regression pin: an output generated purely from the compiled-in catalog must carry the
    // toolchain stamp, so upgrading the compiler rebuilds it.
    const auto& stampedEmbeddedOnly = stamped.depsForTypeKey("uavcan.node.Heartbeat:1:0");
    if (stampedEmbeddedOnly.size() != 1U || !containsSuffix(stampedEmbeddedOnly, "/dsdlc"))
    {
        std::cerr << "depsForTypeKey for an embedded type should be exactly the toolchain stamp\n";
        return false;
    }

    // Mixed closure: real inputs are still listed, and the stamp joins them.
    const auto& stampedMixed = stamped.depsForTypeKey("ns.A:1:0");
    if (!isSortedAndUnique(stampedMixed))
    {
        std::cerr << "depsForTypeKey(ns.A:1:0) must stay sorted and unique with the stamp appended\n";
        return false;
    }
    if (!containsSuffix(stampedMixed, "/dsdlc") || !containsSuffix(stampedMixed, "/A.1.0.dsdl") ||
        !containsSuffix(stampedMixed, "/B.1.0.dsdl") || !containsSuffix(stampedMixed, "/D.1.0.dsdl"))
    {
        std::cerr << "depsForTypeKey(ns.A:1:0) should list real inputs plus the toolchain stamp\n";
        return false;
    }
    if (containsSuffix(stampedMixed, "Heartbeat.1.0.dsdl"))
    {
        std::cerr << "the stamp must not reintroduce synthetic embedded paths\n";
        return false;
    }

    // A closure that never touches the catalog must not gain the stamp; a compiler upgrade is not a
    // reason to rebuild output that owes the catalog nothing.
    const auto& stampedLocalOnly = stamped.depsForTypeKey("ns.B:1:0");
    if (containsSuffix(stampedLocalOnly, "/dsdlc"))
    {
        std::cerr << "depsForTypeKey(ns.B:1:0) draws on no embedded type and must not carry the stamp\n";
        return false;
    }

    // Merging must not duplicate the stamp contributed by several keys.
    const auto& stampedMerged = stamped.depsForRequiredTypeKeys({"ns.A:1:0", "uavcan.node.Heartbeat:1:0"});
    if (!isSortedAndUnique(stampedMerged) || !containsSuffix(stampedMerged, "/dsdlc"))
    {
        std::cerr << "depsForRequiredTypeKeys should carry exactly one toolchain stamp entry\n";
        return false;
    }

    return true;
}
