# lanyard.health.BatteryStatus.2.0

State of one battery pack, published at 2 Hz by the smart battery or

| | |
|---|---|
| Full name | `lanyard.health.BatteryStatus` |
| Version | 2.0 |
| Kind | Message |
| Fixed port ID | 6251 |
| Transport tier | CAN FD |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Message | 128 | 75 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# State of one battery pack, published at 2 Hz by the smart battery or
# power module.
#
# TRANSPORT TIER: CAN FD.
#
# VERSIONING: this definition replaces the polled LegacyBatteryPoll.1.0
# service, which is @deprecated. The change of shape -- from a service
# the flight controller had to poll to a message the pack publishes on
# its own -- is why this is a 2.0 and not a minor revision of anything.
# The deprecated service remains in the namespace so that both halves of
# a migration are visible.

uavcan.time.SynchronizedTimestamp.1.0 timestamp
# Moment the measurements were sampled.

uavcan.si.unit.voltage.Scalar.1.0 voltage
# Pack terminal voltage in volts.

uavcan.si.unit.electric_current.Scalar.1.0 current
# Pack current in amperes. Positive is discharge, negative is charge.

uavcan.si.unit.temperature.Scalar.1.0 temperature
# Hottest cell temperature in kelvin. Kelvin, not Celsius, because that
# is the SI unit the standard type carries; using the standard type
# means a generic consumer needs no per-vendor conversion, and the
# arithmetic to display Celsius belongs in the display layer.

float16 remaining_capacity_ratio
# State of charge, 0.0 (empty) to 1.0 (full).

float16 full_charge_capacity_ah
# Capacity of the pack when fully charged, in ampere-hours. Declines
# over the pack's life, so a consumer computing endurance uses this
# rather than the nameplate capacity.

uint16 cycle_count
# Completed charge/discharge cycles, for maintenance scheduling.

uint8 status_flags
# Bit mask of active pack conditions.
#   bit 0 - charging
#   bit 1 - cell imbalance beyond threshold
#   bit 2 - over-temperature
#   bit 3 - under-voltage cutoff imminent
#   bit 4 - pack requires service
#   bits 5..7 - reserved, transmitted as zero

float16[<=24] cell_voltage
# Per-cell voltages in volts. The array length reports the pack's series
# count, so one definition serves the 6S and 12S packs in the fleet and
# the 24S pack on the heavy airframe.

