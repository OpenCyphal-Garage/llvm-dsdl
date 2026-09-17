//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <optional>

#include "llvmdsdl/CodeGen/emitter/CHeaderRender.h"

#include "UnitTests.h"

bool runCHeaderRenderTests()
{
    llvmdsdl::SectionMetadata metadata;
    metadata.fullName                     = "uavcan.node.Heartbeat";
    metadata.majorVersion                 = 1;
    metadata.minorVersion                 = 0;
    metadata.extentBytes                  = 7;
    metadata.serializationBufferSizeBytes = 12;
    metadata.alias                        = {true, "eligible"};

    const auto metadataLines = llvmdsdl::emitter::c::renderTypeMetadataMacros("uavcan__node__Heartbeat", metadata);
    if (metadataLines.size() != 8U)
    {
        std::cerr << "renderTypeMetadataMacros expected 8 lines\n";
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
    if (metadataLines[4] != "#define uavcan__node__Heartbeat_ZOH_ALIAS_ELIGIBLE_ true")
    {
        std::cerr << "renderTypeMetadataMacros alias line mismatch\n";
        return false;
    }
    if (metadataLines[6] != "#define uavcan__node__Heartbeat_IS_DEPRECATED_ false")
    {
        std::cerr << "renderTypeMetadataMacros deprecation line mismatch\n";
        return false;
    }
    if (metadataLines[7] != "#define uavcan__node__Heartbeat_HAS_FIXED_PORT_ID_ false")
    {
        std::cerr << "renderTypeMetadataMacros port-ID line mismatch\n";
        return false;
    }

    // A message that has a subject-ID declares it beside the flag that says it has one.
    metadata.fixedPortId  = 7509U;
    const auto withPortId = llvmdsdl::emitter::c::renderTypeMetadataMacros("uavcan__node__Heartbeat", metadata);
    if ((withPortId.size() != 9U) || (withPortId[8] != "#define uavcan__node__Heartbeat_FIXED_PORT_ID_ 7509U"))
    {
        std::cerr << "renderTypeMetadataMacros port-ID value mismatch\n";
        return false;
    }

    // A service's request is not the type the service is reached through, so it declares neither.
    metadata.declaresPortId = false;
    if (llvmdsdl::emitter::c::renderTypeMetadataMacros("uavcan__node__Heartbeat", metadata).size() != 7U)
    {
        std::cerr << "renderTypeMetadataMacros emitted a port-ID for a section that declares none\n";
        return false;
    }

    const auto aliasIdentity = llvmdsdl::emitter::c::renderServiceAliasIdentityMacros("uavcan__srv__NodeInfo",
                                                                                      "uavcan.srv.NodeInfo",
                                                                                      2,
                                                                                      1,
                                                                                      std::nullopt);
    if (aliasIdentity.size() != 3U)
    {
        std::cerr << "renderServiceAliasIdentityMacros expected 3 lines\n";
        return false;
    }
    if (aliasIdentity[1] != "#define uavcan__srv__NodeInfo_FULL_NAME_AND_VERSION_ \"uavcan.srv.NodeInfo.2.1\"")
    {
        std::cerr << "renderServiceAliasIdentityMacros version line mismatch\n";
        return false;
    }
    if (aliasIdentity[2] != "#define uavcan__srv__NodeInfo_HAS_FIXED_PORT_ID_ false")
    {
        std::cerr << "renderServiceAliasIdentityMacros port-ID line mismatch\n";
        return false;
    }

    // A service that has a service-ID declares it on the alias, which is the type that stands for
    // the service; its request and response declare nothing.
    const auto aliasWithPortId = llvmdsdl::emitter::c::renderServiceAliasIdentityMacros("uavcan__node__ExecuteCommand",
                                                                                        "uavcan.node.ExecuteCommand",
                                                                                        1,
                                                                                        3,
                                                                                        435U);
    if (aliasWithPortId.size() != 4U)
    {
        std::cerr << "renderServiceAliasIdentityMacros expected 4 lines with a port-ID\n";
        return false;
    }
    if (aliasWithPortId[3] != "#define uavcan__node__ExecuteCommand_FIXED_PORT_ID_ 435U")
    {
        std::cerr << "renderServiceAliasIdentityMacros port-ID value mismatch\n";
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
