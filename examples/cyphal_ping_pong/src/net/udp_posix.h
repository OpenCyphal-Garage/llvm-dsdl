//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// POSIX UDP networking shim for libudpard demo applications.
///
/// The API isolates Linux/macOS socket details (multicast join, non-blocking IO,
/// and polling) from application logic so the node code can stay transport-focused.
///
//===----------------------------------------------------------------------===//

#ifndef LLVMDSDL_EXAMPLES_CYPHAL_PING_PONG_UDP_POSIX_H
#define LLVMDSDL_EXAMPLES_CYPHAL_PING_PONG_UDP_POSIX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Transmit socket handle.
    typedef struct
    {
        int fd;
    } UDPTxHandle;

    /// @brief Receive socket handle.
    typedef struct
    {
        int fd;
    } UDPRxHandle;

    /// @brief Initialises a transmit socket for multicast Cyphal/UDP traffic.
    /// @param[out] self Transmit handle to initialise.
    /// @param[in] local_iface_address Local interface IPv4 address in host byte order.
    /// @return `0` on success, otherwise a negated `errno` value.
    int16_t udpTxInit(UDPTxHandle* self, uint32_t local_iface_address);

    /// @brief Sends one datagram to a remote endpoint.
    /// @param[in,out] self Initialised transmit socket handle.
    /// @param[in] remote_address Destination IPv4 address in host byte order.
    /// @param[in] remote_port Destination UDP port in host byte order.
    /// @param[in] dscp DSCP value in range [0, 63].
    /// @param[in] payload_size Payload size in bytes.
    /// @param[in] payload Payload pointer.
    /// @return `1` if sent, `0` if socket is not writable, otherwise a negated `errno`.
    int16_t udpTxSend(UDPTxHandle* self,
                      uint32_t     remote_address,
                      uint16_t     remote_port,
                      uint8_t      dscp,
                      size_t       payload_size,
                      const void*  payload);

    /// @brief Closes a transmit socket handle.
    /// @param[in,out] self Handle to close. `NULL` is ignored.
    void udpTxClose(UDPTxHandle* self);

    /// @brief Initialises a receive socket for a multicast endpoint.
    /// @param[out] self Receive handle to initialise.
    /// @param[in] local_iface_address Local interface IPv4 address in host byte order.
    /// @param[in] multicast_group Multicast group IPv4 address in host byte order.
    /// @param[in] remote_port UDP port in host byte order.
    /// @return `0` on success, otherwise a negated `errno` value.
    int16_t udpRxInit(UDPRxHandle* self, uint32_t local_iface_address, uint32_t multicast_group, uint16_t remote_port);

    /// @brief Receives one datagram without blocking.
    /// @param[in,out] self Initialised receive socket handle.
    /// @param[in,out] inout_payload_size On input, destination capacity; on output, bytes read.
    /// @param[out] out_payload Destination buffer.
    /// @return `1` if one datagram was received, `0` if socket is not readable, otherwise a negated `errno`.
    int16_t udpRxReceive(UDPRxHandle* self, size_t* inout_payload_size, void* out_payload);

    /// @brief Closes a receive socket handle.
    /// @param[in,out] self Handle to close. `NULL` is ignored.
    void udpRxClose(UDPRxHandle* self);

    /// @brief Awaitable transmit socket for `udpWait`.
    typedef struct
    {
        UDPTxHandle* handle;
        bool         ready;
        void*        user_reference;
    } UDPTxAwaitable;

    /// @brief Awaitable receive socket for `udpWait`.
    typedef struct
    {
        UDPRxHandle* handle;
        bool         ready;
        void*        user_reference;
    } UDPRxAwaitable;

    /// @brief Waits until timeout expiry or any socket in the set becomes ready.
    /// @param[in] timeout_usec Timeout in microseconds.
    /// @param[in] tx_count Number of TX awaitables.
    /// @param[in,out] tx TX awaitables.
    /// @param[in] rx_count Number of RX awaitables.
    /// @param[in,out] rx RX awaitables.
    /// @return `0` on success, otherwise a negated `errno` value.
    int16_t udpWait(uint64_t timeout_usec, size_t tx_count, UDPTxAwaitable* tx, size_t rx_count, UDPRxAwaitable* rx);

    /// @brief Parses a dotted-quad IPv4 interface address into host byte order.
    /// @param[in] address Interface address string (for example `127.0.0.1`).
    /// @return Parsed IPv4 address in host byte order, or `0` on parse failure.
    uint32_t udpParseIfaceAddress(const char* address);

#ifdef __cplusplus
}
#endif

#endif  // LLVMDSDL_EXAMPLES_CYPHAL_PING_PONG_UDP_POSIX_H
