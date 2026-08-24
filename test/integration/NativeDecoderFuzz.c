//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// Coverage-guided fuzz harness over the *generated native C deserializers*.
//
// This is the load-bearing safety test the project previously lacked: it feeds
// arbitrary attacker-controlled bytes straight into the generated `deserialize_`
// entrypoints (the code path that consumes untrusted wire payloads) while the
// whole translation set is compiled under AddressSanitizer + UndefinedBehavior
// Sanitizer. Any out-of-bounds read, signed-overflow, or misaligned access in a
// generated decoder becomes a hard failure instead of silent corruption.
//
// The curated type set deliberately covers the adversarial shapes called out in
// the architecture review:
//   * Heartbeat            — simple fixed-size message (baseline).
//   * Health / Integer8    — narrow scalar width + signedness normalization.
//   * Frame (union)        — tagged union dispatch (invalid tag rejection).
//   * ExecuteCommand.{Req,Resp} — variable-length arrays with length prefixes.
//   * port.List            — nested delimited composites + union-of-composites
//                            + fixed-array-of-variable-array (the deepest path).
//
// Three build modes share one input function:
//   * default (libFuzzer)   — libFuzzer supplies main(); coverage-guided.
//   * -DNDF_MODE_SEEDS      — main() emits a valid seed-corpus file per type.
//   * -DNDF_MODE_REPLAY     — main(argc,argv) replays each file argument once,
//                             so a committed regression corpus stays meaningful
//                             even on a toolchain without libFuzzer, still under
//                             ASan/UBSan.
//
//===----------------------------------------------------------------------===//

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "uavcan/metatransport/can/Frame_0_2.h"
#include "uavcan/node/ExecuteCommand_1_3.h"
#include "uavcan/node/Health_1_0.h"
#include "uavcan/node/Heartbeat_1_0.h"
#include "uavcan/node/port/List_1_0.h"
#include "uavcan/primitive/scalar/Integer8_1_0.h"

// Largest generated serialization buffer across the curated set (port.List is by
// far the biggest). A compile-time check below guards against this drifting.
#define NDF_SCRATCH_BYTES 65536U

// Round-trip one decoded object through serialize_ so the serialize path is also
// exercised on adversarially-shaped-but-accepted inputs. `capacity` is the
// type's own SERIALIZATION_BUFFER_SIZE macro so serialize never legitimately
// overflows; ASan catches it if the generated code does.
#define NDF_HANDLE(TYPE, DESER, SER, BUFSZ)                                        \
    do {                                                                           \
        TYPE _obj;                                                                 \
        memset(&_obj, 0, sizeof(_obj));                                            \
        size_t _consumed = size;                                                   \
        const int8_t _rc = DESER(&_obj, data, &_consumed);                         \
        if (_rc >= 0) {                                                            \
            static uint8_t _out[NDF_SCRATCH_BYTES];                                \
            size_t         _outsz = (size_t)(BUFSZ);                               \
            (void)SER(&_obj, _out, &_outsz);                                       \
        }                                                                          \
    } while (0)

// Number of curated decoder targets; the first payload byte selects among them.
#define NDF_TARGET_COUNT 7U

static void ndf_dispatch(unsigned target, const uint8_t* data, size_t size)
{
    switch (target % NDF_TARGET_COUNT) {
    case 0U:
        NDF_HANDLE(uavcan__node__Heartbeat@CV1_0@,
                   uavcan__node__Heartbeat@CV1_0@__deserialize_,
                   uavcan__node__Heartbeat@CV1_0@__serialize_,
                   uavcan__node__Heartbeat@CV1_0@_SERIALIZATION_BUFFER_SIZE_BYTES_);
        break;
    case 1U:
        NDF_HANDLE(uavcan__node__Health@CV1_0@,
                   uavcan__node__Health@CV1_0@__deserialize_,
                   uavcan__node__Health@CV1_0@__serialize_,
                   uavcan__node__Health@CV1_0@_SERIALIZATION_BUFFER_SIZE_BYTES_);
        break;
    case 2U:
        NDF_HANDLE(uavcan__primitive__scalar__Integer8@CV1_0@,
                   uavcan__primitive__scalar__Integer8@CV1_0@__deserialize_,
                   uavcan__primitive__scalar__Integer8@CV1_0@__serialize_,
                   uavcan__primitive__scalar__Integer8@CV1_0@_SERIALIZATION_BUFFER_SIZE_BYTES_);
        break;
    case 3U:
        NDF_HANDLE(uavcan__metatransport__can__Frame@CV0_2@,
                   uavcan__metatransport__can__Frame@CV0_2@__deserialize_,
                   uavcan__metatransport__can__Frame@CV0_2@__serialize_,
                   uavcan__metatransport__can__Frame@CV0_2@_SERIALIZATION_BUFFER_SIZE_BYTES_);
        break;
    case 4U:
        NDF_HANDLE(uavcan__node__ExecuteCommand@CV1_3@__Request,
                   uavcan__node__ExecuteCommand@CV1_3@__Request__deserialize_,
                   uavcan__node__ExecuteCommand@CV1_3@__Request__serialize_,
                   uavcan__node__ExecuteCommand@CV1_3@__Request_SERIALIZATION_BUFFER_SIZE_BYTES_);
        break;
    case 5U:
        NDF_HANDLE(uavcan__node__ExecuteCommand@CV1_3@__Response,
                   uavcan__node__ExecuteCommand@CV1_3@__Response__deserialize_,
                   uavcan__node__ExecuteCommand@CV1_3@__Response__serialize_,
                   uavcan__node__ExecuteCommand@CV1_3@__Response_SERIALIZATION_BUFFER_SIZE_BYTES_);
        break;
    default:
        NDF_HANDLE(uavcan__node__port__List@CV1_0@,
                   uavcan__node__port__List@CV1_0@__deserialize_,
                   uavcan__node__port__List@CV1_0@__serialize_,
                   uavcan__node__port__List@CV1_0@_SERIALIZATION_BUFFER_SIZE_BYTES_);
        break;
    }
}

