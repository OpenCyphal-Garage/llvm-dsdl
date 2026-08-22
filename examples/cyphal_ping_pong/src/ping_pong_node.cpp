//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Cyphal/UDP ping-pong demo node using libudpard and generated C++ types.
///
/// The executable can be launched as two peers with different node IDs; each
/// peer periodically sends PingPong requests and responds to incoming requests.
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
#include <unordered_map>
#include <vector>

extern "C"
{
#include "udpard.h"
#include "udp_posix.h"
}

#include "dsdl/demo/ping/PingPong_1_0.hpp"

namespace
{

using PingPongRequest  = dsdl::demo::ping::PingPong_Request;
using PingPongResponse = dsdl::demo::ping::PingPong_Response;

constexpr size_t            kTxQueueCapacity    = 64U;
constexpr size_t            kRxDatagramCapacity = 2048U;
constexpr UdpardMicrosecond kTxDeadlineUsec     = 1000000ULL;
constexpr UdpardMicrosecond kDefaultPeriodUsec  = 500000ULL;

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
    std::string       name         = "node";
    UdpardNodeID      nodeId       = UDPARD_NODE_ID_UNSET;
    UdpardNodeID      peerNodeId   = UDPARD_NODE_ID_UNSET;
    UdpardPortID      serviceId    = 300U;
    std::string       ifaceAddress = "127.0.0.1";
    UdpardMicrosecond periodUsec   = kDefaultPeriodUsec;
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
                 "  --name <label>           Node label for log output (default: node)\n"
                 "  --node-id <n>            Local node-ID [0, %u]\n"
                 "  --peer-node-id <n>       Peer node-ID [0, %u]\n"
                 "  --service-id <n>         Service-ID [0, %u] (default: 300)\n"
                 "  --iface <ipv4>           Local iface IPv4 address (default: 127.0.0.1)\n"
                 "  --period-ms <n>          Ping period in milliseconds (default: 500)\n"
                 "  --help                   Show this help\n",
                 programName,
                 static_cast<unsigned>(UDPARD_NODE_ID_MAX),
                 static_cast<unsigned>(UDPARD_NODE_ID_MAX),
                 static_cast<unsigned>(UDPARD_SERVICE_ID_MAX));
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
        else if (arg == "--peer-node-id")
        {
            unsigned long long parsed = 0ULL;
            if (!parseUnsigned(value, parsed) || (parsed > UDPARD_NODE_ID_MAX))
            {
                std::fprintf(stderr, "Invalid --peer-node-id: %s\n", value);
                return ParseResult::Error;
            }
            out.peerNodeId = static_cast<UdpardNodeID>(parsed);
        }
        else if (arg == "--service-id")
        {
            unsigned long long parsed = 0ULL;
            if (!parseUnsigned(value, parsed) || (parsed > UDPARD_SERVICE_ID_MAX))
            {
                std::fprintf(stderr, "Invalid --service-id: %s\n", value);
                return ParseResult::Error;
            }
            out.serviceId = static_cast<UdpardPortID>(parsed);
        }
        else if (arg == "--iface")
        {
            out.ifaceAddress = value;
        }
        else if (arg == "--period-ms")
        {
            unsigned long long parsed = 0ULL;
            if (!parseUnsigned(value, parsed) || (parsed == 0ULL))
            {
                std::fprintf(stderr, "Invalid --period-ms: %s\n", value);
                return ParseResult::Error;
            }
            out.periodUsec = static_cast<UdpardMicrosecond>(parsed * 1000ULL);
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
    if (out.peerNodeId > UDPARD_NODE_ID_MAX)
    {
        std::fprintf(stderr, "--peer-node-id is required\n");
        return ParseResult::Error;
    }
    return ParseResult::Success;
}

struct NodeApp
{
    Options options;

    uint32_t localIfaceAddress = 0U;

    UdpardTxMemoryResources txMemory = {};
    UdpardRxMemoryResources rxMemory = {};

    UdpardTx              tx            = {};
    UdpardRxRPCDispatcher rpcDispatcher = {};
    UdpardRxRPCPort       requestPort   = {};
    UdpardRxRPCPort       responsePort  = {};
    UdpardUDPIPEndpoint   rpcEndpoint   = {};

    UDPTxHandle txSocket = {.fd = -1};
    UDPRxHandle rxSocket = {.fd = -1};

    UdpardTransferID requestTransferId = 0U;
    uint64_t         nextSequence      = 1U;

    UdpardMicrosecond                               nextPingAt        = 0U;
    std::unordered_map<uint64_t, UdpardMicrosecond> pendingBySequence = {};
};

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

std::vector<uint8_t> serializeRequest(const PingPongRequest& request, int8_t& outStatus)
{
    std::vector<uint8_t> bytes(PingPongRequest::SERIALIZATION_BUFFER_SIZE_BYTES);
    size_t               size = bytes.size();
    outStatus                 = request.serialize(bytes.data(), &size);
    bytes.resize(size);
    return bytes;
}

