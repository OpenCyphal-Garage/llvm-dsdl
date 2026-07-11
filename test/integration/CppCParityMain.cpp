//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Differential parity harness comparing generated C++ and C bindings.
///
/// The test executes randomized roundtrips and checks deserialize/serialize
/// return codes, consumed sizes, and serialized byte equivalence.
///
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

extern "C"
{
#include "uavcan/metatransport/can/Frame_0_2.h"
#include "uavcan/node/ExecuteCommand_1_3.h"
#include "uavcan/node/Heartbeat_1_0.h"
#include "uavcan/node/Health_1_0.h"
#include "uavcan/node/port/List_1_0.h"
#include "uavcan/primitive/scalar/Integer8_1_0.h"
#include "uavcan/primitive/scalar/Real32_1_0.h"
#include "uavcan/time/SynchronizedTimestamp_1_0.h"
}

#include "uavcan/metatransport/can/Frame_0_2.hpp"
#include "uavcan/node/ExecuteCommand_1_3.hpp"
#include "uavcan/node/Heartbeat_1_0.hpp"
#include "uavcan/node/Health_1_0.hpp"
#include "uavcan/node/port/List_1_0.hpp"
#include "uavcan/primitive/scalar/Integer8_1_0.hpp"
#include "uavcan/primitive/scalar/Real32_1_0.hpp"
#include "uavcan/time/SynchronizedTimestamp_1_0.hpp"

namespace
{

std::uint64_t gRngState = UINT64_C(0xD6E8FEB86659FD93);

std::uint32_t nextRandomU32()
{
    gRngState ^= gRngState << 13U;
    gRngState ^= gRngState >> 7U;
    gRngState ^= gRngState << 17U;
    return static_cast<std::uint32_t>(gRngState & UINT64_C(0xFFFFFFFF));
}

void fillRandomBytes(std::uint8_t* const dst, const std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i)
    {
        dst[i] = static_cast<std::uint8_t>(nextRandomU32() & UINT32_C(0xFF));
    }
}

void dumpBytes(const char* const label, const std::uint8_t* const data, const std::size_t size)
{
    std::fprintf(stderr, "%s (%zu):", label, size);
    for (std::size_t i = 0; i < size; ++i)
    {
        std::fprintf(stderr, " %02X", data[i]);
    }
    std::fprintf(stderr, "\n");
}

template <typename CObj, typename CppObj>
int runCase(const char* const name,
            const std::size_t iterations,
            const std::size_t cMaxSize,
            const std::size_t cppMaxSize,
            std::int8_t (*cDeserialize)(CObj*, const std::uint8_t*, std::size_t*),
            std::int8_t (*cSerialize)(const CObj*, std::uint8_t*, std::size_t*),
            std::int8_t (*cppDeserialize)(CppObj*, const std::uint8_t*, std::size_t*),
            std::int8_t (*cppSerialize)(const CppObj*, std::uint8_t*, std::size_t*),
            const bool compareBytes = true)
{
    static_assert(std::is_trivially_copyable<CObj>::value, "C object must be trivially copyable");

    constexpr std::size_t kMaxIoBuffer = 2048U;
    std::uint8_t          input[kMaxIoBuffer];
    std::uint8_t          cOutput[kMaxIoBuffer];
    std::uint8_t          cppOutput[kMaxIoBuffer];
    const std::size_t     maxSerialized = (cMaxSize > cppMaxSize) ? cMaxSize : cppMaxSize;
    const std::size_t     maxInput      = maxSerialized + 16U;

    if (maxInput > kMaxIoBuffer || maxSerialized > kMaxIoBuffer)
    {
        std::fprintf(stderr, "Case %s exceeds harness static buffer sizing\n", name);
        return 1;
    }

    for (std::size_t iter = 0; iter < iterations; ++iter)
    {
        const std::size_t inputSize =
            static_cast<std::size_t>(nextRandomU32() % static_cast<std::uint32_t>(maxInput + 1U));
        fillRandomBytes(input, inputSize);

        CObj cObj{};
        std::memset(&cObj, 0, sizeof(cObj));
        CppObj cppObj{};

        std::size_t       cConsumed    = inputSize;
        std::size_t       cppConsumed  = inputSize;
        const std::int8_t cDesResult   = cDeserialize(&cObj, input, &cConsumed);
        const std::int8_t cppDesResult = cppDeserialize(&cppObj, input, &cppConsumed);
        if ((cDesResult != cppDesResult) || (cConsumed != cppConsumed))
        {
            std::fprintf(stderr,
                         "Deserialize mismatch in %s iter=%zu input_size=%zu C(rc=%d,consumed=%zu) "
                         "C++(rc=%d,consumed=%zu)\n",
                         name,
                         iter,
                         inputSize,
                         static_cast<int>(cDesResult),
                         cConsumed,
                         static_cast<int>(cppDesResult),
                         cppConsumed);
            dumpBytes("input", input, inputSize);
            return 1;
        }

        if (cDesResult < 0)
        {
            continue;
        }

        std::memset(cOutput, 0xA5, sizeof(cOutput));
        std::memset(cppOutput, 0xA5, sizeof(cppOutput));
        std::size_t       cSize        = maxSerialized;
        std::size_t       cppSize      = maxSerialized;
        const std::int8_t cSerResult   = cSerialize(&cObj, cOutput, &cSize);
        const std::int8_t cppSerResult = cppSerialize(&cppObj, cppOutput, &cppSize);
        const bool byteMismatch = compareBytes && (cSize == cppSize) && (std::memcmp(cOutput, cppOutput, cSize) != 0);
        if ((cSerResult != cppSerResult) || (cSize != cppSize) || byteMismatch)
        {
            std::fprintf(stderr,
                         "Serialize mismatch in %s iter=%zu C(rc=%d,size=%zu) C++(rc=%d,size=%zu)\n",
                         name,
                         iter,
                         static_cast<int>(cSerResult),
                         cSize,
                         static_cast<int>(cppSerResult),
                         cppSize);
            dumpBytes("input", input, inputSize);
            dumpBytes("c", cOutput, cSize);
            dumpBytes("cpp", cppOutput, cppSize);
            return 1;
        }
    }

    std::printf("PASS %s random (%zu iterations)\n", name, iterations);
    return 0;
}

std::int8_t cHeartbeatDeserialize(uavcan__node__Heartbeat* outObj, const std::uint8_t* buffer, std::size_t* inoutSize);
std::int8_t cHeartbeatSerialize(const uavcan__node__Heartbeat* obj, std::uint8_t* buffer, std::size_t* inoutSize);
std::int8_t cppHeartbeatDeserialize(uavcan::node::Heartbeat* outObj,
                                    const std::uint8_t*      buffer,
                                    std::size_t*             inoutSize);
std::int8_t cppHeartbeatSerialize(const uavcan::node::Heartbeat* obj, std::uint8_t* buffer, std::size_t* inoutSize);
std::int8_t cExecuteRequestSerialize(const uavcan__node__ExecuteCommand__Request* obj,
                                     std::uint8_t*                                buffer,
                                     std::size_t*                                 inoutSize);
std::int8_t cExecuteRequestDeserialize(uavcan__node__ExecuteCommand__Request* outObj,
                                       const std::uint8_t*                    buffer,
                                       std::size_t*                           inoutSize);
std::int8_t cppExecuteRequestSerialize(const uavcan::node::ExecuteCommand_1_3__Request* obj,
                                       std::uint8_t*                                    buffer,
                                       std::size_t*                                     inoutSize);
