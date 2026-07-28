# lanyard.flight.VehicleState.1.0

Consolidated vehicle state, published by the flight controller at 50

| | |
|---|---|
| Full name | `lanyard.flight.VehicleState` |
| Version | 1.0 |
| Kind | Message |
| Fixed port ID | 6210 |
| Transport tier | CAN FD |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Message | 128 | 49 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# Consolidated vehicle state, published by the flight controller at 50
# Hz.
#
# TRANSPORT TIER: CAN FD.
#
# WHY DELIMITED: this is the definition most likely to acquire new
# fields as the airframe evolves, and it is consumed by a wide
# population of nodes that are updated on their own schedules. The
# @extent below reserves wire space so that a future minor version may
# append fields; a reader built against 1.0 will decode the fields it
# knows and skip the rest instead of failing.
#
# Compare EscStatus.1.0, which makes the opposite trade: sealed,
# minimal, and frozen forever.
#
# See VehicleState.1.1 for a non-breaking extension of this definition,
# and VehicleState.2.0 for a breaking one.

uavcan.time.SynchronizedTimestamp.1.0 timestamp
# The network-synchronized moment this state estimate is valid for.
# Reusing the standard timestamp type rather than declaring a local
# uint64 is what allows a receiver to correlate this message with
# samples from any other node on the bus without knowing anything about
# this vendor's definitions.

lanyard.flight.FlightMode.1.0 mode
# The active flight mode.

uavcan.si.unit.angle.Quaternion.1.0 orientation_ned
# Vehicle attitude as a unit quaternion in the North-East-Down reference
# frame. Quaternions are used rather than Euler angles because they do
# not gimbal-lock at +/-90 degrees of pitch, which a vehicle with any
# aerobatic envelope will reach.

uavcan.si.unit.angular_velocity.Vector3.1.0 angular_velocity
# Body-frame roll, pitch, and yaw rates in radians per second.

uavcan.si.unit.velocity.Vector3.1.0 velocity_ned
# Velocity over ground in the North-East-Down frame, in meters per
# second.

bool armed
# True when the propulsion system is energized and will respond to
# throttle commands.

void7
# Padding to a byte boundary.