std::vector<uint8_t> serializeResponse(const PingPongResponse& response, int8_t& outStatus)
{
    std::vector<uint8_t> bytes(PingPongResponse::SERIALIZATION_BUFFER_SIZE_BYTES);
    size_t               size = bytes.size();
    outStatus                 = response.serialize(bytes.data(), &size);
    bytes.resize(size);
    return bytes;
}

bool gatherTransferPayload(const UdpardRxTransfer& transfer, std::vector<uint8_t>& out)
{
    out.resize(transfer.payload_size);
    if (transfer.payload_size == 0U)
    {
        return true;
    }
    const size_t gathered = udpardGather(transfer.payload, out.size(), out.data());
    return gathered == out.size();
}

bool enqueueRequest(NodeApp& app, const UdpardMicrosecond now)
{
    PingPongRequest request = {};
    request.sequence        = app.nextSequence++;
    request.sent_usec       = now;

    int8_t               status = 0;
    std::vector<uint8_t> data   = serializeRequest(request, status);
    if (status < 0)
    {
        std::fprintf(stderr, "[%s] request serialize failed: %d\n", app.options.name.c_str(), static_cast<int>(status));
        return false;
    }

    const int32_t enqueueRc = udpardTxRequest(&app.tx,
                                              now + kTxDeadlineUsec,
                                              UdpardPriorityNominal,
                                              app.options.serviceId,
                                              app.options.peerNodeId,
                                              app.requestTransferId++,
                                              {.size = data.size(), .data = data.data()},
                                              nullptr);
    if (enqueueRc < 0)
    {
        std::fprintf(stderr,
                     "[%s] request enqueue failed: %ld\n",
                     app.options.name.c_str(),
                     static_cast<long>(enqueueRc));
        return false;
    }

    app.pendingBySequence[request.sequence] = now;
    std::fprintf(stderr,
                 "[%s] tx request seq=%llu peer=%u queued_frames=%ld\n",
                 app.options.name.c_str(),
                 static_cast<unsigned long long>(request.sequence),
                 static_cast<unsigned>(app.options.peerNodeId),
                 static_cast<long>(enqueueRc));
    return true;
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
            std::fprintf(stderr, "[%s] drop expired datagram\n", app.options.name.c_str());
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

        std::fprintf(stderr,
                     "[%s] udpTxSend failed (%d), dropping datagram\n",
                     app.options.name.c_str(),
                     static_cast<int>(sendRc));
        udpardTxFree(app.tx.memory, udpardTxPop(&app.tx, item));
    }
}

void handleResponse(NodeApp& app, const UdpardRxRPCTransfer& transfer, const UdpardMicrosecond now)
{
    std::vector<uint8_t> payload = {};
    if (!gatherTransferPayload(transfer.base, payload))
    {
        std::fprintf(stderr, "[%s] failed to gather response payload\n", app.options.name.c_str());
        return;
    }

    PingPongResponse response = {};
    size_t           consumed = payload.size();
    const uint8_t*   dataPtr  = payload.empty() ? nullptr : payload.data();
    const int8_t     rc       = response.deserialize(dataPtr, &consumed);
    if (rc < 0)
    {
        std::fprintf(stderr, "[%s] response deserialize failed: %d\n", app.options.name.c_str(), static_cast<int>(rc));
        return;
    }

    const auto pendingIt = app.pendingBySequence.find(response.sequence);
    if (pendingIt == app.pendingBySequence.end())
    {
        std::fprintf(stderr,
                     "[%s] rx response seq=%llu source=%u (no pending request)\n",
                     app.options.name.c_str(),
                     static_cast<unsigned long long>(response.sequence),
                     static_cast<unsigned>(transfer.base.source_node_id));
        return;
    }

    const UdpardMicrosecond sentAt = pendingIt->second;
    app.pendingBySequence.erase(pendingIt);
    const UdpardMicrosecond rtt = (now > sentAt) ? (now - sentAt) : 0U;
    std::fprintf(stderr,
                 "[%s] rx response seq=%llu source=%u rtt=%.3f ms\n",
                 app.options.name.c_str(),
                 static_cast<unsigned long long>(response.sequence),
                 static_cast<unsigned>(transfer.base.source_node_id),
                 static_cast<double>(rtt) / 1000.0);
}