std::int8_t cppExecuteRequestDeserialize(uavcan::node::ExecuteCommand_1_3__Request* outObj,
                                         const std::uint8_t*                        buffer,
                                         std::size_t*                               inoutSize);
std::int8_t cExecuteResponseDeserialize(uavcan__node__ExecuteCommand__Response* outObj,
                                        const std::uint8_t*                     buffer,
                                        std::size_t*                            inoutSize);
std::int8_t cExecuteResponseSerialize(const uavcan__node__ExecuteCommand__Response* obj,
                                      std::uint8_t*                                 buffer,
                                      std::size_t*                                  inoutSize);
std::int8_t cppExecuteResponseDeserialize(uavcan::node::ExecuteCommand_1_3__Response* outObj,
                                          const std::uint8_t*                         buffer,
                                          std::size_t*                                inoutSize);
std::int8_t cppExecuteResponseSerialize(const uavcan::node::ExecuteCommand_1_3__Response* obj,
                                        std::uint8_t*                                     buffer,
                                        std::size_t*                                      inoutSize);
std::int8_t cFrameDeserialize(uavcan__metatransport__can__Frame* outObj,
                              const std::uint8_t*                buffer,
                              std::size_t*                       inoutSize);
std::int8_t cFrameSerialize(const uavcan__metatransport__can__Frame* obj, std::uint8_t* buffer, std::size_t* inoutSize);
std::int8_t cppFrameDeserialize(uavcan::metatransport::can::Frame_0_2* outObj,
                                const std::uint8_t*                    buffer,
                                std::size_t*                           inoutSize);
std::int8_t cppFrameSerialize(const uavcan::metatransport::can::Frame_0_2* obj,
                              std::uint8_t*                                buffer,
                              std::size_t*                                 inoutSize);
std::int8_t cHealthDeserialize(uavcan__node__Health* outObj, const std::uint8_t* buffer, std::size_t* inoutSize);
std::int8_t cHealthSerialize(const uavcan__node__Health* obj, std::uint8_t* buffer, std::size_t* inoutSize);
std::int8_t cppHealthDeserialize(uavcan::node::Health* outObj, const std::uint8_t* buffer, std::size_t* inoutSize);
std::int8_t cppHealthSerialize(const uavcan::node::Health* obj, std::uint8_t* buffer, std::size_t* inoutSize);
std::int8_t cSynchronizedTimestampDeserialize(uavcan__time__SynchronizedTimestamp* outObj,
                                              const std::uint8_t*                  buffer,
                                              std::size_t*                         inoutSize);
std::int8_t cSynchronizedTimestampSerialize(const uavcan__time__SynchronizedTimestamp* obj,
                                            std::uint8_t*                              buffer,
                                            std::size_t*                               inoutSize);
std::int8_t cppSynchronizedTimestampDeserialize(uavcan::time::SynchronizedTimestamp* outObj,
                                                const std::uint8_t*                  buffer,
                                                std::size_t*                         inoutSize);
std::int8_t cppSynchronizedTimestampSerialize(const uavcan::time::SynchronizedTimestamp* obj,
                                              std::uint8_t*                              buffer,
                                              std::size_t*                               inoutSize);
std::int8_t cInteger8Deserialize(uavcan__primitive__scalar__Integer8* outObj,
                                 const std::uint8_t*                  buffer,
                                 std::size_t*                         inoutSize);
std::int8_t cInteger8Serialize(const uavcan__primitive__scalar__Integer8* obj,
                               std::uint8_t*                              buffer,
                               std::size_t*                               inoutSize);
std::int8_t cppInteger8Deserialize(uavcan::primitive::scalar::Integer8* outObj,
                                   const std::uint8_t*                  buffer,
                                   std::size_t*                         inoutSize);
std::int8_t cppInteger8Serialize(const uavcan::primitive::scalar::Integer8* obj,
                                 std::uint8_t*                              buffer,
                                 std::size_t*                               inoutSize);

