//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>

#include "llvmdsdl/CodeGen/emitter/CHeaderRender.h"

#include "UnitTests.h"

bool runCHeaderRenderTests()
{
    llvmdsdl::emitter::c::HeaderTypeMetadata metadata;
    metadata.typeName                     = "uavcan__node__Heartbeat";
    metadata.fullName                     = "uavcan.node.Heartbeat";
    metadata.majorVersion                 = 1;
    metadata.minorVersion                 = 0;
    metadata.extentBytes                  = 7;
    metadata.serializationBufferSizeBytes = 12;

    const auto metadataLines = llvmdsdl::emitter::c::renderTypeMetadataMacros(metadata);
    if (metadataLines.size() != 4U)
    {
        std::cerr << "renderTypeMetadataMacros expected 4 lines\n";
        return false;
    }
    if (metadataLines[0] != "#define uavcan__node__Heartbeat_FULL_NAME_ \"uavcan.node.Heartbeat\"")
    {
        std::cerr << "renderTypeMetadataMacros full-name line mismatch\n";
        return false;
    }
    if (metadataLines[2] != "#define uavcan__node__Heartbeat_EXTENT_BYTES_ 7UL")
    {
        std::cerr << "renderTypeMetadataMacros extent line mismatch\n";
        return false;
    }

    const auto aliasIdentity =
        llvmdsdl::emitter::c::renderServiceAliasIdentityMacros("uavcan__srv__NodeInfo", "uavcan.srv.NodeInfo", 2, 1);
    if (aliasIdentity.size() != 2U)
    {
        std::cerr << "renderServiceAliasIdentityMacros expected 2 lines\n";
        return false;
    }
    if (aliasIdentity[1] != "#define uavcan__srv__NodeInfo_FULL_NAME_AND_VERSION_ \"uavcan.srv.NodeInfo.2.1\"")
    {
        std::cerr << "renderServiceAliasIdentityMacros version line mismatch\n";
        return false;
    }

    const auto aliasBridge = llvmdsdl::emitter::c::renderServiceAliasBridgeLines("uavcan__srv__NodeInfo",
                                                                                 "uavcan__srv__NodeInfo__Request",
                                                                                 false);
    if (aliasBridge.size() != 5U)
    {
        std::cerr << "renderServiceAliasBridgeLines expected 5 lines\n";
        return false;
    }
    // The alias names the request type through its tag: the typedef is what carries a deprecation
    // attribute, and a tag never does.
    if (aliasBridge[0] != "typedef struct uavcan__srv__NodeInfo__Request uavcan__srv__NodeInfo;")
    {
        std::cerr << "renderServiceAliasBridgeLines typedef mismatch\n";
        return false;
    }

    const auto deprecatedBridge = llvmdsdl::emitter::c::renderServiceAliasBridgeLines("uavcan__srv__NodeInfo",
                                                                                      "uavcan__srv__NodeInfo__Request",
                                                                                      true);
    if (deprecatedBridge[0] !=
        "typedef struct uavcan__srv__NodeInfo__Request uavcan__srv__NodeInfo __attribute__((deprecated));")
    {
        std::cerr << "renderServiceAliasBridgeLines deprecated typedef mismatch\n";
        return false;
    }

    const auto wrappers =
        llvmdsdl::emitter::c::renderServiceAliasWrapperLines("uavcan__srv__NodeInfo", "uavcan__srv__NodeInfo__Request");
    if (wrappers.size() != 16U)
    {
        std::cerr << "renderServiceAliasWrapperLines expected 16 lines\n";
        return false;
    }
    if (wrappers[0] != "static inline int8_t uavcan__srv__NodeInfo__serialize_(const struct "
                       "uavcan__srv__NodeInfo__Request* const obj, uint8_t* const buffer, size_t* const "
                       "inout_buffer_size_bytes)")
    {
        std::cerr << "renderServiceAliasWrapperLines serialize signature mismatch\n";
        return false;
    }
    if (wrappers[6] !=
        "  return uavcan__srv__NodeInfo__Request__deserialize_(out_obj, buffer, inout_buffer_size_bytes);")
    {
        std::cerr << "renderServiceAliasWrapperLines deserialize body mismatch\n";
        return false;
    }

    return true;
}
