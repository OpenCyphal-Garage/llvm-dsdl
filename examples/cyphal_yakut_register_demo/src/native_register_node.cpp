//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Native Cyphal node demo exposing heartbeat and register services.
///
/// This example uses libudpard transport, a small POSIX UDP shim, and
/// llvm-dsdl-generated C++ types from the standard uavcan namespace.
///
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

extern "C"
{
#include "udp_posix.h"
#include "udpard.h"
}

#include "uavcan/node/Health_1_0.hpp"
#include "uavcan/node/Heartbeat_1_0.hpp"
#include "uavcan/node/Mode_1_0.hpp"
#include "uavcan/register/Access_1_0.hpp"
#include "uavcan/register/List_1_0.hpp"
#include "uavcan/register/Name_1_0.hpp"
#include "uavcan/register/Value_1_0.hpp"

namespace
{

constexpr UdpardPortID      kSubjectHeartbeat      = 7509U;
constexpr UdpardPortID      kServiceRegisterAccess = 384U;
constexpr UdpardPortID      kServiceRegisterList   = 385U;
constexpr size_t            kTxQueueCapacity       = 128U;
constexpr size_t            kRxDatagramCapacity    = 2048U;
constexpr UdpardMicrosecond kTxDeadlineUsec        = 1000000ULL;

volatile std::sig_atomic_t gStopRequested = 0;

void requestStop(const int)
{
    gStopRequested = 1;
}

UdpardMicrosecond getMonotonicMicroseconds()
{
    struct timespec ts = {};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        return 0U;
    }
    return (static_cast<UdpardMicrosecond>(ts.tv_sec) * 1000000ULL) +
           static_cast<UdpardMicrosecond>(ts.tv_nsec / 1000ULL);
}

void* heapAllocate(void* const, const size_t size)
{
    return std::malloc(size);
}

void heapDeallocate(void* const, const size_t, void* const pointer)
{
    std::free(pointer);
}

struct Options
{
    std::string  name            = "native";
    UdpardNodeID nodeId          = UDPARD_NODE_ID_UNSET;
    std::string  ifaceAddress    = "127.0.0.1";
    uint32_t     heartbeatRateHz = 1U;
};

enum class ParseResult
{
    Success,
    Help,
    Error,
};

void printUsage(const char* const programName)
{
    std::fprintf(stderr,
                 "Usage: %s [options]\n"
                 "  --name <label>              Node label for log output (default: native)\n"
                 "  --node-id <n>               Local node-ID [0, %u]\n"
                 "  --iface <ipv4>              Local iface IPv4 address (default: 127.0.0.1)\n"
                 "  --heartbeat-rate-hz <n>     Heartbeat publication rate in Hz (default: 1)\n"
                 "  --help                      Show this help\n",
                 programName,
                 static_cast<unsigned>(UDPARD_NODE_ID_MAX));
}

bool parseUnsigned(const char* const text, unsigned long long& outValue)
{
    if (text == nullptr)
    {
        return false;
    }
    errno                          = 0;
    char*                    tail  = nullptr;
    const unsigned long long value = std::strtoull(text, &tail, 10);
    if ((errno != 0) || (tail == text) || (tail == nullptr) || (*tail != '\0'))
    {
        return false;
    }
    outValue = value;
    return true;
}

ParseResult parseOptions(const int argc, char* const argv[], Options& out)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--help")
        {
            printUsage(argv[0]);
            return ParseResult::Help;
        }
        if (i + 1 >= argc)
        {
            std::fprintf(stderr, "Missing value for option: %s\n", arg.c_str());
            return ParseResult::Error;
        }

        const char* const value = argv[++i];
        if (arg == "--name")
        {
            out.name = value;
        }
        else if (arg == "--node-id")
        {
            unsigned long long parsed = 0ULL;
            if (!parseUnsigned(value, parsed) || (parsed > UDPARD_NODE_ID_MAX))
            {
                std::fprintf(stderr, "Invalid --node-id: %s\n", value);
                return ParseResult::Error;
            }
            out.nodeId = static_cast<UdpardNodeID>(parsed);
        }
        else if (arg == "--iface")
        {
            out.ifaceAddress = value;
        }
        else if (arg == "--heartbeat-rate-hz")
        {
            unsigned long long parsed = 0ULL;
            if (!parseUnsigned(value, parsed) || (parsed == 0ULL) || (parsed > 1000ULL))
            {
                std::fprintf(stderr, "Invalid --heartbeat-rate-hz: %s\n", value);
                return ParseResult::Error;
            }
            out.heartbeatRateHz = static_cast<uint32_t>(parsed);
        }
        else
        {
            std::fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            return ParseResult::Error;
        }
    }
    if (out.nodeId > UDPARD_NODE_ID_MAX)
    {
        std::fprintf(stderr, "--node-id is required\n");
        return ParseResult::Error;
    }
    return ParseResult::Success;
}