int runDirectedErrorCases()
{
    {
        // Truncated Heartbeat deserialization should succeed via implicit zero-extension.
        const std::uint8_t      input[1] = {0x00U};
        uavcan__node__Heartbeat cObj{};
        uavcan::node::Heartbeat cppObj{};
        std::size_t             cConsumed   = 0U;
        std::size_t             cppConsumed = 0U;
        const std::int8_t       cRc         = cHeartbeatDeserialize(&cObj, input, &cConsumed);
        const std::int8_t       cppRc       = cppHeartbeatDeserialize(&cppObj, input, &cppConsumed);
        const std::int8_t       expected    = static_cast<std::int8_t>(0);
        if ((cRc != expected) || (cppRc != expected) || (cConsumed != cppConsumed) || (cConsumed != 0U))
        {
            std::fprintf(stderr,
                         "Directed mismatch (Heartbeat empty-input deserialize): "
                         "C(rc=%d,consumed=%zu) C++(rc=%d,consumed=%zu)\n",
                         static_cast<int>(cRc),
                         cConsumed,
                         static_cast<int>(cppRc),
                         cppConsumed);
            return 1;
        }
        std::printf("INFO cpp-c directed marker heartbeat_empty_deserialize\n");
    }

    {
        // Invalid union tag in Frame deserialization.
        const std::uint8_t                    input[1] = {0xFFU};
        uavcan__metatransport__can__Frame     cObj{};
        uavcan::metatransport::can::Frame_0_2 cppObj{};
        std::size_t                           cConsumed   = sizeof(input);
        std::size_t                           cppConsumed = sizeof(input);
        const std::int8_t                     cRc         = cFrameDeserialize(&cObj, input, &cConsumed);
        const std::int8_t                     cppRc       = cppFrameDeserialize(&cppObj, input, &cppConsumed);
        const std::int8_t                     expected    = static_cast<std::int8_t>(-11);
        if ((cRc != expected) || (cppRc != expected) || (cConsumed != cppConsumed))
        {
            std::fprintf(stderr,
                         "Directed mismatch (Frame bad union-tag deserialize): "
                         "C(rc=%d,consumed=%zu) C++(rc=%d,consumed=%zu)\n",
                         static_cast<int>(cRc),
                         cConsumed,
                         static_cast<int>(cppRc),
                         cppConsumed);
            return 1;
        }
        std::printf("INFO cpp-c directed marker frame_bad_union_tag_deserialize\n");
    }

    {
        // Service request mixed-path case: declared parameter length exceeds provided payload bytes.
        // Deserialization should succeed via implicit truncation/zero-extension.
        const std::uint8_t input[4] = {0x34U, 0x12U, 0x02U, 0xAAU};  // command=0x1234, parameter.count=2
        uavcan__node__ExecuteCommand__Request     cObj{};
        uavcan::node::ExecuteCommand_1_3__Request cppObj{};
        std::size_t                               cConsumed   = sizeof(input);
        std::size_t                               cppConsumed = sizeof(input);
        const std::int8_t                         cDesRc      = cExecuteRequestDeserialize(&cObj, input, &cConsumed);
        const std::int8_t                         cppDesRc = cppExecuteRequestDeserialize(&cppObj, input, &cppConsumed);
        if ((cDesRc != 0) || (cppDesRc != 0) || (cConsumed != cppConsumed))
        {
            std::fprintf(stderr,
                         "Directed mismatch (ExecuteCommand.Request truncated-payload deserialize): "
                         "C(rc=%d,consumed=%zu) C++(rc=%d,consumed=%zu)\n",
                         static_cast<int>(cDesRc),
                         cConsumed,
                         static_cast<int>(cppDesRc),
                         cppConsumed);
            return 1;
        }

        std::uint8_t cOut[300]{};
        std::uint8_t cppOut[300]{};
        std::size_t  cSize =
            static_cast<std::size_t>(uavcan__node__ExecuteCommand__Request_SERIALIZATION_BUFFER_SIZE_BYTES_);
        std::size_t       cppSize      = uavcan::node::ExecuteCommand_1_3__Request::SERIALIZATION_BUFFER_SIZE_BYTES;
        const std::int8_t cSerRc       = cExecuteRequestSerialize(&cObj, cOut, &cSize);
        const std::int8_t cppSerRc     = cppExecuteRequestSerialize(&cppObj, cppOut, &cppSize);
        const bool        byteMismatch = (cSize == cppSize) && (std::memcmp(cOut, cppOut, cSize) != 0);
        if ((cSerRc != cppSerRc) || (cSize != cppSize) || byteMismatch)
        {
            std::fprintf(stderr,
                         "Directed mismatch (ExecuteCommand.Request truncated-payload serialize): "
                         "C(rc=%d,size=%zu) C++(rc=%d,size=%zu)\n",
                         static_cast<int>(cSerRc),
                         cSize,
                         static_cast<int>(cppSerRc),
                         cppSize);
            dumpBytes("input", input, sizeof(input));
            dumpBytes("c", cOut, cSize);
            dumpBytes("cpp", cppOut, cppSize);
            return 1;
        }
        std::printf("INFO cpp-c directed marker execute_request_truncated_payload_roundtrip\n");
    }

    {
        // Service response mixed-path case: declared output length exceeds provided payload bytes.
        // This should deserialize successfully via implicit truncation/zero-extension.
        const std::uint8_t input[3] = {0x01U, 0x02U, 0xAAU};  // status=1, output.count=2, payload[0]=0xAA
        uavcan__node__ExecuteCommand__Response     cObj{};
        uavcan::node::ExecuteCommand_1_3__Response cppObj{};
        std::size_t                                cConsumed   = sizeof(input);
        std::size_t                                cppConsumed = sizeof(input);
        const std::int8_t                          cDesRc      = cExecuteResponseDeserialize(&cObj, input, &cConsumed);
        const std::int8_t cppDesRc = cppExecuteResponseDeserialize(&cppObj, input, &cppConsumed);
        if ((cDesRc != 0) || (cppDesRc != 0) || (cConsumed != cppConsumed))
        {
            std::fprintf(stderr,
                         "Directed mismatch (ExecuteCommand.Response truncated-payload deserialize): "
                         "C(rc=%d,consumed=%zu) C++(rc=%d,consumed=%zu)\n",
                         static_cast<int>(cDesRc),
                         cConsumed,
                         static_cast<int>(cppDesRc),
                         cppConsumed);
            return 1;
        }

        std::uint8_t cOut[128]{};
        std::uint8_t cppOut[128]{};
        std::size_t  cSize =
            static_cast<std::size_t>(uavcan__node__ExecuteCommand__Response_SERIALIZATION_BUFFER_SIZE_BYTES_);
        std::size_t       cppSize      = uavcan::node::ExecuteCommand_1_3__Response::SERIALIZATION_BUFFER_SIZE_BYTES;
        const std::int8_t cSerRc       = cExecuteResponseSerialize(&cObj, cOut, &cSize);
        const std::int8_t cppSerRc     = cppExecuteResponseSerialize(&cppObj, cppOut, &cppSize);
        const bool        byteMismatch = (cSize == cppSize) && (std::memcmp(cOut, cppOut, cSize) != 0);
        if ((cSerRc != cppSerRc) || (cSize != cppSize) || byteMismatch)
        {
            std::fprintf(stderr,
                         "Directed mismatch (ExecuteCommand.Response truncated-payload serialize): "
                         "C(rc=%d,size=%zu) C++(rc=%d,size=%zu)\n",
                         static_cast<int>(cSerRc),
                         cSize,
                         static_cast<int>(cppSerRc),
                         cppSize);
            dumpBytes("input", input, sizeof(input));
            dumpBytes("c", cOut, cSize);
            dumpBytes("cpp", cppOut, cppSize);
            return 1;
        }
        std::printf("INFO cpp-c directed marker execute_response_truncated_payload_roundtrip\n");
    }

    {
        // Invalid variable-array length in ExecuteCommand.Response deserialization.
        const std::uint8_t                         input[2] = {0x00U, 0xFFU};  // status=0, output.count=255
        uavcan__node__ExecuteCommand__Response     cObj{};
        uavcan::node::ExecuteCommand_1_3__Response cppObj{};
        std::size_t                                cConsumed   = sizeof(input);
        std::size_t                                cppConsumed = sizeof(input);
        const std::int8_t                          cRc         = cExecuteResponseDeserialize(&cObj, input, &cConsumed);
        const std::int8_t                          cppRc = cppExecuteResponseDeserialize(&cppObj, input, &cppConsumed);
        const std::int8_t                          expected = static_cast<std::int8_t>(-10);
        if ((cRc != expected) || (cppRc != expected) || (cConsumed != cppConsumed))
        {
            std::fprintf(stderr,
                         "Directed mismatch (ExecuteCommand.Response bad array-length deserialize): "
                         "C(rc=%d,consumed=%zu) C++(rc=%d,consumed=%zu)\n",
                         static_cast<int>(cRc),
                         cConsumed,
                         static_cast<int>(cppRc),
                         cppConsumed);
            return 1;
        }
        std::printf("INFO cpp-c directed marker execute_response_bad_array_length_deserialize\n");
    }

    {
        // Invalid delimiter header in List deserialization.
        const std::uint8_t           input[4] = {0xFFU, 0xFFU, 0xFFU, 0x7FU};
        uavcan__node__port__List     cObj{};
        uavcan::node::port::List_1_0 cppObj{};
        std::size_t                  cConsumed   = sizeof(input);
        std::size_t                  cppConsumed = sizeof(input);
        const std::int8_t            cRc         = uavcan__node__port__List__deserialize_(&cObj, input, &cConsumed);
        const std::int8_t            cppRc       = cppObj.deserialize(input, &cppConsumed);
        const std::int8_t            expected    = static_cast<std::int8_t>(-12);
        if ((cRc != expected) || (cppRc != expected) || (cConsumed != cppConsumed))
        {
            std::fprintf(stderr,
                         "Directed mismatch (List bad delimiter-header deserialize): "
                         "C(rc=%d,consumed=%zu) C++(rc=%d,consumed=%zu)\n",
                         static_cast<int>(cRc),
                         cConsumed,
                         static_cast<int>(cppRc),
                         cppConsumed);
            return 1;
        }
        std::printf("INFO cpp-c directed marker list_bad_delimiter_header_deserialize\n");
    }

    {
        // Multi-level delimiter failure: first delimiter valid (0), second delimiter invalid.
        const std::uint8_t           input[8] = {0x00U, 0x00U, 0x00U, 0x00U, 0xFFU, 0xFFU, 0xFFU, 0x7FU};
        uavcan__node__port__List     cObj{};
        uavcan::node::port::List_1_0 cppObj{};
        std::size_t                  cConsumed   = sizeof(input);
        std::size_t                  cppConsumed = sizeof(input);
        const std::int8_t            cRc         = uavcan__node__port__List__deserialize_(&cObj, input, &cConsumed);
        const std::int8_t            cppRc       = cppObj.deserialize(input, &cppConsumed);
        const std::int8_t            expected    = static_cast<std::int8_t>(-12);
        if ((cRc != expected) || (cppRc != expected) || (cConsumed != cppConsumed))
        {
            std::fprintf(stderr,
                         "Directed mismatch (List second delimiter-header deserialize): "
                         "C(rc=%d,consumed=%zu) C++(rc=%d,consumed=%zu)\n",
                         static_cast<int>(cRc),
                         cConsumed,
                         static_cast<int>(cppRc),
                         cppConsumed);
            return 1;
        }
        std::printf("INFO cpp-c directed marker list_second_delimiter_header_deserialize\n");
    }

    {
        // Nested-composite failure: List.publishers contains invalid SubjectIDList union tag.
        const std::uint8_t input[5] = {0x01U, 0x00U, 0x00U, 0x00U, 0xFFU};  // publishers delimiter=1, nested _tag_=255
        uavcan__node__port__List     cObj{};
        uavcan::node::port::List_1_0 cppObj{};
        std::size_t                  cConsumed   = sizeof(input);
        std::size_t                  cppConsumed = sizeof(input);
        const std::int8_t            cRc         = uavcan__node__port__List__deserialize_(&cObj, input, &cConsumed);
        const std::int8_t            cppRc       = cppObj.deserialize(input, &cppConsumed);
        const std::int8_t            expected    = static_cast<std::int8_t>(-11);
        if ((cRc != expected) || (cppRc != expected) || (cConsumed != cppConsumed))
        {
            std::fprintf(stderr,
                         "Directed mismatch (List nested bad union-tag deserialize): "
                         "C(rc=%d,consumed=%zu) C++(rc=%d,consumed=%zu)\n",
                         static_cast<int>(cRc),
                         cConsumed,
                         static_cast<int>(cppRc),
                         cppConsumed);
            return 1;
        }
        std::printf("INFO cpp-c directed marker list_nested_bad_union_tag_deserialize\n");
    }

    {
        // Deeper nested chain: first delimited section valid, second section has nested bad union tag.
        const std::uint8_t           input[9] = {0x00U,
                                                 0x00U,
                                                 0x00U,
                                                 0x00U,  // publishers delimiter=0
                                                 0x01U,
                                                 0x00U,
                                                 0x00U,
                                                 0x00U,   // subscribers delimiter=1
                                                 0xFFU};  // subscribers nested _tag_=255
        uavcan__node__port__List     cObj{};
        uavcan::node::port::List_1_0 cppObj{};
        std::size_t                  cConsumed   = sizeof(input);
        std::size_t                  cppConsumed = sizeof(input);
        const std::int8_t            cRc         = uavcan__node__port__List__deserialize_(&cObj, input, &cConsumed);
        const std::int8_t            cppRc       = cppObj.deserialize(input, &cppConsumed);
        const std::int8_t            expected    = static_cast<std::int8_t>(-11);
        if ((cRc != expected) || (cppRc != expected) || (cConsumed != cppConsumed))
        {
            std::fprintf(stderr,
                         "Directed mismatch (List second-section nested bad union-tag deserialize): "
                         "C(rc=%d,consumed=%zu) C++(rc=%d,consumed=%zu)\n",
                         static_cast<int>(cRc),
                         cConsumed,
                         static_cast<int>(cppRc),
                         cppConsumed);
            return 1;
        }
        std::printf("INFO cpp-c directed marker list_second_section_nested_bad_union_tag_deserialize\n");
    }

    {
        // Deeper delimiter chain: first two sections valid, third delimiter invalid.
        const std::uint8_t input[12] = {
            0x00U,
            0x00U,
            0x00U,
            0x00U,  // publishers delimiter=0
            0x00U,
            0x00U,
            0x00U,
            0x00U,  // subscribers delimiter=0
            0xFFU,
            0xFFU,
            0xFFU,
            0x7FU  // clients delimiter invalid
        };
        uavcan__node__port__List     cObj{};
        uavcan::node::port::List_1_0 cppObj{};
        std::size_t                  cConsumed   = sizeof(input);
        std::size_t                  cppConsumed = sizeof(input);
        const std::int8_t            cRc         = uavcan__node__port__List__deserialize_(&cObj, input, &cConsumed);
        const std::int8_t            cppRc       = cppObj.deserialize(input, &cppConsumed);
        const std::int8_t            expected    = static_cast<std::int8_t>(-12);
        if ((cRc != expected) || (cppRc != expected) || (cConsumed != cppConsumed))
        {
            std::fprintf(stderr,
                         "Directed mismatch (List third delimiter-header deserialize): "
                         "C(rc=%d,consumed=%zu) C++(rc=%d,consumed=%zu)\n",
                         static_cast<int>(cRc),
                         cConsumed,
                         static_cast<int>(cppRc),
                         cppConsumed);
            return 1;
        }
        std::printf("INFO cpp-c directed marker list_third_delimiter_header_deserialize\n");
    }

    {
        // Nested-composite failure: List.publishers sparse_list length exceeds SubjectIDList capacity.
        uavcan__node__port__List     cObj{};
        uavcan::node::port::List_1_0 cppObj{};
        cObj.publishers._tag_ = 1U;
        cObj.publishers.sparse_list.count =
            static_cast<std::size_t>(uavcan__node__port__SubjectIDList_SPARSE_LIST_ARRAY_CAPACITY_ + 1U);
        cppObj.publishers._tag_ = 1U;
        cppObj.publishers.sparse_list.resize(uavcan::node::port::SubjectIDList_1_0::SPARSE_LIST_ARRAY_CAPACITY + 1U);
        std::uint8_t      cBuffer[9000]{};
        std::uint8_t      cppBuffer[9000]{};
        std::size_t       cSize   = static_cast<std::size_t>(uavcan__node__port__List_SERIALIZATION_BUFFER_SIZE_BYTES_);
        std::size_t       cppSize = uavcan::node::port::List_1_0::SERIALIZATION_BUFFER_SIZE_BYTES;
        const std::int8_t cRc     = uavcan__node__port__List__serialize_(&cObj, cBuffer, &cSize);
        const std::int8_t cppRc   = cppObj.serialize(cppBuffer, &cppSize);
        const std::int8_t expected = static_cast<std::int8_t>(-10);
        if ((cRc != expected) || (cppRc != expected))
        {
            std::fprintf(stderr,
                         "Directed mismatch (List nested bad array-length serialize): "
                         "C(rc=%d,size=%zu) C++(rc=%d,size=%zu)\n",
                         static_cast<int>(cRc),
                         cSize,
                         static_cast<int>(cppRc),
                         cppSize);
            return 1;
        }
        std::printf("INFO cpp-c directed marker list_nested_bad_array_length_serialize\n");
    }

    {
        // Invalid union tag in Frame serialization.
        uavcan__metatransport__can__Frame     cObj{};
        uavcan::metatransport::can::Frame_0_2 cppObj{};
        cObj._tag_   = 0xFFU;
        cppObj._tag_ = 0xFFU;
        std::uint8_t cBuffer[128]{};
        std::uint8_t cppBuffer[128]{};
        std::size_t  cSize =
            static_cast<std::size_t>(uavcan__metatransport__can__Frame_SERIALIZATION_BUFFER_SIZE_BYTES_);
        std::size_t       cppSize  = uavcan::metatransport::can::Frame_0_2::SERIALIZATION_BUFFER_SIZE_BYTES;
        const std::int8_t cRc      = cFrameSerialize(&cObj, cBuffer, &cSize);
        const std::int8_t cppRc    = cppFrameSerialize(&cppObj, cppBuffer, &cppSize);
        const std::int8_t expected = static_cast<std::int8_t>(-11);
        if ((cRc != expected) || (cppRc != expected))
        {
            std::fprintf(stderr,
                         "Directed mismatch (Frame bad union-tag serialize): C(rc=%d,size=%zu) "
                         "C++(rc=%d,size=%zu)\n",
                         static_cast<int>(cRc),
                         cSize,
                         static_cast<int>(cppRc),
                         cppSize);
            return 1;
        }
        std::printf("INFO cpp-c directed marker frame_bad_union_tag_serialize\n");
    }

    {
        // Service request-path failure: too-small buffer on serialization.
        uavcan__node__ExecuteCommand__Request     cObj{};
        uavcan::node::ExecuteCommand_1_3__Request cppObj{};
        std::uint8_t                              cBuffer[300]{};
        std::uint8_t                              cppBuffer[300]{};
        std::size_t                               cSize =
            static_cast<std::size_t>(uavcan__node__ExecuteCommand__Request_SERIALIZATION_BUFFER_SIZE_BYTES_ - 1U);
        std::size_t       cppSize  = uavcan::node::ExecuteCommand_1_3__Request::SERIALIZATION_BUFFER_SIZE_BYTES - 1U;
        const std::int8_t cRc      = cExecuteRequestSerialize(&cObj, cBuffer, &cSize);
        const std::int8_t cppRc    = cppExecuteRequestSerialize(&cppObj, cppBuffer, &cppSize);
        const std::int8_t expected = static_cast<std::int8_t>(-3);
        if ((cRc != expected) || (cppRc != expected))
        {
            std::fprintf(stderr,
                         "Directed mismatch (ExecuteCommand.Request too-small buffer serialize): "
                         "C(rc=%d,size=%zu) C++(rc=%d,size=%zu)\n",
                         static_cast<int>(cRc),
                         cSize,
                         static_cast<int>(cppRc),
                         cppSize);
            return 1;
        }
        std::printf("INFO cpp-c directed marker execute_request_too_small_serialize\n");
    }

    {
        // Service request-path failure: ExecuteCommand.Request parameter length exceeds capacity.
        uavcan__node__ExecuteCommand__Request     cObj{};
        uavcan::node::ExecuteCommand_1_3__Request cppObj{};
        cObj.command = 0U;
        cObj.parameter.count =
            static_cast<std::size_t>(uavcan__node__ExecuteCommand__Request_PARAMETER_ARRAY_CAPACITY_ + 1U);
        cppObj.command = 0U;
        cppObj.parameter.resize(uavcan::node::ExecuteCommand_1_3__Request::PARAMETER_ARRAY_CAPACITY + 1U, 0U);
        std::uint8_t cBuffer[300]{};
        std::uint8_t cppBuffer[300]{};
        std::size_t  cSize =
            static_cast<std::size_t>(uavcan__node__ExecuteCommand__Request_SERIALIZATION_BUFFER_SIZE_BYTES_);
        std::size_t       cppSize  = uavcan::node::ExecuteCommand_1_3__Request::SERIALIZATION_BUFFER_SIZE_BYTES;
        const std::int8_t cRc      = cExecuteRequestSerialize(&cObj, cBuffer, &cSize);
        const std::int8_t cppRc    = cppExecuteRequestSerialize(&cppObj, cppBuffer, &cppSize);
        const std::int8_t expected = static_cast<std::int8_t>(-10);
        if ((cRc != expected) || (cppRc != expected))
        {
            std::fprintf(stderr,
                         "Directed mismatch (ExecuteCommand.Request bad array-length serialize): "
                         "C(rc=%d,size=%zu) C++(rc=%d,size=%zu)\n",
                         static_cast<int>(cRc),
                         cSize,
                         static_cast<int>(cppRc),
                         cppSize);
            return 1;
        }
        std::printf("INFO cpp-c directed marker execute_request_bad_array_length_serialize\n");
    }

    {
        // Invalid variable-array length in ExecuteCommand.Response serialization.
        uavcan__node__ExecuteCommand__Response     cObj{};
        uavcan::node::ExecuteCommand_1_3__Response cppObj{};
        cObj.status = 0U;
        cObj.output.count =
            static_cast<std::size_t>(uavcan__node__ExecuteCommand__Response_OUTPUT_ARRAY_CAPACITY_ + 1U);
        cppObj.status = 0U;
        cppObj.output.resize(uavcan::node::ExecuteCommand_1_3__Response::OUTPUT_ARRAY_CAPACITY + 1U, 0U);
        std::uint8_t cBuffer[128]{};
        std::uint8_t cppBuffer[128]{};
        std::size_t  cSize =
            static_cast<std::size_t>(uavcan__node__ExecuteCommand__Response_SERIALIZATION_BUFFER_SIZE_BYTES_);
        std::size_t       cppSize  = uavcan::node::ExecuteCommand_1_3__Response::SERIALIZATION_BUFFER_SIZE_BYTES;
        const std::int8_t cRc      = cExecuteResponseSerialize(&cObj, cBuffer, &cSize);
        const std::int8_t cppRc    = cppExecuteResponseSerialize(&cppObj, cppBuffer, &cppSize);
        const std::int8_t expected = static_cast<std::int8_t>(-10);
        if ((cRc != expected) || (cppRc != expected))
        {
            std::fprintf(stderr,
                         "Directed mismatch (ExecuteCommand.Response bad array-length serialize): "
                         "C(rc=%d,size=%zu) C++(rc=%d,size=%zu)\n",
                         static_cast<int>(cRc),
                         cSize,
                         static_cast<int>(cppRc),
                         cppSize);
            return 1;
        }
        std::printf("INFO cpp-c directed marker execute_response_bad_array_length_serialize\n");
    }

    {
        // Too-small buffer in Heartbeat serialization.
        uavcan__node__Heartbeat cObj{};
        uavcan::node::Heartbeat cppObj{};
        std::uint8_t            cBuffer[8]{};
        std::uint8_t            cppBuffer[8]{};
        std::size_t cSize     = static_cast<std::size_t>(uavcan__node__Heartbeat_SERIALIZATION_BUFFER_SIZE_BYTES_ - 1U);
        std::size_t cppSize   = uavcan::node::Heartbeat::SERIALIZATION_BUFFER_SIZE_BYTES - 1U;
        const std::int8_t cRc = cHeartbeatSerialize(&cObj, cBuffer, &cSize);
        const std::int8_t cppRc    = cppHeartbeatSerialize(&cppObj, cppBuffer, &cppSize);
        const std::int8_t expected = static_cast<std::int8_t>(-3);
        if ((cRc != expected) || (cppRc != expected))
        {
            std::fprintf(stderr,
                         "Directed mismatch (Heartbeat too-small buffer serialize): C(rc=%d,size=%zu) "
                         "C++(rc=%d,size=%zu)\n",
                         static_cast<int>(cRc),
                         cSize,
                         static_cast<int>(cppRc),
                         cppSize);
            return 1;
        }
        std::printf("INFO cpp-c directed marker heartbeat_too_small_serialize\n");
    }

    {
        // Saturating serialize edge: Health.value is saturated uint2, so 0xFF must clamp to 0x03.
        uavcan__node__Health cObj{};
        uavcan::node::Health cppObj{};
        cObj.value   = 0xFFU;
        cppObj.value = 0xFFU;
        std::uint8_t      cBuffer[8]{};
        std::uint8_t      cppBuffer[8]{};
        std::size_t       cSize   = static_cast<std::size_t>(uavcan__node__Health_SERIALIZATION_BUFFER_SIZE_BYTES_);
        std::size_t       cppSize = uavcan::node::Health::SERIALIZATION_BUFFER_SIZE_BYTES;
        const std::int8_t cRc     = cHealthSerialize(&cObj, cBuffer, &cSize);
        const std::int8_t cppRc   = cppHealthSerialize(&cppObj, cppBuffer, &cppSize);
        if ((cRc != 0) || (cppRc != 0) || (cSize != 1U) || (cppSize != 1U) || (cBuffer[0] != cppBuffer[0]) ||
            (cBuffer[0] != 0x03U))
        {
            std::fprintf(stderr,
                         "Directed mismatch (Health saturating serialize): "
                         "C(rc=%d,size=%zu,byte=%02X) C++(rc=%d,size=%zu,byte=%02X)\n",
                         static_cast<int>(cRc),
                         cSize,
                         cBuffer[0],
                         static_cast<int>(cppRc),
                         cppSize,
                         cppBuffer[0]);
            return 1;
        }
        std::printf("INFO cpp-c directed marker health_saturating_serialize\n");
    }

    {
        // Truncating serialize edge: SynchronizedTimestamp.microsecond is truncated uint56.
        constexpr std::uint64_t             kInput      = UINT64_C(0xFEDCBA9876543210);
        const std::uint8_t                  expected[7] = {0x10U, 0x32U, 0x54U, 0x76U, 0x98U, 0xBAU, 0xDCU};
        uavcan__time__SynchronizedTimestamp cObj{};
        uavcan::time::SynchronizedTimestamp cppObj{};
        cObj.microsecond   = kInput;
        cppObj.microsecond = kInput;
        std::uint8_t cBuffer[16]{};
        std::uint8_t cppBuffer[16]{};
        std::size_t  cSize =
            static_cast<std::size_t>(uavcan__time__SynchronizedTimestamp_SERIALIZATION_BUFFER_SIZE_BYTES_);
        std::size_t       cppSize     = uavcan::time::SynchronizedTimestamp::SERIALIZATION_BUFFER_SIZE_BYTES;
        const std::int8_t cRc         = cSynchronizedTimestampSerialize(&cObj, cBuffer, &cSize);
        const std::int8_t cppRc       = cppSynchronizedTimestampSerialize(&cppObj, cppBuffer, &cppSize);
        const bool        cExpected   = (cSize == 7U) && (std::memcmp(cBuffer, expected, 7U) == 0);
        const bool        cppExpected = (cppSize == 7U) && (std::memcmp(cppBuffer, expected, 7U) == 0);
        if ((cRc != 0) || (cppRc != 0) || (cSize != cppSize) || (std::memcmp(cBuffer, cppBuffer, cSize) != 0) ||
            !cExpected || !cppExpected)
        {
            std::fprintf(stderr,
                         "Directed mismatch (SynchronizedTimestamp truncating serialize): "
                         "C(rc=%d,size=%zu) C++(rc=%d,size=%zu)\n",
                         static_cast<int>(cRc),
                         cSize,
                         static_cast<int>(cppRc),
                         cppSize);
            dumpBytes("c", cBuffer, cSize);
            dumpBytes("cpp", cppBuffer, cppSize);
            return 1;
        }
        std::printf("INFO cpp-c directed marker synchronized_timestamp_truncating_serialize\n");
    }

    {
        // Signed scalar edge vectors for Integer8.
        const std::uint8_t                  input[1] = {0x80U};  // -128 in two's complement.
        uavcan__primitive__scalar__Integer8 cObj{};
        uavcan::primitive::scalar::Integer8 cppObj{};
        std::size_t                         cConsumed   = sizeof(input);
        std::size_t                         cppConsumed = sizeof(input);
        const std::int8_t                   cDesRc      = cInteger8Deserialize(&cObj, input, &cConsumed);
        const std::int8_t                   cppDesRc    = cppInteger8Deserialize(&cppObj, input, &cppConsumed);
        if ((cDesRc != 0) || (cppDesRc != 0) || (cConsumed != cppConsumed) ||
            (cObj.value != static_cast<std::int8_t>(-128)) || (cppObj.value != static_cast<std::int8_t>(-128)))
        {
            std::fprintf(stderr,
                         "Directed mismatch (Integer8 signed deserialize): "
                         "C(rc=%d,consumed=%zu,value=%d) C++(rc=%d,consumed=%zu,value=%d)\n",
                         static_cast<int>(cDesRc),
                         cConsumed,
                         static_cast<int>(cObj.value),
                         static_cast<int>(cppDesRc),
                         cppConsumed,
                         static_cast<int>(cppObj.value));
            return 1;
        }

        cObj.value   = static_cast<std::int8_t>(-1);
        cppObj.value = static_cast<std::int8_t>(-1);
        std::uint8_t cBuffer[8]{};
        std::uint8_t cppBuffer[8]{};
        std::size_t  cSize =
            static_cast<std::size_t>(uavcan__primitive__scalar__Integer8_SERIALIZATION_BUFFER_SIZE_BYTES_);
        std::size_t       cppSize  = uavcan::primitive::scalar::Integer8::SERIALIZATION_BUFFER_SIZE_BYTES;
        const std::int8_t cSerRc   = cInteger8Serialize(&cObj, cBuffer, &cSize);
        const std::int8_t cppSerRc = cppInteger8Serialize(&cppObj, cppBuffer, &cppSize);
        if ((cSerRc != 0) || (cppSerRc != 0) || (cSize != 1U) || (cppSize != 1U) || (cBuffer[0] != cppBuffer[0]) ||
            (cBuffer[0] != 0xFFU))
        {
            std::fprintf(stderr,
                         "Directed mismatch (Integer8 signed serialize): "
                         "C(rc=%d,size=%zu,byte=%02X) C++(rc=%d,size=%zu,byte=%02X)\n",
                         static_cast<int>(cSerRc),
                         cSize,
                         cBuffer[0],
                         static_cast<int>(cppSerRc),
                         cppSize,
                         cppBuffer[0]);
            return 1;
        }
        std::printf("INFO cpp-c directed marker integer8_signed_roundtrip\n");
    }

    {
        // Float32 signaling-NaN payload must survive deserialize->serialize byte-exactly in both C
        // and C++. Regression guard for the float32 -> double -> float32 round-trip that quieted
        // signaling NaNs (the quiet bit 0x40 in byte[2] must stay clear, i.e. 0x80 not 0xC0).
        // float16 is intentionally excluded: the shared runtime canonicalizes half-precision NaN
        // payloads to 0x7E00 for every backend, so there is no stable payload to preserve there.
        const std::uint8_t                golden[4] = {0x01U, 0x00U, 0x80U, 0x7FU};  // float32 sNaN
        uavcan__primitive__scalar__Real32 cObj{};
        uavcan::primitive::scalar::Real32 cppObj{};
        std::size_t                       cConsumed   = sizeof(golden);
        std::size_t                       cppConsumed = sizeof(golden);
        const std::int8_t                 cDesRc = uavcan__primitive__scalar__Real32__deserialize_(&cObj, golden, &cConsumed);
        const std::int8_t                 cppDesRc = cppObj.deserialize(golden, &cppConsumed);
        std::uint8_t                      cOut[8]{};
        std::uint8_t                      cppOut[8]{};
        std::size_t                       cSize    = sizeof(cOut);
        std::size_t                       cppSize  = sizeof(cppOut);
        const std::int8_t                 cSerRc   = uavcan__primitive__scalar__Real32__serialize_(&cObj, cOut, &cSize);
        const std::int8_t                 cppSerRc = cppObj.serialize(cppOut, &cppSize);
        if ((cDesRc != 0) || (cppDesRc != 0) || (cConsumed != cppConsumed) || (cSerRc != 0) || (cppSerRc != 0) ||
            (cSize != 4U) || (cppSize != 4U) || (std::memcmp(cOut, golden, 4U) != 0) ||
            (std::memcmp(cppOut, golden, 4U) != 0))
        {
            std::fprintf(stderr,
                         "Directed mismatch (Real32 signaling-NaN payload roundtrip): "
                         "C(rc=%d,size=%zu) C++(rc=%d,size=%zu)\n",
                         static_cast<int>(cSerRc),
                         cSize,
                         static_cast<int>(cppSerRc),
                         cppSize);
            dumpBytes("golden", golden, 4U);
            dumpBytes("c", cOut, cSize);
            dumpBytes("cpp", cppOut, cppSize);
            return 1;
        }
        std::printf("INFO cpp-c directed marker real32_signaling_nan_payload_roundtrip\n");
    }

    std::printf("PASS directed_error_parity directed\n");
    return 0;
}