void handleRequest(NodeApp& app, const UdpardRxRPCTransfer& transfer, const UdpardMicrosecond now)
{
    std::vector<uint8_t> payload = {};
    if (!gatherTransferPayload(transfer.base, payload))
    {
        std::fprintf(stderr, "[%s] failed to gather request payload\n", app.options.name.c_str());
        return;
    }

    PingPongRequest request  = {};
    size_t          consumed = payload.size();
    const uint8_t*  dataPtr  = payload.empty() ? nullptr : payload.data();
    const int8_t    rc       = request.deserialize(dataPtr, &consumed);
    if (rc < 0)
    {
        std::fprintf(stderr, "[%s] request deserialize failed: %d\n", app.options.name.c_str(), static_cast<int>(rc));
        return;
    }

    std::fprintf(stderr,
                 "[%s] rx request seq=%llu source=%u\n",
                 app.options.name.c_str(),
                 static_cast<unsigned long long>(request.sequence),
                 static_cast<unsigned>(transfer.base.source_node_id));

    PingPongResponse response = {};
    response.sequence         = request.sequence;
    response.echoed_sent_usec = request.sent_usec;
    response.responder_usec   = now;

    int8_t               serStatus = 0;
    std::vector<uint8_t> data      = serializeResponse(response, serStatus);
    if (serStatus < 0)
    {
        std::fprintf(stderr,
                     "[%s] response serialize failed: %d\n",
                     app.options.name.c_str(),
                     static_cast<int>(serStatus));
        return;
    }

    const int32_t enqueueRc = udpardTxRespond(&app.tx,
                                              now + kTxDeadlineUsec,
                                              UdpardPriorityNominal,
                                              transfer.service_id,
                                              transfer.base.source_node_id,
                                              transfer.base.transfer_id,
                                              {.size = data.size(), .data = data.data()},
                                              nullptr);
    if (enqueueRc < 0)
    {
        std::fprintf(stderr,
                     "[%s] response enqueue failed: %ld\n",
                     app.options.name.c_str(),
                     static_cast<long>(enqueueRc));
    }
}

void processRx(NodeApp& app)
{
    for (;;)
    {
        void* const datagramBuffer = std::malloc(kRxDatagramCapacity);
        if (datagramBuffer == nullptr)
        {
            std::fprintf(stderr, "[%s] out of memory allocating RX datagram buffer\n", app.options.name.c_str());
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

        UdpardRxRPCTransfer transfer   = {};
        UdpardRxRPCPort*    outPort    = nullptr;
        const auto          now        = getMonotonicMicroseconds();
        const int_fast8_t   dispatchRc = udpardRxRPCDispatcherReceive(&app.rpcDispatcher,
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

        if (outPort == &app.requestPort)
        {
            handleRequest(app, transfer, now);
        }
        else if (outPort == &app.responsePort)
        {
            handleResponse(app, transfer, now);
        }
        else
        {
            std::fprintf(stderr, "[%s] dispatcher returned unknown RPC port\n", app.options.name.c_str());
        }

        udpardRxFragmentFree(transfer.base.payload, app.rxMemory.fragment, app.rxMemory.payload);
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

    const int_fast8_t listenRequestRc = udpardRxRPCDispatcherListen(&app.rpcDispatcher,
                                                                    &app.requestPort,
                                                                    app.options.serviceId,
                                                                    true,
                                                                    PingPongRequest::EXTENT_BYTES);
    if (listenRequestRc < 0)
    {
        std::fprintf(stderr,
                     "[%s] listen request failed: %ld\n",
                     app.options.name.c_str(),
                     static_cast<long>(listenRequestRc));
        return false;
    }

    const int_fast8_t listenResponseRc = udpardRxRPCDispatcherListen(&app.rpcDispatcher,
                                                                     &app.responsePort,
                                                                     app.options.serviceId,
                                                                     false,
                                                                     PingPongResponse::EXTENT_BYTES);
    if (listenResponseRc < 0)
    {
        std::fprintf(stderr,
                     "[%s] listen response failed: %ld\n",
                     app.options.name.c_str(),
                     static_cast<long>(listenResponseRc));
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

    app.nextPingAt = getMonotonicMicroseconds() + app.options.periodUsec;
    std::fprintf(stderr,
                 "[%s] started node_id=%u peer=%u service=%u iface=%s rpc_group=0x%08x:%u period_ms=%llu\n",
                 app.options.name.c_str(),
                 static_cast<unsigned>(app.options.nodeId),
                 static_cast<unsigned>(app.options.peerNodeId),
                 static_cast<unsigned>(app.options.serviceId),
                 app.options.ifaceAddress.c_str(),
                 static_cast<unsigned>(app.rpcEndpoint.ip_address),
                 static_cast<unsigned>(app.rpcEndpoint.udp_port),
                 static_cast<unsigned long long>(app.options.periodUsec / 1000ULL));
    return true;
}

void shutdown(NodeApp& app)
{
    udpardRxRPCDispatcherCancel(&app.rpcDispatcher, app.options.serviceId, true);
    udpardRxRPCDispatcherCancel(&app.rpcDispatcher, app.options.serviceId, false);
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
        if (now >= app.nextPingAt)
        {
            (void) enqueueRequest(app, now);
            do
            {
                app.nextPingAt += app.options.periodUsec;
            } while (app.nextPingAt <= now);
        }

        pumpTx(app, now);

        const UdpardMicrosecond nowAfterTx = getMonotonicMicroseconds();
        const UdpardMicrosecond untilNextPing =
            (app.nextPingAt > nowAfterTx) ? (app.nextPingAt - nowAfterTx) : app.options.periodUsec;
        const UdpardMicrosecond timeoutUsec = std::min<UdpardMicrosecond>(untilNextPing, 50000ULL);

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
            processRx(app);
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
