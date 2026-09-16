//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// One instruction-count measurement of one generated C serialiser.
///
/// The process runs a single (case, implementation, operation) loop so that
/// cachegrind's whole-program total describes that loop and nothing else. The
/// runner invokes each measurement twice, at zero iterations and at the
/// configured count, and takes the difference: process start, the dynamic
/// loader, the fixture search and this file's own printing are all fixed cost
/// and cancel. That subtraction is exact only while the two runs differ in the
/// trip count alone, which is what the iteration argument's fixed digit width
/// and the absence of the count from anything printed here preserve.
///
/// Both implementations are timed on one payload: the fixture is a wire image
/// both deserialise, and the harness requires the two objects to re-serialise
/// to identical bytes before measuring. The differential parity lane is where
/// that agreement is established; it is asserted again here because the
/// comparison means nothing if the two sides are holding different values.
///
/// The `dsdlc` mode is whichever implementation of the published entry points
/// this binary was linked against: the compiled C, or the objects dsdlc emits
/// itself. Both define the same symbols, so they cannot be linked side by side,
/// and the runner builds one driver per implementation and labels the numbers.
///
/// Every call goes through a function pointer, including the `noop` stubs. This
/// project emits its serialisers out of line into `.c` files and the peer emits
/// `static inline` definitions into its headers, and an indirect call is a shape
/// both can be measured behind. It therefore measures the bodies: what an
/// optimiser would make of the peer's body inlined at a real call site, and of
/// ours behind a call it cannot see into, is a different question from this one.
///
//===----------------------------------------------------------------------===//

#include "DifferentialParityABI.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const DifferentialCaseInfo g_ours_case_heartbeat;
extern const DifferentialCaseInfo g_ours_case_execute_command_request;
extern const DifferentialCaseInfo g_ours_case_execute_command_response;
extern const DifferentialCaseInfo g_ours_case_register_value;
extern const DifferentialCaseInfo g_ours_case_can_frame;
extern const DifferentialCaseInfo g_ours_case_real32_array;
extern const DifferentialCaseInfo g_ours_case_subject_id_list;
extern const DifferentialCaseInfo g_ours_case_node_id_alloc;
extern const DifferentialCaseInfo g_ours_case_diag_record;
extern const DifferentialCaseInfo g_ours_case_sync_timestamp;

extern const DifferentialCaseInfo g_nv_case_heartbeat;
extern const DifferentialCaseInfo g_nv_case_execute_command_request;
extern const DifferentialCaseInfo g_nv_case_execute_command_response;
extern const DifferentialCaseInfo g_nv_case_register_value;
extern const DifferentialCaseInfo g_nv_case_can_frame;
extern const DifferentialCaseInfo g_nv_case_real32_array;
extern const DifferentialCaseInfo g_nv_case_subject_id_list;
extern const DifferentialCaseInfo g_nv_case_node_id_alloc;
extern const DifferentialCaseInfo g_nv_case_diag_record;
extern const DifferentialCaseInfo g_nv_case_sync_timestamp;

typedef struct
{
    const char*                 name;
    const DifferentialCaseInfo* dsdlc;
    const DifferentialCaseInfo* nnvg;
} CasePair;

