//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// C ABI harness for C-vs-Rust differential parity integration tests.
///
/// This harness bridges generated C types to Rust parity drivers by exposing
/// stable roundtrip and directed-serialize entry points.
///
//===----------------------------------------------------------------------===//

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "uavcan/metatransport/can/Frame_0_2.h"
#include "uavcan/node/ExecuteCommand_1_3.h"
#include "uavcan/node/Heartbeat_1_0.h"
#include "uavcan/node/Health_1_0.h"
#include "uavcan/node/port/List_1_0.h"
#include "uavcan/primitive/scalar/Integer8_1_0.h"
#include "uavcan/primitive/scalar/Real32_1_0.h"
#include "uavcan/register/Value_1_0.h"
#include "uavcan/time/SynchronizedTimestamp_1_0.h"

typedef struct CCaseResult
{
    int8_t deserialize_rc;
    size_t deserialize_consumed;
    int8_t serialize_rc;
    size_t serialize_size;
} CCaseResult;

static int run_heartbeat(const uint8_t* const input,
                         const size_t         input_size,
                         uint8_t* const       output,
                         const size_t         output_capacity,
                         CCaseResult* const   result)
{
    uavcan__node__Heartbeat@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));

    size_t       consumed        = input_size;
    const int8_t des             = uavcan__node__Heartbeat@CV1_0@__deserialize_(&obj, input, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    if (des < 0)
    {
        return 0;
    }

    size_t       out_size  = output_capacity;
    const int8_t ser       = uavcan__node__Heartbeat@CV1_0@__serialize_(&obj, output, &out_size);
    result->serialize_rc   = ser;
    result->serialize_size = out_size;
    return 0;
}

static int run_health(const uint8_t* const input,
                      const size_t         input_size,
                      uint8_t* const       output,
                      const size_t         output_capacity,
                      CCaseResult* const   result)
{
    uavcan__node__Health@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));

    size_t       consumed        = input_size;
    const int8_t des             = uavcan__node__Health@CV1_0@__deserialize_(&obj, input, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    if (des < 0)
    {
        return 0;
    }

    size_t       out_size  = output_capacity;
    const int8_t ser       = uavcan__node__Health@CV1_0@__serialize_(&obj, output, &out_size);
    result->serialize_rc   = ser;
    result->serialize_size = out_size;
    return 0;
}

static int run_synchronized_timestamp(const uint8_t* const input,
                                      const size_t         input_size,
                                      uint8_t* const       output,
                                      const size_t         output_capacity,
                                      CCaseResult* const   result)
{
    uavcan__time__SynchronizedTimestamp@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));

    size_t       consumed        = input_size;
    const int8_t des             = uavcan__time__SynchronizedTimestamp@CV1_0@__deserialize_(&obj, input, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    if (des < 0)
    {
        return 0;
    }

    size_t       out_size  = output_capacity;
    const int8_t ser       = uavcan__time__SynchronizedTimestamp@CV1_0@__serialize_(&obj, output, &out_size);
    result->serialize_rc   = ser;
    result->serialize_size = out_size;
    return 0;
}

static int run_integer8(const uint8_t* const input,
                        const size_t         input_size,
                        uint8_t* const       output,
                        const size_t         output_capacity,
                        CCaseResult* const   result)
{
    uavcan__primitive__scalar__Integer8@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));

    size_t       consumed        = input_size;
    const int8_t des             = uavcan__primitive__scalar__Integer8@CV1_0@__deserialize_(&obj, input, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    if (des < 0)
    {
        return 0;
    }

    size_t       out_size  = output_capacity;
    const int8_t ser       = uavcan__primitive__scalar__Integer8@CV1_0@__serialize_(&obj, output, &out_size);
    result->serialize_rc   = ser;
    result->serialize_size = out_size;
    return 0;
}

