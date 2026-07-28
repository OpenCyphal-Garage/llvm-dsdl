# lanyard.propulsion.EscStatus.2.0

High-rate telemetry from a single electronic speed controller (ESC),

| | |
|---|---|
| Full name | `lanyard.propulsion.EscStatus` |
| Version | 2.0 |
| Kind | Message |
| Fixed port ID | 6201 |
| Transport tier | CAN FD |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Message | 32 | 13 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# High-rate telemetry from a single electronic speed controller (ESC),
# extensible revision.
#
# TRANSPORT TIER: CAN FD.
#
# WHY A MAJOR BUMP: EscStatus.1.0 is @sealed. A sealed type has no
# delimiter header, so a reader compiled against 1.0 cannot skip over
# fields it does not know about -- appending anything would make every
# existing reader misinterpret the tail of the message. Sealing is
# therefore a one-way door: the only way to add a field to a sealed type
# is to publish a new major version, which also means a new fixed port
# identifier (6200 -> 6201) because a port carries exactly one major
# version.
#
# This revision buys extensibility with an @extent: the serialized value
# is prefixed with a 32-bit length header, and any reader may skip a
# longer-than-expected tail. Minor versions of this type (2.1, 2.2, ...)
# may append fields without breaking readers built against 2.0, as long
# as they stay within the declared extent. That flexibility costs four
# bytes on the wire plus the slack reserved by the extent, which is why
# 1.0 remains the right choice for classic-CAN deployments.

uint14 erpm_x10
# Electrical RPM divided by ten, as in EscStatus.1.0.

uint12 dc_voltage_dv
# DC bus voltage in decivolts (0.1 V per LSB).

int12 dc_current_da
# DC bus current in deciamperes (0.1 A per LSB).

int9 motor_temperature_c
# Motor winding temperature in whole degrees Celsius.

int9 controller_temperature_c
# Power stage (MOSFET) temperature in whole degrees Celsius. New in 2.0:
# the field that could not be added to the sealed 1.0 layout, and the
# reason this major version exists.

uint8 duty_cycle_pct
# Commanded duty cycle as a percentage of the DC bus voltage, 0 to 100.

uint4 error_flags
# Latched controller faults; see EscStatus.1.0 for the bit assignments.

void4
# Padding to a byte boundary.

uint32 total_run_time_s
# Cumulative powered-on time of this controller in seconds. Used for
# maintenance scheduling.

