#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "dsdl_runtime.h"
#include "uavcan/node/Heartbeat_1_0.h"
#include "uavcan/metatransport/can/Frame_0_2.h"
#include "uavcan/primitive/array/Real32_1_0.h"
#include "uavcan/node/port/SubjectIDList_1_0.h"
#include "uavcan/pnp/NodeIDAllocationData_2_0.h"
#include "uavcan/diagnostic/Record_1_1.h"
#include "uavcan/time/SynchronizedTimestamp_1_0.h"

/* The default and the spec's own definition of it, deserialising nothing, compared where a caller can
 * look: on the wire. Both objects start as garbage, so a member the initialiser left alone would show. */
static int failures = 0;
#define CHECK(NAME, T)                                                                                       \
    do                                                                                                       \
    {                                                                                                        \
        T       a, b;                                                                                        \
        uint8_t wa[4096], wb[4096];                                                                          \
        size_t  na = sizeof wa, nb = sizeof wb, none = 0;                                                    \
        memset(&a, 0xAB, sizeof a);                                                                          \
        memset(&b, 0xAB, sizeof b);                                                                          \
        const int8_t ia = T##__initialize_(&a);                                                              \
        const int8_t db = T##__deserialize_(&b, NULL, &none);                                                \
        const int8_t sa = T##__serialize_(&a, wa, &na), sb = T##__serialize_(&b, wb, &nb);                   \
        const int    same = ia == 0 && db == 0 && sa == 0 && sb == 0 && na == nb && memcmp(wa, wb, na) == 0; \
        printf("%-40s %s (%zu bytes)\n", NAME, same ? "same" : "DIFFER", na);                                \
        if (!same)                                                                                           \
        {                                                                                                    \
            ++failures;                                                                                      \
        }                                                                                                    \
        if (T##__initialize_(NULL) != -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT)                                  \
        {                                                                                                    \
            puts("null accepted");                                                                           \
            ++failures;                                                                                      \
        }                                                                                                    \
    } while (0)

int main(void)
{
    CHECK("uavcan.node.Heartbeat", uavcan__node__Heartbeat);
    CHECK("uavcan.metatransport.can.Frame", uavcan__metatransport__can__Frame);
    CHECK("uavcan.primitive.array.Real32", uavcan__primitive__array__Real32);
    CHECK("uavcan.node.port.SubjectIDList", uavcan__node__port__SubjectIDList);
    CHECK("uavcan.pnp.NodeIDAllocationData", uavcan__pnp__NodeIDAllocationData);
    CHECK("uavcan.diagnostic.Record", uavcan__diagnostic__Record);
    CHECK("uavcan.time.SynchronizedTimestamp", uavcan__time__SynchronizedTimestamp);
    return failures == 0 ? 0 : 1;
}