static int run_execute_command_request(const uint8_t* const input,
                                       const size_t         input_size,
                                       uint8_t* const       output,
                                       const size_t         output_capacity,
                                       CCaseResult* const   result)
{
    uavcan__node__ExecuteCommand@CV1_3@__Request obj;
    memset(&obj, 0, sizeof(obj));

    size_t       consumed        = input_size;
    const int8_t des             = uavcan__node__ExecuteCommand@CV1_3@__Request__deserialize_(&obj, input, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    if (des < 0)
    {
        return 0;
    }

    size_t       out_size  = output_capacity;
    const int8_t ser       = uavcan__node__ExecuteCommand@CV1_3@__Request__serialize_(&obj, output, &out_size);
    result->serialize_rc   = ser;
    result->serialize_size = out_size;
    return 0;
}

static int run_execute_command_response(const uint8_t* const input,
                                        const size_t         input_size,
                                        uint8_t* const       output,
                                        const size_t         output_capacity,
                                        CCaseResult* const   result)
{
    uavcan__node__ExecuteCommand@CV1_3@__Response obj;
    memset(&obj, 0, sizeof(obj));

    size_t       consumed        = input_size;
    const int8_t des             = uavcan__node__ExecuteCommand@CV1_3@__Response__deserialize_(&obj, input, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    if (des < 0)
    {
        return 0;
    }

    size_t       out_size  = output_capacity;
    const int8_t ser       = uavcan__node__ExecuteCommand@CV1_3@__Response__serialize_(&obj, output, &out_size);
    result->serialize_rc   = ser;
    result->serialize_size = out_size;
    return 0;
}

static int run_frame(const uint8_t* const input,
                     const size_t         input_size,
                     uint8_t* const       output,
                     const size_t         output_capacity,
                     CCaseResult* const   result)
{
    uavcan__metatransport__can__Frame@CV0_2@ obj;
    memset(&obj, 0, sizeof(obj));

    size_t       consumed        = input_size;
    const int8_t des             = uavcan__metatransport__can__Frame@CV0_2@__deserialize_(&obj, input, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    if (des < 0)
    {
        return 0;
    }

    size_t       out_size  = output_capacity;
    const int8_t ser       = uavcan__metatransport__can__Frame@CV0_2@__serialize_(&obj, output, &out_size);
    result->serialize_rc   = ser;
    result->serialize_size = out_size;
    return 0;
}

static int run_real32(const uint8_t* const input,
                      const size_t         input_size,
                      uint8_t* const       output,
                      const size_t         output_capacity,
                      CCaseResult* const   result)
{
    uavcan__primitive__scalar__Real32@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));

    size_t       consumed        = input_size;
    const int8_t des             = uavcan__primitive__scalar__Real32@CV1_0@__deserialize_(&obj, input, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    if (des < 0)
    {
        return 0;
    }

    size_t       out_size  = output_capacity;
    const int8_t ser       = uavcan__primitive__scalar__Real32@CV1_0@__serialize_(&obj, output, &out_size);
    result->serialize_rc   = ser;
    result->serialize_size = out_size;
    return 0;
}

static int run_value(const uint8_t* const input,
                     const size_t         input_size,
                     uint8_t* const       output,
                     const size_t         output_capacity,
                     CCaseResult* const   result)
{
    uavcan__register___Value@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));

    size_t       consumed        = input_size;
    const int8_t des             = uavcan__register___Value@CV1_0@__deserialize_(&obj, input, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    if (des < 0)
    {
        return 0;
    }

    size_t       out_size  = output_capacity;
    const int8_t ser       = uavcan__register___Value@CV1_0@__serialize_(&obj, output, &out_size);
    result->serialize_rc   = ser;
    result->serialize_size = out_size;
    return 0;
}

int c_heartbeat_roundtrip(const uint8_t* const input,
                          const size_t         input_size,
                          uint8_t* const       output,
                          const size_t         output_capacity,
                          CCaseResult* const   result)
{
    if ((input == NULL) || (output == NULL) || (result == NULL))
    {
        return -1;
    }
    return run_heartbeat(input, input_size, output, output_capacity, result);
}

int c_health_roundtrip(const uint8_t* const input,
                       const size_t         input_size,
                       uint8_t* const       output,
                       const size_t         output_capacity,
                       CCaseResult* const   result)
{
    if ((input == NULL) || (output == NULL) || (result == NULL))
    {
        return -1;
    }
    return run_health(input, input_size, output, output_capacity, result);
}

int c_synchronized_timestamp_roundtrip(const uint8_t* const input,
                                       const size_t         input_size,
                                       uint8_t* const       output,
                                       const size_t         output_capacity,
                                       CCaseResult* const   result)
{
    if ((input == NULL) || (output == NULL) || (result == NULL))
    {
        return -1;
    }
    return run_synchronized_timestamp(input, input_size, output, output_capacity, result);
}

int c_integer8_roundtrip(const uint8_t* const input,
                         const size_t         input_size,
                         uint8_t* const       output,
                         const size_t         output_capacity,
                         CCaseResult* const   result)
{
    if ((input == NULL) || (output == NULL) || (result == NULL))
    {
        return -1;
    }
    return run_integer8(input, input_size, output, output_capacity, result);
}

int c_execute_command_request_roundtrip(const uint8_t* const input,
                                        const size_t         input_size,
                                        uint8_t* const       output,
                                        const size_t         output_capacity,
                                        CCaseResult* const   result)
{
    if ((input == NULL) || (output == NULL) || (result == NULL))
    {
        return -1;
    }
    return run_execute_command_request(input, input_size, output, output_capacity, result);
}

int c_execute_command_response_roundtrip(const uint8_t* const input,
                                         const size_t         input_size,
                                         uint8_t* const       output,
                                         const size_t         output_capacity,
                                         CCaseResult* const   result)
{
    if ((input == NULL) || (output == NULL) || (result == NULL))
    {
        return -1;
    }
    return run_execute_command_response(input, input_size, output, output_capacity, result);
}

int c_frame_roundtrip(const uint8_t* const input,
                      const size_t         input_size,
                      uint8_t* const       output,
                      const size_t         output_capacity,
                      CCaseResult* const   result)
{
    if ((input == NULL) || (output == NULL) || (result == NULL))
    {
        return -1;
    }
    return run_frame(input, input_size, output, output_capacity, result);
}

int c_value_roundtrip(const uint8_t* const input,
                      const size_t         input_size,
                      uint8_t* const       output,
                      const size_t         output_capacity,
                      CCaseResult* const   result)
{
    if ((input == NULL) || (output == NULL) || (result == NULL))
    {
        return -1;
    }
    return run_value(input, input_size, output, output_capacity, result);
}

int c_real32_roundtrip(const uint8_t* const input,
                       const size_t         input_size,
                       uint8_t* const       output,
                       const size_t         output_capacity,
                       CCaseResult* const   result)
{
    if ((input == NULL) || (output == NULL) || (result == NULL))
    {
        return -1;
    }
    return run_real32(input, input_size, output, output_capacity, result);
}

int c_frame_bad_union_tag_deserialize(CCaseResult* const result)
{
    if (result == NULL)
    {
        return -1;
    }
    const uint8_t                     input[1] = {0xFFU};
    uavcan__metatransport__can__Frame@CV0_2@ obj;
    memset(&obj, 0, sizeof(obj));
    size_t       consumed        = sizeof(input);
    const int8_t des             = uavcan__metatransport__can__Frame@CV0_2@__deserialize_(&obj, input, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    return 0;
}

int c_execute_response_bad_array_length_deserialize(CCaseResult* const result)
{
    if (result == NULL)
    {
        return -1;
    }
    const uint8_t                          input[2] = {0x00U, 0xFFU};  // status=0, output.count=255
    uavcan__node__ExecuteCommand@CV1_3@__Response obj;
    memset(&obj, 0, sizeof(obj));
    size_t       consumed        = sizeof(input);
    const int8_t des             = uavcan__node__ExecuteCommand@CV1_3@__Response__deserialize_(&obj, input, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    return 0;
}

int c_list_bad_delimiter_header_deserialize(CCaseResult* const result)
{
    if (result == NULL)
    {
        return -1;
    }
    const uint8_t            input[4] = {0xFFU, 0xFFU, 0xFFU, 0x7FU};
    uavcan__node__port__List@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));
    size_t       consumed        = sizeof(input);
    const int8_t des             = uavcan__node__port__List@CV1_0@__deserialize_(&obj, input, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    return 0;
}

int c_heartbeat_empty_deserialize(CCaseResult* const result)
{
    if (result == NULL)
    {
        return -1;
    }
    const uint8_t           input[1] = {0x00U};
    uavcan__node__Heartbeat@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));
    size_t       consumed        = 0U;
    const int8_t des             = uavcan__node__Heartbeat@CV1_0@__deserialize_(&obj, input, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    return 0;
}

int c_list_nested_bad_union_tag_deserialize(CCaseResult* const result)
{
    if (result == NULL)
    {
        return -1;
    }
    const uint8_t            input[5] = {0x01U, 0x00U, 0x00U, 0x00U, 0xFFU};
    uavcan__node__port__List@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));
    size_t       consumed        = sizeof(input);
    const int8_t des             = uavcan__node__port__List@CV1_0@__deserialize_(&obj, input, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    return 0;
}