std::int8_t cHeartbeatDeserialize(uavcan__node__Heartbeat* const outObj,
                                  const std::uint8_t* const      buffer,
                                  std::size_t* const             inoutSize)
{
    return uavcan__node__Heartbeat__deserialize_(outObj, buffer, inoutSize);
}
std::int8_t cHeartbeatSerialize(const uavcan__node__Heartbeat* const obj,
                                std::uint8_t* const                  buffer,
                                std::size_t* const                   inoutSize)
{
    return uavcan__node__Heartbeat__serialize_(obj, buffer, inoutSize);
}
std::int8_t cppHeartbeatDeserialize(uavcan::node::Heartbeat* const outObj,
                                    const std::uint8_t* const      buffer,
                                    std::size_t* const             inoutSize)
{
    return outObj->deserialize(buffer, inoutSize);
}
std::int8_t cppHeartbeatSerialize(const uavcan::node::Heartbeat* const obj,
                                  std::uint8_t* const                  buffer,
                                  std::size_t* const                   inoutSize)
{
    return obj->serialize(buffer, inoutSize);
}

std::int8_t cExecuteRequestDeserialize(uavcan__node__ExecuteCommand__Request* const outObj,
                                       const std::uint8_t* const                    buffer,
                                       std::size_t* const                           inoutSize)
{
    return uavcan__node__ExecuteCommand__Request__deserialize_(outObj, buffer, inoutSize);
}
std::int8_t cExecuteRequestSerialize(const uavcan__node__ExecuteCommand__Request* const obj,
                                     std::uint8_t* const                                buffer,
                                     std::size_t* const                                 inoutSize)
{
    return uavcan__node__ExecuteCommand__Request__serialize_(obj, buffer, inoutSize);
}
std::int8_t cppExecuteRequestDeserialize(uavcan::node::ExecuteCommand_1_3__Request* const outObj,
                                         const std::uint8_t* const                        buffer,
                                         std::size_t* const                               inoutSize)
{
    return outObj->deserialize(buffer, inoutSize);
}
std::int8_t cppExecuteRequestSerialize(const uavcan::node::ExecuteCommand_1_3__Request* const obj,
                                       std::uint8_t* const                                    buffer,
                                       std::size_t* const                                     inoutSize)
{
    return obj->serialize(buffer, inoutSize);
}

