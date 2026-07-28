# lanyard.payload.GimbalStatus.1.0

Attitude and mode of the camera gimbal, published at 20 Hz.

| | |
|---|---|
| Full name | `lanyard.payload.GimbalStatus` |
| Version | 1.0 |
| Kind | Message |
| Fixed port ID | 6230 |
| Transport tier | CAN FD |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Message | 25 | 25 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# Attitude and mode of the camera gimbal, published at 20 Hz.
#
# TRANSPORT TIER: CAN FD.
#
# WHY SEALED: the gimbal controller is a small microcontroller with a
# static receive buffer, and this message has been stable across three
# airframes. A sealed definition lets the generated decoder be a
# straight-line series of loads with no length header to interpret and
# no skip path to implement.

uavcan.si.unit.angle.Vector3.1.0 orientation_body
# Gimbal orientation relative to the airframe body frame, as roll,
# pitch, and yaw in radians. Body frame rather than earth frame because
# the value is used to command the gimbal, and the operator commands it
# relative to the vehicle.

uavcan.si.unit.angular_velocity.Vector3.1.0 rate_body
# Gimbal angular rate about the same three axes, in radians per second.

uint3 mode
# Active stabilization mode; one of the constants below.

uint3 MODE_STOWED = 0
# The gimbal is parked and mechanically locked; motors are unpowered.

uint3 MODE_FOLLOW = 1
# Yaw follows the airframe heading; roll and pitch are
# horizon-stabilized.

uint3 MODE_LOCK = 2
# All three axes are earth-referenced; the gimbal holds a fixed
# geographic pointing direction.

uint3 MODE_TRACK = 3
# The gimbal is slaved to a tracker running on the payload computer.

uint3 MODE_FAULT = 4
# The gimbal has faulted and is not stabilizing; see error_flags.

uint4 error_flags
# Latched faults.
#   bit 0 - an axis reached its mechanical limit
#   bit 1 - motor driver over-temperature
#   bit 2 - inertial sensor failure
#   bit 3 - lost the airframe attitude reference

bool recording
# True when the payload is writing to storage. Carried here rather than
# in a separate message so that an operator watching gimbal telemetry
# sees the recording state at the same rate and latency.

@assert _offset_.max <= 63 * 8
# Must remain a single CAN FD frame.

