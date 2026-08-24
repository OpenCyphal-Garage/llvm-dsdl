//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// C ABI harness for signed-narrow C-vs-Rust parity integration tests.
///
/// This harness exports randomized and directed entry points for the signed
/// 3-bit saturation/truncation fixtures used by the Rust parity driver.
///
//===----------------------------------------------------------------------===//

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "vendor/Int3Sat_1_0.h"
#include "vendor/Int3Trunc_1_0.h"

typedef struct CCaseResult
{
    int8_t deserialize_rc;
    size_t deserialize_consumed;
    int8_t serialize_rc;
    size_t serialize_size;
} CCaseResult;

static int run_int3sat(const uint8_t* const input,
                       const size_t         input_size,
                       uint8_t* const       output,
                       const size_t         output_capacity,
                       CCaseResult* const   result)
{
    vendor__Int3Sat@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));

    size_t       consumed        = input_size;
    const int8_t des             = vendor__Int3Sat@CV1_0@__deserialize_(&obj, input, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    if (des < 0)
    {
        return 0;
    }

    size_t       out_size  = output_capacity;
    const int8_t ser       = vendor__Int3Sat@CV1_0@__serialize_(&obj, output, &out_size);
    result->serialize_rc   = ser;
    result->serialize_size = out_size;
    return 0;
}

static int run_int3trunc(const uint8_t* const input,
                         const size_t         input_size,
                         uint8_t* const       output,
                         const size_t         output_capacity,
                         CCaseResult* const   result)
{
    vendor__Int3Trunc@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));

    size_t       consumed        = input_size;
    const int8_t des             = vendor__Int3Trunc@CV1_0@__deserialize_(&obj, input, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    if (des < 0)
    {
        return 0;
    }

    size_t       out_size  = output_capacity;
    const int8_t ser       = vendor__Int3Trunc@CV1_0@__serialize_(&obj, output, &out_size);
    result->serialize_rc   = ser;
    result->serialize_size = out_size;
    return 0;
}

int c_int3sat_roundtrip(const uint8_t* const input,
                        const size_t         input_size,
                        uint8_t* const       output,
                        const size_t         output_capacity,
                        CCaseResult* const   result)
{
    if ((input == NULL) || (output == NULL) || (result == NULL))
    {
        return -1;
    }
    return run_int3sat(input, input_size, output, output_capacity, result);
}

int c_int3trunc_roundtrip(const uint8_t* const input,
                          const size_t         input_size,
                          uint8_t* const       output,
                          const size_t         output_capacity,
                          CCaseResult* const   result)
{
    if ((input == NULL) || (output == NULL) || (result == NULL))
    {
        return -1;
    }
    return run_int3trunc(input, input_size, output, output_capacity, result);
}

int c_int3sat_directed_serialize(const int8_t       value,
                                 uint8_t* const     output,
                                 const size_t       output_capacity,
                                 CCaseResult* const result)
{
    if ((output == NULL) || (result == NULL))
    {
        return -1;
    }

    vendor__Int3Sat@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));
    obj.value = value;

    result->deserialize_rc       = 0;
    result->deserialize_consumed = 0;

    size_t       out_size  = output_capacity;
    const int8_t ser       = vendor__Int3Sat@CV1_0@__serialize_(&obj, output, &out_size);
    result->serialize_rc   = ser;
    result->serialize_size = out_size;
    return 0;
}

int c_int3trunc_directed_serialize(const int8_t       value,
                                   uint8_t* const     output,
                                   const size_t       output_capacity,
                                   CCaseResult* const result)
{
    if ((output == NULL) || (result == NULL))
    {
        return -1;
    }

    vendor__Int3Trunc@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));
    obj.value = value;

    result->deserialize_rc       = 0;
    result->deserialize_consumed = 0;

    size_t       out_size  = output_capacity;
    const int8_t ser       = vendor__Int3Trunc@CV1_0@__serialize_(&obj, output, &out_size);
    result->serialize_rc   = ser;
    result->serialize_size = out_size;
    return 0;
}

int c_int3sat_deserialize_value(const uint8_t sample, int8_t* const out_value, CCaseResult* const result)
{
    if ((out_value == NULL) || (result == NULL))
    {
        return -1;
    }

    vendor__Int3Sat@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));

    size_t       consumed        = 1U;
    const int8_t des             = vendor__Int3Sat@CV1_0@__deserialize_(&obj, &sample, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    *out_value                   = obj.value;
    return 0;
}

int c_int3trunc_deserialize_value(const uint8_t sample, int8_t* const out_value, CCaseResult* const result)
{
    if ((out_value == NULL) || (result == NULL))
    {
        return -1;
    }

    vendor__Int3Trunc@CV1_0@ obj;
    memset(&obj, 0, sizeof(obj));

    size_t       consumed        = 1U;
    const int8_t des             = vendor__Int3Trunc@CV1_0@__deserialize_(&obj, &sample, &consumed);
    result->deserialize_rc       = des;
    result->deserialize_consumed = consumed;
    result->serialize_rc         = 0;
    result->serialize_size       = 0;
    *out_value                   = obj.value;
    return 0;
}