std::int8_t cExecuteResponseDeserialize(uavcan__node__ExecuteCommand__Response* const outObj,
                                        const std::uint8_t* const                     buffer,
                                        std::size_t* const                            inoutSize)
{
    return uavcan__node__ExecuteCommand__Response__deserialize_(outObj, buffer, inoutSize);
}
std::int8_t cExecuteResponseSerialize(const uavcan__node__ExecuteCommand__Response* const obj,
                                      std::uint8_t* const                                 buffer,
                                      std::size_t* const                                  inoutSize)
{
    return uavcan__node__ExecuteCommand__Response__serialize_(obj, buffer, inoutSize);
}
std::int8_t cppExecuteResponseDeserialize(uavcan::node::ExecuteCommand_1_3__Response* const outObj,
                                          const std::uint8_t* const                         buffer,
                                          std::size_t* const                                inoutSize)
{
    return outObj->deserialize(buffer, inoutSize);
}
std::int8_t cppExecuteResponseSerialize(const uavcan::node::ExecuteCommand_1_3__Response* const obj,
                                        std::uint8_t* const                                     buffer,
                                        std::size_t* const                                      inoutSize)
{
    return obj->serialize(buffer, inoutSize);
}

std::int8_t cFrameDeserialize(uavcan__metatransport__can__Frame* const outObj,
                              const std::uint8_t* const                buffer,
                              std::size_t* const                       inoutSize)
{
    return uavcan__metatransport__can__Frame__deserialize_(outObj, buffer, inoutSize);
}
std::int8_t cFrameSerialize(const uavcan__metatransport__can__Frame* const obj,
                            std::uint8_t* const                            buffer,
                            std::size_t* const                             inoutSize)
{
    return uavcan__metatransport__can__Frame__serialize_(obj, buffer, inoutSize);
}
std::int8_t cppFrameDeserialize(uavcan::metatransport::can::Frame_0_2* const outObj,
                                const std::uint8_t* const                    buffer,
                                std::size_t* const                           inoutSize)
{
    return outObj->deserialize(buffer, inoutSize);
}
std::int8_t cppFrameSerialize(const uavcan::metatransport::can::Frame_0_2* const obj,
                              std::uint8_t* const                                buffer,
                              std::size_t* const                                 inoutSize)
{
    return obj->serialize(buffer, inoutSize);
}