@extent 32 * 8
# 32 bytes of room, of which 13 are in use. The remaining 19 are the
# budget available to future minor versions; a reader of 2.0 will
# tolerate any 2.x message up to this size, and none larger.
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* High-rate telemetry from a single electronic speed controller (ESC), */
    /* extensible revision. */
    /*  */
    /* TRANSPORT TIER: CAN FD. */
    /*  */
    /* WHY A MAJOR BUMP: EscStatus.1.0 is @sealed. A sealed type has no */
    /* delimiter header, so a reader compiled against 1.0 cannot skip over */
    /* fields it does not know about -- appending anything would make every */
    /* existing reader misinterpret the tail of the message. Sealing is */
    /* therefore a one-way door: the only way to add a field to a sealed type */
    /* is to publish a new major version, which also means a new fixed port */
    /* identifier (6200 -> 6201) because a port carries exactly one major */
    /* version. */
    /*  */
    /* This revision buys extensibility with an @extent: the serialized value */
    /* is prefixed with a 32-bit length header, and any reader may skip a */
    /* longer-than-expected tail. Minor versions of this type (2.1, 2.2, ...) */
    /* may append fields without breaking readers built against 2.0, as long */
    /* as they stay within the declared extent. That flexibility costs four */
    /* bytes on the wire plus the slack reserved by the extent, which is why */
    /* 1.0 remains the right choice for classic-CAN deployments. */
    typedef struct lanyard__propulsion__EscStatus {
      /* Electrical RPM divided by ten, as in EscStatus.1.0. */
      uint16_t erpm_x10;
      /* DC bus voltage in decivolts (0.1 V per LSB). */
      uint16_t dc_voltage_dv;
      /* DC bus current in deciamperes (0.1 A per LSB). */
      int16_t dc_current_da;
      /* Motor winding temperature in whole degrees Celsius. */
      int16_t motor_temperature_c;
      /* Power stage (MOSFET) temperature in whole degrees Celsius. New in 2.0: */
      /* the field that could not be added to the sealed 1.0 layout, and the */
      /* reason this major version exists. */
      int16_t controller_temperature_c;
      /* Commanded duty cycle as a percentage of the DC bus voltage, 0 to 100. */
      uint8_t duty_cycle_pct;
      /* Latched controller faults; see EscStatus.1.0 for the bit assignments. */
      uint8_t error_flags;
      /* Cumulative powered-on time of this controller in seconds. Used for */
      /* maintenance scheduling. */
      uint32_t total_run_time_s;
    } lanyard__propulsion__EscStatus;

    ```

=== "C++ (std)"

    ```cpp
    // High-rate telemetry from a single electronic speed controller (ESC),
    // extensible revision.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY A MAJOR BUMP: EscStatus.1.0 is @sealed. A sealed type has no
    // delimiter header, so a reader compiled against 1.0 cannot skip over
    // fields it does not know about -- appending anything would make every
    // existing reader misinterpret the tail of the message. Sealing is
    // therefore a one-way door: the only way to add a field to a sealed type
    // is to publish a new major version, which also means a new fixed port
    // identifier (6200 -> 6201) because a port carries exactly one major
    // version.
    // 
    // This revision buys extensibility with an @extent: the serialized value
    // is prefixed with a 32-bit length header, and any reader may skip a
    // longer-than-expected tail. Minor versions of this type (2.1, 2.2, ...)
    // may append fields without breaking readers built against 2.0, as long
    // as they stay within the declared extent. That flexibility costs four
    // bytes on the wire plus the slack reserved by the extent, which is why
    // 1.0 remains the right choice for classic-CAN deployments.
    struct EscStatus_2_0 {
      // Electrical RPM divided by ten, as in EscStatus.1.0.
      std::uint16_t erpm_x10{};
      // DC bus voltage in decivolts (0.1 V per LSB).
      std::uint16_t dc_voltage_dv{};
      // DC bus current in deciamperes (0.1 A per LSB).
      std::int16_t dc_current_da{};
      // Motor winding temperature in whole degrees Celsius.
      std::int16_t motor_temperature_c{};
      // Power stage (MOSFET) temperature in whole degrees Celsius. New in 2.0:
      // the field that could not be added to the sealed 1.0 layout, and the
      // reason this major version exists.
      std::int16_t controller_temperature_c{};
      // Commanded duty cycle as a percentage of the DC bus voltage, 0 to 100.
      std::uint8_t duty_cycle_pct{};
      // Latched controller faults; see EscStatus.1.0 for the bit assignments.
      std::uint8_t error_flags{};
      // Cumulative powered-on time of this controller in seconds. Used for
      // maintenance scheduling.
      std::uint32_t total_run_time_s{};
      static constexpr const char* FULL_NAME = "lanyard.propulsion.EscStatus";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.propulsion.EscStatus.2.0";
      static constexpr std::size_t EXTENT_BYTES = 32U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 13U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return EscStatus_2_0__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return EscStatus_2_0__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return EscStatus_2_0__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return EscStatus_2_0__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // High-rate telemetry from a single electronic speed controller (ESC),
    // extensible revision.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY A MAJOR BUMP: EscStatus.1.0 is @sealed. A sealed type has no
    // delimiter header, so a reader compiled against 1.0 cannot skip over
    // fields it does not know about -- appending anything would make every
    // existing reader misinterpret the tail of the message. Sealing is
    // therefore a one-way door: the only way to add a field to a sealed type
    // is to publish a new major version, which also means a new fixed port
    // identifier (6200 -> 6201) because a port carries exactly one major
    // version.
    // 
    // This revision buys extensibility with an @extent: the serialized value
    // is prefixed with a 32-bit length header, and any reader may skip a
    // longer-than-expected tail. Minor versions of this type (2.1, 2.2, ...)
    // may append fields without breaking readers built against 2.0, as long
    // as they stay within the declared extent. That flexibility costs four
    // bytes on the wire plus the slack reserved by the extent, which is why
    // 1.0 remains the right choice for classic-CAN deployments.
    struct EscStatus_2_0 {
      // Electrical RPM divided by ten, as in EscStatus.1.0.
      std::uint16_t erpm_x10{};
      // DC bus voltage in decivolts (0.1 V per LSB).
      std::uint16_t dc_voltage_dv{};
      // DC bus current in deciamperes (0.1 A per LSB).
      std::int16_t dc_current_da{};
      // Motor winding temperature in whole degrees Celsius.
      std::int16_t motor_temperature_c{};
      // Power stage (MOSFET) temperature in whole degrees Celsius. New in 2.0:
      // the field that could not be added to the sealed 1.0 layout, and the
      // reason this major version exists.
      std::int16_t controller_temperature_c{};
      // Commanded duty cycle as a percentage of the DC bus voltage, 0 to 100.
      std::uint8_t duty_cycle_pct{};
      // Latched controller faults; see EscStatus.1.0 for the bit assignments.
      std::uint8_t error_flags{};
      // Cumulative powered-on time of this controller in seconds. Used for
      // maintenance scheduling.
      std::uint32_t total_run_time_s{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      EscStatus_2_0() = default;
      explicit EscStatus_2_0(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
      }
      static constexpr const char* FULL_NAME = "lanyard.propulsion.EscStatus";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.propulsion.EscStatus.2.0";
      static constexpr std::size_t EXTENT_BYTES = 32U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 13U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return EscStatus_2_0__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return EscStatus_2_0__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return EscStatus_2_0__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return EscStatus_2_0__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return EscStatus_2_0__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return EscStatus_2_0__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // High-rate telemetry from a single electronic speed controller (ESC),
    // extensible revision.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY A MAJOR BUMP: EscStatus.1.0 is @sealed. A sealed type has no
    // delimiter header, so a reader compiled against 1.0 cannot skip over
    // fields it does not know about -- appending anything would make every
    // existing reader misinterpret the tail of the message. Sealing is
    // therefore a one-way door: the only way to add a field to a sealed type
    // is to publish a new major version, which also means a new fixed port
    // identifier (6200 -> 6201) because a port carries exactly one major
    // version.
    // 
    // This revision buys extensibility with an @extent: the serialized value
    // is prefixed with a 32-bit length header, and any reader may skip a
    // longer-than-expected tail. Minor versions of this type (2.1, 2.2, ...)
    // may append fields without breaking readers built against 2.0, as long
    // as they stay within the declared extent. That flexibility costs four
    // bytes on the wire plus the slack reserved by the extent, which is why
    // 1.0 remains the right choice for classic-CAN deployments.
    struct EscStatus_2_0 {
      // Electrical RPM divided by ten, as in EscStatus.1.0.
      std::uint16_t erpm_x10{};
      // DC bus voltage in decivolts (0.1 V per LSB).
      std::uint16_t dc_voltage_dv{};
      // DC bus current in deciamperes (0.1 A per LSB).
      std::int16_t dc_current_da{};
      // Motor winding temperature in whole degrees Celsius.
      std::int16_t motor_temperature_c{};
      // Power stage (MOSFET) temperature in whole degrees Celsius. New in 2.0:
      // the field that could not be added to the sealed 1.0 layout, and the
      // reason this major version exists.
      std::int16_t controller_temperature_c{};
      // Commanded duty cycle as a percentage of the DC bus voltage, 0 to 100.
      std::uint8_t duty_cycle_pct{};
      // Latched controller faults; see EscStatus.1.0 for the bit assignments.
      std::uint8_t error_flags{};
      // Cumulative powered-on time of this controller in seconds. Used for
      // maintenance scheduling.
      std::uint32_t total_run_time_s{};
      static constexpr const char* FULL_NAME = "lanyard.propulsion.EscStatus";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.propulsion.EscStatus.2.0";
      static constexpr std::size_t EXTENT_BYTES = 32U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 13U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return EscStatus_2_0__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return EscStatus_2_0__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return EscStatus_2_0__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return EscStatus_2_0__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// High-rate telemetry from a single electronic speed controller (ESC),
    /// extensible revision.
    /// 
    /// TRANSPORT TIER: CAN FD.
    /// 
    /// WHY A MAJOR BUMP: EscStatus.1.0 is @sealed. A sealed type has no
    /// delimiter header, so a reader compiled against 1.0 cannot skip over
    /// fields it does not know about -- appending anything would make every
    /// existing reader misinterpret the tail of the message. Sealing is
    /// therefore a one-way door: the only way to add a field to a sealed type
    /// is to publish a new major version, which also means a new fixed port
    /// identifier (6200 -> 6201) because a port carries exactly one major
    /// version.
    /// 
    /// This revision buys extensibility with an @extent: the serialized value
    /// is prefixed with a 32-bit length header, and any reader may skip a
    /// longer-than-expected tail. Minor versions of this type (2.1, 2.2, ...)
    /// may append fields without breaking readers built against 2.0, as long
    /// as they stay within the declared extent. That flexibility costs four
    /// bytes on the wire plus the slack reserved by the extent, which is why
    /// 1.0 remains the right choice for classic-CAN deployments.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_propulsion_EscStatus_2_0 {
        /// Electrical RPM divided by ten, as in EscStatus.1.0.
        pub erpm_x10: u16,
        /// DC bus voltage in decivolts (0.1 V per LSB).
        pub dc_voltage_dv: u16,
        /// DC bus current in deciamperes (0.1 A per LSB).
        pub dc_current_da: i16,
        /// Motor winding temperature in whole degrees Celsius.
        pub motor_temperature_c: i16,
        /// Power stage (MOSFET) temperature in whole degrees Celsius. New in 2.0:
        /// the field that could not be added to the sealed 1.0 layout, and the
        /// reason this major version exists.
        pub controller_temperature_c: i16,
        /// Commanded duty cycle as a percentage of the DC bus voltage, 0 to 100.
        pub duty_cycle_pct: u8,
        /// Latched controller faults; see EscStatus.1.0 for the bit assignments.
        pub error_flags: u8,
        /// Cumulative powered-on time of this controller in seconds. Used for
        /// maintenance scheduling.
        pub total_run_time_s: u32,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// High-rate telemetry from a single electronic speed controller (ESC),
    /// extensible revision.
    /// 
    /// TRANSPORT TIER: CAN FD.
    /// 
    /// WHY A MAJOR BUMP: EscStatus.1.0 is @sealed. A sealed type has no
    /// delimiter header, so a reader compiled against 1.0 cannot skip over
    /// fields it does not know about -- appending anything would make every
    /// existing reader misinterpret the tail of the message. Sealing is
    /// therefore a one-way door: the only way to add a field to a sealed type
    /// is to publish a new major version, which also means a new fixed port
    /// identifier (6200 -> 6201) because a port carries exactly one major
    /// version.
    /// 
    /// This revision buys extensibility with an @extent: the serialized value
    /// is prefixed with a 32-bit length header, and any reader may skip a
    /// longer-than-expected tail. Minor versions of this type (2.1, 2.2, ...)
    /// may append fields without breaking readers built against 2.0, as long
    /// as they stay within the declared extent. That flexibility costs four
    /// bytes on the wire plus the slack reserved by the extent, which is why
    /// 1.0 remains the right choice for classic-CAN deployments.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_propulsion_EscStatus_2_0 {
        /// Electrical RPM divided by ten, as in EscStatus.1.0.
        pub erpm_x10: u16,
        /// DC bus voltage in decivolts (0.1 V per LSB).
        pub dc_voltage_dv: u16,
        /// DC bus current in deciamperes (0.1 A per LSB).
        pub dc_current_da: i16,
        /// Motor winding temperature in whole degrees Celsius.
        pub motor_temperature_c: i16,
        /// Power stage (MOSFET) temperature in whole degrees Celsius. New in 2.0:
        /// the field that could not be added to the sealed 1.0 layout, and the
        /// reason this major version exists.
        pub controller_temperature_c: i16,
        /// Commanded duty cycle as a percentage of the DC bus voltage, 0 to 100.
        pub duty_cycle_pct: u8,
        /// Latched controller faults; see EscStatus.1.0 for the bit assignments.
        pub error_flags: u8,
        /// Cumulative powered-on time of this controller in seconds. Used for
        /// maintenance scheduling.
        pub total_run_time_s: u32,
    }

    ```

=== "Go"

    ```go
    // High-rate telemetry from a single electronic speed controller (ESC),
    // extensible revision.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY A MAJOR BUMP: EscStatus.1.0 is @sealed. A sealed type has no
    // delimiter header, so a reader compiled against 1.0 cannot skip over
    // fields it does not know about -- appending anything would make every
    // existing reader misinterpret the tail of the message. Sealing is
    // therefore a one-way door: the only way to add a field to a sealed type
    // is to publish a new major version, which also means a new fixed port
    // identifier (6200 -> 6201) because a port carries exactly one major
    // version.
    // 
    // This revision buys extensibility with an @extent: the serialized value
    // is prefixed with a 32-bit length header, and any reader may skip a
    // longer-than-expected tail. Minor versions of this type (2.1, 2.2, ...)
    // may append fields without breaking readers built against 2.0, as long
    // as they stay within the declared extent. That flexibility costs four
    // bytes on the wire plus the slack reserved by the extent, which is why
    // 1.0 remains the right choice for classic-CAN deployments.
    type EscStatus_2_0 struct {
      // Electrical RPM divided by ten, as in EscStatus.1.0.
      ErpmX10 uint16
      // DC bus voltage in decivolts (0.1 V per LSB).
      DcVoltageDv uint16
      // DC bus current in deciamperes (0.1 A per LSB).
      DcCurrentDa int16
      // Motor winding temperature in whole degrees Celsius.
      MotorTemperatureC int16
      // Power stage (MOSFET) temperature in whole degrees Celsius. New in 2.0:
      // the field that could not be added to the sealed 1.0 layout, and the
      // reason this major version exists.
      ControllerTemperatureC int16
      // Commanded duty cycle as a percentage of the DC bus voltage, 0 to 100.
      DutyCyclePct uint8
      // Latched controller faults; see EscStatus.1.0 for the bit assignments.
      ErrorFlags uint8
      // Cumulative powered-on time of this controller in seconds. Used for
      // maintenance scheduling.
      TotalRunTimeS uint32
    }

    ```

=== "TypeScript"

    ```typescript
    // High-rate telemetry from a single electronic speed controller (ESC),
    // extensible revision.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY A MAJOR BUMP: EscStatus.1.0 is @sealed. A sealed type has no
    // delimiter header, so a reader compiled against 1.0 cannot skip over
    // fields it does not know about -- appending anything would make every
    // existing reader misinterpret the tail of the message. Sealing is
    // therefore a one-way door: the only way to add a field to a sealed type
    // is to publish a new major version, which also means a new fixed port
    // identifier (6200 -> 6201) because a port carries exactly one major
    // version.
    // 
    // This revision buys extensibility with an @extent: the serialized value
    // is prefixed with a 32-bit length header, and any reader may skip a
    // longer-than-expected tail. Minor versions of this type (2.1, 2.2, ...)
    // may append fields without breaking readers built against 2.0, as long
    // as they stay within the declared extent. That flexibility costs four
    // bytes on the wire plus the slack reserved by the extent, which is why
    // 1.0 remains the right choice for classic-CAN deployments.
    export interface EscStatus_2_0 {
      // Electrical RPM divided by ten, as in EscStatus.1.0.
      erpm_x10: number;
      // DC bus voltage in decivolts (0.1 V per LSB).
      dc_voltage_dv: number;
      // DC bus current in deciamperes (0.1 A per LSB).
      dc_current_da: number;
      // Motor winding temperature in whole degrees Celsius.
      motor_temperature_c: number;
      // Power stage (MOSFET) temperature in whole degrees Celsius. New in 2.0:
      // the field that could not be added to the sealed 1.0 layout, and the
      // reason this major version exists.
      controller_temperature_c: number;
      // Commanded duty cycle as a percentage of the DC bus voltage, 0 to 100.
      duty_cycle_pct: number;
      // Latched controller faults; see EscStatus.1.0 for the bit assignments.
      error_flags: number;
      // Cumulative powered-on time of this controller in seconds. Used for
      // maintenance scheduling.
      total_run_time_s: number;
    }

    ```

=== "Python"

    ```python
    # High-rate telemetry from a single electronic speed controller (ESC),
    # extensible revision.
    # 
    # TRANSPORT TIER: CAN FD.
    # 
    # WHY A MAJOR BUMP: EscStatus.1.0 is @sealed. A sealed type has no
    # delimiter header, so a reader compiled against 1.0 cannot skip over
    # fields it does not know about -- appending anything would make every
    # existing reader misinterpret the tail of the message. Sealing is
    # therefore a one-way door: the only way to add a field to a sealed type
    # is to publish a new major version, which also means a new fixed port
    # identifier (6200 -> 6201) because a port carries exactly one major
    # version.
    # 
    # This revision buys extensibility with an @extent: the serialized value
    # is prefixed with a 32-bit length header, and any reader may skip a
    # longer-than-expected tail. Minor versions of this type (2.1, 2.2, ...)
    # may append fields without breaking readers built against 2.0, as long
    # as they stay within the declared extent. That flexibility costs four
    # bytes on the wire plus the slack reserved by the extent, which is why
    # 1.0 remains the right choice for classic-CAN deployments.
    @dataclass(slots=True)
    class EscStatus_2_0:
        # Electrical RPM divided by ten, as in EscStatus.1.0.
        erpm_x10: int = 0
        # DC bus voltage in decivolts (0.1 V per LSB).
        dc_voltage_dv: int = 0
        # DC bus current in deciamperes (0.1 A per LSB).
        dc_current_da: int = 0
        # Motor winding temperature in whole degrees Celsius.
        motor_temperature_c: int = 0
        # Power stage (MOSFET) temperature in whole degrees Celsius. New in 2.0:
        # the field that could not be added to the sealed 1.0 layout, and the
        # reason this major version exists.
        controller_temperature_c: int = 0
        # Commanded duty cycle as a percentage of the DC bus voltage, 0 to 100.
        duty_cycle_pct: int = 0
        # Latched controller faults; see EscStatus.1.0 for the bit assignments.
        error_flags: int = 0
        # Cumulative powered-on time of this controller in seconds. Used for
        # maintenance scheduling.
        total_run_time_s: int = 0

    ```
