//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>

#include "llvmdsdl/CodeGen/DefinitionPathProjection.h"

#include "UnitTests.h"

bool runDefinitionPathProjectionTests()
{
    using llvmdsdl::CodegenNamingLanguage;

    if (llvmdsdl::renderVersionedTypeName(CodegenNamingLanguage::TypeScript, "VehicleState", 1, 2) !=
        "VehicleState_1_2")
    {
        std::cerr << "renderVersionedTypeName unexpected TypeScript result\n";
        return false;
    }

    if (llvmdsdl::renderVersionedFileStem(CodegenNamingLanguage::Python, "9AxisIMU", 1, 0) != "_9axis_imu_1_0")
    {
        std::cerr << "renderVersionedFileStem unexpected Python result\n";
        return false;
    }

    const auto nsPath =
        llvmdsdl::renderNamespaceRelativePath(CodegenNamingLanguage::TypeScript, {"Acme", "FlightControl"});
    if (nsPath.generic_string() != "acme/flight_control")
    {
        std::cerr << "renderNamespaceRelativePath unexpected TypeScript result\n";
        return false;
    }

    llvmdsdl::DiscoveredDefinition discovered;
    discovered.shortName           = "Telemetry";
    discovered.namespaceComponents = {"Acme"};
    discovered.majorVersion        = 1;
    discovered.minorVersion        = 0;
    const auto discoveredPath =
        llvmdsdl::renderRelativeTypeFilePath(CodegenNamingLanguage::TypeScript, discovered, ".ts");
    if (discoveredPath.generic_string() != "acme/telemetry_1_0.ts")
    {
        std::cerr << "renderRelativeTypeFilePath unexpected discovered result\n";
        return false;
    }

    llvmdsdl::SemanticTypeRef ref;
    ref.namespaceComponents = {"acme", "vision"};
    ref.shortName           = "ObjectTrack";
    ref.majorVersion        = 2;
    ref.minorVersion        = 3;
    const auto refPath      = llvmdsdl::renderRelativeTypeFilePath(CodegenNamingLanguage::Python, ref, "py");
    if (refPath.generic_string() != "acme/vision/object_track_2_3.py")
    {
        std::cerr << "renderRelativeTypeFilePath unexpected type-ref result\n";
        return false;
    }

    return true;
}