// Fail the build if any curated type outgrows the shared scratch buffer.
typedef char ndf_scratch_fits_list[(uavcan__node__port__List@CV1_0@_SERIALIZATION_BUFFER_SIZE_BYTES_
                                    <= NDF_SCRATCH_BYTES)
                                       ? 1
                                       : -1];

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size == 0U) {
        return 0;
    }
    // First byte selects the decoder; the remainder is the untrusted payload.
    const unsigned target = (unsigned)data[0];
    ndf_dispatch(target, data + 1, size - 1U);
    return 0;
}

#if defined(NDF_MODE_SEEDS) || defined(NDF_MODE_REPLAY)
#include <stdio.h>
#include <stdlib.h>
#endif

#if defined(NDF_MODE_SEEDS)
// Emit one valid seed per curated target: a default-constructed object,
// serialized, prefixed with the selector byte so it routes to its own decoder.
static int ndf_emit_seed(const char* dir, unsigned target, const uint8_t* body, size_t body_len)
{
    char path[4096];
    (void)snprintf(path, sizeof(path), "%s/seed_%02u.bin", dir, target % NDF_TARGET_COUNT);
    FILE* fp = fopen(path, "wb");
    if (fp == NULL) {
        (void)fprintf(stderr, "cannot open seed file: %s\n", path);
        return 1;
    }
    const uint8_t sel = (uint8_t)(target % NDF_TARGET_COUNT);
    (void)fwrite(&sel, 1U, 1U, fp);
    if (body_len > 0U) {
        (void)fwrite(body, 1U, body_len, fp);
    }
    (void)fclose(fp);
    return 0;
}

#define NDF_EMIT(TARGET, TYPE, SER, BUFSZ)                                         \
    do {                                                                           \
        TYPE _obj;                                                                 \
        memset(&_obj, 0, sizeof(_obj));                                            \
        static uint8_t _buf[NDF_SCRATCH_BYTES];                                    \
        size_t         _sz = (size_t)(BUFSZ);                                      \
        if (SER(&_obj, _buf, &_sz) >= 0) {                                         \
            rc |= ndf_emit_seed(dir, (TARGET), _buf, _sz);                         \
        }                                                                          \
    } while (0)

int main(int argc, char** argv)
{
    if (argc < 2) {
        (void)fprintf(stderr, "usage: %s <corpus-dir>\n", argv[0]);
        return 2;
    }
    const char* dir = argv[1];
    int         rc  = 0;
    NDF_EMIT(0U, uavcan__node__Heartbeat@CV1_0@, uavcan__node__Heartbeat@CV1_0@__serialize_,
             uavcan__node__Heartbeat@CV1_0@_SERIALIZATION_BUFFER_SIZE_BYTES_);
    NDF_EMIT(1U, uavcan__node__Health@CV1_0@, uavcan__node__Health@CV1_0@__serialize_,
             uavcan__node__Health@CV1_0@_SERIALIZATION_BUFFER_SIZE_BYTES_);
    NDF_EMIT(2U, uavcan__primitive__scalar__Integer8@CV1_0@, uavcan__primitive__scalar__Integer8@CV1_0@__serialize_,
             uavcan__primitive__scalar__Integer8@CV1_0@_SERIALIZATION_BUFFER_SIZE_BYTES_);
    NDF_EMIT(3U, uavcan__metatransport__can__Frame@CV0_2@, uavcan__metatransport__can__Frame@CV0_2@__serialize_,
             uavcan__metatransport__can__Frame@CV0_2@_SERIALIZATION_BUFFER_SIZE_BYTES_);
    NDF_EMIT(4U, uavcan__node__ExecuteCommand@CV1_3@__Request, uavcan__node__ExecuteCommand@CV1_3@__Request__serialize_,
             uavcan__node__ExecuteCommand@CV1_3@__Request_SERIALIZATION_BUFFER_SIZE_BYTES_);
    NDF_EMIT(5U, uavcan__node__ExecuteCommand@CV1_3@__Response, uavcan__node__ExecuteCommand@CV1_3@__Response__serialize_,
             uavcan__node__ExecuteCommand@CV1_3@__Response_SERIALIZATION_BUFFER_SIZE_BYTES_);
    NDF_EMIT(6U, uavcan__node__port__List@CV1_0@, uavcan__node__port__List@CV1_0@__serialize_,
             uavcan__node__port__List@CV1_0@_SERIALIZATION_BUFFER_SIZE_BYTES_);
    return rc;
}
#elif defined(NDF_MODE_REPLAY)
// Replay each file argument once through the input function under ASan/UBSan.
// Used both to regression-check committed crashers and as the no-libFuzzer
// fallback so the lane still runs (deterministically) on such a toolchain.
int main(int argc, char** argv)
{
    static uint8_t buf[NDF_SCRATCH_BYTES];
    int            rc = 0;
    for (int i = 1; i < argc; ++i) {
        FILE* fp = fopen(argv[i], "rb");
        if (fp == NULL) {
            (void)fprintf(stderr, "cannot open replay input: %s\n", argv[i]);
            rc = 1;
            continue;
        }
        const size_t n = fread(buf, 1U, sizeof(buf), fp);
        (void)fclose(fp);
        (void)LLVMFuzzerTestOneInput(buf, n);
    }
    (void)fprintf(stdout, "native-decoder-fuzz-replay-ok files=%d\n", argc - 1);
    return rc;
}
#endif