/// The cases and their order are the differential parity lane's, so a case
/// index means the same thing in both lanes' output.
static const CasePair kCases[] = {
    {"uavcan.node.Heartbeat.1.0", &g_ours_case_heartbeat, &g_nv_case_heartbeat},
    {"uavcan.node.ExecuteCommand.Request.1.3",
     &g_ours_case_execute_command_request,
     &g_nv_case_execute_command_request},
    {"uavcan.node.ExecuteCommand.Response.1.3",
     &g_ours_case_execute_command_response,
     &g_nv_case_execute_command_response},
    {"uavcan.register.Value.1.0", &g_ours_case_register_value, &g_nv_case_register_value},
    {"uavcan.metatransport.can.Frame.0.2", &g_ours_case_can_frame, &g_nv_case_can_frame},
    {"uavcan.primitive.array.Real32.1.0", &g_ours_case_real32_array, &g_nv_case_real32_array},
    {"uavcan.node.port.SubjectIDList.1.0", &g_ours_case_subject_id_list, &g_nv_case_subject_id_list},
    {"uavcan.pnp.NodeIDAllocationData.2.0", &g_ours_case_node_id_alloc, &g_nv_case_node_id_alloc},
    {"uavcan.diagnostic.Record.1.1", &g_ours_case_diag_record, &g_nv_case_diag_record},
    {"uavcan.time.SynchronizedTimestamp.1.0", &g_ours_case_sync_timestamp, &g_nv_case_sync_timestamp},
};

enum
{
    /// SubjectIDList.1.0 carries a bool[8192] bitset variant, which is the
    /// largest object and the largest wire image of the ten.
    kObjectCapacity = 16384,
    kWireCapacity   = 4096,
    /// Bounds on the fixture search. Each stage stops early once
    /// @ref kFixtureAcceptances images have been accepted, so a type whose first
    /// draw decodes costs a handful of attempts, while register.Value.1.0, which
    /// accepts about one draw in twenty, still finds several.
    kFixtureAttempts    = 512,
    kFixtureAcceptances = 8,
    /// Bytes disturbed per mutation.
    kMutationBytes = 4,
    /// Digits the iteration argument arrives with. A fixed width keeps its
    /// parsing cost the same at zero iterations as at the full count.
    kIterationDigits = 9,
};

/// Aligned, and not incidentally: a library `memcpy` takes a different number of
/// instructions for the same size at a different alignment, and the two drivers
/// lay their memory out differently. Pinning the alignment is what lets the same
/// code measured in both of them answer the same number.
enum
{
    kBufferAlignment = 64
};

_Alignas(kBufferAlignment) static uint8_t g_candidate[kWireCapacity];
_Alignas(kBufferAlignment) static uint8_t g_fixture[kWireCapacity];
_Alignas(kBufferAlignment) static uint8_t g_dsdlcImage[kWireCapacity];
_Alignas(kBufferAlignment) static uint8_t g_nnvgImage[kWireCapacity];
_Alignas(kBufferAlignment) static uint8_t g_output[kWireCapacity];
_Alignas(kBufferAlignment) static uint8_t g_dsdlcObject[kObjectCapacity];
_Alignas(kBufferAlignment) static uint8_t g_nnvgObject[kObjectCapacity];

/// Written at run time from the selected case, so the stub calls below are
/// reached through a pointer no optimiser can resolve. A const table of stubs
/// would be devirtualised and the loop around it elided, and the scaffolding
/// cost this mode exists to measure would come back as zero.
static DifferentialCaseInfo g_noopInfo;

/// The implementation under measurement, handed to the loops through a volatile.
///
/// Internal linkage and a stub in this file are not an opaque boundary: whole
/// program optimisation can see that this only ever holds one of three known
/// addresses, inline the body -- the empty one especially -- and drop the loop,
/// leaving the scaffolding measured as nothing and the subtraction meaningless.
/// A volatile load has no known value, so every build calls indirectly and all
/// three modes are measured in the same shape, which is what the ratios claim.
static const DifferentialCaseInfo* volatile g_measured;

static int8_t noopSerialize(const void* const obj, uint8_t* const buffer, size_t* const inoutSize)
{
    (void) obj;
    (void) buffer;
    (void) inoutSize;
    return 0;
}

static int8_t noopDeserialize(void* const outObj, const uint8_t* const buffer, size_t* const inoutSize)
{
    (void) outObj;
    (void) buffer;
    (void) inoutSize;
    return 0;
}

static uint64_t g_rngState = UINT64_C(0x9E3779B97F4A7C15);

