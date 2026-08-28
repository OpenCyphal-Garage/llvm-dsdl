//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>

#include "llvmdsdl/CodeGen/CHeaderRender.h"

#include "UnitTests.h"

bool runCHeaderRenderTests()
{
    llvmdsdl::CHeaderTypeMetadata metadata;
    metadata.typeName                     = "uavcan__node__Heartbeat";
    metadata.fullName                     = "uavcan.node.Heartbeat";
    metadata.majorVersion                 = 1;
    metadata.minorVersion                 = 0;
    metadata.extentBytes                  = 7;
    metadata.serializationBufferSizeBytes = 12;

    const auto metadataLines = llvmdsdl::renderCTypeMetadataMacros(metadata);
    if (metadataLines.size() != 4U)
    {
        std::cerr << "renderCTypeMetadataMacros expected 4 lines\n";
        return false;
    }
    if (metadataLines[0] != "#define uavcan__node__Heartbeat_FULL_NAME_ \"uavcan.node.Heartbeat\"")
    {
        std::cerr << "renderCTypeMetadataMacros full-name line mismatch\n";
        return false;
    }
    if (metadataLines[2] != "#define uavcan__node__Heartbeat_EXTENT_BYTES_ 7UL")
    {
        std::cerr << "renderCTypeMetadataMacros extent line mismatch\n";
        return false;
    }

    const auto aliasIdentity =
        llvmdsdl::renderCServiceAliasIdentityMacros("uavcan__srv__NodeInfo", "uavcan.srv.NodeInfo", 2, 1);
    if (aliasIdentity.size() != 2U)
    {
        std::cerr << "renderCServiceAliasIdentityMacros expected 2 lines\n";
        return false;
    }
    if (aliasIdentity[1] != "#define uavcan__srv__NodeInfo_FULL_NAME_AND_VERSION_ \"uavcan.srv.NodeInfo.2.1\"")
    {
        std::cerr << "renderCServiceAliasIdentityMacros version line mismatch\n";
        return false;
    }

    const auto aliasBridge =
        llvmdsdl::renderCServiceAliasBridgeLines("uavcan__srv__NodeInfo", "uavcan__srv__NodeInfo__Request");
    if (aliasBridge.size() != 5U)
    {
        std::cerr << "renderCServiceAliasBridgeLines expected 5 lines\n";
        return false;
    }
    if (aliasBridge[0] != "typedef uavcan__srv__NodeInfo__Request uavcan__srv__NodeInfo;")
    {
        std::cerr << "renderCServiceAliasBridgeLines typedef mismatch\n";
        return false;
    }

    const auto wrappers =
        llvmdsdl::renderCServiceAliasWrapperLines("uavcan__srv__NodeInfo", "uavcan__srv__NodeInfo__Request");
    if (wrappers.size() != 16U)
    {
        std::cerr << "renderCServiceAliasWrapperLines expected 16 lines\n";
        return false;
    }
    if (!wrappers[0].contains("uavcan__srv__NodeInfo__serialize_"))
    {
        std::cerr << "renderCServiceAliasWrapperLines serialize signature mismatch\n";
        return false;
    }
    if (!wrappers[6].contains("uavcan__srv__NodeInfo__Request"))
    {
        std::cerr << "renderCServiceAliasWrapperLines deserialize body mismatch\n";
        return false;
    }

    return true;
}