@sealed
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* Attitude and mode of the camera gimbal, published at 20 Hz. */
    /*  */
    /* TRANSPORT TIER: CAN FD. */
    /*  */
    /* WHY SEALED: the gimbal controller is a small microcontroller with a */
    /* static receive buffer, and this message has been stable across three */
    /* airframes. A sealed definition lets the generated decoder be a */
    /* straight-line series of loads with no length header to interpret and */
    /* no skip path to implement. */
    typedef struct lanyard__payload__GimbalStatus {
      /* Gimbal orientation relative to the airframe body frame, as roll, */
      /* pitch, and yaw in radians. Body frame rather than earth frame because */
      /* the value is used to command the gimbal, and the operator commands it */
      /* relative to the vehicle. */
      uavcan__si__unit__angle__Vector3 orientation_body;
      /* Gimbal angular rate about the same three axes, in radians per second. */
      uavcan__si__unit__angular_velocity__Vector3 rate_body;
      /* Active stabilization mode; one of the constants below. */
      uint8_t mode;
      /* Latched faults. */
      /*   bit 0 - an axis reached its mechanical limit */
      /*   bit 1 - motor driver over-temperature */
      /*   bit 2 - inertial sensor failure */
      /*   bit 3 - lost the airframe attitude reference */
      uint8_t error_flags;
      /* True when the payload is writing to storage. Carried here rather than */
      /* in a separate message so that an operator watching gimbal telemetry */
      /* sees the recording state at the same rate and latency. */
      bool recording;
    } lanyard__payload__GimbalStatus;

    ```

=== "C++ (std)"

    ```cpp
    // Attitude and mode of the camera gimbal, published at 20 Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY SEALED: the gimbal controller is a small microcontroller with a
    // static receive buffer, and this message has been stable across three
    // airframes. A sealed definition lets the generated decoder be a
    // straight-line series of loads with no length header to interpret and
    // no skip path to implement.
    struct GimbalStatus {
      // Gimbal orientation relative to the airframe body frame, as roll,
      // pitch, and yaw in radians. Body frame rather than earth frame because
      // the value is used to command the gimbal, and the operator commands it
      // relative to the vehicle.
      ::uavcan::si::unit::angle::Vector3 orientation_body{};
      // Gimbal angular rate about the same three axes, in radians per second.
      ::uavcan::si::unit::angular_velocity::Vector3 rate_body{};
      // Active stabilization mode; one of the constants below.
      std::uint8_t mode{};
      // Latched faults.
      //   bit 0 - an axis reached its mechanical limit
      //   bit 1 - motor driver over-temperature
      //   bit 2 - inertial sensor failure
      //   bit 3 - lost the airframe attitude reference
      std::uint8_t error_flags{};
      // True when the payload is writing to storage. Carried here rather than
      // in a separate message so that an operator watching gimbal telemetry
      // sees the recording state at the same rate and latency.
      bool recording{};
      static constexpr const char* FULL_NAME = "lanyard.payload.GimbalStatus";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.payload.GimbalStatus.1.0";
      static constexpr std::size_t EXTENT_BYTES = 25U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 25U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      // The gimbal is parked and mechanically locked; motors are unpowered.
      static constexpr auto MODE_STOWED = 0;
      // Yaw follows the airframe heading; roll and pitch are
      // horizon-stabilized.
      static constexpr auto MODE_FOLLOW = 1;
      // All three axes are earth-referenced; the gimbal holds a fixed
      // geographic pointing direction.
      static constexpr auto MODE_LOCK = 2;
      // The gimbal is slaved to a tracker running on the payload computer.
      static constexpr auto MODE_TRACK = 3;
      // The gimbal has faulted and is not stabilizing; see error_flags.
      static constexpr auto MODE_FAULT = 4;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return GimbalStatus__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return GimbalStatus__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return GimbalStatus__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return GimbalStatus__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // Attitude and mode of the camera gimbal, published at 20 Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY SEALED: the gimbal controller is a small microcontroller with a
    // static receive buffer, and this message has been stable across three
    // airframes. A sealed definition lets the generated decoder be a
    // straight-line series of loads with no length header to interpret and
    // no skip path to implement.
    struct GimbalStatus {
      // Gimbal orientation relative to the airframe body frame, as roll,
      // pitch, and yaw in radians. Body frame rather than earth frame because
      // the value is used to command the gimbal, and the operator commands it
      // relative to the vehicle.
      ::uavcan::si::unit::angle::Vector3 orientation_body{};
      // Gimbal angular rate about the same three axes, in radians per second.
      ::uavcan::si::unit::angular_velocity::Vector3 rate_body{};
      // Active stabilization mode; one of the constants below.
      std::uint8_t mode{};
      // Latched faults.
      //   bit 0 - an axis reached its mechanical limit
      //   bit 1 - motor driver over-temperature
      //   bit 2 - inertial sensor failure
      //   bit 3 - lost the airframe attitude reference
      std::uint8_t error_flags{};
      // True when the payload is writing to storage. Carried here rather than
      // in a separate message so that an operator watching gimbal telemetry
      // sees the recording state at the same rate and latency.
      bool recording{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      GimbalStatus() = default;
      explicit GimbalStatus(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
        orientation_body.set_memory_resource(_memory_resource);
        rate_body.set_memory_resource(_memory_resource);
      }
      static constexpr const char* FULL_NAME = "lanyard.payload.GimbalStatus";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.payload.GimbalStatus.1.0";
      static constexpr std::size_t EXTENT_BYTES = 25U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 25U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      // The gimbal is parked and mechanically locked; motors are unpowered.
      static constexpr auto MODE_STOWED = 0;
      // Yaw follows the airframe heading; roll and pitch are
      // horizon-stabilized.
      static constexpr auto MODE_FOLLOW = 1;
      // All three axes are earth-referenced; the gimbal holds a fixed
      // geographic pointing direction.
      static constexpr auto MODE_LOCK = 2;
      // The gimbal is slaved to a tracker running on the payload computer.
      static constexpr auto MODE_TRACK = 3;
      // The gimbal has faulted and is not stabilizing; see error_flags.
      static constexpr auto MODE_FAULT = 4;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return GimbalStatus__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return GimbalStatus__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return GimbalStatus__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return GimbalStatus__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return GimbalStatus__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return GimbalStatus__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // Attitude and mode of the camera gimbal, published at 20 Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY SEALED: the gimbal controller is a small microcontroller with a
    // static receive buffer, and this message has been stable across three
    // airframes. A sealed definition lets the generated decoder be a
    // straight-line series of loads with no length header to interpret and
    // no skip path to implement.
    struct GimbalStatus {
      // Gimbal orientation relative to the airframe body frame, as roll,
      // pitch, and yaw in radians. Body frame rather than earth frame because
      // the value is used to command the gimbal, and the operator commands it
      // relative to the vehicle.
      ::uavcan::si::unit::angle::Vector3 orientation_body{};
      // Gimbal angular rate about the same three axes, in radians per second.
      ::uavcan::si::unit::angular_velocity::Vector3 rate_body{};
      // Active stabilization mode; one of the constants below.
      std::uint8_t mode{};
      // Latched faults.
      //   bit 0 - an axis reached its mechanical limit
      //   bit 1 - motor driver over-temperature
      //   bit 2 - inertial sensor failure
      //   bit 3 - lost the airframe attitude reference
      std::uint8_t error_flags{};
      // True when the payload is writing to storage. Carried here rather than
      // in a separate message so that an operator watching gimbal telemetry
      // sees the recording state at the same rate and latency.
      bool recording{};
      static constexpr const char* FULL_NAME = "lanyard.payload.GimbalStatus";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.payload.GimbalStatus.1.0";
      static constexpr std::size_t EXTENT_BYTES = 25U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 25U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      // The gimbal is parked and mechanically locked; motors are unpowered.
      static constexpr auto MODE_STOWED = 0;
      // Yaw follows the airframe heading; roll and pitch are
      // horizon-stabilized.
      static constexpr auto MODE_FOLLOW = 1;
      // All three axes are earth-referenced; the gimbal holds a fixed
      // geographic pointing direction.
      static constexpr auto MODE_LOCK = 2;
      // The gimbal is slaved to a tracker running on the payload computer.
      static constexpr auto MODE_TRACK = 3;
      // The gimbal has faulted and is not stabilizing; see error_flags.
      static constexpr auto MODE_FAULT = 4;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return GimbalStatus__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return GimbalStatus__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return GimbalStatus__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return GimbalStatus__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// Attitude and mode of the camera gimbal, published at 20 Hz.
    /// 
    /// TRANSPORT TIER: CAN FD.
    /// 
    /// WHY SEALED: the gimbal controller is a small microcontroller with a
    /// static receive buffer, and this message has been stable across three
    /// airframes. A sealed definition lets the generated decoder be a
    /// straight-line series of loads with no length header to interpret and
    /// no skip path to implement.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_payload_GimbalStatus_1_0 {
        /// Gimbal orientation relative to the airframe body frame, as roll,
        /// pitch, and yaw in radians. Body frame rather than earth frame because
        /// the value is used to command the gimbal, and the operator commands it
        /// relative to the vehicle.
        pub orientation_body: uavcan_si_unit_angle_Vector3_1_0,
        /// Gimbal angular rate about the same three axes, in radians per second.
        pub rate_body: uavcan_si_unit_angular_velocity_Vector3_1_0,
        /// Active stabilization mode; one of the constants below.
        pub mode: u8,
        /// Latched faults.
        ///   bit 0 - an axis reached its mechanical limit
        ///   bit 1 - motor driver over-temperature
        ///   bit 2 - inertial sensor failure
        ///   bit 3 - lost the airframe attitude reference
        pub error_flags: u8,
        /// True when the payload is writing to storage. Carried here rather than
        /// in a separate message so that an operator watching gimbal telemetry
        /// sees the recording state at the same rate and latency.
        pub recording: bool,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// Attitude and mode of the camera gimbal, published at 20 Hz.
    /// 
    /// TRANSPORT TIER: CAN FD.
    /// 
    /// WHY SEALED: the gimbal controller is a small microcontroller with a
    /// static receive buffer, and this message has been stable across three
    /// airframes. A sealed definition lets the generated decoder be a
    /// straight-line series of loads with no length header to interpret and
    /// no skip path to implement.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_payload_GimbalStatus_1_0 {
        /// Gimbal orientation relative to the airframe body frame, as roll,
        /// pitch, and yaw in radians. Body frame rather than earth frame because
        /// the value is used to command the gimbal, and the operator commands it
        /// relative to the vehicle.
        pub orientation_body: uavcan_si_unit_angle_Vector3_1_0,
        /// Gimbal angular rate about the same three axes, in radians per second.
        pub rate_body: uavcan_si_unit_angular_velocity_Vector3_1_0,
        /// Active stabilization mode; one of the constants below.
        pub mode: u8,
        /// Latched faults.
        ///   bit 0 - an axis reached its mechanical limit
        ///   bit 1 - motor driver over-temperature
        ///   bit 2 - inertial sensor failure
        ///   bit 3 - lost the airframe attitude reference
        pub error_flags: u8,
        /// True when the payload is writing to storage. Carried here rather than
        /// in a separate message so that an operator watching gimbal telemetry
        /// sees the recording state at the same rate and latency.
        pub recording: bool,
    }

    ```

=== "Go"

    ```go
    // Attitude and mode of the camera gimbal, published at 20 Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY SEALED: the gimbal controller is a small microcontroller with a
    // static receive buffer, and this message has been stable across three
    // airframes. A sealed definition lets the generated decoder be a
    // straight-line series of loads with no length header to interpret and
    // no skip path to implement.
    type GimbalStatus_1_0 struct {
      // Gimbal orientation relative to the airframe body frame, as roll,
      // pitch, and yaw in radians. Body frame rather than earth frame because
      // the value is used to command the gimbal, and the operator commands it
      // relative to the vehicle.
      OrientationBody pkg_uavcan_si_unit_angle.Vector3_1_0
      // Gimbal angular rate about the same three axes, in radians per second.
      RateBody pkg_uavcan_si_unit_angular_velocity.Vector3_1_0
      // Active stabilization mode; one of the constants below.
      Mode uint8
      // Latched faults.
      //   bit 0 - an axis reached its mechanical limit
      //   bit 1 - motor driver over-temperature
      //   bit 2 - inertial sensor failure
      //   bit 3 - lost the airframe attitude reference
      ErrorFlags uint8
      // True when the payload is writing to storage. Carried here rather than
      // in a separate message so that an operator watching gimbal telemetry
      // sees the recording state at the same rate and latency.
      Recording bool
    }

    ```

=== "TypeScript"

    ```typescript
    // Attitude and mode of the camera gimbal, published at 20 Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY SEALED: the gimbal controller is a small microcontroller with a
    // static receive buffer, and this message has been stable across three
    // airframes. A sealed definition lets the generated decoder be a
    // straight-line series of loads with no length header to interpret and
    // no skip path to implement.
    export interface GimbalStatus_1_0 {
      // Gimbal orientation relative to the airframe body frame, as roll,
      // pitch, and yaw in radians. Body frame rather than earth frame because
      // the value is used to command the gimbal, and the operator commands it
      // relative to the vehicle.
      orientation_body: Vector3_1_0;
      // Gimbal angular rate about the same three axes, in radians per second.
      rate_body: Vector3_1_0;
      // Active stabilization mode; one of the constants below.
      mode: number;
      // Latched faults.
      //   bit 0 - an axis reached its mechanical limit
      //   bit 1 - motor driver over-temperature
      //   bit 2 - inertial sensor failure
      //   bit 3 - lost the airframe attitude reference
      error_flags: number;
      // True when the payload is writing to storage. Carried here rather than
      // in a separate message so that an operator watching gimbal telemetry
      // sees the recording state at the same rate and latency.
      recording: boolean;
    }

    ```

=== "Python"

    ```python
    # Attitude and mode of the camera gimbal, published at 20 Hz.
    # 
    # TRANSPORT TIER: CAN FD.
    # 
    # WHY SEALED: the gimbal controller is a small microcontroller with a
    # static receive buffer, and this message has been stable across three
    # airframes. A sealed definition lets the generated decoder be a
    # straight-line series of loads with no length header to interpret and
    # no skip path to implement.
    @dataclass(slots=True)
    class GimbalStatus_1_0:
        # Gimbal orientation relative to the airframe body frame, as roll,
        # pitch, and yaw in radians. Body frame rather than earth frame because
        # the value is used to command the gimbal, and the operator commands it
        # relative to the vehicle.
        orientation_body: Vector3_1_0 = field(default_factory=Vector3_1_0)
        # Gimbal angular rate about the same three axes, in radians per second.
        rate_body: Vector3_1_0 = field(default_factory=Vector3_1_0)
        # Active stabilization mode; one of the constants below.
        mode: int = 0
        # Latched faults.
        #   bit 0 - an axis reached its mechanical limit
        #   bit 1 - motor driver over-temperature
        #   bit 2 - inertial sensor failure
        #   bit 3 - lost the airframe attitude reference
        error_flags: int = 0
        # True when the payload is writing to storage. Carried here rather than
        # in a separate message so that an operator watching gimbal telemetry
        # sees the recording state at the same rate and latency.
        recording: bool = False

    ```
