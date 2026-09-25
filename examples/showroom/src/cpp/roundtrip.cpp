//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// Shared by every C++ recipe in the matrix. Nothing here is specific to a build system: if this
// compiles and exits zero, the build found the generated headers, compiled against them, and could
// call them. That is the entire claim a build integration makes.
//
// It is not a serialiser test. Whether the bytes are right is settled by test/lit and
// test/integration against the specification and the Dafny model. What this asserts is round-trip
// identity -- the weakest property that cannot pass by accident if the wiring is wrong -- for the
// `lanyard.health.SystemHealth.1.0` the other rows use, and for a wire-flat type read back through
// its field accessors. The accessors take a span, and which span is the build's decision: the
// recipe's vocabulary file binds it, and the type named below is the one it bound.
//
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "cetl/pf20/span.hpp"
#include "lanyard/health/SystemHealth_1_0.hpp"
#include "lanyard/link/TelemetryLinkStats_1_0.hpp"

#define CHECK(cond, ...)                         \
    do                                           \
    {                                            \
        if (!(cond))                             \
        {                                        \
            (void) std::fprintf(stderr, "FAIL: "); \
            (void) std::fprintf(stderr, __VA_ARGS__); \
            (void) std::fprintf(stderr, "\n");   \
            return 1;                            \
        }                                        \
    } while (0)

namespace
{

const char* const kSubsystemNames[] = {"gnss", "esc.3", "imu.0"};

int roundTripSystemHealth()
{
    using lanyard::health::SystemHealth;

    // Deliberately not the zero value: an integration that serialised nothing and deserialised
    // nothing would round-trip a zeroed object and prove nothing.
    SystemHealth original;
    original.timestamp.microsecond  = 1234567890123ULL;
    original.aggregate_health.value = 2U;  // CAUTION, on the standard four-level scale
    original.subsystem.resize(sizeof(kSubsystemNames) / sizeof(kSubsystemNames[0]));
    for (std::size_t i = 0U; i < original.subsystem.size(); ++i)
    {
        auto& report          = original.subsystem[i];
        report.health.value   = static_cast<std::uint8_t>(i % 4U);
        report.severity.value = static_cast<std::uint8_t>(i % 8U);
        report.fault_code     = static_cast<std::uint16_t>(0x1000U + i);
        report.name.resize(std::strlen(kSubsystemNames[i]));
        std::memcpy(report.name.data(), kSubsystemNames[i], report.name.size());
    }

    std::uint8_t buffer[SystemHealth::SERIALIZATION_BUFFER_SIZE_BYTES];
    std::size_t  size = sizeof(buffer);
    const auto   ser  = original.serialize(buffer, &size);
    CHECK(ser == DSDL_RUNTIME_SUCCESS, "serialize returned %d", static_cast<int>(ser));

    SystemHealth restored;
    std::size_t  consumed = size;
    const auto   des      = restored.deserialize(buffer, &consumed);
    CHECK(des == DSDL_RUNTIME_SUCCESS, "deserialize returned %d", static_cast<int>(des));

    CHECK(restored.timestamp.microsecond == original.timestamp.microsecond, "timestamp differs");
    CHECK(restored.aggregate_health.value == original.aggregate_health.value, "aggregate_health differs");
    CHECK(restored.subsystem.size() == original.subsystem.size(),
          "subsystem.count: %zu != %zu",
          restored.subsystem.size(),
          original.subsystem.size());
    for (std::size_t i = 0U; i < original.subsystem.size(); ++i)
    {
        const auto& a = original.subsystem[i];
        const auto& b = restored.subsystem[i];
        CHECK(a.health.value == b.health.value, "subsystem[%zu].health differs", i);
        CHECK(a.severity.value == b.severity.value, "subsystem[%zu].severity differs", i);
        CHECK(a.fault_code == b.fault_code, "subsystem[%zu].fault_code differs", i);
        CHECK(a.name.size() == b.name.size(), "subsystem[%zu].name.count differs", i);
        CHECK(std::memcmp(a.name.data(), b.name.data(), a.name.size()) == 0, "subsystem[%zu].name differs", i);
    }

    (void) std::printf("round-trip OK: %zu subsystems, %zu bytes on the wire\n", restored.subsystem.size(), size);
    return 0;
}

int readLinkStatsThroughAccessors()
{
    using lanyard::link::TelemetryLinkStats;
    using uavcan::time::SynchronizedTimestamp;

    TelemetryLinkStats original;
    original.timestamp.microsecond = 987654321ULL;
    original.rssi_dbm              = -71;
    original.noise_floor_dbm       = -98;
    original.link_quality_pct      = 87U;
    original.tx_packets            = 40012U;
    original.rx_packets            = 39987U;

    std::uint8_t buffer[TelemetryLinkStats::SERIALIZATION_BUFFER_SIZE_BYTES];
    std::size_t  size = sizeof(buffer);
    const auto   ser  = original.serialize(buffer, &size);
    CHECK(ser == DSDL_RUNTIME_SUCCESS, "link stats serialize returned %d", static_cast<int>(ser));

    // The wire, read field by field without a deserialise. The span type is the one the recipe's
    // vocabulary bound; a nested type's getter answers a span for the nested type's own getters.
    const cetl::pf20::span<const std::uint8_t> wire(buffer, size);
    CHECK(TelemetryLinkStats::get_rssi_dbm(wire) == original.rssi_dbm, "rssi_dbm read differs");
    CHECK(TelemetryLinkStats::get_noise_floor_dbm(wire) == original.noise_floor_dbm, "noise_floor_dbm read differs");
    CHECK(TelemetryLinkStats::get_link_quality_pct(wire) == original.link_quality_pct,
          "link_quality_pct read differs");
    CHECK(TelemetryLinkStats::get_tx_packets(wire) == original.tx_packets, "tx_packets read differs");
    CHECK(TelemetryLinkStats::get_rx_packets(wire) == original.rx_packets, "rx_packets read differs");
    CHECK(SynchronizedTimestamp::get_microsecond(TelemetryLinkStats::get_timestamp(wire)) ==
              original.timestamp.microsecond,
          "timestamp read through the nested getter differs");

    (void) std::printf("accessors OK: %zu bytes read field by field\n", size);
    return 0;
}

}  // namespace

int main()
{
    const int health = roundTripSystemHealth();
    if (health != 0)
    {
        return health;
    }
    return readLinkStatsThroughAccessors();
}