static uint32_t nextRandomU32(void)
{
    g_rngState ^= g_rngState << 13U;
    g_rngState ^= g_rngState >> 7U;
    g_rngState ^= g_rngState << 17U;
    return (uint32_t) (g_rngState & UINT64_C(0xFFFFFFFF));
}

static void fillRandomBytes(uint8_t* const dst, const size_t size)
{
    for (size_t i = 0; i < size; ++i)
    {
        dst[i] = (uint8_t) (nextRandomU32() & 0xFFU);
    }
}

/// FNV-1a over the fixture, so a number that moves between runs can be pinned
/// on the payload or on the code. Two drivers measuring the same digest are
/// measuring the same work.
static uint32_t digestBytes(const uint8_t* const data, const size_t size)
{
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= (uint32_t) data[i];
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static size_t maxSize(const size_t a, const size_t b)
{
    return (a > b) ? a : b;
}

/// Reads the iteration count from exactly @ref kIterationDigits digits.
static bool parseIterations(const char* const text, size_t* const out)
{
    if (strlen(text) != (size_t) kIterationDigits)
    {
        return false;
    }
    size_t value = 0;
    for (int i = 0; i < kIterationDigits; ++i)
    {
        const char digit = text[i];
        if ((digit < '0') || (digit > '9'))
        {
            return false;
        }
        value = (value * 10U) + (size_t) (digit - '0');
    }
    *out = value;
    return true;
}

typedef enum
{
    kCandidateRejected,
    kCandidateAccepted,
    kCandidateDisagreed,
} CandidateResult;

/// Decodes one candidate image on both sides and re-encodes it. A candidate is
/// accepted when both decode it; the canonical image and the objects holding it
/// are left in the globals.
static CandidateResult tryCandidate(const CasePair* const testCase,
                                    const uint8_t* const  image,
                                    const size_t          available,
                                    size_t* const         canonicalSize)
{
    memset(g_dsdlcObject, 0, testCase->dsdlc->object_size);
    memset(g_nnvgObject, 0, testCase->nnvg->object_size);

    size_t dsdlcConsumed = available;
    size_t nnvgConsumed  = available;
    if ((testCase->dsdlc->deserialize(g_dsdlcObject, image, &dsdlcConsumed) < 0) ||
        (testCase->nnvg->deserialize(g_nnvgObject, image, &nnvgConsumed) < 0))
    {
        return kCandidateRejected;
    }

    size_t dsdlcImageSize = testCase->dsdlc->max_serialized_size;
    size_t nnvgImageSize  = testCase->nnvg->max_serialized_size;
    if ((testCase->dsdlc->serialize(g_dsdlcObject, g_dsdlcImage, &dsdlcImageSize) < 0) ||
        (testCase->nnvg->serialize(g_nnvgObject, g_nnvgImage, &nnvgImageSize) < 0))
    {
        return kCandidateRejected;
    }

    if ((dsdlcImageSize != nnvgImageSize) || (memcmp(g_dsdlcImage, g_nnvgImage, dsdlcImageSize) != 0))
    {
        return kCandidateDisagreed;
    }

    *canonicalSize = dsdlcImageSize;
    return kCandidateAccepted;
}

/// Finds the payload both implementations will be measured on and leaves it in
/// @ref g_fixture, with both objects holding it.
///
/// The search starts from the image a zeroed object encodes to, which every
/// type can produce, and then looks for a larger one: first among random wire
/// images, then among mutations of the best one found. A type whose wire format
/// random bytes can satisfy is measured near its full extent that way. A type
/// carrying delimiter headers rejects almost every random image -- a header has
/// to agree with the length that follows it -- and is measured on what the
/// search did reach, which is why the harness reports where the fixture came
/// from and the report records it.
static bool selectFixture(const CasePair* const testCase, size_t* const fixtureSize, const char** const fixtureSource)
{
    const size_t drawSize = maxSize(testCase->dsdlc->max_serialized_size, testCase->nnvg->max_serialized_size);
    if ((drawSize > sizeof(g_candidate)) || (testCase->dsdlc->object_size > sizeof(g_dsdlcObject)) ||
        (testCase->nnvg->object_size > sizeof(g_nnvgObject)))
    {
        fprintf(stderr, "case %s exceeds the harness buffers\n", testCase->name);
        return false;
    }

    memset(g_dsdlcObject, 0, testCase->dsdlc->object_size);
    memset(g_nnvgObject, 0, testCase->nnvg->object_size);
    size_t dsdlcZeroSize = testCase->dsdlc->max_serialized_size;
    size_t nnvgZeroSize  = testCase->nnvg->max_serialized_size;
    if ((testCase->dsdlc->serialize(g_dsdlcObject, g_dsdlcImage, &dsdlcZeroSize) < 0) ||
        (testCase->nnvg->serialize(g_nnvgObject, g_nnvgImage, &nnvgZeroSize) < 0))
    {
        fprintf(stderr, "case %s: a zeroed object did not encode\n", testCase->name);
        return false;
    }
    if ((dsdlcZeroSize != nnvgZeroSize) || (memcmp(g_dsdlcImage, g_nnvgImage, dsdlcZeroSize) != 0))
    {
        fprintf(stderr,
                "case %s: the two implementations encode a zeroed object "
                "differently, so they would be measured on different values\n",
                testCase->name);
        return false;
    }
    size_t best = dsdlcZeroSize;
    memcpy(g_fixture, g_dsdlcImage, dsdlcZeroSize);
    const char* source = "zero";

    int accepted = 0;
    for (int attempt = 0; (attempt < kFixtureAttempts) && (accepted < kFixtureAcceptances); ++attempt)
    {
        fillRandomBytes(g_candidate, drawSize);
        size_t                canonicalSize = 0;
        const CandidateResult result        = tryCandidate(testCase, g_candidate, drawSize, &canonicalSize);
        if (result == kCandidateDisagreed)
        {
            fprintf(stderr,
                    "case %s: the two implementations disagree on the wire image of a "
                    "random candidate\n",
                    testCase->name);
            return false;
        }
        if (result == kCandidateRejected)
        {
            continue;
        }
        ++accepted;
        if (canonicalSize > best)
        {
            best = canonicalSize;
            memcpy(g_fixture, g_dsdlcImage, canonicalSize);
            source = "random";
        }
    }

    accepted = 0;
    for (int attempt = 0; (attempt < kFixtureAttempts) && (accepted < kFixtureAcceptances); ++attempt)
    {
        memcpy(g_candidate, g_fixture, best);
        if (drawSize > best)
        {
            fillRandomBytes(&g_candidate[best], drawSize - best);
        }
        for (int i = 0; i < kMutationBytes; ++i)
        {
            g_candidate[nextRandomU32() % (uint32_t) best] = (uint8_t) (nextRandomU32() & 0xFFU);
        }
        size_t                canonicalSize = 0;
        const CandidateResult result        = tryCandidate(testCase, g_candidate, drawSize, &canonicalSize);
        if (result == kCandidateDisagreed)
        {
            fprintf(stderr,
                    "case %s: the two implementations disagree on the wire image of a "
                    "mutated candidate\n",
                    testCase->name);
            return false;
        }
        if (result == kCandidateRejected)
        {
            continue;
        }
        ++accepted;
        if (canonicalSize > best)
        {
            best = canonicalSize;
            memcpy(g_fixture, g_dsdlcImage, canonicalSize);
            source = "mutated";
        }
    }

    size_t canonicalSize = 0;
    if (tryCandidate(testCase, g_fixture, best, &canonicalSize) != kCandidateAccepted)
    {
        fprintf(stderr, "case %s: the chosen fixture was rejected on replay\n", testCase->name);
        return false;
    }

    *fixtureSize   = best;
    *fixtureSource = source;
    return true;
}

static int measureEncode(const DifferentialCaseInfo* const info, const void* const object, const size_t iterations)
{
    for (size_t i = 0; i < iterations; ++i)
    {
        size_t size = info->max_serialized_size;
        if (info->serialize(object, g_output, &size) < 0)
        {
            return 1;
        }
    }
    return 0;
}

static int measureDecode(const DifferentialCaseInfo* const info,
                         void* const                       object,
                         const size_t                      fixtureSize,
                         const size_t                      iterations)
{
    for (size_t i = 0; i < iterations; ++i)
    {
        size_t size = fixtureSize;
        if (info->deserialize(object, g_fixture, &size) < 0)
        {
            return 1;
        }
    }
    return 0;
}

static void usage(void)
{
    fprintf(stderr,
            "usage: <case-index> <dsdlc|nnvg|noop> <encode|decode> <iterations>\n"
            "       iterations is decimal, zero padded to %d digits\n",
            kIterationDigits);
}

int main(int argc, char** argv)
{
    if (argc != 5)
    {
        usage();
        return 2;
    }

    const size_t        caseCount   = sizeof(kCases) / sizeof(kCases[0]);
    char*               indexEnd    = NULL;
    const unsigned long parsedIndex = strtoul(argv[1], &indexEnd, 10);
    if ((indexEnd == NULL) || (*indexEnd != '\0') || (parsedIndex >= (unsigned long) caseCount))
    {
        usage();
        return 2;
    }
    const CasePair* const testCase = &kCases[parsedIndex];

    const bool isDsdlc = strcmp(argv[2], "dsdlc") == 0;
    const bool isNnvg  = strcmp(argv[2], "nnvg") == 0;
    const bool isNoop  = strcmp(argv[2], "noop") == 0;
    if (!isDsdlc && !isNnvg && !isNoop)
    {
        usage();
        return 2;
    }

    const bool isEncode = strcmp(argv[3], "encode") == 0;
    const bool isDecode = strcmp(argv[3], "decode") == 0;
    if (!isEncode && !isDecode)
    {
        usage();
        return 2;
    }

    size_t iterations = 0;
    if (!parseIterations(argv[4], &iterations))
    {
        usage();
        return 2;
    }

    size_t      fixtureSize   = 0;
    const char* fixtureSource = NULL;
    if (!selectFixture(testCase, &fixtureSize, &fixtureSource))
    {
        return 1;
    }

    // Identical in both runs of a measurement, and the runner reads the fixture
    // size from it. Deliberately not the iteration count: printing that would
    // put a digit-dependent cost into the difference.
    printf("FIXTURE %s wire_bytes=%zu source=%s digest=%08x\n",
           testCase->name,
           fixtureSize,
           fixtureSource,
           digestBytes(g_fixture, fixtureSize));

    const DifferentialCaseInfo* info   = NULL;
    void*                       object = NULL;
    if (isDsdlc)
    {
        info   = testCase->dsdlc;
        object = g_dsdlcObject;
    }
    else if (isNnvg)
    {
        info   = testCase->nnvg;
        object = g_nnvgObject;
    }
    else
    {
        g_noopInfo.object_size         = testCase->dsdlc->object_size;
        g_noopInfo.max_serialized_size = testCase->dsdlc->max_serialized_size;
        g_noopInfo.serialize           = &noopSerialize;
        g_noopInfo.deserialize         = &noopDeserialize;
        info                           = &g_noopInfo;
        object                         = g_dsdlcObject;
    }

    g_measured                                 = info;
    const DifferentialCaseInfo* const measured = g_measured;
    const int                         result   = isEncode ? measureEncode(measured, object, iterations)
                                                          : measureDecode(measured, object, fixtureSize, iterations);
    if (result != 0)
    {
        fprintf(stderr, "case %s: the measured call failed mid-loop\n", testCase->name);
        return 1;
    }
    return 0;
}
