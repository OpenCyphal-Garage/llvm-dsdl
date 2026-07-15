//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

// Runtime forward/backward compatibility test for the generated C serializers.
//
// The existing round-trip tests always decode a buffer with the SAME type version that produced it,
// so consumed == the delimiter header and the version-skew skip is never exercised. This driver decodes
// across two versions of a delimited composite (`wire.nar.Inner` has one field; `wire.wid.Inner`
// appends a second), which is the only shape that catches a decoder that advances the outer offset by
// the nested's consumed count instead of the delimiter-header size.
//
//   forward compat : a wide (newer) writer's buffer decoded by a narrow (older) reader must skip the
//                    appended field and still read the trailing `tail` field correctly.
//   zero extension : a narrow (older) writer's buffer decoded by a wide (newer) reader must zero-fill
//                    the field the writer never sent.
//
// Prints one marker per case; the harness compares markers across C/Rust/Go.

#include "wire/nar/Holder_1_0.h"
#include "wire/wid/Holder_1_0.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    // Forward compatibility: wide writer -> narrow reader skips the appended `y`.
    wire__wid__Holder wide_in;
    memset(&wide_in, 0, sizeof wide_in);
    wide_in.inner.x = 1U;
    wide_in.inner.y = 2U;
    wide_in.tail    = 99U;
    uint8_t wire[64];
    size_t  wire_size = sizeof wire;
    if (wire__wid__Holder__serialize_(&wide_in, wire, &wire_size) != 0)
    {
        printf("c_serialize_wide_failed\n");
        return 2;
    }
    wire__nar__Holder narrow_out;
    memset(&narrow_out, 0, sizeof narrow_out);
    size_t narrow_consumed = wire_size;
    if (wire__nar__Holder__deserialize_(&narrow_out, wire, &narrow_consumed) != 0)
    {
        printf("c_deserialize_narrow_failed\n");
        return 3;
    }
    if (narrow_out.inner.x != 1U || narrow_out.tail != 99U)
    {
        printf("c_forward_compat_mismatch x=%u tail=%u\n", narrow_out.inner.x, narrow_out.tail);
        return 4;
    }
    printf("fwdcompat_ok\n");

    // Zero extension: narrow writer -> wide reader zero-fills the absent `y`.
    wire__nar__Holder narrow_in;
    memset(&narrow_in, 0, sizeof narrow_in);
    narrow_in.inner.x = 5U;
    narrow_in.tail    = 77U;
    uint8_t wire2[64];
    size_t  wire2_size = sizeof wire2;
    if (wire__nar__Holder__serialize_(&narrow_in, wire2, &wire2_size) != 0)
    {
        printf("c_serialize_narrow_failed\n");
        return 5;
    }
    wire__wid__Holder wide_out;
    memset(&wide_out, 0xEE, sizeof wide_out);  // poison so a missed zero-extension is visible
    size_t wide_consumed = wire2_size;
    if (wire__wid__Holder__deserialize_(&wide_out, wire2, &wide_consumed) != 0)
    {
        printf("c_deserialize_wide_failed\n");
        return 6;
    }
    if (wide_out.inner.x != 5U || wide_out.inner.y != 0U || wide_out.tail != 77U)
    {
        printf("c_zero_extension_mismatch x=%u y=%u tail=%u\n", wide_out.inner.x, wide_out.inner.y, wide_out.tail);
        return 7;
    }
    printf("zeroext_ok\n");
    return 0;
}