@extent 128 * 8
# Roughly 50 bytes are in use; the remainder is the growth budget for
# future 1.x minor versions.
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* Consolidated vehicle state, published by the flight controller at 50 */
    /* Hz. */
    /*  */
    /* TRANSPORT TIER: CAN FD. */
    /*  */
    /* WHY DELIMITED: this is the definition most likely to acquire new */
    /* fields as the airframe evolves, and it is consumed by a wide */
    /* population of nodes that are updated on their own schedules. The */
    /* @extent below reserves wire space so that a future minor version may */
    /* append fields; a reader built against 1.0 will decode the fields it */
    /* knows and skip the rest instead of failing. */
    /*  */
    /* Compare EscStatus.1.0, which makes the opposite trade: sealed, */
    /* minimal, and frozen forever. */
    /*  */
    /* See VehicleState.1.1 for a non-breaking extension of this definition, */
    /* and VehicleState.2.0 for a breaking one. */
    typedef struct lanyard__flight__VehicleState {
      /* The network-synchronized moment this state estimate is valid for. */
      /* Reusing the standard timestamp type rather than declaring a local */
      /* uint64 is what allows a receiver to correlate this message with */
      /* samples from any other node on the bus without knowing anything about */
      /* this vendor's definitions. */
      uavcan__time__SynchronizedTimestamp timestamp;
      /* The active flight mode. */
      lanyard__flight__FlightMode mode;
      /* Vehicle attitude as a unit quaternion in the North-East-Down reference */
      /* frame. Quaternions are used rather than Euler angles because they do */
      /* not gimbal-lock at +/-90 degrees of pitch, which a vehicle with any */
      /* aerobatic envelope will reach. */
      uavcan__si__unit__angle__Quaternion orientation_ned;
      /* Body-frame roll, pitch, and yaw rates in radians per second. */
      uavcan__si__unit__angular_velocity__Vector3 angular_velocity;
      /* Velocity over ground in the North-East-Down frame, in meters per */
      /* second. */
      uavcan__si__unit__velocity__Vector3 velocity_ned;
      /* True when the propulsion system is energized and will respond to */
      /* throttle commands. */
      bool armed;
    } lanyard__flight__VehicleState;

    ```

=== "C++ (std)"

    ```cpp
    // Consolidated vehicle state, published by the flight controller at 50
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY DELIMITED: this is the definition most likely to acquire new
    // fields as the airframe evolves, and it is consumed by a wide
    // population of nodes that are updated on their own schedules. The
    // @extent below reserves wire space so that a future minor version may
    // append fields; a reader built against 1.0 will decode the fields it
    // knows and skip the rest instead of failing.
    // 
    // Compare EscStatus.1.0, which makes the opposite trade: sealed,
    // minimal, and frozen forever.
    // 
    // See VehicleState.1.1 for a non-breaking extension of this definition,
    // and VehicleState.2.0 for a breaking one.
    struct VehicleState_1_0 {
      // The network-synchronized moment this state estimate is valid for.
      // Reusing the standard timestamp type rather than declaring a local
      // uint64 is what allows a receiver to correlate this message with
      // samples from any other node on the bus without knowing anything about
      // this vendor's definitions.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // The active flight mode.
      ::lanyard::flight::FlightMode mode{};
      // Vehicle attitude as a unit quaternion in the North-East-Down reference
      // frame. Quaternions are used rather than Euler angles because they do
      // not gimbal-lock at +/-90 degrees of pitch, which a vehicle with any
      // aerobatic envelope will reach.
      ::uavcan::si::unit::angle::Quaternion orientation_ned{};
      // Body-frame roll, pitch, and yaw rates in radians per second.
      ::uavcan::si::unit::angular_velocity::Vector3 angular_velocity{};
      // Velocity over ground in the North-East-Down frame, in meters per
      // second.
      ::uavcan::si::unit::velocity::Vector3 velocity_ned{};
      // True when the propulsion system is energized and will respond to
      // throttle commands.
      bool armed{};
      static constexpr const char* FULL_NAME = "lanyard.flight.VehicleState";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.flight.VehicleState.1.0";
      static constexpr std::size_t EXTENT_BYTES = 128U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 49U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return VehicleState_1_0__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return VehicleState_1_0__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return VehicleState_1_0__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return VehicleState_1_0__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // Consolidated vehicle state, published by the flight controller at 50
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY DELIMITED: this is the definition most likely to acquire new
    // fields as the airframe evolves, and it is consumed by a wide
    // population of nodes that are updated on their own schedules. The
    // @extent below reserves wire space so that a future minor version may
    // append fields; a reader built against 1.0 will decode the fields it
    // knows and skip the rest instead of failing.
    // 
    // Compare EscStatus.1.0, which makes the opposite trade: sealed,
    // minimal, and frozen forever.
    // 
    // See VehicleState.1.1 for a non-breaking extension of this definition,
    // and VehicleState.2.0 for a breaking one.
    struct VehicleState_1_0 {
      // The network-synchronized moment this state estimate is valid for.
      // Reusing the standard timestamp type rather than declaring a local
      // uint64 is what allows a receiver to correlate this message with
      // samples from any other node on the bus without knowing anything about
      // this vendor's definitions.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // The active flight mode.
      ::lanyard::flight::FlightMode mode{};
      // Vehicle attitude as a unit quaternion in the North-East-Down reference
      // frame. Quaternions are used rather than Euler angles because they do
      // not gimbal-lock at +/-90 degrees of pitch, which a vehicle with any
      // aerobatic envelope will reach.
      ::uavcan::si::unit::angle::Quaternion orientation_ned{};
      // Body-frame roll, pitch, and yaw rates in radians per second.
      ::uavcan::si::unit::angular_velocity::Vector3 angular_velocity{};
      // Velocity over ground in the North-East-Down frame, in meters per
      // second.
      ::uavcan::si::unit::velocity::Vector3 velocity_ned{};
      // True when the propulsion system is energized and will respond to
      // throttle commands.
      bool armed{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      VehicleState_1_0() = default;
      explicit VehicleState_1_0(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
        timestamp.set_memory_resource(_memory_resource);
        mode.set_memory_resource(_memory_resource);
        orientation_ned.set_memory_resource(_memory_resource);
        angular_velocity.set_memory_resource(_memory_resource);
        velocity_ned.set_memory_resource(_memory_resource);
      }
      static constexpr const char* FULL_NAME = "lanyard.flight.VehicleState";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.flight.VehicleState.1.0";
      static constexpr std::size_t EXTENT_BYTES = 128U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 49U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return VehicleState_1_0__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return VehicleState_1_0__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return VehicleState_1_0__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return VehicleState_1_0__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return VehicleState_1_0__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return VehicleState_1_0__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // Consolidated vehicle state, published by the flight controller at 50
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY DELIMITED: this is the definition most likely to acquire new
    // fields as the airframe evolves, and it is consumed by a wide
    // population of nodes that are updated on their own schedules. The
    // @extent below reserves wire space so that a future minor version may
    // append fields; a reader built against 1.0 will decode the fields it
    // knows and skip the rest instead of failing.
    // 
    // Compare EscStatus.1.0, which makes the opposite trade: sealed,
    // minimal, and frozen forever.
    // 
    // See VehicleState.1.1 for a non-breaking extension of this definition,
    // and VehicleState.2.0 for a breaking one.
    struct VehicleState_1_0 {
      // The network-synchronized moment this state estimate is valid for.
      // Reusing the standard timestamp type rather than declaring a local
      // uint64 is what allows a receiver to correlate this message with
      // samples from any other node on the bus without knowing anything about
      // this vendor's definitions.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // The active flight mode.
      ::lanyard::flight::FlightMode mode{};
      // Vehicle attitude as a unit quaternion in the North-East-Down reference
      // frame. Quaternions are used rather than Euler angles because they do
      // not gimbal-lock at +/-90 degrees of pitch, which a vehicle with any
      // aerobatic envelope will reach.
      ::uavcan::si::unit::angle::Quaternion orientation_ned{};
      // Body-frame roll, pitch, and yaw rates in radians per second.
      ::uavcan::si::unit::angular_velocity::Vector3 angular_velocity{};
      // Velocity over ground in the North-East-Down frame, in meters per
      // second.
      ::uavcan::si::unit::velocity::Vector3 velocity_ned{};
      // True when the propulsion system is energized and will respond to
      // throttle commands.
      bool armed{};
      static constexpr const char* FULL_NAME = "lanyard.flight.VehicleState";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.flight.VehicleState.1.0";
      static constexpr std::size_t EXTENT_BYTES = 128U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 49U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return VehicleState_1_0__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return VehicleState_1_0__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return VehicleState_1_0__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return VehicleState_1_0__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// Consolidated vehicle state, published by the flight controller at 50
    /// Hz.
    /// 
    /// TRANSPORT TIER: CAN FD.
    /// 
    /// WHY DELIMITED: this is the definition most likely to acquire new
    /// fields as the airframe evolves, and it is consumed by a wide
    /// population of nodes that are updated on their own schedules. The
    /// @extent below reserves wire space so that a future minor version may
    /// append fields; a reader built against 1.0 will decode the fields it
    /// knows and skip the rest instead of failing.
    /// 
    /// Compare EscStatus.1.0, which makes the opposite trade: sealed,
    /// minimal, and frozen forever.
    /// 
    /// See VehicleState.1.1 for a non-breaking extension of this definition,
    /// and VehicleState.2.0 for a breaking one.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_flight_VehicleState_1_0 {
        /// The network-synchronized moment this state estimate is valid for.
        /// Reusing the standard timestamp type rather than declaring a local
        /// uint64 is what allows a receiver to correlate this message with
        /// samples from any other node on the bus without knowing anything about
        /// this vendor's definitions.
        pub timestamp: uavcan_time_SynchronizedTimestamp_1_0,
        /// The active flight mode.
        pub mode: lanyard_flight_FlightMode_1_0,
        /// Vehicle attitude as a unit quaternion in the North-East-Down reference
        /// frame. Quaternions are used rather than Euler angles because they do
        /// not gimbal-lock at +/-90 degrees of pitch, which a vehicle with any
        /// aerobatic envelope will reach.
        pub orientation_ned: uavcan_si_unit_angle_Quaternion_1_0,
        /// Body-frame roll, pitch, and yaw rates in radians per second.
        pub angular_velocity: uavcan_si_unit_angular_velocity_Vector3_1_0,
        /// Velocity over ground in the North-East-Down frame, in meters per
        /// second.
        pub velocity_ned: uavcan_si_unit_velocity_Vector3_1_0,
        /// True when the propulsion system is energized and will respond to
        /// throttle commands.
        pub armed: bool,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// Consolidated vehicle state, published by the flight controller at 50
    /// Hz.
    /// 
    /// TRANSPORT TIER: CAN FD.
    /// 
    /// WHY DELIMITED: this is the definition most likely to acquire new
    /// fields as the airframe evolves, and it is consumed by a wide
    /// population of nodes that are updated on their own schedules. The
    /// @extent below reserves wire space so that a future minor version may
    /// append fields; a reader built against 1.0 will decode the fields it
    /// knows and skip the rest instead of failing.
    /// 
    /// Compare EscStatus.1.0, which makes the opposite trade: sealed,
    /// minimal, and frozen forever.
    /// 
    /// See VehicleState.1.1 for a non-breaking extension of this definition,
    /// and VehicleState.2.0 for a breaking one.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_flight_VehicleState_1_0 {
        /// The network-synchronized moment this state estimate is valid for.
        /// Reusing the standard timestamp type rather than declaring a local
        /// uint64 is what allows a receiver to correlate this message with
        /// samples from any other node on the bus without knowing anything about
        /// this vendor's definitions.
        pub timestamp: uavcan_time_SynchronizedTimestamp_1_0,
        /// The active flight mode.
        pub mode: lanyard_flight_FlightMode_1_0,
        /// Vehicle attitude as a unit quaternion in the North-East-Down reference
        /// frame. Quaternions are used rather than Euler angles because they do
        /// not gimbal-lock at +/-90 degrees of pitch, which a vehicle with any
        /// aerobatic envelope will reach.
        pub orientation_ned: uavcan_si_unit_angle_Quaternion_1_0,
        /// Body-frame roll, pitch, and yaw rates in radians per second.
        pub angular_velocity: uavcan_si_unit_angular_velocity_Vector3_1_0,
        /// Velocity over ground in the North-East-Down frame, in meters per
        /// second.
        pub velocity_ned: uavcan_si_unit_velocity_Vector3_1_0,
        /// True when the propulsion system is energized and will respond to
        /// throttle commands.
        pub armed: bool,
    }

    ```

=== "Go"

    ```go
    // Consolidated vehicle state, published by the flight controller at 50
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY DELIMITED: this is the definition most likely to acquire new
    // fields as the airframe evolves, and it is consumed by a wide
    // population of nodes that are updated on their own schedules. The
    // @extent below reserves wire space so that a future minor version may
    // append fields; a reader built against 1.0 will decode the fields it
    // knows and skip the rest instead of failing.
    // 
    // Compare EscStatus.1.0, which makes the opposite trade: sealed,
    // minimal, and frozen forever.
    // 
    // See VehicleState.1.1 for a non-breaking extension of this definition,
    // and VehicleState.2.0 for a breaking one.
    type VehicleState_1_0 struct {
      // The network-synchronized moment this state estimate is valid for.
      // Reusing the standard timestamp type rather than declaring a local
      // uint64 is what allows a receiver to correlate this message with
      // samples from any other node on the bus without knowing anything about
      // this vendor's definitions.
      Timestamp pkg_uavcan_time.SynchronizedTimestamp_1_0
      // The active flight mode.
      Mode FlightMode_1_0
      // Vehicle attitude as a unit quaternion in the North-East-Down reference
      // frame. Quaternions are used rather than Euler angles because they do
      // not gimbal-lock at +/-90 degrees of pitch, which a vehicle with any
      // aerobatic envelope will reach.
      OrientationNed pkg_uavcan_si_unit_angle.Quaternion_1_0
      // Body-frame roll, pitch, and yaw rates in radians per second.
      AngularVelocity pkg_uavcan_si_unit_angular_velocity.Vector3_1_0
      // Velocity over ground in the North-East-Down frame, in meters per
      // second.
      VelocityNed pkg_uavcan_si_unit_velocity.Vector3_1_0
      // True when the propulsion system is energized and will respond to
      // throttle commands.
      Armed bool
    }

    ```

=== "TypeScript"

    ```typescript
    // Consolidated vehicle state, published by the flight controller at 50
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY DELIMITED: this is the definition most likely to acquire new
    // fields as the airframe evolves, and it is consumed by a wide
    // population of nodes that are updated on their own schedules. The
    // @extent below reserves wire space so that a future minor version may
    // append fields; a reader built against 1.0 will decode the fields it
    // knows and skip the rest instead of failing.
    // 
    // Compare EscStatus.1.0, which makes the opposite trade: sealed,
    // minimal, and frozen forever.
    // 
    // See VehicleState.1.1 for a non-breaking extension of this definition,
    // and VehicleState.2.0 for a breaking one.
    export interface VehicleState_1_0 {
      // The network-synchronized moment this state estimate is valid for.
      // Reusing the standard timestamp type rather than declaring a local
      // uint64 is what allows a receiver to correlate this message with
      // samples from any other node on the bus without knowing anything about
      // this vendor's definitions.
      timestamp: SynchronizedTimestamp_1_0;
      // The active flight mode.
      mode: FlightMode_1_0;
      // Vehicle attitude as a unit quaternion in the North-East-Down reference
      // frame. Quaternions are used rather than Euler angles because they do
      // not gimbal-lock at +/-90 degrees of pitch, which a vehicle with any
      // aerobatic envelope will reach.
      orientation_ned: Quaternion_1_0;
      // Body-frame roll, pitch, and yaw rates in radians per second.
      angular_velocity: Vector3_1_0;
      // Velocity over ground in the North-East-Down frame, in meters per
      // second.
      velocity_ned: Vector3_1_0;
      // True when the propulsion system is energized and will respond to
      // throttle commands.
      armed: boolean;
    }

    ```

=== "Python"

    ```python
    # Consolidated vehicle state, published by the flight controller at 50
    # Hz.
    # 
    # TRANSPORT TIER: CAN FD.
    # 
    # WHY DELIMITED: this is the definition most likely to acquire new
    # fields as the airframe evolves, and it is consumed by a wide
    # population of nodes that are updated on their own schedules. The
    # @extent below reserves wire space so that a future minor version may
    # append fields; a reader built against 1.0 will decode the fields it
    # knows and skip the rest instead of failing.
    # 
    # Compare EscStatus.1.0, which makes the opposite trade: sealed,
    # minimal, and frozen forever.
    # 
    # See VehicleState.1.1 for a non-breaking extension of this definition,
    # and VehicleState.2.0 for a breaking one.
    @dataclass(slots=True)
    class VehicleState_1_0:
        # The network-synchronized moment this state estimate is valid for.
        # Reusing the standard timestamp type rather than declaring a local
        # uint64 is what allows a receiver to correlate this message with
        # samples from any other node on the bus without knowing anything about
        # this vendor's definitions.
        timestamp: SynchronizedTimestamp_1_0 = field(default_factory=SynchronizedTimestamp_1_0)
        # The active flight mode.
        mode: FlightMode_1_0 = field(default_factory=FlightMode_1_0)
        # Vehicle attitude as a unit quaternion in the North-East-Down reference
        # frame. Quaternions are used rather than Euler angles because they do
        # not gimbal-lock at +/-90 degrees of pitch, which a vehicle with any
        # aerobatic envelope will reach.
        orientation_ned: Quaternion_1_0 = field(default_factory=Quaternion_1_0)
        # Body-frame roll, pitch, and yaw rates in radians per second.
        angular_velocity: Vector3_1_0 = field(default_factory=Vector3_1_0)
        # Velocity over ground in the North-East-Down frame, in meters per
        # second.
        velocity_ned: Vector3_1_0 = field(default_factory=Vector3_1_0)
        # True when the propulsion system is energized and will respond to
        # throttle commands.
        armed: bool = False

    ```