enum class RegisterKind
{
    Natural16,
    Natural32,
    String,
};

struct RegisterEntry
{
    std::string  name;
    RegisterKind kind       = RegisterKind::Natural32;
    bool         mutable_   = false;
    bool         persistent = false;
    uint16_t     natural16  = 0U;
    uint32_t     natural32  = 0U;
    std::string  stringValue;
};

struct NodeApp
{
    Options  options;
    uint32_t localIfaceAddress = 0U;

    UdpardTxMemoryResources txMemory = {};
    UdpardRxMemoryResources rxMemory = {};

    UdpardTx              tx                 = {};
    UdpardRxRPCDispatcher rpcDispatcher      = {};
    UdpardRxRPCPort       registerAccessPort = {};
    UdpardRxRPCPort       registerListPort   = {};
    UdpardUDPIPEndpoint   rpcEndpoint        = {};

    UDPTxHandle txSocket = {.fd = -1};
    UDPRxHandle rxSocket = {.fd = -1};

    UdpardTransferID  heartbeatTransferId = 0U;
    UdpardMicrosecond startedAt           = 0U;
    UdpardMicrosecond heartbeatPeriodUsec = 1000000ULL;
    UdpardMicrosecond nextHeartbeatAt     = 0U;
    uint32_t          heartbeatCounter    = 0U;

    std::vector<RegisterEntry> registers = {};
};

void setRegisterName(uavcan::register_::Name& out, const std::string& name)
{
    out.name.assign(name.begin(), name.end());
}

std::string decodeRegisterName(const uavcan::register_::Name& name)
{
    return std::string(name.name.begin(), name.name.end());
}

uavcan::register_::Value makeNatural16Value(const uint16_t value)
{
    uavcan::register_::Value out = {};
    out._tag_                    = 10U;
    out.natural16.value          = {value};
    return out;
}

uavcan::register_::Value makeNatural32Value(const uint32_t value)
{
    uavcan::register_::Value out = {};
    out._tag_                    = 9U;
    out.natural32.value          = {value};
    return out;
}

uavcan::register_::Value makeStringValue(const std::string& value)
{
    uavcan::register_::Value out = {};
    out._tag_                    = 1U;
    out.string.value.assign(value.begin(), value.end());
    return out;
}

uavcan::register_::Value makeEmptyValue()
{
    uavcan::register_::Value out = {};
    out._tag_                    = 0U;
    return out;
}

uavcan::register_::Value exportRegisterValue(const RegisterEntry& entry)
{
    switch (entry.kind)
    {
    case RegisterKind::Natural16:
        return makeNatural16Value(entry.natural16);
    case RegisterKind::Natural32:
        return makeNatural32Value(entry.natural32);
    case RegisterKind::String:
        return makeStringValue(entry.stringValue);
    }
    return makeEmptyValue();
}

