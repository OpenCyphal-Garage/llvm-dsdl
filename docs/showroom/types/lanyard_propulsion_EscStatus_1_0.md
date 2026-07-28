# lanyard.propulsion.EscStatus.1.0

High-rate telemetry from a single electronic speed controller (ESC).

| | |
|---|---|
| Full name | `lanyard.propulsion.EscStatus` |
| Version | 1.0 |
| Kind | Message |
| Fixed port ID | 6200 |
| Transport tier | Classic CAN (CAN 2.0B). This definition is sealed and |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Message | 7 | 7 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# High-rate telemetry from a single electronic speed controller (ESC).
#
# TRANSPORT TIER: Classic CAN (CAN 2.0B). This definition is sealed and
# hand-packed so that a complete serialized value fits inside the seven
# payload bytes of one classic-CAN frame, leaving the eighth byte for
# the Cyphal/CAN tail byte. A multi-frame transfer would cost an extra
# transfer-CRC and would make the message vulnerable to frame loss,
# which is unacceptable for a value published at 100 Hz per ESC on a
# shared bus.
#
# WHY SEALED: sealing removes the four-byte delimiter header that a
# delimited (extensible) type carries, and it lets the serializer emit a
# fixed-size, fully static layout. The price is that this definition can
# never gain a field: any addition is a breaking change requiring a new
# major version. See EscStatus.2.0 for the extensible successor and the
# reasoning behind the migration.
#
# The scaled-integer fields below are the classic embedded trade: they
# cost a multiply on each side but a fraction of the bits that an IEEE
# 754 float would.

uint14 erpm_x10
# Electrical RPM divided by ten. Multiply by ten to recover eRPM; divide
# by the motor pole pair count to recover mechanical RPM. Range 0 to
# 163,830 eRPM, which covers every rotorcraft ESC in the fleet.

uint12 dc_voltage_dv
# DC bus voltage in decivolts (0.1 V per LSB). Range 0 to 409.5 V.

int12 dc_current_da
# DC bus current in deciamperes (0.1 A per LSB). Range -204.8 to +204.7
# A. Negative values indicate regenerative braking current flowing back
# into the pack.

int9 motor_temperature_c
# Motor winding temperature in whole degrees Celsius, range -256 to
# +255. Whole degrees are adequate because the thermal time constant of
# a motor is measured in tens of seconds.

uint4 error_flags
# Bit mask of latched controller faults. Cleared when the controller is
# re-armed.
#   bit 0 - over-current trip
#   bit 1 - over-temperature trip
#   bit 2 - desynchronization detected
#   bit 3 - supply under-voltage

void5
# Explicit padding to a whole number of bytes. Void fields are
# serialized as zeros and are not surfaced in generated code; they exist
# so that the wire layout is byte-aligned by construction rather than by
# accident.

@assert _offset_.max == 56
# The single-frame classic-CAN budget, asserted rather than merely
# documented. Adding a field or widening one breaks this build instead
# of silently pushing the message into a multi-frame transfer.

