# lanyard.link.TelemetryLinkStats.1.0

Health of the air-to-ground telemetry link, published at 1 Hz by the

| | |
|---|---|
| Full name | `lanyard.link.TelemetryLinkStats` |
| Version | 1.0 |
| Kind | Message |
| Fixed port ID | 6241 |
| Transport tier | CAN FD |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Message | 24 | 24 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# Health of the air-to-ground telemetry link, published at 1 Hz by the
# radio modem node.
#
# TRANSPORT TIER: CAN FD.

uavcan.time.SynchronizedTimestamp.1.0 timestamp
# Moment the counters below were sampled. Because every node timestamps
# against the same synchronized clock, a ground station can align this
# sample with the vehicle state that produced it without assuming
# anything about bus latency.

int8 rssi_dbm
# Received signal strength in dBm at the air end of the link. Signed and
# in real units, unlike the receiver-specific scale in RcInput.1.0,
# because a modem reports a calibrated figure.

int8 noise_floor_dbm
# Measured noise floor in dBm. The difference between this and rssi_dbm
# is the link margin, which is the number an operator actually watches.

uint8 link_quality_pct
# Percentage of expected packets successfully received over the last
# second, 0 to 100.

uint32 tx_packets
# Packets transmitted since power-on.

uint32 rx_packets
# Packets received since power-on.

uint32 dropped_packets
# Packets lost since power-on, as counted by sequence number gaps.

uint16 round_trip_ms
# Most recent measured round-trip time in milliseconds.

@assert _offset_.max <= 63 * 8
# Must remain a single CAN FD frame.

