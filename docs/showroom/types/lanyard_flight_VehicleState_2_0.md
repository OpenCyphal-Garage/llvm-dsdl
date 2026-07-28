# lanyard.flight.VehicleState.2.0

Consolidated vehicle state, published by the flight controller at 50

| | |
|---|---|
| Full name | `lanyard.flight.VehicleState` |
| Version | 2.0 |
| Kind | Message |
| Fixed port ID | 6211 |
| Transport tier | CAN FD |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Message | 192 | 57 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# Consolidated vehicle state, published by the flight controller at 50
# Hz.
#
# TRANSPORT TIER: CAN FD.
#
# VERSIONING -- THIS IS THE BREAKING CHANGE EXAMPLE.
#
# Every difference from VehicleState.1.1 below is individually
# sufficient to require a major bump:
#
#   1. `height_above_takeoff` is retyped from length.Scalar (float32) to
#      length.WideScalar (float64). Same name, same position, different
#      wire width: a 1.x reader would decode garbage from that offset
#      onward. Retyping a field is always breaking, even when the new
#      type is strictly more precise.
#   2. `armed` (bool) is replaced by `status_flags` (uint8), folding
#      several booleans into one field. Removing or repurposing a field
#      is breaking.
#   3. The extent grows from 128 to 192 bytes. A 1.x reader sizes its
#      receive buffer from the extent it was compiled against, so a
#      larger message can overrun it.
#
# Because major versions are not interchangeable, this definition takes
# a new fixed port identifier (6211). A subject carries exactly one
# major version; nodes migrate by switching subscriptions, and a bridge
# node may publish both during a transition.

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

uavcan.si.unit.length.WideScalar.1.0 height_above_takeoff
# Height above the recorded takeoff elevation, in meters. Widened from
# float32 to float64 in 2.0 so that the integrated altitude solution
# stops accumulating rounding error over long survey flights.

uint8 status_flags
# Bit mask replacing the standalone `armed` boolean of 1.x.
#   bit 0 - propulsion armed
#   bit 1 - navigation solution valid
#   bit 2 - geofence breached
#   bit 3 - low battery threshold crossed
#   bits 4..7 - reserved, transmitted as zero