@sealed
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* High-rate telemetry from a single electronic speed controller (ESC). */
    /*  */
    /* TRANSPORT TIER: Classic CAN (CAN 2.0B). This definition is sealed and */
    /* hand-packed so that a complete serialized value fits inside the seven */
    /* payload bytes of one classic-CAN frame, leaving the eighth byte for */
    /* the Cyphal/CAN tail byte. A multi-frame transfer would cost an extra */
    /* transfer-CRC and would make the message vulnerable to frame loss, */
    /* which is unacceptable for a value published at 100 Hz per ESC on a */
    /* shared bus. */
    /*  */
    /* WHY SEALED: sealing removes the four-byte delimiter header that a */
    /* delimited (extensible) type carries, and it lets the serializer emit a */
    /* fixed-size, fully static layout. The price is that this definition can */
    /* never gain a field: any addition is a breaking change requiring a new */
    /* major version. See EscStatus.2.0 for the extensible successor and the */
    /* reasoning behind the migration. */
    /*  */
    /* The scaled-integer fields below are the classic embedded trade: they */
    /* cost a multiply on each side but a fraction of the bits that an IEEE */
    /* 754 float would. */
    typedef struct lanyard__propulsion__EscStatus {
      /* Electrical RPM divided by ten. Multiply by ten to recover eRPM; divide */
      /* by the motor pole pair count to recover mechanical RPM. Range 0 to */
      /* 163,830 eRPM, which covers every rotorcraft ESC in the fleet. */
      uint16_t erpm_x10;
      /* DC bus voltage in decivolts (0.1 V per LSB). Range 0 to 409.5 V. */
      uint16_t dc_voltage_dv;
      /* DC bus current in deciamperes (0.1 A per LSB). Range -204.8 to +204.7 */
      /* A. Negative values indicate regenerative braking current flowing back */
      /* into the pack. */
      int16_t dc_current_da;
      /* Motor winding temperature in whole degrees Celsius, range -256 to */
      /* +255. Whole degrees are adequate because the thermal time constant of */
      /* a motor is measured in tens of seconds. */
      int16_t motor_temperature_c;
      /* Bit mask of latched controller faults. Cleared when the controller is */
      /* re-armed. */
      /*   bit 0 - over-current trip */
      /*   bit 1 - over-temperature trip */
      /*   bit 2 - desynchronization detected */
      /*   bit 3 - supply under-voltage */
      uint8_t error_flags;
    } lanyard__propulsion__EscStatus;

    ```

=== "C++ (std)"

    ```cpp
    // High-rate telemetry from a single electronic speed controller (ESC).
    // 
    // TRANSPORT TIER: Classic CAN (CAN 2.0B). This definition is sealed and
    // hand-packed so that a complete serialized value fits inside the seven
    // payload bytes of one classic-CAN frame, leaving the eighth byte for
    // the Cyphal/CAN tail byte. A multi-frame transfer would cost an extra
    // transfer-CRC and would make the message vulnerable to frame loss,
    // which is unacceptable for a value published at 100 Hz per ESC on a
    // shared bus.
    // 
    // WHY SEALED: sealing removes the four-byte delimiter header that a
    // delimited (extensible) type carries, and it lets the serializer emit a
    // fixed-size, fully static layout. The price is that this definition can
    // never gain a field: any addition is a breaking change requiring a new
    // major version. See EscStatus.2.0 for the extensible successor and the
    // reasoning behind the migration.
    // 
    // The scaled-integer fields below are the classic embedded trade: they
    // cost a multiply on each side but a fraction of the bits that an IEEE
    // 754 float would.
    struct EscStatus_1_0 {
      // Electrical RPM divided by ten. Multiply by ten to recover eRPM; divide
      // by the motor pole pair count to recover mechanical RPM. Range 0 to
      // 163,830 eRPM, which covers every rotorcraft ESC in the fleet.
      std::uint16_t erpm_x10{};
      // DC bus voltage in decivolts (0.1 V per LSB). Range 0 to 409.5 V.
      std::uint16_t dc_voltage_dv{};
      // DC bus current in deciamperes (0.1 A per LSB). Range -204.8 to +204.7
      // A. Negative values indicate regenerative braking current flowing back
      // into the pack.
      std::int16_t dc_current_da{};
      // Motor winding temperature in whole degrees Celsius, range -256 to
      // +255. Whole degrees are adequate because the thermal time constant of
      // a motor is measured in tens of seconds.
      std::int16_t motor_temperature_c{};
      // Bit mask of latched controller faults. Cleared when the controller is
      // re-armed.
      //   bit 0 - over-current trip
      //   bit 1 - over-temperature trip
      //   bit 2 - desynchronization detected
      //   bit 3 - supply under-voltage
      std::uint8_t error_flags{};
      static constexpr const char* FULL_NAME = "lanyard.propulsion.EscStatus";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.propulsion.EscStatus.1.0";
      static constexpr std::size_t EXTENT_BYTES = 7U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 7U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return EscStatus_1_0__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return EscStatus_1_0__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return EscStatus_1_0__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return EscStatus_1_0__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // High-rate telemetry from a single electronic speed controller (ESC).
    // 
    // TRANSPORT TIER: Classic CAN (CAN 2.0B). This definition is sealed and
    // hand-packed so that a complete serialized value fits inside the seven
    // payload bytes of one classic-CAN frame, leaving the eighth byte for
    // the Cyphal/CAN tail byte. A multi-frame transfer would cost an extra
    // transfer-CRC and would make the message vulnerable to frame loss,
    // which is unacceptable for a value published at 100 Hz per ESC on a
    // shared bus.
    // 
    // WHY SEALED: sealing removes the four-byte delimiter header that a
    // delimited (extensible) type carries, and it lets the serializer emit a
    // fixed-size, fully static layout. The price is that this definition can
    // never gain a field: any addition is a breaking change requiring a new
    // major version. See EscStatus.2.0 for the extensible successor and the
    // reasoning behind the migration.
    // 
    // The scaled-integer fields below are the classic embedded trade: they
    // cost a multiply on each side but a fraction of the bits that an IEEE
    // 754 float would.
    struct EscStatus_1_0 {
      // Electrical RPM divided by ten. Multiply by ten to recover eRPM; divide
      // by the motor pole pair count to recover mechanical RPM. Range 0 to
      // 163,830 eRPM, which covers every rotorcraft ESC in the fleet.
      std::uint16_t erpm_x10{};
      // DC bus voltage in decivolts (0.1 V per LSB). Range 0 to 409.5 V.
      std::uint16_t dc_voltage_dv{};
      // DC bus current in deciamperes (0.1 A per LSB). Range -204.8 to +204.7
      // A. Negative values indicate regenerative braking current flowing back
      // into the pack.
      std::int16_t dc_current_da{};
      // Motor winding temperature in whole degrees Celsius, range -256 to
      // +255. Whole degrees are adequate because the thermal time constant of
      // a motor is measured in tens of seconds.
      std::int16_t motor_temperature_c{};
      // Bit mask of latched controller faults. Cleared when the controller is
      // re-armed.
      //   bit 0 - over-current trip
      //   bit 1 - over-temperature trip
      //   bit 2 - desynchronization detected
      //   bit 3 - supply under-voltage
      std::uint8_t error_flags{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      EscStatus_1_0() = default;
      explicit EscStatus_1_0(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
      }
      static constexpr const char* FULL_NAME = "lanyard.propulsion.EscStatus";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.propulsion.EscStatus.1.0";
      static constexpr std::size_t EXTENT_BYTES = 7U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 7U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return EscStatus_1_0__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return EscStatus_1_0__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return EscStatus_1_0__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return EscStatus_1_0__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return EscStatus_1_0__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return EscStatus_1_0__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // High-rate telemetry from a single electronic speed controller (ESC).
    // 
    // TRANSPORT TIER: Classic CAN (CAN 2.0B). This definition is sealed and
    // hand-packed so that a complete serialized value fits inside the seven
    // payload bytes of one classic-CAN frame, leaving the eighth byte for
    // the Cyphal/CAN tail byte. A multi-frame transfer would cost an extra
    // transfer-CRC and would make the message vulnerable to frame loss,
    // which is unacceptable for a value published at 100 Hz per ESC on a
    // shared bus.
    // 
    // WHY SEALED: sealing removes the four-byte delimiter header that a
    // delimited (extensible) type carries, and it lets the serializer emit a
    // fixed-size, fully static layout. The price is that this definition can
    // never gain a field: any addition is a breaking change requiring a new
    // major version. See EscStatus.2.0 for the extensible successor and the
    // reasoning behind the migration.
    // 
    // The scaled-integer fields below are the classic embedded trade: they
    // cost a multiply on each side but a fraction of the bits that an IEEE
    // 754 float would.
    struct EscStatus_1_0 {
      // Electrical RPM divided by ten. Multiply by ten to recover eRPM; divide
      // by the motor pole pair count to recover mechanical RPM. Range 0 to
      // 163,830 eRPM, which covers every rotorcraft ESC in the fleet.
      std::uint16_t erpm_x10{};
      // DC bus voltage in decivolts (0.1 V per LSB). Range 0 to 409.5 V.
      std::uint16_t dc_voltage_dv{};
      // DC bus current in deciamperes (0.1 A per LSB). Range -204.8 to +204.7
      // A. Negative values indicate regenerative braking current flowing back
      // into the pack.
      std::int16_t dc_current_da{};
      // Motor winding temperature in whole degrees Celsius, range -256 to
      // +255. Whole degrees are adequate because the thermal time constant of
      // a motor is measured in tens of seconds.
      std::int16_t motor_temperature_c{};
      // Bit mask of latched controller faults. Cleared when the controller is
      // re-armed.
      //   bit 0 - over-current trip
      //   bit 1 - over-temperature trip
      //   bit 2 - desynchronization detected
      //   bit 3 - supply under-voltage
      std::uint8_t error_flags{};
      static constexpr const char* FULL_NAME = "lanyard.propulsion.EscStatus";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.propulsion.EscStatus.1.0";
      static constexpr std::size_t EXTENT_BYTES = 7U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 7U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return EscStatus_1_0__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return EscStatus_1_0__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return EscStatus_1_0__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return EscStatus_1_0__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// High-rate telemetry from a single electronic speed controller (ESC).
    /// 
    /// TRANSPORT TIER: Classic CAN (CAN 2.0B). This definition is sealed and
    /// hand-packed so that a complete serialized value fits inside the seven
    /// payload bytes of one classic-CAN frame, leaving the eighth byte for
    /// the Cyphal/CAN tail byte. A multi-frame transfer would cost an extra
    /// transfer-CRC and would make the message vulnerable to frame loss,
    /// which is unacceptable for a value published at 100 Hz per ESC on a
    /// shared bus.
    /// 
    /// WHY SEALED: sealing removes the four-byte delimiter header that a
    /// delimited (extensible) type carries, and it lets the serializer emit a
    /// fixed-size, fully static layout. The price is that this definition can
    /// never gain a field: any addition is a breaking change requiring a new
    /// major version. See EscStatus.2.0 for the extensible successor and the
    /// reasoning behind the migration.
    /// 
    /// The scaled-integer fields below are the classic embedded trade: they
    /// cost a multiply on each side but a fraction of the bits that an IEEE
    /// 754 float would.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_propulsion_EscStatus_1_0 {
        /// Electrical RPM divided by ten. Multiply by ten to recover eRPM; divide
        /// by the motor pole pair count to recover mechanical RPM. Range 0 to
        /// 163,830 eRPM, which covers every rotorcraft ESC in the fleet.
        pub erpm_x10: u16,
        /// DC bus voltage in decivolts (0.1 V per LSB). Range 0 to 409.5 V.
        pub dc_voltage_dv: u16,
        /// DC bus current in deciamperes (0.1 A per LSB). Range -204.8 to +204.7
        /// A. Negative values indicate regenerative braking current flowing back
        /// into the pack.
        pub dc_current_da: i16,
        /// Motor winding temperature in whole degrees Celsius, range -256 to
        /// +255. Whole degrees are adequate because the thermal time constant of
        /// a motor is measured in tens of seconds.
        pub motor_temperature_c: i16,
        /// Bit mask of latched controller faults. Cleared when the controller is
        /// re-armed.
        ///   bit 0 - over-current trip
        ///   bit 1 - over-temperature trip
        ///   bit 2 - desynchronization detected
        ///   bit 3 - supply under-voltage
        pub error_flags: u8,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// High-rate telemetry from a single electronic speed controller (ESC).
    /// 
    /// TRANSPORT TIER: Classic CAN (CAN 2.0B). This definition is sealed and
    /// hand-packed so that a complete serialized value fits inside the seven
    /// payload bytes of one classic-CAN frame, leaving the eighth byte for
    /// the Cyphal/CAN tail byte. A multi-frame transfer would cost an extra
    /// transfer-CRC and would make the message vulnerable to frame loss,
    /// which is unacceptable for a value published at 100 Hz per ESC on a
    /// shared bus.
    /// 
    /// WHY SEALED: sealing removes the four-byte delimiter header that a
    /// delimited (extensible) type carries, and it lets the serializer emit a
    /// fixed-size, fully static layout. The price is that this definition can
    /// never gain a field: any addition is a breaking change requiring a new
    /// major version. See EscStatus.2.0 for the extensible successor and the
    /// reasoning behind the migration.
    /// 
    /// The scaled-integer fields below are the classic embedded trade: they
    /// cost a multiply on each side but a fraction of the bits that an IEEE
    /// 754 float would.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_propulsion_EscStatus_1_0 {
        /// Electrical RPM divided by ten. Multiply by ten to recover eRPM; divide
        /// by the motor pole pair count to recover mechanical RPM. Range 0 to
        /// 163,830 eRPM, which covers every rotorcraft ESC in the fleet.
        pub erpm_x10: u16,
        /// DC bus voltage in decivolts (0.1 V per LSB). Range 0 to 409.5 V.
        pub dc_voltage_dv: u16,
        /// DC bus current in deciamperes (0.1 A per LSB). Range -204.8 to +204.7
        /// A. Negative values indicate regenerative braking current flowing back
        /// into the pack.
        pub dc_current_da: i16,
        /// Motor winding temperature in whole degrees Celsius, range -256 to
        /// +255. Whole degrees are adequate because the thermal time constant of
        /// a motor is measured in tens of seconds.
        pub motor_temperature_c: i16,
        /// Bit mask of latched controller faults. Cleared when the controller is
        /// re-armed.
        ///   bit 0 - over-current trip
        ///   bit 1 - over-temperature trip
        ///   bit 2 - desynchronization detected
        ///   bit 3 - supply under-voltage
        pub error_flags: u8,
    }

    ```

=== "Go"

    ```go
    // High-rate telemetry from a single electronic speed controller (ESC).
    // 
    // TRANSPORT TIER: Classic CAN (CAN 2.0B). This definition is sealed and
    // hand-packed so that a complete serialized value fits inside the seven
    // payload bytes of one classic-CAN frame, leaving the eighth byte for
    // the Cyphal/CAN tail byte. A multi-frame transfer would cost an extra
    // transfer-CRC and would make the message vulnerable to frame loss,
    // which is unacceptable for a value published at 100 Hz per ESC on a
    // shared bus.
    // 
    // WHY SEALED: sealing removes the four-byte delimiter header that a
    // delimited (extensible) type carries, and it lets the serializer emit a
    // fixed-size, fully static layout. The price is that this definition can
    // never gain a field: any addition is a breaking change requiring a new
    // major version. See EscStatus.2.0 for the extensible successor and the
    // reasoning behind the migration.
    // 
    // The scaled-integer fields below are the classic embedded trade: they
    // cost a multiply on each side but a fraction of the bits that an IEEE
    // 754 float would.
    type EscStatus_1_0 struct {
      // Electrical RPM divided by ten. Multiply by ten to recover eRPM; divide
      // by the motor pole pair count to recover mechanical RPM. Range 0 to
      // 163,830 eRPM, which covers every rotorcraft ESC in the fleet.
      ErpmX10 uint16
      // DC bus voltage in decivolts (0.1 V per LSB). Range 0 to 409.5 V.
      DcVoltageDv uint16
      // DC bus current in deciamperes (0.1 A per LSB). Range -204.8 to +204.7
      // A. Negative values indicate regenerative braking current flowing back
      // into the pack.
      DcCurrentDa int16
      // Motor winding temperature in whole degrees Celsius, range -256 to
      // +255. Whole degrees are adequate because the thermal time constant of
      // a motor is measured in tens of seconds.
      MotorTemperatureC int16
      // Bit mask of latched controller faults. Cleared when the controller is
      // re-armed.
      //   bit 0 - over-current trip
      //   bit 1 - over-temperature trip
      //   bit 2 - desynchronization detected
      //   bit 3 - supply under-voltage
      ErrorFlags uint8
    }

    ```

=== "TypeScript"

    ```typescript
    // High-rate telemetry from a single electronic speed controller (ESC).
    // 
    // TRANSPORT TIER: Classic CAN (CAN 2.0B). This definition is sealed and
    // hand-packed so that a complete serialized value fits inside the seven
    // payload bytes of one classic-CAN frame, leaving the eighth byte for
    // the Cyphal/CAN tail byte. A multi-frame transfer would cost an extra
    // transfer-CRC and would make the message vulnerable to frame loss,
    // which is unacceptable for a value published at 100 Hz per ESC on a
    // shared bus.
    // 
    // WHY SEALED: sealing removes the four-byte delimiter header that a
    // delimited (extensible) type carries, and it lets the serializer emit a
    // fixed-size, fully static layout. The price is that this definition can
    // never gain a field: any addition is a breaking change requiring a new
    // major version. See EscStatus.2.0 for the extensible successor and the
    // reasoning behind the migration.
    // 
    // The scaled-integer fields below are the classic embedded trade: they
    // cost a multiply on each side but a fraction of the bits that an IEEE
    // 754 float would.
    export interface EscStatus_1_0 {
      // Electrical RPM divided by ten. Multiply by ten to recover eRPM; divide
      // by the motor pole pair count to recover mechanical RPM. Range 0 to
      // 163,830 eRPM, which covers every rotorcraft ESC in the fleet.
      erpm_x10: number;
      // DC bus voltage in decivolts (0.1 V per LSB). Range 0 to 409.5 V.
      dc_voltage_dv: number;
      // DC bus current in deciamperes (0.1 A per LSB). Range -204.8 to +204.7
      // A. Negative values indicate regenerative braking current flowing back
      // into the pack.
      dc_current_da: number;
      // Motor winding temperature in whole degrees Celsius, range -256 to
      // +255. Whole degrees are adequate because the thermal time constant of
      // a motor is measured in tens of seconds.
      motor_temperature_c: number;
      // Bit mask of latched controller faults. Cleared when the controller is
      // re-armed.
      //   bit 0 - over-current trip
      //   bit 1 - over-temperature trip
      //   bit 2 - desynchronization detected
      //   bit 3 - supply under-voltage
      error_flags: number;
    }

    ```

=== "Python"

    ```python
    # High-rate telemetry from a single electronic speed controller (ESC).
    # 
    # TRANSPORT TIER: Classic CAN (CAN 2.0B). This definition is sealed and
    # hand-packed so that a complete serialized value fits inside the seven
    # payload bytes of one classic-CAN frame, leaving the eighth byte for
    # the Cyphal/CAN tail byte. A multi-frame transfer would cost an extra
    # transfer-CRC and would make the message vulnerable to frame loss,
    # which is unacceptable for a value published at 100 Hz per ESC on a
    # shared bus.
    # 
    # WHY SEALED: sealing removes the four-byte delimiter header that a
    # delimited (extensible) type carries, and it lets the serializer emit a
    # fixed-size, fully static layout. The price is that this definition can
    # never gain a field: any addition is a breaking change requiring a new
    # major version. See EscStatus.2.0 for the extensible successor and the
    # reasoning behind the migration.
    # 
    # The scaled-integer fields below are the classic embedded trade: they
    # cost a multiply on each side but a fraction of the bits that an IEEE
    # 754 float would.
    @dataclass(slots=True)
    class EscStatus_1_0:
        # Electrical RPM divided by ten. Multiply by ten to recover eRPM; divide
        # by the motor pole pair count to recover mechanical RPM. Range 0 to
        # 163,830 eRPM, which covers every rotorcraft ESC in the fleet.
        erpm_x10: int = 0
        # DC bus voltage in decivolts (0.1 V per LSB). Range 0 to 409.5 V.
        dc_voltage_dv: int = 0
        # DC bus current in deciamperes (0.1 A per LSB). Range -204.8 to +204.7
        # A. Negative values indicate regenerative braking current flowing back
        # into the pack.
        dc_current_da: int = 0
        # Motor winding temperature in whole degrees Celsius, range -256 to
        # +255. Whole degrees are adequate because the thermal time constant of
        # a motor is measured in tens of seconds.
        motor_temperature_c: int = 0
        # Bit mask of latched controller faults. Cleared when the controller is
        # re-armed.
        #   bit 0 - over-current trip
        #   bit 1 - over-temperature trip
        #   bit 2 - desynchronization detected
        #   bit 3 - supply under-voltage
        error_flags: int = 0

    ```
