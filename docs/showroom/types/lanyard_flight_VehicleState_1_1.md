# lanyard.flight.VehicleState.1.1

Consolidated vehicle state, published by the flight controller at 50

| | |
|---|---|
| Full name | `lanyard.flight.VehicleState` |
| Version | 1.1 |
| Kind | Message |
| Fixed port ID | 6210 |
| Transport tier | CAN FD |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Message | 128 | 53 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# Consolidated vehicle state, published by the flight controller at 50
# Hz.
#
# TRANSPORT TIER: CAN FD.
#
# VERSIONING -- THIS IS THE NON-BREAKING CHANGE EXAMPLE.
#
# Relative to VehicleState.1.0 this definition appends one field and
# changes nothing else. That is permissible without a major version bump
# because:
#
#   1. The type is delimited (@extent), so every serialized value is
#      preceded by a length header and a reader may skip a tail it does
#      not recognize.
#   2. The extent is unchanged, so a 1.0 reader's buffer is still large
#      enough for a 1.1 message.
#   3. Existing fields keep their order, type, and meaning, so a 1.0
#      reader decoding a 1.1 message gets correct values for everything
#      it knows about and ignores height_above_takeoff.
#   4. A 1.1 reader decoding a 1.0 message sees a short tail and leaves
#      height_above_takeoff at its zero value -- which is why an
#      appended field must have a sane interpretation when absent.
#
# Because it is compatible in both directions, 1.1 keeps the same fixed
# port identifier as 1.0: both minor versions share port 6210 and
# coexist on the same subject.

uavcan.time.SynchronizedTimestamp.1.0 timestamp
# The network-synchronized moment this state estimate is valid for.

lanyard.flight.FlightMode.1.0 mode
# The active flight mode.

uavcan.si.unit.angle.Quaternion.1.0 orientation_ned
# Vehicle attitude as a unit quaternion in the North-East-Down reference
# frame.

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

uavcan.si.unit.length.Scalar.1.0 height_above_takeoff
# NEW IN 1.1. Height above the recorded takeoff elevation, in meters.
# Zero -- the value a 1.0 sender effectively transmits by omission --
# correctly means "at takeoff elevation", so the absent case degrades
# gracefully. An appended field whose zero value meant something
# dangerous would have been a breaking change in practice even though
# the wire format tolerated it.