bool extractSingleUnsigned(const uavcan::register_::Value& value, uint64_t& out)
{
    switch (value._tag_)
    {
    case 11U:  // natural8
        if (!value.natural8.value.empty())
        {
            out = value.natural8.value.front();
            return true;
        }
        break;
    case 10U:  // natural16
        if (!value.natural16.value.empty())
        {
            out = value.natural16.value.front();
            return true;
        }
        break;
    case 9U:  // natural32
        if (!value.natural32.value.empty())
        {
            out = value.natural32.value.front();
            return true;
        }
        break;
    case 8U:  // natural64
        if (!value.natural64.value.empty())
        {
            out = value.natural64.value.front();
            return true;
        }
        break;
    case 7U:  // integer8
        if (!value.integer8.value.empty() && (value.integer8.value.front() >= 0))
        {
            out = static_cast<uint64_t>(value.integer8.value.front());
            return true;
        }
        break;
    case 6U:  // integer16
        if (!value.integer16.value.empty() && (value.integer16.value.front() >= 0))
        {
            out = static_cast<uint64_t>(value.integer16.value.front());
            return true;
        }
        break;
    case 5U:  // integer32
        if (!value.integer32.value.empty() && (value.integer32.value.front() >= 0))
        {
            out = static_cast<uint64_t>(value.integer32.value.front());
            return true;
        }
        break;
    case 4U:  // integer64
        if (!value.integer64.value.empty() && (value.integer64.value.front() >= 0))
        {
            out = static_cast<uint64_t>(value.integer64.value.front());
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

bool applyRegisterWrite(RegisterEntry& entry, const uavcan::register_::Value& value)
{
    if (value._tag_ == 0U)
    {
        return false;
    }
    switch (entry.kind)
    {
    case RegisterKind::Natural16: {
        uint64_t parsed = 0U;
        if (!extractSingleUnsigned(value, parsed))
        {
            return false;
        }
        entry.natural16 = static_cast<uint16_t>(std::min<uint64_t>(parsed, UINT16_MAX));
        return true;
    }
    case RegisterKind::Natural32: {
        uint64_t parsed = 0U;
        if (!extractSingleUnsigned(value, parsed))
        {
            return false;
        }
        entry.natural32 = static_cast<uint32_t>(std::min<uint64_t>(parsed, UINT32_MAX));
        return true;
    }
    case RegisterKind::String:
        if (value._tag_ != 1U)
        {
            return false;
        }
        entry.stringValue.assign(value.string.value.begin(), value.string.value.end());
        return true;
    }
    return false;
}

RegisterEntry* findRegister(NodeApp& app, const std::string& name)
{
    for (auto& entry : app.registers)
    {
        if (entry.name == name)
        {
            return &entry;
        }
    }
    return nullptr;
}

void initializeRegisters(NodeApp& app)
{
    app.registers.clear();
    app.registers.push_back(RegisterEntry{
        .name        = "uavcan.node.id",
        .kind        = RegisterKind::Natural16,
        .mutable_    = true,
        .persistent  = true,
        .natural16   = static_cast<uint16_t>(app.options.nodeId),
        .stringValue = {},
    });
    app.registers.push_back(RegisterEntry{
        .name        = "uavcan.node.description",
        .kind        = RegisterKind::String,
        .mutable_    = true,
        .persistent  = true,
        .stringValue = "llvm-dsdl native register demo node",
    });
    app.registers.push_back(RegisterEntry{
        .name        = "uavcan.udp.iface",
        .kind        = RegisterKind::String,
        .mutable_    = true,
        .persistent  = true,
        .stringValue = app.options.ifaceAddress,
    });
    app.registers.push_back(RegisterEntry{
        .name        = "demo.rate_hz",
        .kind        = RegisterKind::Natural32,
        .mutable_    = true,
        .persistent  = true,
        .natural32   = app.options.heartbeatRateHz,
        .stringValue = {},
    });
    app.registers.push_back(RegisterEntry{
        .name        = "demo.counter",
        .kind        = RegisterKind::Natural32,
        .mutable_    = true,
        .persistent  = false,
        .natural32   = 0U,
        .stringValue = {},
    });
    app.registers.push_back(RegisterEntry{
        .name        = "sys.version",
        .kind        = RegisterKind::String,
        .mutable_    = false,
        .persistent  = true,
        .stringValue = "0.1.0-demo",
    });
}

void updateHeartbeatPeriodFromRegisters(NodeApp& app)
{
    RegisterEntry* const rate = findRegister(app, "demo.rate_hz");
    if ((rate == nullptr) || (rate->kind != RegisterKind::Natural32))
    {
        app.heartbeatPeriodUsec = 1000000ULL;
        return;
    }
    const uint32_t hz       = (rate->natural32 == 0U) ? 1U : rate->natural32;
    app.heartbeatPeriodUsec = std::max<UdpardMicrosecond>(1U, 1000000ULL / hz);
}

void drainTxQueue(NodeApp& app)
{
    for (;;)
    {
        UdpardTxItem* const item = udpardTxPeek(&app.tx);
        if (item == nullptr)
        {
            break;
        }
        udpardTxFree(app.tx.memory, udpardTxPop(&app.tx, item));
    }
}

std::vector<uint8_t> gatherPayload(const UdpardRxTransfer& transfer)
{
    std::vector<uint8_t> out(transfer.payload_size);
    if (!out.empty())
    {
        (void) udpardGather(transfer.payload, out.size(), out.data());
    }
    return out;
}

template <typename MessageT>
bool serializeMessage(const MessageT& message, std::vector<uint8_t>& outBytes)
{
    outBytes.assign(MessageT::SERIALIZATION_BUFFER_SIZE_BYTES, 0U);
    size_t       ioSize = outBytes.size();
    const int8_t rc     = message.serialize(outBytes.data(), &ioSize);
    if (rc < 0)
    {
        return false;
    }
    outBytes.resize(ioSize);
    return true;
}

template <typename MessageT>
bool deserializeMessage(const std::vector<uint8_t>& bytes, MessageT& outMessage)
{
    size_t         ioSize = bytes.size();
    const uint8_t* ptr    = bytes.empty() ? nullptr : bytes.data();
    const int8_t   rc     = outMessage.deserialize(ptr, &ioSize);
    return rc >= 0;
}

void pumpTx(NodeApp& app, const UdpardMicrosecond now)
{
    for (;;)
    {
        UdpardTxItem* const item = udpardTxPeek(&app.tx);
        if (item == nullptr)
        {
            break;
        }
        if (item->deadline_usec <= now)
        {
            udpardTxFree(app.tx.memory, udpardTxPop(&app.tx, item));
            continue;
        }
        const int16_t sendRc = udpTxSend(&app.txSocket,
                                         item->destination.ip_address,
                                         item->destination.udp_port,
                                         item->dscp,
                                         item->datagram_payload.size,
                                         item->datagram_payload.data);
        if (sendRc == 1)
        {
            udpardTxFree(app.tx.memory, udpardTxPop(&app.tx, item));
            continue;
        }
        if (sendRc == 0)
        {
            break;
        }
        std::fprintf(stderr, "[%s] udpTxSend failed: %d\n", app.options.name.c_str(), static_cast<int>(sendRc));
        udpardTxFree(app.tx.memory, udpardTxPop(&app.tx, item));
    }
}

void sendRpcResponse(NodeApp&                    app,
                     const UdpardMicrosecond     now,
                     const UdpardPortID          serviceId,
                     const UdpardNodeID          destinationNodeId,
                     const UdpardTransferID      transferId,
                     const std::vector<uint8_t>& payload)
{
    const int32_t enqueueRc = udpardTxRespond(&app.tx,
                                              now + kTxDeadlineUsec,
                                              UdpardPriorityNominal,
                                              serviceId,
                                              destinationNodeId,
                                              transferId,
                                              {.size = payload.size(), .data = payload.data()},
                                              nullptr);
    if (enqueueRc < 0)
    {
        std::fprintf(stderr,
                     "[%s] udpardTxRespond failed: %ld\n",
                     app.options.name.c_str(),
                     static_cast<long>(enqueueRc));
    }
}

void handleRegisterListRequest(NodeApp& app, const UdpardRxRPCTransfer& transfer, const UdpardMicrosecond now)
{
    const std::vector<uint8_t>       raw     = gatherPayload(transfer.base);
    uavcan::register_::List_Request request = {};
    if (!deserializeMessage(raw, request))
    {
        std::fprintf(stderr, "[%s] failed to deserialize register.List request\n", app.options.name.c_str());
        return;
    }

    uavcan::register_::List_Response response = {};
    if (request.index < app.registers.size())
    {
        setRegisterName(response.name, app.registers[request.index].name);
    }
    else
    {
        response.name.name.clear();
    }

    std::vector<uint8_t> encoded = {};
    if (!serializeMessage(response, encoded))
    {
        std::fprintf(stderr, "[%s] failed to serialize register.List response\n", app.options.name.c_str());
        return;
    }
    sendRpcResponse(app, now, kServiceRegisterList, transfer.base.source_node_id, transfer.base.transfer_id, encoded);
}

void handleRegisterAccessRequest(NodeApp& app, const UdpardRxRPCTransfer& transfer, const UdpardMicrosecond now)
{
    const std::vector<uint8_t>         raw     = gatherPayload(transfer.base);
    uavcan::register_::Access_Request request = {};
    if (!deserializeMessage(raw, request))
    {
        std::fprintf(stderr, "[%s] failed to deserialize register.Access request\n", app.options.name.c_str());
        return;
    }

    const std::string requestedName = decodeRegisterName(request.name);
    RegisterEntry*    entry         = findRegister(app, requestedName);

    uavcan::register_::Access_Response response = {};
    response.timestamp.microsecond               = uavcan::time::SynchronizedTimestamp::UNKNOWN;

    if (entry == nullptr)
    {
        response.mutable_   = false;
        response.persistent = false;
        response.value      = makeEmptyValue();
    }
    else
    {
        if ((request.value._tag_ != 0U) && entry->mutable_)
        {
            (void) applyRegisterWrite(*entry, request.value);
            if (entry->name == "demo.rate_hz")
            {
                updateHeartbeatPeriodFromRegisters(app);
            }
        }
        response.mutable_   = entry->mutable_;
        response.persistent = entry->persistent;
        response.value      = exportRegisterValue(*entry);
    }

    std::vector<uint8_t> encoded = {};
    if (!serializeMessage(response, encoded))
    {
        std::fprintf(stderr, "[%s] failed to serialize register.Access response\n", app.options.name.c_str());
        return;
    }
    sendRpcResponse(app, now, kServiceRegisterAccess, transfer.base.source_node_id, transfer.base.transfer_id, encoded);
}

void processRpcRx(NodeApp& app)
{
    for (;;)
    {
        void* const datagramBuffer = std::malloc(kRxDatagramCapacity);
        if (datagramBuffer == nullptr)
        {
            return;
        }
        size_t        payloadSize = kRxDatagramCapacity;
        const int16_t rxRc        = udpRxReceive(&app.rxSocket, &payloadSize, datagramBuffer);
        if (rxRc == 0)
        {
            std::free(datagramBuffer);
            return;
        }
        if (rxRc < 0)
        {
            std::free(datagramBuffer);
            if ((rxRc != -EAGAIN) && (rxRc != -EWOULDBLOCK))
            {
                std::fprintf(stderr,
                             "[%s] udpRxReceive failed: %d\n",
                             app.options.name.c_str(),
                             static_cast<int>(rxRc));
            }
            return;
        }

        UdpardRxRPCTransfer     transfer   = {};
        UdpardRxRPCPort*        outPort    = nullptr;
        const UdpardMicrosecond now        = getMonotonicMicroseconds();
        const int_fast8_t       dispatchRc = udpardRxRPCDispatcherReceive(&app.rpcDispatcher,
                                                                    now,
                                                                          {.size = payloadSize, .data = datagramBuffer},
                                                                    0U,
                                                                    &outPort,
                                                                    &transfer);
        if (dispatchRc < 0)
        {
            std::fprintf(stderr,
                         "[%s] udpardRxRPCDispatcherReceive failed: %ld\n",
                         app.options.name.c_str(),
                         static_cast<long>(dispatchRc));
            continue;
        }
        if (dispatchRc == 0)
        {
            continue;
        }

        if (outPort == &app.registerListPort)
        {
            handleRegisterListRequest(app, transfer, now);
        }
        else if (outPort == &app.registerAccessPort)
        {
            handleRegisterAccessRequest(app, transfer, now);
        }

        udpardRxFragmentFree(transfer.base.payload, app.rxMemory.fragment, app.rxMemory.payload);
    }
}

void publishHeartbeat(NodeApp& app, const UdpardMicrosecond now)
{
    auto* const counterEntry = findRegister(app, "demo.counter");
    if (counterEntry != nullptr)
    {
        counterEntry->natural32 = app.heartbeatCounter++;
    }

    uavcan::node::Heartbeat heartbeat     = {};
    const uint64_t          uptimeSeconds = (now > app.startedAt) ? ((now - app.startedAt) / 1000000ULL) : 0ULL;
    heartbeat.uptime                      = static_cast<uint32_t>(std::min<uint64_t>(uptimeSeconds, UINT32_MAX));
    heartbeat.health.value                = uavcan::node::Health::NOMINAL;
    heartbeat.mode.value                  = uavcan::node::Mode::OPERATIONAL;
    heartbeat.vendor_specific_status_code = static_cast<uint8_t>(app.heartbeatCounter & 0xFFU);

    std::vector<uint8_t> encoded = {};
    if (!serializeMessage(heartbeat, encoded))
    {
        std::fprintf(stderr, "[%s] heartbeat serialization failed\n", app.options.name.c_str());
        return;
    }

    const int32_t enqueueRc = udpardTxPublish(&app.tx,
                                              now + kTxDeadlineUsec,
                                              UdpardPriorityNominal,
                                              kSubjectHeartbeat,
                                              app.heartbeatTransferId++,
                                              {.size = encoded.size(), .data = encoded.data()},
                                              nullptr);
    if (enqueueRc < 0)
    {
        std::fprintf(stderr,
                     "[%s] heartbeat enqueue failed: %ld\n",
                     app.options.name.c_str(),
                     static_cast<long>(enqueueRc));
    }
}

bool initialize(NodeApp& app)
{
    app.localIfaceAddress = udpParseIfaceAddress(app.options.ifaceAddress.c_str());
    if (app.localIfaceAddress == 0U)
    {
        std::fprintf(stderr,
                     "[%s] invalid --iface value: %s\n",
                     app.options.name.c_str(),
                     app.options.ifaceAddress.c_str());
        return false;
    }

    const UdpardMemoryResource resource = {.user_reference = nullptr,
                                           .deallocate     = heapDeallocate,
                                           .allocate       = heapAllocate};
    app.txMemory                        = {.fragment = resource, .payload = resource};
    app.rxMemory                        = {.session  = resource,
                                           .fragment = resource,
                                           .payload  = {.user_reference = nullptr, .deallocate = heapDeallocate}};

    const int_fast8_t txInitRc = udpardTxInit(&app.tx, &app.options.nodeId, kTxQueueCapacity, app.txMemory);
    if (txInitRc < 0)
    {
        std::fprintf(stderr, "[%s] udpardTxInit failed: %ld\n", app.options.name.c_str(), static_cast<long>(txInitRc));
        return false;
    }

    const int_fast8_t dispatcherInitRc = udpardRxRPCDispatcherInit(&app.rpcDispatcher, app.rxMemory);
    if (dispatcherInitRc < 0)
    {
        std::fprintf(stderr,
                     "[%s] udpardRxRPCDispatcherInit failed: %ld\n",
                     app.options.name.c_str(),
                     static_cast<long>(dispatcherInitRc));
        return false;
    }

    const int_fast8_t dispatcherStartRc =
        udpardRxRPCDispatcherStart(&app.rpcDispatcher, app.options.nodeId, &app.rpcEndpoint);
    if (dispatcherStartRc < 0)
    {
        std::fprintf(stderr,
                     "[%s] udpardRxRPCDispatcherStart failed: %ld\n",
                     app.options.name.c_str(),
                     static_cast<long>(dispatcherStartRc));
        return false;
    }

    const int_fast8_t accessListenRc = udpardRxRPCDispatcherListen(&app.rpcDispatcher,
                                                                   &app.registerAccessPort,
                                                                   kServiceRegisterAccess,
                                                                   true,
                                                                   uavcan::register_::Access_Request::EXTENT_BYTES);
    if (accessListenRc < 0)
    {
        std::fprintf(stderr,
                     "[%s] register.Access listen failed: %ld\n",
                     app.options.name.c_str(),
                     static_cast<long>(accessListenRc));
        return false;
    }

    const int_fast8_t listListenRc = udpardRxRPCDispatcherListen(&app.rpcDispatcher,
                                                                 &app.registerListPort,
                                                                 kServiceRegisterList,
                                                                 true,
                                                                 uavcan::register_::List_Request::EXTENT_BYTES);
    if (listListenRc < 0)
    {
        std::fprintf(stderr,
                     "[%s] register.List listen failed: %ld\n",
                     app.options.name.c_str(),
                     static_cast<long>(listListenRc));
        return false;
    }

    const int16_t txSocketRc = udpTxInit(&app.txSocket, app.localIfaceAddress);
    if (txSocketRc < 0)
    {
        std::fprintf(stderr, "[%s] udpTxInit failed: %d\n", app.options.name.c_str(), static_cast<int>(txSocketRc));
        return false;
    }

    const int16_t rxSocketRc =
        udpRxInit(&app.rxSocket, app.localIfaceAddress, app.rpcEndpoint.ip_address, app.rpcEndpoint.udp_port);
    if (rxSocketRc < 0)
    {
        std::fprintf(stderr, "[%s] udpRxInit failed: %d\n", app.options.name.c_str(), static_cast<int>(rxSocketRc));
        return false;
    }

    initializeRegisters(app);
    updateHeartbeatPeriodFromRegisters(app);
    app.startedAt       = getMonotonicMicroseconds();
    app.nextHeartbeatAt = app.startedAt + app.heartbeatPeriodUsec;

    std::fprintf(stderr,
                 "[%s] started node_id=%u iface=%s heartbeat_hz=%u rpc_group=0x%08x:%u\n",
                 app.options.name.c_str(),
                 static_cast<unsigned>(app.options.nodeId),
                 app.options.ifaceAddress.c_str(),
                 app.options.heartbeatRateHz,
                 static_cast<unsigned>(app.rpcEndpoint.ip_address),
                 static_cast<unsigned>(app.rpcEndpoint.udp_port));
    return true;
}

void shutdown(NodeApp& app)
{
    udpardRxRPCDispatcherCancel(&app.rpcDispatcher, kServiceRegisterAccess, true);
    udpardRxRPCDispatcherCancel(&app.rpcDispatcher, kServiceRegisterList, true);
    udpRxClose(&app.rxSocket);
    udpTxClose(&app.txSocket);
    drainTxQueue(app);
}

}  // namespace

int main(int argc, char** argv)
{
    Options options = {};
    switch (parseOptions(argc, argv, options))
    {
    case ParseResult::Help:
        return 0;
    case ParseResult::Error:
        printUsage(argv[0]);
        return 1;
    case ParseResult::Success:
        break;
    }

    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);

    NodeApp app = {};
    app.options = options;
    if (!initialize(app))
    {
        shutdown(app);
        return 1;
    }

    while (gStopRequested == 0)
    {
        const UdpardMicrosecond now = getMonotonicMicroseconds();
        if (now >= app.nextHeartbeatAt)
        {
            publishHeartbeat(app, now);
            do
            {
                app.nextHeartbeatAt += app.heartbeatPeriodUsec;
            } while (app.nextHeartbeatAt <= now);
        }

        pumpTx(app, now);

        const UdpardMicrosecond nowAfterTx = getMonotonicMicroseconds();
        const UdpardMicrosecond untilNextHeartbeat =
            (app.nextHeartbeatAt > nowAfterTx) ? (app.nextHeartbeatAt - nowAfterTx) : app.heartbeatPeriodUsec;
        const UdpardMicrosecond timeoutUsec = std::min<UdpardMicrosecond>(untilNextHeartbeat, 50000ULL);

        UDPTxAwaitable txAwait = {.handle = &app.txSocket, .ready = false, .user_reference = nullptr};
        UDPRxAwaitable rxAwait = {.handle = &app.rxSocket, .ready = false, .user_reference = nullptr};
        const int16_t  waitRc  = udpWait(timeoutUsec, 1U, &txAwait, 1U, &rxAwait);
        if (waitRc < 0)
        {
            if (waitRc == -EINTR)
            {
                continue;
            }
            std::fprintf(stderr, "[%s] udpWait failed: %d\n", app.options.name.c_str(), static_cast<int>(waitRc));
            break;
        }

        if (rxAwait.ready)
        {
            processRpcRx(app);
        }
        if (txAwait.ready)
        {
            pumpTx(app, getMonotonicMicroseconds());
        }
    }

    shutdown(app);
    std::fprintf(stderr, "[%s] stopped\n", app.options.name.c_str());
    return 0;
}