int c_list_second_delimiter_bad_deserialize(CCaseResult* const result)
{
    if (result == NULL)
    {
        return -1;
    }
    const uint8_t            input[8] = {0x00U, 0x00U, 0x00U, 0x00U, 0xFFU, 0xFFU, 0xFFU, 0x7FU};
    uavcan__node__port__List@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));
    size_t       consumed        = sizeof(input);
    const int8_t des             = uavcan__node__port__List@CV1_0@__deserialize_(&obj, input, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    return 0;
}

int c_list_second_section_nested_bad_union_tag_deserialize(CCaseResult* const result)
{
    if (result == NULL)
    {
        return -1;
    }
    const uint8_t            input[9] = {0x00U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0xFFU};
    uavcan__node__port__List@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));
    size_t       consumed        = sizeof(input);
    const int8_t des             = uavcan__node__port__List@CV1_0@__deserialize_(&obj, input, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    return 0;
}

int c_list_third_delimiter_bad_deserialize(CCaseResult* const result)
{
    if (result == NULL)
    {
        return -1;
    }
    const uint8_t input[12] = {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0xFFU, 0xFFU, 0xFFU, 0x7FU};
    uavcan__node__port__List@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));
    size_t       consumed        = sizeof(input);
    const int8_t des             = uavcan__node__port__List@CV1_0@__deserialize_(&obj, input, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    return 0;
}