@sealed
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* Health of the air-to-ground telemetry link, published at 1 Hz by the */
    /* radio modem node. */
    /*  */
    /* TRANSPORT TIER: CAN FD. */
    typedef struct lanyard__link__TelemetryLinkStats {
      /* Moment the counters below were sampled. Because every node timestamps */
      /* against the same synchronized clock, a ground station can align this */
      /* sample with the vehicle state that produced it without assuming */
      /* anything about bus latency. */
      uavcan__time__SynchronizedTimestamp timestamp;
      /* Received signal strength in dBm at the air end of the link. Signed and */
      /* in real units, unlike the receiver-specific scale in RcInput.1.0, */
      /* because a modem reports a calibrated figure. */
      int8_t rssi_dbm;
      /* Measured noise floor in dBm. The difference between this and rssi_dbm */
      /* is the link margin, which is the number an operator actually watches. */
      int8_t noise_floor_dbm;
      /* Percentage of expected packets successfully received over the last */
      /* second, 0 to 100. */
      uint8_t link_quality_pct;
      /* Packets transmitted since power-on. */
      uint32_t tx_packets;
      /* Packets received since power-on. */
      uint32_t rx_packets;
      /* Packets lost since power-on, as counted by sequence number gaps. */
      uint32_t dropped_packets;
      /* Most recent measured round-trip time in milliseconds. */
      uint16_t round_trip_ms;
    } lanyard__link__TelemetryLinkStats;

    ```

=== "C++ (std)"

    ```cpp
    // Health of the air-to-ground telemetry link, published at 1 Hz by the
    // radio modem node.
    // 
    // TRANSPORT TIER: CAN FD.
    struct TelemetryLinkStats {
      // Moment the counters below were sampled. Because every node timestamps
      // against the same synchronized clock, a ground station can align this
      // sample with the vehicle state that produced it without assuming
      // anything about bus latency.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // Received signal strength in dBm at the air end of the link. Signed and
      // in real units, unlike the receiver-specific scale in RcInput.1.0,
      // because a modem reports a calibrated figure.
      std::int8_t rssi_dbm{};
      // Measured noise floor in dBm. The difference between this and rssi_dbm
      // is the link margin, which is the number an operator actually watches.
      std::int8_t noise_floor_dbm{};
      // Percentage of expected packets successfully received over the last
      // second, 0 to 100.
      std::uint8_t link_quality_pct{};
      // Packets transmitted since power-on.
      std::uint32_t tx_packets{};
      // Packets received since power-on.
      std::uint32_t rx_packets{};
      // Packets lost since power-on, as counted by sequence number gaps.
      std::uint32_t dropped_packets{};
      // Most recent measured round-trip time in milliseconds.
      std::uint16_t round_trip_ms{};
      static constexpr const char* FULL_NAME = "lanyard.link.TelemetryLinkStats";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.link.TelemetryLinkStats.1.0";
      static constexpr std::size_t EXTENT_BYTES = 24U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 24U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return TelemetryLinkStats__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return TelemetryLinkStats__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return TelemetryLinkStats__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return TelemetryLinkStats__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // Health of the air-to-ground telemetry link, published at 1 Hz by the
    // radio modem node.
    // 
    // TRANSPORT TIER: CAN FD.
    struct TelemetryLinkStats {
      // Moment the counters below were sampled. Because every node timestamps
      // against the same synchronized clock, a ground station can align this
      // sample with the vehicle state that produced it without assuming
      // anything about bus latency.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // Received signal strength in dBm at the air end of the link. Signed and
      // in real units, unlike the receiver-specific scale in RcInput.1.0,
      // because a modem reports a calibrated figure.
      std::int8_t rssi_dbm{};
      // Measured noise floor in dBm. The difference between this and rssi_dbm
      // is the link margin, which is the number an operator actually watches.
      std::int8_t noise_floor_dbm{};
      // Percentage of expected packets successfully received over the last
      // second, 0 to 100.
      std::uint8_t link_quality_pct{};
      // Packets transmitted since power-on.
      std::uint32_t tx_packets{};
      // Packets received since power-on.
      std::uint32_t rx_packets{};
      // Packets lost since power-on, as counted by sequence number gaps.
      std::uint32_t dropped_packets{};
      // Most recent measured round-trip time in milliseconds.
      std::uint16_t round_trip_ms{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      TelemetryLinkStats() = default;
      explicit TelemetryLinkStats(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
        timestamp.set_memory_resource(_memory_resource);
      }
      static constexpr const char* FULL_NAME = "lanyard.link.TelemetryLinkStats";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.link.TelemetryLinkStats.1.0";
      static constexpr std::size_t EXTENT_BYTES = 24U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 24U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return TelemetryLinkStats__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return TelemetryLinkStats__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return TelemetryLinkStats__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return TelemetryLinkStats__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return TelemetryLinkStats__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return TelemetryLinkStats__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // Health of the air-to-ground telemetry link, published at 1 Hz by the
    // radio modem node.
    // 
    // TRANSPORT TIER: CAN FD.
    struct TelemetryLinkStats {
      // Moment the counters below were sampled. Because every node timestamps
      // against the same synchronized clock, a ground station can align this
      // sample with the vehicle state that produced it without assuming
      // anything about bus latency.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // Received signal strength in dBm at the air end of the link. Signed and
      // in real units, unlike the receiver-specific scale in RcInput.1.0,
      // because a modem reports a calibrated figure.
      std::int8_t rssi_dbm{};
      // Measured noise floor in dBm. The difference between this and rssi_dbm
      // is the link margin, which is the number an operator actually watches.
      std::int8_t noise_floor_dbm{};
      // Percentage of expected packets successfully received over the last
      // second, 0 to 100.
      std::uint8_t link_quality_pct{};
      // Packets transmitted since power-on.
      std::uint32_t tx_packets{};
      // Packets received since power-on.
      std::uint32_t rx_packets{};
      // Packets lost since power-on, as counted by sequence number gaps.
      std::uint32_t dropped_packets{};
      // Most recent measured round-trip time in milliseconds.
      std::uint16_t round_trip_ms{};
      static constexpr const char* FULL_NAME = "lanyard.link.TelemetryLinkStats";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.link.TelemetryLinkStats.1.0";
      static constexpr std::size_t EXTENT_BYTES = 24U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 24U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return TelemetryLinkStats__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return TelemetryLinkStats__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return TelemetryLinkStats__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return TelemetryLinkStats__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// Health of the air-to-ground telemetry link, published at 1 Hz by the
    /// radio modem node.
    /// 
    /// TRANSPORT TIER: CAN FD.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_link_TelemetryLinkStats_1_0 {
        /// Moment the counters below were sampled. Because every node timestamps
        /// against the same synchronized clock, a ground station can align this
        /// sample with the vehicle state that produced it without assuming
        /// anything about bus latency.
        pub timestamp: uavcan_time_SynchronizedTimestamp_1_0,
        /// Received signal strength in dBm at the air end of the link. Signed and
        /// in real units, unlike the receiver-specific scale in RcInput.1.0,
        /// because a modem reports a calibrated figure.
        pub rssi_dbm: i8,
        /// Measured noise floor in dBm. The difference between this and rssi_dbm
        /// is the link margin, which is the number an operator actually watches.
        pub noise_floor_dbm: i8,
        /// Percentage of expected packets successfully received over the last
        /// second, 0 to 100.
        pub link_quality_pct: u8,
        /// Packets transmitted since power-on.
        pub tx_packets: u32,
        /// Packets received since power-on.
        pub rx_packets: u32,
        /// Packets lost since power-on, as counted by sequence number gaps.
        pub dropped_packets: u32,
        /// Most recent measured round-trip time in milliseconds.
        pub round_trip_ms: u16,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// Health of the air-to-ground telemetry link, published at 1 Hz by the
    /// radio modem node.
    /// 
    /// TRANSPORT TIER: CAN FD.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_link_TelemetryLinkStats_1_0 {
        /// Moment the counters below were sampled. Because every node timestamps
        /// against the same synchronized clock, a ground station can align this
        /// sample with the vehicle state that produced it without assuming
        /// anything about bus latency.
        pub timestamp: uavcan_time_SynchronizedTimestamp_1_0,
        /// Received signal strength in dBm at the air end of the link. Signed and
        /// in real units, unlike the receiver-specific scale in RcInput.1.0,
        /// because a modem reports a calibrated figure.
        pub rssi_dbm: i8,
        /// Measured noise floor in dBm. The difference between this and rssi_dbm
        /// is the link margin, which is the number an operator actually watches.
        pub noise_floor_dbm: i8,
        /// Percentage of expected packets successfully received over the last
        /// second, 0 to 100.
        pub link_quality_pct: u8,
        /// Packets transmitted since power-on.
        pub tx_packets: u32,
        /// Packets received since power-on.
        pub rx_packets: u32,
        /// Packets lost since power-on, as counted by sequence number gaps.
        pub dropped_packets: u32,
        /// Most recent measured round-trip time in milliseconds.
        pub round_trip_ms: u16,
    }

    ```

=== "Go"

    ```go
    // Health of the air-to-ground telemetry link, published at 1 Hz by the
    // radio modem node.
    // 
    // TRANSPORT TIER: CAN FD.
    type TelemetryLinkStats_1_0 struct {
      // Moment the counters below were sampled. Because every node timestamps
      // against the same synchronized clock, a ground station can align this
      // sample with the vehicle state that produced it without assuming
      // anything about bus latency.
      Timestamp pkg_uavcan_time.SynchronizedTimestamp_1_0
      // Received signal strength in dBm at the air end of the link. Signed and
      // in real units, unlike the receiver-specific scale in RcInput.1.0,
      // because a modem reports a calibrated figure.
      RssiDbm int8
      // Measured noise floor in dBm. The difference between this and rssi_dbm
      // is the link margin, which is the number an operator actually watches.
      NoiseFloorDbm int8
      // Percentage of expected packets successfully received over the last
      // second, 0 to 100.
      LinkQualityPct uint8
      // Packets transmitted since power-on.
      TxPackets uint32
      // Packets received since power-on.
      RxPackets uint32
      // Packets lost since power-on, as counted by sequence number gaps.
      DroppedPackets uint32
      // Most recent measured round-trip time in milliseconds.
      RoundTripMs uint16
    }

    ```

=== "TypeScript"

    ```typescript
    // Health of the air-to-ground telemetry link, published at 1 Hz by the
    // radio modem node.
    // 
    // TRANSPORT TIER: CAN FD.
    export interface TelemetryLinkStats_1_0 {
      // Moment the counters below were sampled. Because every node timestamps
      // against the same synchronized clock, a ground station can align this
      // sample with the vehicle state that produced it without assuming
      // anything about bus latency.
      timestamp: SynchronizedTimestamp_1_0;
      // Received signal strength in dBm at the air end of the link. Signed and
      // in real units, unlike the receiver-specific scale in RcInput.1.0,
      // because a modem reports a calibrated figure.
      rssi_dbm: number;
      // Measured noise floor in dBm. The difference between this and rssi_dbm
      // is the link margin, which is the number an operator actually watches.
      noise_floor_dbm: number;
      // Percentage of expected packets successfully received over the last
      // second, 0 to 100.
      link_quality_pct: number;
      // Packets transmitted since power-on.
      tx_packets: number;
      // Packets received since power-on.
      rx_packets: number;
      // Packets lost since power-on, as counted by sequence number gaps.
      dropped_packets: number;
      // Most recent measured round-trip time in milliseconds.
      round_trip_ms: number;
    }

    ```

=== "Python"

    ```python
    # Health of the air-to-ground telemetry link, published at 1 Hz by the
    # radio modem node.
    # 
    # TRANSPORT TIER: CAN FD.
    @dataclass(slots=True)
    class TelemetryLinkStats_1_0:
        # Moment the counters below were sampled. Because every node timestamps
        # against the same synchronized clock, a ground station can align this
        # sample with the vehicle state that produced it without assuming
        # anything about bus latency.
        timestamp: SynchronizedTimestamp_1_0 = field(default_factory=SynchronizedTimestamp_1_0)
        # Received signal strength in dBm at the air end of the link. Signed and
        # in real units, unlike the receiver-specific scale in RcInput.1.0,
        # because a modem reports a calibrated figure.
        rssi_dbm: int = 0
        # Measured noise floor in dBm. The difference between this and rssi_dbm
        # is the link margin, which is the number an operator actually watches.
        noise_floor_dbm: int = 0
        # Percentage of expected packets successfully received over the last
        # second, 0 to 100.
        link_quality_pct: int = 0
        # Packets transmitted since power-on.
        tx_packets: int = 0
        # Packets received since power-on.
        rx_packets: int = 0
        # Packets lost since power-on, as counted by sequence number gaps.
        dropped_packets: int = 0
        # Most recent measured round-trip time in milliseconds.
        round_trip_ms: int = 0

    ```