std::int8_t cHealthDeserialize(uavcan__node__Health* const outObj,
                               const std::uint8_t* const   buffer,
                               std::size_t* const          inoutSize)
{
    return uavcan__node__Health__deserialize_(outObj, buffer, inoutSize);
}
std::int8_t cHealthSerialize(const uavcan__node__Health* const obj,
                             std::uint8_t* const               buffer,
                             std::size_t* const                inoutSize)
{
    return uavcan__node__Health__serialize_(obj, buffer, inoutSize);
}
std::int8_t cppHealthDeserialize(uavcan::node::Health* const outObj,
                                 const std::uint8_t* const   buffer,
                                 std::size_t* const          inoutSize)
{
    return outObj->deserialize(buffer, inoutSize);
}
std::int8_t cppHealthSerialize(const uavcan::node::Health* const obj,
                               std::uint8_t* const               buffer,
                               std::size_t* const                inoutSize)
{
    return obj->serialize(buffer, inoutSize);
}

std::int8_t cSynchronizedTimestampDeserialize(uavcan__time__SynchronizedTimestamp* const outObj,
                                              const std::uint8_t* const                  buffer,
                                              std::size_t* const                         inoutSize)
{
    return uavcan__time__SynchronizedTimestamp__deserialize_(outObj, buffer, inoutSize);
}
std::int8_t cSynchronizedTimestampSerialize(const uavcan__time__SynchronizedTimestamp* const obj,
                                            std::uint8_t* const                              buffer,
                                            std::size_t* const                               inoutSize)
{
    return uavcan__time__SynchronizedTimestamp__serialize_(obj, buffer, inoutSize);
}
std::int8_t cppSynchronizedTimestampDeserialize(uavcan::time::SynchronizedTimestamp* const outObj,
                                                const std::uint8_t* const                  buffer,
                                                std::size_t* const                         inoutSize)
{
    return outObj->deserialize(buffer, inoutSize);
}
std::int8_t cppSynchronizedTimestampSerialize(const uavcan::time::SynchronizedTimestamp* const obj,
                                              std::uint8_t* const                              buffer,
                                              std::size_t* const                               inoutSize)
{
    return obj->serialize(buffer, inoutSize);
}

