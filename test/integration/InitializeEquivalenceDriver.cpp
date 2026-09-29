#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "uavcan/node/Heartbeat_1_0.hpp"
#include "uavcan/metatransport/can/Frame_0_2.hpp"
#include "uavcan/primitive/array/Real32_1_0.hpp"
#include "uavcan/node/port/SubjectIDList_1_0.hpp"
#include "uavcan/pnp/NodeIDAllocationData_2_0.hpp"
#include "uavcan/diagnostic/Record_1_1.hpp"
#include "uavcan/time/SynchronizedTimestamp_1_0.hpp"

// The default is what the member initialisers say; the spec's definition is deserialising nothing.
template <typename T>
bool check(const char* name)
{
    T                         a{};
    T                         b{};
    std::size_t               none = 0;
    const std::int8_t         db   = b.deserialize(nullptr, &none);
    std::vector<std::uint8_t> wa(4096), wb(4096);
    std::size_t               na = wa.size(), nb = wb.size();
    const std::int8_t         sa = a.serialize(wa.data(), &na);
    const std::int8_t         sb = b.serialize(wb.data(), &nb);
    const bool same = db == 0 && sa == 0 && sb == 0 && na == nb && std::memcmp(wa.data(), wb.data(), na) == 0;
    std::printf("%-40s %s (%zu bytes)\n", name, same ? "same" : "DIFFER", na);
    return same;
}

int main()
{
    int failures = 0;
    failures += check<uavcan::node::Heartbeat>("uavcan.node.Heartbeat") ? 0 : 1;
    failures += check<uavcan::metatransport::can::Frame>("uavcan.metatransport.can.Frame") ? 0 : 1;
    failures += check<uavcan::primitive::array::Real32>("uavcan.primitive.array.Real32") ? 0 : 1;
    failures += check<uavcan::node::port::SubjectIDList>("uavcan.node.port.SubjectIDList") ? 0 : 1;
    failures += check<uavcan::pnp::NodeIDAllocationData>("uavcan.pnp.NodeIDAllocationData") ? 0 : 1;
    failures += check<uavcan::diagnostic::Record>("uavcan.diagnostic.Record") ? 0 : 1;
    failures += check<uavcan::time::SynchronizedTimestamp>("uavcan.time.SynchronizedTimestamp") ? 0 : 1;
    return failures == 0 ? 0 : 1;
}