int c_list_nested_bad_array_length_serialize(CCaseResult* const result)
{
    if (result == NULL)
    {
        return -1;
    }
    uavcan__node__port__List@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));
    obj.publishers._tag_             = 1U;
    obj.publishers.sparse_list.count = (size_t) uavcan__node__port__SubjectIDList@CV1_0@_SPARSE_LIST_ARRAY_CAPACITY_ + 1U;
    uint8_t output[9000];
    memset(output, 0, sizeof(output));
    size_t       out_size        = (size_t) uavcan__node__port__List@CV1_0@_SERIALIZATION_BUFFER_SIZE_BYTES_;
    const int8_t ser             = uavcan__node__port__List@CV1_0@__serialize_(&obj, output, &out_size);
    result->deserialize_rc       = 0;
    result->deserialize_consumed = 0;
    result->serialize_rc         = ser;
    result->serialize_size       = out_size;
    return 0;
}

int c_frame_bad_union_tag_serialize(CCaseResult* const result)
{
    if (result == NULL)
    {
        return -1;
    }
    uavcan__metatransport__can__Frame@CV0_2@ obj;
    memset(&obj, 0, sizeof(obj));
    obj._tag_ = 0xFFU;
    uint8_t output[128];
    memset(output, 0, sizeof(output));
    size_t       out_size        = (size_t) uavcan__metatransport__can__Frame@CV0_2@_SERIALIZATION_BUFFER_SIZE_BYTES_;
    const int8_t ser             = uavcan__metatransport__can__Frame@CV0_2@__serialize_(&obj, output, &out_size);
    result->deserialize_rc       = 0;
    result->deserialize_consumed = 0;
    result->serialize_rc         = ser;
    result->serialize_size       = out_size;
    return 0;
}

int c_execute_response_bad_array_length_serialize(CCaseResult* const result)
{
    if (result == NULL)
    {
        return -1;
    }
    uavcan__node__ExecuteCommand@CV1_3@__Response obj;
    memset(&obj, 0, sizeof(obj));
    obj.status       = 0U;
    obj.output.count = (size_t) uavcan__node__ExecuteCommand@CV1_3@__Response_OUTPUT_ARRAY_CAPACITY_ + 1U;
    uint8_t output[128];
    memset(output, 0, sizeof(output));
    size_t       out_size        = (size_t) uavcan__node__ExecuteCommand@CV1_3@__Response_SERIALIZATION_BUFFER_SIZE_BYTES_;
    const int8_t ser             = uavcan__node__ExecuteCommand@CV1_3@__Response__serialize_(&obj, output, &out_size);
    result->deserialize_rc       = 0;
    result->deserialize_consumed = 0;
    result->serialize_rc         = ser;
    result->serialize_size       = out_size;
    return 0;
}