@extent 192 * 8
# Grown from 128 bytes to accommodate the wider velocity field and leave
# a growth budget for 2.x.
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
    /* VERSIONING -- THIS IS THE BREAKING CHANGE EXAMPLE. */
    /*  */
    /* Every difference from VehicleState.1.1 below is individually */
    /* sufficient to require a major bump: */
    /*  */
    /*   1. `height_above_takeoff` is retyped from length.Scalar (float32) to */
    /*      length.WideScalar (float64). Same name, same position, different */
    /*      wire width: a 1.x reader would decode garbage from that offset */
    /*      onward. Retyping a field is always breaking, even when the new */
    /*      type is strictly more precise. */
    /*   2. `armed` (bool) is replaced by `status_flags` (uint8), folding */
    /*      several booleans into one field. Removing or repurposing a field */
    /*      is breaking. */
    /*   3. The extent grows from 128 to 192 bytes. A 1.x reader sizes its */
    /*      receive buffer from the extent it was compiled against, so a */
    /*      larger message can overrun it. */
    /*  */
    /* Because major versions are not interchangeable, this definition takes */
    /* a new fixed port identifier (6211). A subject carries exactly one */
    /* major version; nodes migrate by switching subscriptions, and a bridge */
    /* node may publish both during a transition. */
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
      /* Height above the recorded takeoff elevation, in meters. Widened from */
      /* float32 to float64 in 2.0 so that the integrated altitude solution */
      /* stops accumulating rounding error over long survey flights. */
      uavcan__si__unit__length__WideScalar height_above_takeoff;
      /* Bit mask replacing the standalone `armed` boolean of 1.x. */
      /*   bit 0 - propulsion armed */
      /*   bit 1 - navigation solution valid */
      /*   bit 2 - geofence breached */
      /*   bit 3 - low battery threshold crossed */
      /*   bits 4..7 - reserved, transmitted as zero */
      uint8_t status_flags;
    } lanyard__flight__VehicleState;

    ```

=== "C++ (std)"

    ```cpp
    // Consolidated vehicle state, published by the flight controller at 50
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // VERSIONING -- THIS IS THE BREAKING CHANGE EXAMPLE.
    // 
    // Every difference from VehicleState.1.1 below is individually
    // sufficient to require a major bump:
    // 
    //   1. `height_above_takeoff` is retyped from length.Scalar (float32) to
    //      length.WideScalar (float64). Same name, same position, different
    //      wire width: a 1.x reader would decode garbage from that offset
    //      onward. Retyping a field is always breaking, even when the new
    //      type is strictly more precise.
    //   2. `armed` (bool) is replaced by `status_flags` (uint8), folding
    //      several booleans into one field. Removing or repurposing a field
    //      is breaking.
    //   3. The extent grows from 128 to 192 bytes. A 1.x reader sizes its
    //      receive buffer from the extent it was compiled against, so a
    //      larger message can overrun it.
    // 
    // Because major versions are not interchangeable, this definition takes
    // a new fixed port identifier (6211). A subject carries exactly one
    // major version; nodes migrate by switching subscriptions, and a bridge
    // node may publish both during a transition.
    struct VehicleState_2_0 {
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
      // Height above the recorded takeoff elevation, in meters. Widened from
      // float32 to float64 in 2.0 so that the integrated altitude solution
      // stops accumulating rounding error over long survey flights.
      ::uavcan::si::unit::length::WideScalar height_above_takeoff{};
      // Bit mask replacing the standalone `armed` boolean of 1.x.
      //   bit 0 - propulsion armed
      //   bit 1 - navigation solution valid
      //   bit 2 - geofence breached
      //   bit 3 - low battery threshold crossed
      //   bits 4..7 - reserved, transmitted as zero
      std::uint8_t status_flags{};
      static constexpr const char* FULL_NAME = "lanyard.flight.VehicleState";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.flight.VehicleState.2.0";
      static constexpr std::size_t EXTENT_BYTES = 192U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 57U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return VehicleState_2_0__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return VehicleState_2_0__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return VehicleState_2_0__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return VehicleState_2_0__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
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
    // VERSIONING -- THIS IS THE BREAKING CHANGE EXAMPLE.
    // 
    // Every difference from VehicleState.1.1 below is individually
    // sufficient to require a major bump:
    // 
    //   1. `height_above_takeoff` is retyped from length.Scalar (float32) to
    //      length.WideScalar (float64). Same name, same position, different
    //      wire width: a 1.x reader would decode garbage from that offset
    //      onward. Retyping a field is always breaking, even when the new
    //      type is strictly more precise.
    //   2. `armed` (bool) is replaced by `status_flags` (uint8), folding
    //      several booleans into one field. Removing or repurposing a field
    //      is breaking.
    //   3. The extent grows from 128 to 192 bytes. A 1.x reader sizes its
    //      receive buffer from the extent it was compiled against, so a
    //      larger message can overrun it.
    // 
    // Because major versions are not interchangeable, this definition takes
    // a new fixed port identifier (6211). A subject carries exactly one
    // major version; nodes migrate by switching subscriptions, and a bridge
    // node may publish both during a transition.
    struct VehicleState_2_0 {
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
      // Height above the recorded takeoff elevation, in meters. Widened from
      // float32 to float64 in 2.0 so that the integrated altitude solution
      // stops accumulating rounding error over long survey flights.
      ::uavcan::si::unit::length::WideScalar height_above_takeoff{};
      // Bit mask replacing the standalone `armed` boolean of 1.x.
      //   bit 0 - propulsion armed
      //   bit 1 - navigation solution valid
      //   bit 2 - geofence breached
      //   bit 3 - low battery threshold crossed
      //   bits 4..7 - reserved, transmitted as zero
      std::uint8_t status_flags{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      VehicleState_2_0() = default;
      explicit VehicleState_2_0(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
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
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.flight.VehicleState.2.0";
      static constexpr std::size_t EXTENT_BYTES = 192U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 57U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return VehicleState_2_0__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return VehicleState_2_0__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return VehicleState_2_0__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return VehicleState_2_0__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return VehicleState_2_0__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return VehicleState_2_0__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
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
    // VERSIONING -- THIS IS THE BREAKING CHANGE EXAMPLE.
    // 
    // Every difference from VehicleState.1.1 below is individually
    // sufficient to require a major bump:
    // 
    //   1. `height_above_takeoff` is retyped from length.Scalar (float32) to
    //      length.WideScalar (float64). Same name, same position, different
    //      wire width: a 1.x reader would decode garbage from that offset
    //      onward. Retyping a field is always breaking, even when the new
    //      type is strictly more precise.
    //   2. `armed` (bool) is replaced by `status_flags` (uint8), folding
    //      several booleans into one field. Removing or repurposing a field
    //      is breaking.
    //   3. The extent grows from 128 to 192 bytes. A 1.x reader sizes its
    //      receive buffer from the extent it was compiled against, so a
    //      larger message can overrun it.
    // 
    // Because major versions are not interchangeable, this definition takes
    // a new fixed port identifier (6211). A subject carries exactly one
    // major version; nodes migrate by switching subscriptions, and a bridge
    // node may publish both during a transition.
    struct VehicleState_2_0 {
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
      // Height above the recorded takeoff elevation, in meters. Widened from
      // float32 to float64 in 2.0 so that the integrated altitude solution
      // stops accumulating rounding error over long survey flights.
      ::uavcan::si::unit::length::WideScalar height_above_takeoff{};
      // Bit mask replacing the standalone `armed` boolean of 1.x.
      //   bit 0 - propulsion armed
      //   bit 1 - navigation solution valid
      //   bit 2 - geofence breached
      //   bit 3 - low battery threshold crossed
      //   bits 4..7 - reserved, transmitted as zero
      std::uint8_t status_flags{};
      static constexpr const char* FULL_NAME = "lanyard.flight.VehicleState";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.flight.VehicleState.2.0";
      static constexpr std::size_t EXTENT_BYTES = 192U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 57U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return VehicleState_2_0__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return VehicleState_2_0__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return VehicleState_2_0__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return VehicleState_2_0__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
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
    /// VERSIONING -- THIS IS THE BREAKING CHANGE EXAMPLE.
    /// 
    /// Every difference from VehicleState.1.1 below is individually
    /// sufficient to require a major bump:
    /// 
    ///   1. `height_above_takeoff` is retyped from length.Scalar (float32) to
    ///      length.WideScalar (float64). Same name, same position, different
    ///      wire width: a 1.x reader would decode garbage from that offset
    ///      onward. Retyping a field is always breaking, even when the new
    ///      type is strictly more precise.
    ///   2. `armed` (bool) is replaced by `status_flags` (uint8), folding
    ///      several booleans into one field. Removing or repurposing a field
    ///      is breaking.
    ///   3. The extent grows from 128 to 192 bytes. A 1.x reader sizes its
    ///      receive buffer from the extent it was compiled against, so a
    ///      larger message can overrun it.
    /// 
    /// Because major versions are not interchangeable, this definition takes
    /// a new fixed port identifier (6211). A subject carries exactly one
    /// major version; nodes migrate by switching subscriptions, and a bridge
    /// node may publish both during a transition.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_flight_VehicleState_2_0 {
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
        /// Height above the recorded takeoff elevation, in meters. Widened from
        /// float32 to float64 in 2.0 so that the integrated altitude solution
        /// stops accumulating rounding error over long survey flights.
        pub height_above_takeoff: uavcan_si_unit_length_WideScalar_1_0,
        /// Bit mask replacing the standalone `armed` boolean of 1.x.
        ///   bit 0 - propulsion armed
        ///   bit 1 - navigation solution valid
        ///   bit 2 - geofence breached
        ///   bit 3 - low battery threshold crossed
        ///   bits 4..7 - reserved, transmitted as zero
        pub status_flags: u8,
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
    /// VERSIONING -- THIS IS THE BREAKING CHANGE EXAMPLE.
    /// 
    /// Every difference from VehicleState.1.1 below is individually
    /// sufficient to require a major bump:
    /// 
    ///   1. `height_above_takeoff` is retyped from length.Scalar (float32) to
    ///      length.WideScalar (float64). Same name, same position, different
    ///      wire width: a 1.x reader would decode garbage from that offset
    ///      onward. Retyping a field is always breaking, even when the new
    ///      type is strictly more precise.
    ///   2. `armed` (bool) is replaced by `status_flags` (uint8), folding
    ///      several booleans into one field. Removing or repurposing a field
    ///      is breaking.
    ///   3. The extent grows from 128 to 192 bytes. A 1.x reader sizes its
    ///      receive buffer from the extent it was compiled against, so a
    ///      larger message can overrun it.
    /// 
    /// Because major versions are not interchangeable, this definition takes
    /// a new fixed port identifier (6211). A subject carries exactly one
    /// major version; nodes migrate by switching subscriptions, and a bridge
    /// node may publish both during a transition.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_flight_VehicleState_2_0 {
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
        /// Height above the recorded takeoff elevation, in meters. Widened from
        /// float32 to float64 in 2.0 so that the integrated altitude solution
        /// stops accumulating rounding error over long survey flights.
        pub height_above_takeoff: uavcan_si_unit_length_WideScalar_1_0,
        /// Bit mask replacing the standalone `armed` boolean of 1.x.
        ///   bit 0 - propulsion armed
        ///   bit 1 - navigation solution valid
        ///   bit 2 - geofence breached
        ///   bit 3 - low battery threshold crossed
        ///   bits 4..7 - reserved, transmitted as zero
        pub status_flags: u8,
    }

    ```

=== "Go"

    ```go
    // Consolidated vehicle state, published by the flight controller at 50
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // VERSIONING -- THIS IS THE BREAKING CHANGE EXAMPLE.
    // 
    // Every difference from VehicleState.1.1 below is individually
    // sufficient to require a major bump:
    // 
    //   1. `height_above_takeoff` is retyped from length.Scalar (float32) to
    //      length.WideScalar (float64). Same name, same position, different
    //      wire width: a 1.x reader would decode garbage from that offset
    //      onward. Retyping a field is always breaking, even when the new
    //      type is strictly more precise.
    //   2. `armed` (bool) is replaced by `status_flags` (uint8), folding
    //      several booleans into one field. Removing or repurposing a field
    //      is breaking.
    //   3. The extent grows from 128 to 192 bytes. A 1.x reader sizes its
    //      receive buffer from the extent it was compiled against, so a
    //      larger message can overrun it.
    // 
    // Because major versions are not interchangeable, this definition takes
    // a new fixed port identifier (6211). A subject carries exactly one
    // major version; nodes migrate by switching subscriptions, and a bridge
    // node may publish both during a transition.
    type VehicleState_2_0 struct {
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
      // Height above the recorded takeoff elevation, in meters. Widened from
      // float32 to float64 in 2.0 so that the integrated altitude solution
      // stops accumulating rounding error over long survey flights.
      HeightAboveTakeoff pkg_uavcan_si_unit_length.WideScalar_1_0
      // Bit mask replacing the standalone `armed` boolean of 1.x.
      //   bit 0 - propulsion armed
      //   bit 1 - navigation solution valid
      //   bit 2 - geofence breached
      //   bit 3 - low battery threshold crossed
      //   bits 4..7 - reserved, transmitted as zero
      StatusFlags uint8
    }

    ```

=== "TypeScript"

    ```typescript
    // Consolidated vehicle state, published by the flight controller at 50
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // VERSIONING -- THIS IS THE BREAKING CHANGE EXAMPLE.
    // 
    // Every difference from VehicleState.1.1 below is individually
    // sufficient to require a major bump:
    // 
    //   1. `height_above_takeoff` is retyped from length.Scalar (float32) to
    //      length.WideScalar (float64). Same name, same position, different
    //      wire width: a 1.x reader would decode garbage from that offset
    //      onward. Retyping a field is always breaking, even when the new
    //      type is strictly more precise.
    //   2. `armed` (bool) is replaced by `status_flags` (uint8), folding
    //      several booleans into one field. Removing or repurposing a field
    //      is breaking.
    //   3. The extent grows from 128 to 192 bytes. A 1.x reader sizes its
    //      receive buffer from the extent it was compiled against, so a
    //      larger message can overrun it.
    // 
    // Because major versions are not interchangeable, this definition takes
    // a new fixed port identifier (6211). A subject carries exactly one
    // major version; nodes migrate by switching subscriptions, and a bridge
    // node may publish both during a transition.
    export interface VehicleState_2_0 {
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
      // Height above the recorded takeoff elevation, in meters. Widened from
      // float32 to float64 in 2.0 so that the integrated altitude solution
      // stops accumulating rounding error over long survey flights.
      height_above_takeoff: WideScalar_1_0;
      // Bit mask replacing the standalone `armed` boolean of 1.x.
      //   bit 0 - propulsion armed
      //   bit 1 - navigation solution valid
      //   bit 2 - geofence breached
      //   bit 3 - low battery threshold crossed
      //   bits 4..7 - reserved, transmitted as zero
      status_flags: number;
    }

    ```

=== "Python"

    ```python
    # Consolidated vehicle state, published by the flight controller at 50
    # Hz.
    # 
    # TRANSPORT TIER: CAN FD.
    # 
    # VERSIONING -- THIS IS THE BREAKING CHANGE EXAMPLE.
    # 
    # Every difference from VehicleState.1.1 below is individually
    # sufficient to require a major bump:
    # 
    #   1. `height_above_takeoff` is retyped from length.Scalar (float32) to
    #      length.WideScalar (float64). Same name, same position, different
    #      wire width: a 1.x reader would decode garbage from that offset
    #      onward. Retyping a field is always breaking, even when the new
    #      type is strictly more precise.
    #   2. `armed` (bool) is replaced by `status_flags` (uint8), folding
    #      several booleans into one field. Removing or repurposing a field
    #      is breaking.
    #   3. The extent grows from 128 to 192 bytes. A 1.x reader sizes its
    #      receive buffer from the extent it was compiled against, so a
    #      larger message can overrun it.
    # 
    # Because major versions are not interchangeable, this definition takes
    # a new fixed port identifier (6211). A subject carries exactly one
    # major version; nodes migrate by switching subscriptions, and a bridge
    # node may publish both during a transition.
    @dataclass(slots=True)
    class VehicleState_2_0:
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
        # Height above the recorded takeoff elevation, in meters. Widened from
        # float32 to float64 in 2.0 so that the integrated altitude solution
        # stops accumulating rounding error over long survey flights.
        height_above_takeoff: WideScalar_1_0 = field(default_factory=WideScalar_1_0)
        # Bit mask replacing the standalone `armed` boolean of 1.x.
        #   bit 0 - propulsion armed
        #   bit 1 - navigation solution valid
        #   bit 2 - geofence breached
        #   bit 3 - low battery threshold crossed
        #   bits 4..7 - reserved, transmitted as zero
        status_flags: int = 0

    ```
