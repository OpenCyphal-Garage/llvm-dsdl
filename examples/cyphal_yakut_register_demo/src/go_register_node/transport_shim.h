//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// C transport shim used by the Go Yakut register demo node.
///
/// This shim wraps libudpard and the demo POSIX UDP adapter to provide a
/// compact C API that can be called from cgo.
///
//===----------------------------------------------------------------------===//

#ifndef LLVMDSDL_EXAMPLES_CYPHAL_YAKUT_REGISTER_DEMO_GO_TRANSPORT_SHIM_H
#define LLVMDSDL_EXAMPLES_CYPHAL_YAKUT_REGISTER_DEMO_GO_TRANSPORT_SHIM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Opaque handle for demo node transport state.
    typedef struct GoDemoNode GoDemoNode;

    /// @brief RPC transfer payload returned by @ref go_demo_node_poll_rpc.
    typedef struct
    {
        /// @brief Service identifier of the received RPC.
        uint16_t service_id;
        /// @brief Source node-id that issued the request.
        uint16_t source_node_id;
        /// @brief Transfer-ID associated with this RPC exchange.
        uint8_t  transfer_id;
        /// @brief Pointer to payload bytes owned by the transport layer.
        uint8_t* payload;
        /// @brief Payload size in bytes.
        size_t   payload_size;
    } GoDemoRpcTransfer;

    /// @brief Allocates an uninitialized demo node transport object.
    /// @return Newly allocated node handle or `NULL` on allocation failure.
    GoDemoNode* go_demo_node_create(void);

    /// @brief Releases a node created by @ref go_demo_node_create.
    /// @param[in] node Node handle (nullable).
    void go_demo_node_destroy(GoDemoNode* node);

    /// @brief Returns the maximum supported node-id for this demo transport.
    uint16_t go_demo_node_id_max(void);

    /// @brief Returns the nominal Cyphal transfer priority used by the demo.
    uint8_t go_demo_priority_nominal(void);

    /// @brief Parses an IPv4 interface address string.
    /// @param[in] address Input address string.
    /// @param[out] out_iface_address Parsed address in host byte order.
    /// @return `0` on success, negative on parse failure.
    int go_demo_parse_iface_address(const char* address, uint32_t* out_iface_address);

    /// @brief Initialises node transport and RPC subscriptions.
    /// @param[in,out] node Node handle.
    /// @param[in] node_id Local node identifier.
    /// @param[in] iface_address Bind interface address string.
    /// @param[in] register_access_service_id Service-ID for register access.
    /// @param[in] register_access_request_extent Request extent for register access.
    /// @param[in] register_list_service_id Service-ID for register list.
    /// @param[in] register_list_request_extent Request extent for register list.
    /// @param[in] tx_queue_capacity Transfer queue capacity.
    /// @param[in] rx_datagram_capacity Maximum accepted datagram size.
    /// @return `0` on success, negative on initialisation failure.
    int go_demo_node_init(GoDemoNode* node,
                          uint16_t    node_id,
                          const char* iface_address,
                          uint16_t    register_access_service_id,
                          size_t      register_access_request_extent,
                          uint16_t    register_list_service_id,
                          size_t      register_list_request_extent,
                          uint32_t    tx_queue_capacity,
                          size_t      rx_datagram_capacity);

    /// @brief Returns the UDP endpoint used for incoming RPC traffic.
    /// @param[in] node Node handle.
    /// @param[out] out_ip_address Endpoint IPv4 address in host byte order.
    /// @param[out] out_udp_port Endpoint UDP port.
    /// @return `0` on success, negative on failure.
    int go_demo_node_get_rpc_endpoint(const GoDemoNode* node, uint32_t* out_ip_address, uint16_t* out_udp_port);

    /// @brief Enqueues a Cyphal message publication.
    /// @param[in,out] node Node handle.
    /// @param[in] subject_id Subject-ID to publish on.
    /// @param[in] transfer_id Transfer-ID to use.
    /// @param[in] payload Serialised payload bytes.
    /// @param[in] payload_size Payload size in bytes.
    /// @param[in] deadline_usec Absolute transmit deadline in microseconds.
    /// @param[in] priority Cyphal transfer priority.
    /// @return `0` on success, negative when enqueue fails.
    int go_demo_node_publish(GoDemoNode*     node,
                             uint16_t        subject_id,
                             uint8_t         transfer_id,
                             const uint8_t*  payload,
                             size_t          payload_size,
                             uint64_t        deadline_usec,
                             uint8_t         priority);

    /// @brief Enqueues a Cyphal service response transfer.
    /// @param[in,out] node Node handle.
    /// @param[in] service_id Service-ID being responded to.
    /// @param[in] destination_node_id Requesting node-id.
    /// @param[in] transfer_id Transfer-ID to echo/advance.
    /// @param[in] payload Serialised response payload bytes.
    /// @param[in] payload_size Payload size in bytes.
    /// @param[in] deadline_usec Absolute transmit deadline in microseconds.
    /// @param[in] priority Cyphal transfer priority.
    /// @return `0` on success, negative when enqueue fails.
    int go_demo_node_respond(GoDemoNode*     node,
                             uint16_t        service_id,
                             uint16_t        destination_node_id,
                             uint8_t         transfer_id,
                             const uint8_t*  payload,
                             size_t          payload_size,
                             uint64_t        deadline_usec,
                             uint8_t         priority);

    /// @brief Polls for the next incoming RPC transfer.
    /// @param[in,out] node Node handle.
    /// @param[in] timeout_usec Poll timeout in microseconds.
    /// @param[out] out_transfer Populated transfer descriptor on success.
    /// @return `0` when a transfer is returned, negative on timeout/error.
    int go_demo_node_poll_rpc(GoDemoNode* node, uint64_t timeout_usec, GoDemoRpcTransfer* out_transfer);

    /// @brief Releases payload ownership for a transfer from @ref go_demo_node_poll_rpc.
    /// @param[in,out] transfer Transfer descriptor to release/reset.
    void go_demo_node_release_transfer(GoDemoRpcTransfer* transfer);

    /// @brief Flushes queued transfers that are ready for transmission.
    /// @param[in,out] node Node handle.
    void go_demo_node_pump_tx(GoDemoNode* node);

    /// @brief Returns current monotonic time in microseconds.
    uint64_t go_demo_now_usec(void);

    /// @brief Stops network activity for the node and releases transport resources.
    /// @param[in,out] node Node handle.
    /// @return `0` on success, negative on shutdown failure.
    int go_demo_node_shutdown(GoDemoNode* node);

#ifdef __cplusplus
}
#endif

#endif  // LLVMDSDL_EXAMPLES_CYPHAL_YAKUT_REGISTER_DEMO_GO_TRANSPORT_SHIM_H