int c_execute_request_bad_array_length_serialize(CCaseResult* const result)
{
    if (result == NULL)
    {
        return -1;
    }
    uavcan__node__ExecuteCommand@CV1_3@__Request obj;
    memset(&obj, 0, sizeof(obj));
    obj.command         = 0U;
    obj.parameter.count = (size_t) uavcan__node__ExecuteCommand@CV1_3@__Request_PARAMETER_ARRAY_CAPACITY_ + 1U;
    uint8_t output[300];
    memset(output, 0, sizeof(output));
    size_t       out_size        = (size_t) uavcan__node__ExecuteCommand@CV1_3@__Request_SERIALIZATION_BUFFER_SIZE_BYTES_;
    const int8_t ser             = uavcan__node__ExecuteCommand@CV1_3@__Request__serialize_(&obj, output, &out_size);
    result->deserialize_rc       = 0;
    result->deserialize_consumed = 0;
    result->serialize_rc         = ser;
    result->serialize_size       = out_size;
    return 0;
}

int c_execute_request_too_small_serialize(CCaseResult* const result)
{
    if (result == NULL)
    {
        return -1;
    }
    uavcan__node__ExecuteCommand@CV1_3@__Request obj;
    memset(&obj, 0, sizeof(obj));
    uint8_t output[300];
    memset(output, 0, sizeof(output));
    size_t       out_size        = (size_t) uavcan__node__ExecuteCommand@CV1_3@__Request_SERIALIZATION_BUFFER_SIZE_BYTES_ - 1U;
    const int8_t ser             = uavcan__node__ExecuteCommand@CV1_3@__Request__serialize_(&obj, output, &out_size);
    result->deserialize_rc       = 0;
    result->deserialize_consumed = 0;
    result->serialize_rc         = ser;
    result->serialize_size       = out_size;
    return 0;
}

int c_heartbeat_too_small_serialize(CCaseResult* const result)
{
    if (result == NULL)
    {
        return -1;
    }
    uavcan__node__Heartbeat@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));
    uint8_t output[8];
    memset(output, 0, sizeof(output));
    size_t       out_size        = (size_t) uavcan__node__Heartbeat@CV1_0@_SERIALIZATION_BUFFER_SIZE_BYTES_ - 1U;
    const int8_t ser             = uavcan__node__Heartbeat@CV1_0@__serialize_(&obj, output, &out_size);
    result->deserialize_rc       = 0;
    result->deserialize_consumed = 0;
    result->serialize_rc         = ser;
    result->serialize_size       = out_size;
    return 0;
}

int c_health_saturated_serialize(CCaseResult* const result, uint8_t* const output, const size_t output_capacity)
{
    if ((result == NULL) || (output == NULL))
    {
        return -1;
    }
    uavcan__node__Health@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));
    obj.value = 0xFFU;
    memset(output, 0, output_capacity);
    size_t       out_size        = output_capacity;
    const int8_t ser             = uavcan__node__Health@CV1_0@__serialize_(&obj, output, &out_size);
    result->deserialize_rc       = 0;
    result->deserialize_consumed = 0;
    result->serialize_rc         = ser;
    result->serialize_size       = out_size;
    return 0;
}

int c_synchronized_timestamp_truncated_serialize(CCaseResult* const result,
                                                 uint8_t* const     output,
                                                 const size_t       output_capacity)
{
    if ((result == NULL) || (output == NULL))
    {
        return -1;
    }
    uavcan__time__SynchronizedTimestamp@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));
    obj.microsecond = UINT64_C(0xFEDCBA9876543210);
    memset(output, 0, output_capacity);
    size_t       out_size        = output_capacity;
    const int8_t ser             = uavcan__time__SynchronizedTimestamp@CV1_0@__serialize_(&obj, output, &out_size);
    result->deserialize_rc       = 0;
    result->deserialize_consumed = 0;
    result->serialize_rc         = ser;
    result->serialize_size       = out_size;
    return 0;
}