@extent 128 * 8
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* State of one battery pack, published at 2 Hz by the smart battery or */
    /* power module. */
    /*  */
    /* TRANSPORT TIER: CAN FD. */
    /*  */
    /* VERSIONING: this definition replaces the polled LegacyBatteryPoll.1.0 */
    /* service, which is @deprecated. The change of shape -- from a service */
    /* the flight controller had to poll to a message the pack publishes on */
    /* its own -- is why this is a 2.0 and not a minor revision of anything. */
    /* The deprecated service remains in the namespace so that both halves of */
    /* a migration are visible. */
    typedef struct lanyard__health__BatteryStatus {
      /* Moment the measurements were sampled. */
      uavcan__time__SynchronizedTimestamp timestamp;
      /* Pack terminal voltage in volts. */
      uavcan__si__unit__voltage__Scalar voltage;
      /* Pack current in amperes. Positive is discharge, negative is charge. */
      uavcan__si__unit__electric_current__Scalar current;
      /* Hottest cell temperature in kelvin. Kelvin, not Celsius, because that */
      /* is the SI unit the standard type carries; using the standard type */
      /* means a generic consumer needs no per-vendor conversion, and the */
      /* arithmetic to display Celsius belongs in the display layer. */
      uavcan__si__unit__temperature__Scalar temperature;
      /* State of charge, 0.0 (empty) to 1.0 (full). */
      float remaining_capacity_ratio;
      /* Capacity of the pack when fully charged, in ampere-hours. Declines */
      /* over the pack's life, so a consumer computing endurance uses this */
      /* rather than the nameplate capacity. */
      float full_charge_capacity_ah;
      /* Completed charge/discharge cycles, for maintenance scheduling. */
      uint16_t cycle_count;
      /* Bit mask of active pack conditions. */
      /*   bit 0 - charging */
      /*   bit 1 - cell imbalance beyond threshold */
      /*   bit 2 - over-temperature */
      /*   bit 3 - under-voltage cutoff imminent */
      /*   bit 4 - pack requires service */
      /*   bits 5..7 - reserved, transmitted as zero */
      uint8_t status_flags;
      /* Per-cell voltages in volts. The array length reports the pack's series */
      /* count, so one definition serves the 6S and 12S packs in the fleet and */
      /* the 24S pack on the heavy airframe. */
      struct {
        float elements[24U];
        size_t count;
      } cell_voltage;
    } lanyard__health__BatteryStatus;

    ```

=== "C++ (std)"

    ```cpp
    // State of one battery pack, published at 2 Hz by the smart battery or
    // power module.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // VERSIONING: this definition replaces the polled LegacyBatteryPoll.1.0
    // service, which is @deprecated. The change of shape -- from a service
    // the flight controller had to poll to a message the pack publishes on
    // its own -- is why this is a 2.0 and not a minor revision of anything.
    // The deprecated service remains in the namespace so that both halves of
    // a migration are visible.
    struct BatteryStatus {
      // Moment the measurements were sampled.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // Pack terminal voltage in volts.
      ::uavcan::si::unit::voltage::Scalar voltage{};
      // Pack current in amperes. Positive is discharge, negative is charge.
      ::uavcan::si::unit::electric_current::Scalar current{};
      // Hottest cell temperature in kelvin. Kelvin, not Celsius, because that
      // is the SI unit the standard type carries; using the standard type
      // means a generic consumer needs no per-vendor conversion, and the
      // arithmetic to display Celsius belongs in the display layer.
      ::uavcan::si::unit::temperature::Scalar temperature{};
      // State of charge, 0.0 (empty) to 1.0 (full).
      float remaining_capacity_ratio{};
      // Capacity of the pack when fully charged, in ampere-hours. Declines
      // over the pack's life, so a consumer computing endurance uses this
      // rather than the nameplate capacity.
      float full_charge_capacity_ah{};
      // Completed charge/discharge cycles, for maintenance scheduling.
      std::uint16_t cycle_count{};
      // Bit mask of active pack conditions.
      //   bit 0 - charging
      //   bit 1 - cell imbalance beyond threshold
      //   bit 2 - over-temperature
      //   bit 3 - under-voltage cutoff imminent
      //   bit 4 - pack requires service
      //   bits 5..7 - reserved, transmitted as zero
      std::uint8_t status_flags{};
      // Per-cell voltages in volts. The array length reports the pack's series
      // count, so one definition serves the 6S and 12S packs in the fleet and
      // the 24S pack on the heavy airframe.
      std::vector<float> cell_voltage{};
      static constexpr const char* FULL_NAME = "lanyard.health.BatteryStatus";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.health.BatteryStatus.2.0";
      static constexpr std::size_t EXTENT_BYTES = 128U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 75U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      static constexpr std::size_t CELL_VOLTAGE_ARRAY_CAPACITY = 24U;
      static constexpr bool CELL_VOLTAGE_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return BatteryStatus__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return BatteryStatus__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return BatteryStatus__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return BatteryStatus__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // State of one battery pack, published at 2 Hz by the smart battery or
    // power module.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // VERSIONING: this definition replaces the polled LegacyBatteryPoll.1.0
    // service, which is @deprecated. The change of shape -- from a service
    // the flight controller had to poll to a message the pack publishes on
    // its own -- is why this is a 2.0 and not a minor revision of anything.
    // The deprecated service remains in the namespace so that both halves of
    // a migration are visible.
    struct BatteryStatus {
      // Moment the measurements were sampled.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // Pack terminal voltage in volts.
      ::uavcan::si::unit::voltage::Scalar voltage{};
      // Pack current in amperes. Positive is discharge, negative is charge.
      ::uavcan::si::unit::electric_current::Scalar current{};
      // Hottest cell temperature in kelvin. Kelvin, not Celsius, because that
      // is the SI unit the standard type carries; using the standard type
      // means a generic consumer needs no per-vendor conversion, and the
      // arithmetic to display Celsius belongs in the display layer.
      ::uavcan::si::unit::temperature::Scalar temperature{};
      // State of charge, 0.0 (empty) to 1.0 (full).
      float remaining_capacity_ratio{};
      // Capacity of the pack when fully charged, in ampere-hours. Declines
      // over the pack's life, so a consumer computing endurance uses this
      // rather than the nameplate capacity.
      float full_charge_capacity_ah{};
      // Completed charge/discharge cycles, for maintenance scheduling.
      std::uint16_t cycle_count{};
      // Bit mask of active pack conditions.
      //   bit 0 - charging
      //   bit 1 - cell imbalance beyond threshold
      //   bit 2 - over-temperature
      //   bit 3 - under-voltage cutoff imminent
      //   bit 4 - pack requires service
      //   bits 5..7 - reserved, transmitted as zero
      std::uint8_t status_flags{};
      // Per-cell voltages in volts. The array length reports the pack's series
      // count, so one definition serves the 6S and 12S packs in the fleet and
      // the 24S pack on the heavy airframe.
      std::pmr::vector<float> cell_voltage{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      BatteryStatus() = default;
      explicit BatteryStatus(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
        cell_voltage = decltype(cell_voltage)(_memory_resource);
        timestamp.set_memory_resource(_memory_resource);
        voltage.set_memory_resource(_memory_resource);
        current.set_memory_resource(_memory_resource);
        temperature.set_memory_resource(_memory_resource);
      }
      static constexpr const char* FULL_NAME = "lanyard.health.BatteryStatus";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.health.BatteryStatus.2.0";
      static constexpr std::size_t EXTENT_BYTES = 128U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 75U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      static constexpr std::size_t CELL_VOLTAGE_ARRAY_CAPACITY = 24U;
      static constexpr bool CELL_VOLTAGE_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return BatteryStatus__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return BatteryStatus__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return BatteryStatus__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return BatteryStatus__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return BatteryStatus__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return BatteryStatus__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // State of one battery pack, published at 2 Hz by the smart battery or
    // power module.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // VERSIONING: this definition replaces the polled LegacyBatteryPoll.1.0
    // service, which is @deprecated. The change of shape -- from a service
    // the flight controller had to poll to a message the pack publishes on
    // its own -- is why this is a 2.0 and not a minor revision of anything.
    // The deprecated service remains in the namespace so that both halves of
    // a migration are visible.
    struct BatteryStatus {
      // Moment the measurements were sampled.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // Pack terminal voltage in volts.
      ::uavcan::si::unit::voltage::Scalar voltage{};
      // Pack current in amperes. Positive is discharge, negative is charge.
      ::uavcan::si::unit::electric_current::Scalar current{};
      // Hottest cell temperature in kelvin. Kelvin, not Celsius, because that
      // is the SI unit the standard type carries; using the standard type
      // means a generic consumer needs no per-vendor conversion, and the
      // arithmetic to display Celsius belongs in the display layer.
      ::uavcan::si::unit::temperature::Scalar temperature{};
      // State of charge, 0.0 (empty) to 1.0 (full).
      float remaining_capacity_ratio{};
      // Capacity of the pack when fully charged, in ampere-hours. Declines
      // over the pack's life, so a consumer computing endurance uses this
      // rather than the nameplate capacity.
      float full_charge_capacity_ah{};
      // Completed charge/discharge cycles, for maintenance scheduling.
      std::uint16_t cycle_count{};
      // Bit mask of active pack conditions.
      //   bit 0 - charging
      //   bit 1 - cell imbalance beyond threshold
      //   bit 2 - over-temperature
      //   bit 3 - under-voltage cutoff imminent
      //   bit 4 - pack requires service
      //   bits 5..7 - reserved, transmitted as zero
      std::uint8_t status_flags{};
      // Per-cell voltages in volts. The array length reports the pack's series
      // count, so one definition serves the 6S and 12S packs in the fleet and
      // the 24S pack on the heavy airframe.
      ::llvmdsdl::cpp::autosar::BoundedVector<float, 24U> cell_voltage{};
      static constexpr const char* FULL_NAME = "lanyard.health.BatteryStatus";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.health.BatteryStatus.2.0";
      static constexpr std::size_t EXTENT_BYTES = 128U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 75U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      static constexpr std::size_t CELL_VOLTAGE_ARRAY_CAPACITY = 24U;
      static constexpr bool CELL_VOLTAGE_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return BatteryStatus__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return BatteryStatus__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return BatteryStatus__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return BatteryStatus__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// State of one battery pack, published at 2 Hz by the smart battery or
    /// power module.
    /// 
    /// TRANSPORT TIER: CAN FD.
    /// 
    /// VERSIONING: this definition replaces the polled LegacyBatteryPoll.1.0
    /// service, which is @deprecated. The change of shape -- from a service
    /// the flight controller had to poll to a message the pack publishes on
    /// its own -- is why this is a 2.0 and not a minor revision of anything.
    /// The deprecated service remains in the namespace so that both halves of
    /// a migration are visible.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_health_BatteryStatus_2_0 {
        /// Moment the measurements were sampled.
        pub timestamp: uavcan_time_SynchronizedTimestamp_1_0,
        /// Pack terminal voltage in volts.
        pub voltage: uavcan_si_unit_voltage_Scalar_1_0,
        /// Pack current in amperes. Positive is discharge, negative is charge.
        pub current: uavcan_si_unit_electric_current_Scalar_1_0,
        /// Hottest cell temperature in kelvin. Kelvin, not Celsius, because that
        /// is the SI unit the standard type carries; using the standard type
        /// means a generic consumer needs no per-vendor conversion, and the
        /// arithmetic to display Celsius belongs in the display layer.
        pub temperature: uavcan_si_unit_temperature_Scalar_1_0,
        /// State of charge, 0.0 (empty) to 1.0 (full).
        pub remaining_capacity_ratio: f32,
        /// Capacity of the pack when fully charged, in ampere-hours. Declines
        /// over the pack's life, so a consumer computing endurance uses this
        /// rather than the nameplate capacity.
        pub full_charge_capacity_ah: f32,
        /// Completed charge/discharge cycles, for maintenance scheduling.
        pub cycle_count: u16,
        /// Bit mask of active pack conditions.
        ///   bit 0 - charging
        ///   bit 1 - cell imbalance beyond threshold
        ///   bit 2 - over-temperature
        ///   bit 3 - under-voltage cutoff imminent
        ///   bit 4 - pack requires service
        ///   bits 5..7 - reserved, transmitted as zero
        pub status_flags: u8,
        /// Per-cell voltages in volts. The array length reports the pack's series
        /// count, so one definition serves the 6S and 12S packs in the fleet and
        /// the 24S pack on the heavy airframe.
        pub cell_voltage: crate::dsdl_runtime::DsdlVec<f32>,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// State of one battery pack, published at 2 Hz by the smart battery or
    /// power module.
    /// 
    /// TRANSPORT TIER: CAN FD.
    /// 
    /// VERSIONING: this definition replaces the polled LegacyBatteryPoll.1.0
    /// service, which is @deprecated. The change of shape -- from a service
    /// the flight controller had to poll to a message the pack publishes on
    /// its own -- is why this is a 2.0 and not a minor revision of anything.
    /// The deprecated service remains in the namespace so that both halves of
    /// a migration are visible.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_health_BatteryStatus_2_0 {
        /// Moment the measurements were sampled.
        pub timestamp: uavcan_time_SynchronizedTimestamp_1_0,
        /// Pack terminal voltage in volts.
        pub voltage: uavcan_si_unit_voltage_Scalar_1_0,
        /// Pack current in amperes. Positive is discharge, negative is charge.
        pub current: uavcan_si_unit_electric_current_Scalar_1_0,
        /// Hottest cell temperature in kelvin. Kelvin, not Celsius, because that
        /// is the SI unit the standard type carries; using the standard type
        /// means a generic consumer needs no per-vendor conversion, and the
        /// arithmetic to display Celsius belongs in the display layer.
        pub temperature: uavcan_si_unit_temperature_Scalar_1_0,
        /// State of charge, 0.0 (empty) to 1.0 (full).
        pub remaining_capacity_ratio: f32,
        /// Capacity of the pack when fully charged, in ampere-hours. Declines
        /// over the pack's life, so a consumer computing endurance uses this
        /// rather than the nameplate capacity.
        pub full_charge_capacity_ah: f32,
        /// Completed charge/discharge cycles, for maintenance scheduling.
        pub cycle_count: u16,
        /// Bit mask of active pack conditions.
        ///   bit 0 - charging
        ///   bit 1 - cell imbalance beyond threshold
        ///   bit 2 - over-temperature
        ///   bit 3 - under-voltage cutoff imminent
        ///   bit 4 - pack requires service
        ///   bits 5..7 - reserved, transmitted as zero
        pub status_flags: u8,
        /// Per-cell voltages in volts. The array length reports the pack's series
        /// count, so one definition serves the 6S and 12S packs in the fleet and
        /// the 24S pack on the heavy airframe.
        pub cell_voltage: crate::dsdl_runtime::DsdlVec<f32>,
    }

    ```

=== "Go"

    ```go
    // State of one battery pack, published at 2 Hz by the smart battery or
    // power module.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // VERSIONING: this definition replaces the polled LegacyBatteryPoll.1.0
    // service, which is @deprecated. The change of shape -- from a service
    // the flight controller had to poll to a message the pack publishes on
    // its own -- is why this is a 2.0 and not a minor revision of anything.
    // The deprecated service remains in the namespace so that both halves of
    // a migration are visible.
    type BatteryStatus_2_0 struct {
      // Moment the measurements were sampled.
      Timestamp pkg_uavcan_time.SynchronizedTimestamp_1_0
      // Pack terminal voltage in volts.
      Voltage pkg_uavcan_si_unit_voltage.Scalar_1_0
      // Pack current in amperes. Positive is discharge, negative is charge.
      Current pkg_uavcan_si_unit_electric_current.Scalar_1_0
      // Hottest cell temperature in kelvin. Kelvin, not Celsius, because that
      // is the SI unit the standard type carries; using the standard type
      // means a generic consumer needs no per-vendor conversion, and the
      // arithmetic to display Celsius belongs in the display layer.
      Temperature pkg_uavcan_si_unit_temperature.Scalar_1_0
      // State of charge, 0.0 (empty) to 1.0 (full).
      RemainingCapacityRatio float32
      // Capacity of the pack when fully charged, in ampere-hours. Declines
      // over the pack's life, so a consumer computing endurance uses this
      // rather than the nameplate capacity.
      FullChargeCapacityAh float32
      // Completed charge/discharge cycles, for maintenance scheduling.
      CycleCount uint16
      // Bit mask of active pack conditions.
      //   bit 0 - charging
      //   bit 1 - cell imbalance beyond threshold
      //   bit 2 - over-temperature
      //   bit 3 - under-voltage cutoff imminent
      //   bit 4 - pack requires service
      //   bits 5..7 - reserved, transmitted as zero
      StatusFlags uint8
      // Per-cell voltages in volts. The array length reports the pack's series
      // count, so one definition serves the 6S and 12S packs in the fleet and
      // the 24S pack on the heavy airframe.
      CellVoltage []float32
    }

    ```

=== "TypeScript"

    ```typescript
    // State of one battery pack, published at 2 Hz by the smart battery or
    // power module.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // VERSIONING: this definition replaces the polled LegacyBatteryPoll.1.0
    // service, which is @deprecated. The change of shape -- from a service
    // the flight controller had to poll to a message the pack publishes on
    // its own -- is why this is a 2.0 and not a minor revision of anything.
    // The deprecated service remains in the namespace so that both halves of
    // a migration are visible.
    export interface BatteryStatus_2_0 {
      // Moment the measurements were sampled.
      timestamp: SynchronizedTimestamp_1_0;
      // Pack terminal voltage in volts.
      voltage: Scalar_1_0;
      // Pack current in amperes. Positive is discharge, negative is charge.
      current: Scalar_1_0;
      // Hottest cell temperature in kelvin. Kelvin, not Celsius, because that
      // is the SI unit the standard type carries; using the standard type
      // means a generic consumer needs no per-vendor conversion, and the
      // arithmetic to display Celsius belongs in the display layer.
      temperature: Scalar_1_0;
      // State of charge, 0.0 (empty) to 1.0 (full).
      remaining_capacity_ratio: number;
      // Capacity of the pack when fully charged, in ampere-hours. Declines
      // over the pack's life, so a consumer computing endurance uses this
      // rather than the nameplate capacity.
      full_charge_capacity_ah: number;
      // Completed charge/discharge cycles, for maintenance scheduling.
      cycle_count: number;
      // Bit mask of active pack conditions.
      //   bit 0 - charging
      //   bit 1 - cell imbalance beyond threshold
      //   bit 2 - over-temperature
      //   bit 3 - under-voltage cutoff imminent
      //   bit 4 - pack requires service
      //   bits 5..7 - reserved, transmitted as zero
      status_flags: number;
      // Per-cell voltages in volts. The array length reports the pack's series
      // count, so one definition serves the 6S and 12S packs in the fleet and
      // the 24S pack on the heavy airframe.
      cell_voltage: Array<number>;
    }

    ```

=== "Python"

    ```python
    # State of one battery pack, published at 2 Hz by the smart battery or
    # power module.
    # 
    # TRANSPORT TIER: CAN FD.
    # 
    # VERSIONING: this definition replaces the polled LegacyBatteryPoll.1.0
    # service, which is @deprecated. The change of shape -- from a service
    # the flight controller had to poll to a message the pack publishes on
    # its own -- is why this is a 2.0 and not a minor revision of anything.
    # The deprecated service remains in the namespace so that both halves of
    # a migration are visible.
    @dataclass(slots=True)
    class BatteryStatus_2_0:
        # Moment the measurements were sampled.
        timestamp: SynchronizedTimestamp_1_0 = field(default_factory=SynchronizedTimestamp_1_0)
        # Pack terminal voltage in volts.
        voltage: Scalar_1_0 = field(default_factory=Scalar_1_0)
        # Pack current in amperes. Positive is discharge, negative is charge.
        current: Scalar_1_0 = field(default_factory=Scalar_1_0)
        # Hottest cell temperature in kelvin. Kelvin, not Celsius, because that
        # is the SI unit the standard type carries; using the standard type
        # means a generic consumer needs no per-vendor conversion, and the
        # arithmetic to display Celsius belongs in the display layer.
        temperature: Scalar_1_0 = field(default_factory=Scalar_1_0)
        # State of charge, 0.0 (empty) to 1.0 (full).
        remaining_capacity_ratio: float = 0.0
        # Capacity of the pack when fully charged, in ampere-hours. Declines
        # over the pack's life, so a consumer computing endurance uses this
        # rather than the nameplate capacity.
        full_charge_capacity_ah: float = 0.0
        # Completed charge/discharge cycles, for maintenance scheduling.
        cycle_count: int = 0
        # Bit mask of active pack conditions.
        #   bit 0 - charging
        #   bit 1 - cell imbalance beyond threshold
        #   bit 2 - over-temperature
        #   bit 3 - under-voltage cutoff imminent
        #   bit 4 - pack requires service
        #   bits 5..7 - reserved, transmitted as zero
        status_flags: int = 0
        # Per-cell voltages in volts. The array length reports the pack's series
        # count, so one definition serves the 6S and 12S packs in the fleet and
        # the 24S pack on the heavy airframe.
        cell_voltage: list[float] = field(default_factory=list)

    ```
