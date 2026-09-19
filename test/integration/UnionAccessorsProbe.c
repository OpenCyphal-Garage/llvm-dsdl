//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// The accessors of an equal-length union, against its own decode: the tag at offset nought, every option after it, a
// composite option's bytes, a short buffer, and selecting an option through the tag setter.
//
//===----------------------------------------------------------------------===//

#include "dsdl_runtime.h"
#include "fixtures_union/vendor/Choice_1_0.h"
#include "fixtures_union/vendor/Quad_1_0.h"
#include <stdio.h>
#include <string.h>
static int  failures = 0;
static void check(const char* what, int ok)
{
    printf("  %-44s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok)
        ++failures;
}
int main(void)
{
    /* Wire: tag 1 (real) then float32 5.5 */
    uint8_t     wire[5] = {1, 0, 0, 0, 0};
    const float real    = 5.5f;
    memcpy(wire + 1, &real, 4);
    fixtures_union__vendor__Choice obj;
    size_t                         size = sizeof wire;
    check("deserialise accepted", fixtures_union__vendor__Choice__deserialize_(&obj, wire, &size) == 0);
    check("tag read at offset nought",
          fixtures_union__vendor__Choice__get__tag__(wire, sizeof wire) == 1 && obj._tag_ == 1);
    check("option read after the tag",
          fixtures_union__vendor__Choice__get_real_(wire, sizeof wire) == 5.5f && obj.real == 5.5f);
    check("another option reads the same bytes",
          fixtures_union__vendor__Choice__get_word_(wire, sizeof wire) == 0x40B00000U);
    check("element option reads a byte", fixtures_union__vendor__Choice__get_bytes_(wire, sizeof wire, 3) == 0x40);
    size_t         n = 0;
    const uint8_t* q = fixtures_union__vendor__Choice__get_quad_(wire, sizeof wire, &n);
    check("composite option answers the bytes after the tag",
          q == wire + 1 && n == 4 && fixtures_union__vendor__Quad__get_bytes_(q, n, 3) == 0x40);
    check("short buffer reads the tag and zeros",
          fixtures_union__vendor__Choice__get__tag__(wire, 1) == 1 &&
              fixtures_union__vendor__Choice__get_real_(wire, 1) == 0.0f);
    /* Select the word option and write it, then decode. */
    uint8_t out[5];
    memset(out, 0xEE, sizeof out);
    check("tag setter selects",
          fixtures_union__vendor__Choice__set__tag__(out,
                                                     sizeof out,
                                                     fixtures_union__vendor__Choice_WORD_OPTION_TAG_) == 0);
    check("option setter writes", fixtures_union__vendor__Choice__set_word_(out, sizeof out, 0xAABBCCDDU) == 0);
    size = sizeof out;
    check("decode sees the selection",
          fixtures_union__vendor__Choice__deserialize_(&obj, out, &size) == 0 &&
              obj._tag_ == fixtures_union__vendor__Choice_WORD_OPTION_TAG_ && obj.word == 0xAABBCCDDU);
    check("setter refuses a short buffer",
          fixtures_union__vendor__Choice__set_word_(out, 4, 1U) == -DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL);
    printf("union-accessors %s: %s\n", TARGET_NAME, failures == 0 ? "ok" : "FAILED");
    return failures == 0 ? 0 : 1;
}