std::int8_t cInteger8Deserialize(uavcan__primitive__scalar__Integer8* const outObj,
                                 const std::uint8_t* const                  buffer,
                                 std::size_t* const                         inoutSize)
{
    return uavcan__primitive__scalar__Integer8__deserialize_(outObj, buffer, inoutSize);
}
std::int8_t cInteger8Serialize(const uavcan__primitive__scalar__Integer8* const obj,
                               std::uint8_t* const                              buffer,
                               std::size_t* const                               inoutSize)
{
    return uavcan__primitive__scalar__Integer8__serialize_(obj, buffer, inoutSize);
}
std::int8_t cppInteger8Deserialize(uavcan::primitive::scalar::Integer8* const outObj,
                                   const std::uint8_t* const                  buffer,
                                   std::size_t* const                         inoutSize)
{
    return outObj->deserialize(buffer, inoutSize);
}
std::int8_t cppInteger8Serialize(const uavcan::primitive::scalar::Integer8* const obj,
                                 std::uint8_t* const                              buffer,
                                 std::size_t* const                               inoutSize)
{
    return obj->serialize(buffer, inoutSize);
}

}  // namespace

int main(int argc, char** argv)
{
    constexpr std::size_t kRandomCases   = 7U;
    constexpr std::size_t kDirectedCases = 1U;
    std::size_t           iterations     = 128U;
    if (argc > 1)
    {
        char*               endptr = nullptr;
        const unsigned long parsed = std::strtoul(argv[1], &endptr, 10);
        if ((endptr == nullptr) || (*endptr != '\0') || (parsed == 0UL))
        {
            std::fprintf(stderr, "Invalid iteration count: %s\n", argv[1]);
            return 2;
        }
        iterations = static_cast<std::size_t>(parsed);
    }

    if (runCase<uavcan__node__Heartbeat,
                uavcan::node::Heartbeat>("uavcan.node.Heartbeat.1.0",
                                         iterations,
                                         static_cast<std::size_t>(
                                             uavcan__node__Heartbeat_SERIALIZATION_BUFFER_SIZE_BYTES_),
                                         uavcan::node::Heartbeat::SERIALIZATION_BUFFER_SIZE_BYTES,
                                         cHeartbeatDeserialize,
                                         cHeartbeatSerialize,
                                         cppHeartbeatDeserialize,
                                         cppHeartbeatSerialize) != 0)
    {
        return 1;
    }

    if (runCase<uavcan__node__ExecuteCommand__Request, uavcan::node::ExecuteCommand_1_3__Request>(
            "uavcan.node.ExecuteCommand.Request.1.3",
            iterations,
            static_cast<std::size_t>(uavcan__node__ExecuteCommand__Request_SERIALIZATION_BUFFER_SIZE_BYTES_),
            uavcan::node::ExecuteCommand_1_3__Request::SERIALIZATION_BUFFER_SIZE_BYTES,
            cExecuteRequestDeserialize,
            cExecuteRequestSerialize,
            cppExecuteRequestDeserialize,
            cppExecuteRequestSerialize) != 0)
    {
        return 1;
    }

    if (runCase<uavcan__node__ExecuteCommand__Response, uavcan::node::ExecuteCommand_1_3__Response>(
            "uavcan.node.ExecuteCommand.Response.1.3",
            iterations,
            static_cast<std::size_t>(uavcan__node__ExecuteCommand__Response_SERIALIZATION_BUFFER_SIZE_BYTES_),
            uavcan::node::ExecuteCommand_1_3__Response::SERIALIZATION_BUFFER_SIZE_BYTES,
            cExecuteResponseDeserialize,
            cExecuteResponseSerialize,
            cppExecuteResponseDeserialize,
            cppExecuteResponseSerialize) != 0)
    {
        return 1;
    }

    if (runCase<uavcan__metatransport__can__Frame,
                uavcan::metatransport::can::
                    Frame_0_2>("uavcan.metatransport.can.Frame.0.2",
                               iterations,
                               static_cast<std::size_t>(
                                   uavcan__metatransport__can__Frame_SERIALIZATION_BUFFER_SIZE_BYTES_),
                               uavcan::metatransport::can::Frame_0_2::SERIALIZATION_BUFFER_SIZE_BYTES,
                               cFrameDeserialize,
                               cFrameSerialize,
                               cppFrameDeserialize,
                               cppFrameSerialize) != 0)
    {
        return 1;
    }

    if (runCase<uavcan__node__Health, uavcan::node::Health>("uavcan.node.Health.1.0",
                                                            iterations,
                                                            static_cast<std::size_t>(
                                                                uavcan__node__Health_SERIALIZATION_BUFFER_SIZE_BYTES_),
                                                            uavcan::node::Health::SERIALIZATION_BUFFER_SIZE_BYTES,
                                                            cHealthDeserialize,
                                                            cHealthSerialize,
                                                            cppHealthDeserialize,
                                                            cppHealthSerialize) != 0)
    {
        return 1;
    }

    if (runCase<uavcan__time__SynchronizedTimestamp,
                uavcan::time::
                    SynchronizedTimestamp>("uavcan.time.SynchronizedTimestamp.1.0",
                                           iterations,
                                           static_cast<std::size_t>(
                                               uavcan__time__SynchronizedTimestamp_SERIALIZATION_BUFFER_SIZE_BYTES_),
                                           uavcan::time::SynchronizedTimestamp::SERIALIZATION_BUFFER_SIZE_BYTES,
                                           cSynchronizedTimestampDeserialize,
                                           cSynchronizedTimestampSerialize,
                                           cppSynchronizedTimestampDeserialize,
                                           cppSynchronizedTimestampSerialize) != 0)
    {
        return 1;
    }

    if (runCase<uavcan__primitive__scalar__Integer8,
                uavcan::primitive::scalar::
                    Integer8>("uavcan.primitive.scalar.Integer8.1.0",
                              iterations,
                              static_cast<std::size_t>(
                                  uavcan__primitive__scalar__Integer8_SERIALIZATION_BUFFER_SIZE_BYTES_),
                              uavcan::primitive::scalar::Integer8::SERIALIZATION_BUFFER_SIZE_BYTES,
                              cInteger8Deserialize,
                              cInteger8Serialize,
                              cppInteger8Deserialize,
                              cppInteger8Serialize) != 0)
    {
        return 1;
    }

    if (runDirectedErrorCases() != 0)
    {
        return 1;
    }

    std::printf("PASS cpp-c inventory random_cases=%zu directed_cases=%zu\n", kRandomCases, kDirectedCases);
    std::printf("PASS cpp-c parity random_iterations=%zu random_cases=%zu directed_cases=%zu\n",
                iterations,
                kRandomCases,
                kDirectedCases);
    std::printf("C/C++ parity PASS\n");
    return 0;
}