@extent 128 * 8
# Unchanged from 1.0. Growing the extent would break 1.0 readers whose
# buffers are sized from it.
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
    /* VERSIONING -- THIS IS THE NON-BREAKING CHANGE EXAMPLE. */
    /*  */
    /* Relative to VehicleState.1.0 this definition appends one field and */
    /* changes nothing else. That is permissible without a major version bump */
    /* because: */
    /*  */
    /*   1. The type is delimited (@extent), so every serialized value is */
    /*      preceded by a length header and a reader may skip a tail it does */
    /*      not recognize. */
    /*   2. The extent is unchanged, so a 1.0 reader's buffer is still large */
    /*      enough for a 1.1 message. */
    /*   3. Existing fields keep their order, type, and meaning, so a 1.0 */
    /*      reader decoding a 1.1 message gets correct values for everything */
    /*      it knows about and ignores height_above_takeoff. */
    /*   4. A 1.1 reader decoding a 1.0 message sees a short tail and leaves */
    /*      height_above_takeoff at its zero value -- which is why an */
    /*      appended field must have a sane interpretation when absent. */
    /*  */
    /* Because it is compatible in both directions, 1.1 keeps the same fixed */
    /* port identifier as 1.0: both minor versions share port 6210 and */
    /* coexist on the same subject. */
    typedef struct lanyard__flight__VehicleState {
      /* The network-synchronized moment this state estimate is valid for. */
      uavcan__time__SynchronizedTimestamp timestamp;
      /* The active flight mode. */
      lanyard__flight__FlightMode mode;
      /* Vehicle attitude as a unit quaternion in the North-East-Down reference */
      /* frame. */
      uavcan__si__unit__angle__Quaternion orientation_ned;
      /* Body-frame roll, pitch, and yaw rates in radians per second. */
      uavcan__si__unit__angular_velocity__Vector3 angular_velocity;
      /* Velocity over ground in the North-East-Down frame, in meters per */
      /* second. */
      uavcan__si__unit__velocity__Vector3 velocity_ned;
      /* True when the propulsion system is energized and will respond to */
      /* throttle commands. */
      bool armed;
      /* NEW IN 1.1. Height above the recorded takeoff elevation, in meters. */
      /* Zero -- the value a 1.0 sender effectively transmits by omission -- */
      /* correctly means "at takeoff elevation", so the absent case degrades */
      /* gracefully. An appended field whose zero value meant something */
      /* dangerous would have been a breaking change in practice even though */
      /* the wire format tolerated it. */
      uavcan__si__unit__length__Scalar height_above_takeoff;
    } lanyard__flight__VehicleState;

    ```

=== "C++ (std)"

    ```cpp
    // Consolidated vehicle state, published by the flight controller at 50
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // VERSIONING -- THIS IS THE NON-BREAKING CHANGE EXAMPLE.
    // 
    // Relative to VehicleState.1.0 this definition appends one field and
    // changes nothing else. That is permissible without a major version bump
    // because:
    // 
    //   1. The type is delimited (@extent), so every serialized value is
    //      preceded by a length header and a reader may skip a tail it does
    //      not recognize.
    //   2. The extent is unchanged, so a 1.0 reader's buffer is still large
    //      enough for a 1.1 message.
    //   3. Existing fields keep their order, type, and meaning, so a 1.0
    //      reader decoding a 1.1 message gets correct values for everything
    //      it knows about and ignores height_above_takeoff.
    //   4. A 1.1 reader decoding a 1.0 message sees a short tail and leaves
    //      height_above_takeoff at its zero value -- which is why an
    //      appended field must have a sane interpretation when absent.
    // 
    // Because it is compatible in both directions, 1.1 keeps the same fixed
    // port identifier as 1.0: both minor versions share port 6210 and
    // coexist on the same subject.
    struct VehicleState_1_1 {
      // The network-synchronized moment this state estimate is valid for.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // The active flight mode.
      ::lanyard::flight::FlightMode mode{};
      // Vehicle attitude as a unit quaternion in the North-East-Down reference
      // frame.
      ::uavcan::si::unit::angle::Quaternion orientation_ned{};
      // Body-frame roll, pitch, and yaw rates in radians per second.
      ::uavcan::si::unit::angular_velocity::Vector3 angular_velocity{};
      // Velocity over ground in the North-East-Down frame, in meters per
      // second.
      ::uavcan::si::unit::velocity::Vector3 velocity_ned{};
      // True when the propulsion system is energized and will respond to
      // throttle commands.
      bool armed{};
      // NEW IN 1.1. Height above the recorded takeoff elevation, in meters.
      // Zero -- the value a 1.0 sender effectively transmits by omission --
      // correctly means "at takeoff elevation", so the absent case degrades
      // gracefully. An appended field whose zero value meant something
      // dangerous would have been a breaking change in practice even though
      // the wire format tolerated it.
      ::uavcan::si::unit::length::Scalar height_above_takeoff{};
      static constexpr const char* FULL_NAME = "lanyard.flight.VehicleState";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.flight.VehicleState.1.1";
      static constexpr std::size_t EXTENT_BYTES = 128U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 53U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return VehicleState_1_1__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return VehicleState_1_1__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return VehicleState_1_1__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return VehicleState_1_1__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
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
    // VERSIONING -- THIS IS THE NON-BREAKING CHANGE EXAMPLE.
    // 
    // Relative to VehicleState.1.0 this definition appends one field and
    // changes nothing else. That is permissible without a major version bump
    // because:
    // 
    //   1. The type is delimited (@extent), so every serialized value is
    //      preceded by a length header and a reader may skip a tail it does
    //      not recognize.
    //   2. The extent is unchanged, so a 1.0 reader's buffer is still large
    //      enough for a 1.1 message.
    //   3. Existing fields keep their order, type, and meaning, so a 1.0
    //      reader decoding a 1.1 message gets correct values for everything
    //      it knows about and ignores height_above_takeoff.
    //   4. A 1.1 reader decoding a 1.0 message sees a short tail and leaves
    //      height_above_takeoff at its zero value -- which is why an
    //      appended field must have a sane interpretation when absent.
    // 
    // Because it is compatible in both directions, 1.1 keeps the same fixed
    // port identifier as 1.0: both minor versions share port 6210 and
    // coexist on the same subject.
    struct VehicleState_1_1 {
      // The network-synchronized moment this state estimate is valid for.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // The active flight mode.
      ::lanyard::flight::FlightMode mode{};
      // Vehicle attitude as a unit quaternion in the North-East-Down reference
      // frame.
      ::uavcan::si::unit::angle::Quaternion orientation_ned{};
      // Body-frame roll, pitch, and yaw rates in radians per second.
      ::uavcan::si::unit::angular_velocity::Vector3 angular_velocity{};
      // Velocity over ground in the North-East-Down frame, in meters per
      // second.
      ::uavcan::si::unit::velocity::Vector3 velocity_ned{};
      // True when the propulsion system is energized and will respond to
      // throttle commands.
      bool armed{};
      // NEW IN 1.1. Height above the recorded takeoff elevation, in meters.
      // Zero -- the value a 1.0 sender effectively transmits by omission --
      // correctly means "at takeoff elevation", so the absent case degrades
      // gracefully. An appended field whose zero value meant something
      // dangerous would have been a breaking change in practice even though
      // the wire format tolerated it.
      ::uavcan::si::unit::length::Scalar height_above_takeoff{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      VehicleState_1_1() = default;
      explicit VehicleState_1_1(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
        timestamp.set_memory_resource(_memory_resource);
        mode.set_memory_resource(_memory_resource);
        orientation_ned.set_memory_resource(_memory_resource);
        angular_velocity.set_memory_resource(_memory_resource);
        velocity_ned.set_memory_resource(_memory_resource);
        height_above_takeoff.set_memory_resource(_memory_resource);
      }
      static constexpr const char* FULL_NAME = "lanyard.flight.VehicleState";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.flight.VehicleState.1.1";
      static constexpr std::size_t EXTENT_BYTES = 128U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 53U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return VehicleState_1_1__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return VehicleState_1_1__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return VehicleState_1_1__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return VehicleState_1_1__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return VehicleState_1_1__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return VehicleState_1_1__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
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
    // VERSIONING -- THIS IS THE NON-BREAKING CHANGE EXAMPLE.
    // 
    // Relative to VehicleState.1.0 this definition appends one field and
    // changes nothing else. That is permissible without a major version bump
    // because:
    // 
    //   1. The type is delimited (@extent), so every serialized value is
    //      preceded by a length header and a reader may skip a tail it does
    //      not recognize.
    //   2. The extent is unchanged, so a 1.0 reader's buffer is still large
    //      enough for a 1.1 message.
    //   3. Existing fields keep their order, type, and meaning, so a 1.0
    //      reader decoding a 1.1 message gets correct values for everything
    //      it knows about and ignores height_above_takeoff.
    //   4. A 1.1 reader decoding a 1.0 message sees a short tail and leaves
    //      height_above_takeoff at its zero value -- which is why an
    //      appended field must have a sane interpretation when absent.
    // 
    // Because it is compatible in both directions, 1.1 keeps the same fixed
    // port identifier as 1.0: both minor versions share port 6210 and
    // coexist on the same subject.
    struct VehicleState_1_1 {
      // The network-synchronized moment this state estimate is valid for.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // The active flight mode.
      ::lanyard::flight::FlightMode mode{};
      // Vehicle attitude as a unit quaternion in the North-East-Down reference
      // frame.
      ::uavcan::si::unit::angle::Quaternion orientation_ned{};
      // Body-frame roll, pitch, and yaw rates in radians per second.
      ::uavcan::si::unit::angular_velocity::Vector3 angular_velocity{};
      // Velocity over ground in the North-East-Down frame, in meters per
      // second.
      ::uavcan::si::unit::velocity::Vector3 velocity_ned{};
      // True when the propulsion system is energized and will respond to
      // throttle commands.
      bool armed{};
      // NEW IN 1.1. Height above the recorded takeoff elevation, in meters.
      // Zero -- the value a 1.0 sender effectively transmits by omission --
      // correctly means "at takeoff elevation", so the absent case degrades
      // gracefully. An appended field whose zero value meant something
      // dangerous would have been a breaking change in practice even though
      // the wire format tolerated it.
      ::uavcan::si::unit::length::Scalar height_above_takeoff{};
      static constexpr const char* FULL_NAME = "lanyard.flight.VehicleState";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.flight.VehicleState.1.1";
      static constexpr std::size_t EXTENT_BYTES = 128U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 53U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return VehicleState_1_1__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return VehicleState_1_1__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return VehicleState_1_1__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return VehicleState_1_1__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
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
    /// VERSIONING -- THIS IS THE NON-BREAKING CHANGE EXAMPLE.
    /// 
    /// Relative to VehicleState.1.0 this definition appends one field and
    /// changes nothing else. That is permissible without a major version bump
    /// because:
    /// 
    ///   1. The type is delimited (@extent), so every serialized value is
    ///      preceded by a length header and a reader may skip a tail it does
    ///      not recognize.
    ///   2. The extent is unchanged, so a 1.0 reader's buffer is still large
    ///      enough for a 1.1 message.
    ///   3. Existing fields keep their order, type, and meaning, so a 1.0
    ///      reader decoding a 1.1 message gets correct values for everything
    ///      it knows about and ignores height_above_takeoff.
    ///   4. A 1.1 reader decoding a 1.0 message sees a short tail and leaves
    ///      height_above_takeoff at its zero value -- which is why an
    ///      appended field must have a sane interpretation when absent.
    /// 
    /// Because it is compatible in both directions, 1.1 keeps the same fixed
    /// port identifier as 1.0: both minor versions share port 6210 and
    /// coexist on the same subject.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_flight_VehicleState_1_1 {
        /// The network-synchronized moment this state estimate is valid for.
        pub timestamp: uavcan_time_SynchronizedTimestamp_1_0,
        /// The active flight mode.
        pub mode: lanyard_flight_FlightMode_1_0,
        /// Vehicle attitude as a unit quaternion in the North-East-Down reference
        /// frame.
        pub orientation_ned: uavcan_si_unit_angle_Quaternion_1_0,
        /// Body-frame roll, pitch, and yaw rates in radians per second.
        pub angular_velocity: uavcan_si_unit_angular_velocity_Vector3_1_0,
        /// Velocity over ground in the North-East-Down frame, in meters per
        /// second.
        pub velocity_ned: uavcan_si_unit_velocity_Vector3_1_0,
        /// True when the propulsion system is energized and will respond to
        /// throttle commands.
        pub armed: bool,
        /// NEW IN 1.1. Height above the recorded takeoff elevation, in meters.
        /// Zero -- the value a 1.0 sender effectively transmits by omission --
        /// correctly means "at takeoff elevation", so the absent case degrades
        /// gracefully. An appended field whose zero value meant something
        /// dangerous would have been a breaking change in practice even though
        /// the wire format tolerated it.
        pub height_above_takeoff: uavcan_si_unit_length_Scalar_1_0,
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
    /// VERSIONING -- THIS IS THE NON-BREAKING CHANGE EXAMPLE.
    /// 
    /// Relative to VehicleState.1.0 this definition appends one field and
    /// changes nothing else. That is permissible without a major version bump
    /// because:
    /// 
    ///   1. The type is delimited (@extent), so every serialized value is
    ///      preceded by a length header and a reader may skip a tail it does
    ///      not recognize.
    ///   2. The extent is unchanged, so a 1.0 reader's buffer is still large
    ///      enough for a 1.1 message.
    ///   3. Existing fields keep their order, type, and meaning, so a 1.0
    ///      reader decoding a 1.1 message gets correct values for everything
    ///      it knows about and ignores height_above_takeoff.
    ///   4. A 1.1 reader decoding a 1.0 message sees a short tail and leaves
    ///      height_above_takeoff at its zero value -- which is why an
    ///      appended field must have a sane interpretation when absent.
    /// 
    /// Because it is compatible in both directions, 1.1 keeps the same fixed
    /// port identifier as 1.0: both minor versions share port 6210 and
    /// coexist on the same subject.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_flight_VehicleState_1_1 {
        /// The network-synchronized moment this state estimate is valid for.
        pub timestamp: uavcan_time_SynchronizedTimestamp_1_0,
        /// The active flight mode.
        pub mode: lanyard_flight_FlightMode_1_0,
        /// Vehicle attitude as a unit quaternion in the North-East-Down reference
        /// frame.
        pub orientation_ned: uavcan_si_unit_angle_Quaternion_1_0,
        /// Body-frame roll, pitch, and yaw rates in radians per second.
        pub angular_velocity: uavcan_si_unit_angular_velocity_Vector3_1_0,
        /// Velocity over ground in the North-East-Down frame, in meters per
        /// second.
        pub velocity_ned: uavcan_si_unit_velocity_Vector3_1_0,
        /// True when the propulsion system is energized and will respond to
        /// throttle commands.
        pub armed: bool,
        /// NEW IN 1.1. Height above the recorded takeoff elevation, in meters.
        /// Zero -- the value a 1.0 sender effectively transmits by omission --
        /// correctly means "at takeoff elevation", so the absent case degrades
        /// gracefully. An appended field whose zero value meant something
        /// dangerous would have been a breaking change in practice even though
        /// the wire format tolerated it.
        pub height_above_takeoff: uavcan_si_unit_length_Scalar_1_0,
    }

    ```

=== "Go"

    ```go
    // Consolidated vehicle state, published by the flight controller at 50
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // VERSIONING -- THIS IS THE NON-BREAKING CHANGE EXAMPLE.
    // 
    // Relative to VehicleState.1.0 this definition appends one field and
    // changes nothing else. That is permissible without a major version bump
    // because:
    // 
    //   1. The type is delimited (@extent), so every serialized value is
    //      preceded by a length header and a reader may skip a tail it does
    //      not recognize.
    //   2. The extent is unchanged, so a 1.0 reader's buffer is still large
    //      enough for a 1.1 message.
    //   3. Existing fields keep their order, type, and meaning, so a 1.0
    //      reader decoding a 1.1 message gets correct values for everything
    //      it knows about and ignores height_above_takeoff.
    //   4. A 1.1 reader decoding a 1.0 message sees a short tail and leaves
    //      height_above_takeoff at its zero value -- which is why an
    //      appended field must have a sane interpretation when absent.
    // 
    // Because it is compatible in both directions, 1.1 keeps the same fixed
    // port identifier as 1.0: both minor versions share port 6210 and
    // coexist on the same subject.
    type VehicleState_1_1 struct {
      // The network-synchronized moment this state estimate is valid for.
      Timestamp pkg_uavcan_time.SynchronizedTimestamp_1_0
      // The active flight mode.
      Mode FlightMode_1_0
      // Vehicle attitude as a unit quaternion in the North-East-Down reference
      // frame.
      OrientationNed pkg_uavcan_si_unit_angle.Quaternion_1_0
      // Body-frame roll, pitch, and yaw rates in radians per second.
      AngularVelocity pkg_uavcan_si_unit_angular_velocity.Vector3_1_0
      // Velocity over ground in the North-East-Down frame, in meters per
      // second.
      VelocityNed pkg_uavcan_si_unit_velocity.Vector3_1_0
      // True when the propulsion system is energized and will respond to
      // throttle commands.
      Armed bool
      // NEW IN 1.1. Height above the recorded takeoff elevation, in meters.
      // Zero -- the value a 1.0 sender effectively transmits by omission --
      // correctly means "at takeoff elevation", so the absent case degrades
      // gracefully. An appended field whose zero value meant something
      // dangerous would have been a breaking change in practice even though
      // the wire format tolerated it.
      HeightAboveTakeoff pkg_uavcan_si_unit_length.Scalar_1_0
    }

    ```

=== "TypeScript"

    ```typescript
    // Consolidated vehicle state, published by the flight controller at 50
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // VERSIONING -- THIS IS THE NON-BREAKING CHANGE EXAMPLE.
    // 
    // Relative to VehicleState.1.0 this definition appends one field and
    // changes nothing else. That is permissible without a major version bump
    // because:
    // 
    //   1. The type is delimited (@extent), so every serialized value is
    //      preceded by a length header and a reader may skip a tail it does
    //      not recognize.
    //   2. The extent is unchanged, so a 1.0 reader's buffer is still large
    //      enough for a 1.1 message.
    //   3. Existing fields keep their order, type, and meaning, so a 1.0
    //      reader decoding a 1.1 message gets correct values for everything
    //      it knows about and ignores height_above_takeoff.
    //   4. A 1.1 reader decoding a 1.0 message sees a short tail and leaves
    //      height_above_takeoff at its zero value -- which is why an
    //      appended field must have a sane interpretation when absent.
    // 
    // Because it is compatible in both directions, 1.1 keeps the same fixed
    // port identifier as 1.0: both minor versions share port 6210 and
    // coexist on the same subject.
    export interface VehicleState_1_1 {
      // The network-synchronized moment this state estimate is valid for.
      timestamp: SynchronizedTimestamp_1_0;
      // The active flight mode.
      mode: FlightMode_1_0;
      // Vehicle attitude as a unit quaternion in the North-East-Down reference
      // frame.
      orientation_ned: Quaternion_1_0;
      // Body-frame roll, pitch, and yaw rates in radians per second.
      angular_velocity: Vector3_1_0;
      // Velocity over ground in the North-East-Down frame, in meters per
      // second.
      velocity_ned: Vector3_1_0;
      // True when the propulsion system is energized and will respond to
      // throttle commands.
      armed: boolean;
      // NEW IN 1.1. Height above the recorded takeoff elevation, in meters.
      // Zero -- the value a 1.0 sender effectively transmits by omission --
      // correctly means "at takeoff elevation", so the absent case degrades
      // gracefully. An appended field whose zero value meant something
      // dangerous would have been a breaking change in practice even though
      // the wire format tolerated it.
      height_above_takeoff: Scalar_1_0;
    }

    ```

=== "Python"

    ```python
    # Consolidated vehicle state, published by the flight controller at 50
    # Hz.
    # 
    # TRANSPORT TIER: CAN FD.
    # 
    # VERSIONING -- THIS IS THE NON-BREAKING CHANGE EXAMPLE.
    # 
    # Relative to VehicleState.1.0 this definition appends one field and
    # changes nothing else. That is permissible without a major version bump
    # because:
    # 
    #   1. The type is delimited (@extent), so every serialized value is
    #      preceded by a length header and a reader may skip a tail it does
    #      not recognize.
    #   2. The extent is unchanged, so a 1.0 reader's buffer is still large
    #      enough for a 1.1 message.
    #   3. Existing fields keep their order, type, and meaning, so a 1.0
    #      reader decoding a 1.1 message gets correct values for everything
    #      it knows about and ignores height_above_takeoff.
    #   4. A 1.1 reader decoding a 1.0 message sees a short tail and leaves
    #      height_above_takeoff at its zero value -- which is why an
    #      appended field must have a sane interpretation when absent.
    # 
    # Because it is compatible in both directions, 1.1 keeps the same fixed
    # port identifier as 1.0: both minor versions share port 6210 and
    # coexist on the same subject.
    @dataclass(slots=True)
    class VehicleState_1_1:
        # The network-synchronized moment this state estimate is valid for.
        timestamp: SynchronizedTimestamp_1_0 = field(default_factory=SynchronizedTimestamp_1_0)
        # The active flight mode.
        mode: FlightMode_1_0 = field(default_factory=FlightMode_1_0)
        # Vehicle attitude as a unit quaternion in the North-East-Down reference
        # frame.
        orientation_ned: Quaternion_1_0 = field(default_factory=Quaternion_1_0)
        # Body-frame roll, pitch, and yaw rates in radians per second.
        angular_velocity: Vector3_1_0 = field(default_factory=Vector3_1_0)
        # Velocity over ground in the North-East-Down frame, in meters per
        # second.
        velocity_ned: Vector3_1_0 = field(default_factory=Vector3_1_0)
        # True when the propulsion system is energized and will respond to
        # throttle commands.
        armed: bool = False
        # NEW IN 1.1. Height above the recorded takeoff elevation, in meters.
        # Zero -- the value a 1.0 sender effectively transmits by omission --
        # correctly means "at takeoff elevation", so the absent case degrades
        # gracefully. An appended field whose zero value meant something
        # dangerous would have been a breaking change in practice even though
        # the wire format tolerated it.
        height_above_takeoff: Scalar_1_0 = field(default_factory=Scalar_1_0)

    ```
