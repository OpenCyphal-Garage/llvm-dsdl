# lanyard.nav.Waypoint.1.0

A single mission item.

| | |
|---|---|
| Full name | `lanyard.nav.Waypoint` |
| Version | 1.0 |
| Kind | Message |
| Fixed port ID | none (nested type) |
| Transport tier | unspecified |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Message | 64 | 36 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# A single mission item.
#
# Nested type with no fixed port identifier: waypoints are only ever
# transported inside MissionPlan or UploadMission. It is delimited
# rather than sealed so that a future minor version may add mission item
# parameters without forcing a major bump of every container that embeds
# it.
#
# A delimited type nested inside another type carries its own 32-bit
# length header, which is what lets the outer type skip over an element
# it cannot fully parse. That is four bytes per waypoint -- a real cost
# at 256 waypoints, and the reason this trade is worth stating
# explicitly rather than defaulting either way.

int32 latitude_deg_1e7
# WGS 84 latitude in units of 1e-7 degrees. This fixed-point encoding is
# the standard trick for geodetic coordinates: it holds about 11 mm of
# resolution over the whole globe in 32 bits, whereas float32 degrees
# would degrade to roughly a meter near the poles and float64 would cost
# twice the bandwidth for precision no airframe can fly to.

int32 longitude_deg_1e7
# WGS 84 longitude in units of 1e-7 degrees.

float32 altitude_amsl_m
# Target altitude above mean sea level, in meters.

float16 speed_mps
# Ground speed to hold on the leg approaching this waypoint, in meters
# per second. A negative value means "unchanged from the previous leg".

float16 acceptance_radius_m
# Distance from the waypoint at which it is considered reached, in
# meters.

uint16 loiter_time_s
# Seconds to hold position after reaching the waypoint before
# proceeding. Zero means fly through.

uint8 command
# The action to perform at this waypoint; one of the constants below.

uint8 COMMAND_NAVIGATE = 0
# Fly to the waypoint and continue.

uint8 COMMAND_LOITER = 1
# Fly to the waypoint and orbit for loiter_time_s.

uint8 COMMAND_LAND = 2
# Descend and land at the waypoint.

uint8 COMMAND_TAKEOFF = 3
# Climb to altitude_amsl_m before proceeding to the next item.

uint8 COMMAND_TRIGGER_PAYLOAD = 4
# Fire the payload action configured for this mission at the waypoint.

uint8[<=16] label
# Short human-readable label shown in the ground station, UTF-8, not
# null-terminated. Bounded at 16 bytes because the array is replicated
# up to 256 times inside a mission plan.

@extent 64 * 8
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* A single mission item. */
    /*  */
    /* Nested type with no fixed port identifier: waypoints are only ever */
    /* transported inside MissionPlan or UploadMission. It is delimited */
    /* rather than sealed so that a future minor version may add mission item */
    /* parameters without forcing a major bump of every container that embeds */
    /* it. */
    /*  */
    /* A delimited type nested inside another type carries its own 32-bit */
    /* length header, which is what lets the outer type skip over an element */
    /* it cannot fully parse. That is four bytes per waypoint -- a real cost */
    /* at 256 waypoints, and the reason this trade is worth stating */
    /* explicitly rather than defaulting either way. */
    typedef struct lanyard__nav__Waypoint {
      /* WGS 84 latitude in units of 1e-7 degrees. This fixed-point encoding is */
      /* the standard trick for geodetic coordinates: it holds about 11 mm of */
      /* resolution over the whole globe in 32 bits, whereas float32 degrees */
      /* would degrade to roughly a meter near the poles and float64 would cost */
      /* twice the bandwidth for precision no airframe can fly to. */
      int32_t latitude_deg_1e7;
      /* WGS 84 longitude in units of 1e-7 degrees. */
      int32_t longitude_deg_1e7;
      /* Target altitude above mean sea level, in meters. */
      float altitude_amsl_m;
      /* Ground speed to hold on the leg approaching this waypoint, in meters */
      /* per second. A negative value means "unchanged from the previous leg". */
      float speed_mps;
      /* Distance from the waypoint at which it is considered reached, in */
      /* meters. */
      float acceptance_radius_m;
      /* Seconds to hold position after reaching the waypoint before */
      /* proceeding. Zero means fly through. */
      uint16_t loiter_time_s;
      /* The action to perform at this waypoint; one of the constants below. */
      uint8_t command;
      /* Short human-readable label shown in the ground station, UTF-8, not */
      /* null-terminated. Bounded at 16 bytes because the array is replicated */
      /* up to 256 times inside a mission plan. */
      struct {
        uint8_t elements[16U];
        size_t count;
      } label;
    } lanyard__nav__Waypoint;

    ```

=== "C++ (std)"

    ```cpp
    // A single mission item.
    // 
    // Nested type with no fixed port identifier: waypoints are only ever
    // transported inside MissionPlan or UploadMission. It is delimited
    // rather than sealed so that a future minor version may add mission item
    // parameters without forcing a major bump of every container that embeds
    // it.
    // 
    // A delimited type nested inside another type carries its own 32-bit
    // length header, which is what lets the outer type skip over an element
    // it cannot fully parse. That is four bytes per waypoint -- a real cost
    // at 256 waypoints, and the reason this trade is worth stating
    // explicitly rather than defaulting either way.
    struct Waypoint {
      // WGS 84 latitude in units of 1e-7 degrees. This fixed-point encoding is
      // the standard trick for geodetic coordinates: it holds about 11 mm of
      // resolution over the whole globe in 32 bits, whereas float32 degrees
      // would degrade to roughly a meter near the poles and float64 would cost
      // twice the bandwidth for precision no airframe can fly to.
      std::int32_t latitude_deg_1e7{};
      // WGS 84 longitude in units of 1e-7 degrees.
      std::int32_t longitude_deg_1e7{};
      // Target altitude above mean sea level, in meters.
      float altitude_amsl_m{};
      // Ground speed to hold on the leg approaching this waypoint, in meters
      // per second. A negative value means "unchanged from the previous leg".
      float speed_mps{};
      // Distance from the waypoint at which it is considered reached, in
      // meters.
      float acceptance_radius_m{};
      // Seconds to hold position after reaching the waypoint before
      // proceeding. Zero means fly through.
      std::uint16_t loiter_time_s{};
      // The action to perform at this waypoint; one of the constants below.
      std::uint8_t command{};
      // Short human-readable label shown in the ground station, UTF-8, not
      // null-terminated. Bounded at 16 bytes because the array is replicated
      // up to 256 times inside a mission plan.
      std::vector<std::uint8_t> label{};
      static constexpr const char* FULL_NAME = "lanyard.nav.Waypoint";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.nav.Waypoint.1.0";
      static constexpr std::size_t EXTENT_BYTES = 64U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 36U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "variable-array";
      // Fly to the waypoint and continue.
      static constexpr auto COMMAND_NAVIGATE = 0;
      // Fly to the waypoint and orbit for loiter_time_s.
      static constexpr auto COMMAND_LOITER = 1;
      // Descend and land at the waypoint.
      static constexpr auto COMMAND_LAND = 2;
      // Climb to altitude_amsl_m before proceeding to the next item.
      static constexpr auto COMMAND_TAKEOFF = 3;
      // Fire the payload action configured for this mission at the waypoint.
      static constexpr auto COMMAND_TRIGGER_PAYLOAD = 4;
      static constexpr std::size_t LABEL_ARRAY_CAPACITY = 16U;
      static constexpr bool LABEL_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return Waypoint__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return Waypoint__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return Waypoint__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return Waypoint__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // A single mission item.
    // 
    // Nested type with no fixed port identifier: waypoints are only ever
    // transported inside MissionPlan or UploadMission. It is delimited
    // rather than sealed so that a future minor version may add mission item
    // parameters without forcing a major bump of every container that embeds
    // it.
    // 
    // A delimited type nested inside another type carries its own 32-bit
    // length header, which is what lets the outer type skip over an element
    // it cannot fully parse. That is four bytes per waypoint -- a real cost
    // at 256 waypoints, and the reason this trade is worth stating
    // explicitly rather than defaulting either way.
    struct Waypoint {
      // WGS 84 latitude in units of 1e-7 degrees. This fixed-point encoding is
      // the standard trick for geodetic coordinates: it holds about 11 mm of
      // resolution over the whole globe in 32 bits, whereas float32 degrees
      // would degrade to roughly a meter near the poles and float64 would cost
      // twice the bandwidth for precision no airframe can fly to.
      std::int32_t latitude_deg_1e7{};
      // WGS 84 longitude in units of 1e-7 degrees.
      std::int32_t longitude_deg_1e7{};
      // Target altitude above mean sea level, in meters.
      float altitude_amsl_m{};
      // Ground speed to hold on the leg approaching this waypoint, in meters
      // per second. A negative value means "unchanged from the previous leg".
      float speed_mps{};
      // Distance from the waypoint at which it is considered reached, in
      // meters.
      float acceptance_radius_m{};
      // Seconds to hold position after reaching the waypoint before
      // proceeding. Zero means fly through.
      std::uint16_t loiter_time_s{};
      // The action to perform at this waypoint; one of the constants below.
      std::uint8_t command{};
      // Short human-readable label shown in the ground station, UTF-8, not
      // null-terminated. Bounded at 16 bytes because the array is replicated
      // up to 256 times inside a mission plan.
      std::pmr::vector<std::uint8_t> label{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      Waypoint() = default;
      explicit Waypoint(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
        label = decltype(label)(_memory_resource);
      }
      static constexpr const char* FULL_NAME = "lanyard.nav.Waypoint";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.nav.Waypoint.1.0";
      static constexpr std::size_t EXTENT_BYTES = 64U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 36U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "variable-array";
      // Fly to the waypoint and continue.
      static constexpr auto COMMAND_NAVIGATE = 0;
      // Fly to the waypoint and orbit for loiter_time_s.
      static constexpr auto COMMAND_LOITER = 1;
      // Descend and land at the waypoint.
      static constexpr auto COMMAND_LAND = 2;
      // Climb to altitude_amsl_m before proceeding to the next item.
      static constexpr auto COMMAND_TAKEOFF = 3;
      // Fire the payload action configured for this mission at the waypoint.
      static constexpr auto COMMAND_TRIGGER_PAYLOAD = 4;
      static constexpr std::size_t LABEL_ARRAY_CAPACITY = 16U;
      static constexpr bool LABEL_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return Waypoint__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return Waypoint__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return Waypoint__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return Waypoint__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return Waypoint__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return Waypoint__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // A single mission item.
    // 
    // Nested type with no fixed port identifier: waypoints are only ever
    // transported inside MissionPlan or UploadMission. It is delimited
    // rather than sealed so that a future minor version may add mission item
    // parameters without forcing a major bump of every container that embeds
    // it.
    // 
    // A delimited type nested inside another type carries its own 32-bit
    // length header, which is what lets the outer type skip over an element
    // it cannot fully parse. That is four bytes per waypoint -- a real cost
    // at 256 waypoints, and the reason this trade is worth stating
    // explicitly rather than defaulting either way.
    struct Waypoint {
      // WGS 84 latitude in units of 1e-7 degrees. This fixed-point encoding is
      // the standard trick for geodetic coordinates: it holds about 11 mm of
      // resolution over the whole globe in 32 bits, whereas float32 degrees
      // would degrade to roughly a meter near the poles and float64 would cost
      // twice the bandwidth for precision no airframe can fly to.
      std::int32_t latitude_deg_1e7{};
      // WGS 84 longitude in units of 1e-7 degrees.
      std::int32_t longitude_deg_1e7{};
      // Target altitude above mean sea level, in meters.
      float altitude_amsl_m{};
      // Ground speed to hold on the leg approaching this waypoint, in meters
      // per second. A negative value means "unchanged from the previous leg".
      float speed_mps{};
      // Distance from the waypoint at which it is considered reached, in
      // meters.
      float acceptance_radius_m{};
      // Seconds to hold position after reaching the waypoint before
      // proceeding. Zero means fly through.
      std::uint16_t loiter_time_s{};
      // The action to perform at this waypoint; one of the constants below.
      std::uint8_t command{};
      // Short human-readable label shown in the ground station, UTF-8, not
      // null-terminated. Bounded at 16 bytes because the array is replicated
      // up to 256 times inside a mission plan.
      ::llvmdsdl::cpp::autosar::BoundedVector<std::uint8_t, 16U> label{};
      static constexpr const char* FULL_NAME = "lanyard.nav.Waypoint";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.nav.Waypoint.1.0";
      static constexpr std::size_t EXTENT_BYTES = 64U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 36U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "variable-array";
      // Fly to the waypoint and continue.
      static constexpr auto COMMAND_NAVIGATE = 0;
      // Fly to the waypoint and orbit for loiter_time_s.
      static constexpr auto COMMAND_LOITER = 1;
      // Descend and land at the waypoint.
      static constexpr auto COMMAND_LAND = 2;
      // Climb to altitude_amsl_m before proceeding to the next item.
      static constexpr auto COMMAND_TAKEOFF = 3;
      // Fire the payload action configured for this mission at the waypoint.
      static constexpr auto COMMAND_TRIGGER_PAYLOAD = 4;
      static constexpr std::size_t LABEL_ARRAY_CAPACITY = 16U;
      static constexpr bool LABEL_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return Waypoint__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return Waypoint__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return Waypoint__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return Waypoint__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// A single mission item.
    /// 
    /// Nested type with no fixed port identifier: waypoints are only ever
    /// transported inside MissionPlan or UploadMission. It is delimited
    /// rather than sealed so that a future minor version may add mission item
    /// parameters without forcing a major bump of every container that embeds
    /// it.
    /// 
    /// A delimited type nested inside another type carries its own 32-bit
    /// length header, which is what lets the outer type skip over an element
    /// it cannot fully parse. That is four bytes per waypoint -- a real cost
    /// at 256 waypoints, and the reason this trade is worth stating
    /// explicitly rather than defaulting either way.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_nav_Waypoint_1_0 {
        /// WGS 84 latitude in units of 1e-7 degrees. This fixed-point encoding is
        /// the standard trick for geodetic coordinates: it holds about 11 mm of
        /// resolution over the whole globe in 32 bits, whereas float32 degrees
        /// would degrade to roughly a meter near the poles and float64 would cost
        /// twice the bandwidth for precision no airframe can fly to.
        pub latitude_deg_1e7: i32,
        /// WGS 84 longitude in units of 1e-7 degrees.
        pub longitude_deg_1e7: i32,
        /// Target altitude above mean sea level, in meters.
        pub altitude_amsl_m: f32,
        /// Ground speed to hold on the leg approaching this waypoint, in meters
        /// per second. A negative value means "unchanged from the previous leg".
        pub speed_mps: f32,
        /// Distance from the waypoint at which it is considered reached, in
        /// meters.
        pub acceptance_radius_m: f32,
        /// Seconds to hold position after reaching the waypoint before
        /// proceeding. Zero means fly through.
        pub loiter_time_s: u16,
        /// The action to perform at this waypoint; one of the constants below.
        pub command: u8,
        /// Short human-readable label shown in the ground station, UTF-8, not
        /// null-terminated. Bounded at 16 bytes because the array is replicated
        /// up to 256 times inside a mission plan.
        pub label: crate::dsdl_runtime::DsdlVec<u8>,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// A single mission item.
    /// 
    /// Nested type with no fixed port identifier: waypoints are only ever
    /// transported inside MissionPlan or UploadMission. It is delimited
    /// rather than sealed so that a future minor version may add mission item
    /// parameters without forcing a major bump of every container that embeds
    /// it.
    /// 
    /// A delimited type nested inside another type carries its own 32-bit
    /// length header, which is what lets the outer type skip over an element
    /// it cannot fully parse. That is four bytes per waypoint -- a real cost
    /// at 256 waypoints, and the reason this trade is worth stating
    /// explicitly rather than defaulting either way.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_nav_Waypoint_1_0 {
        /// WGS 84 latitude in units of 1e-7 degrees. This fixed-point encoding is
        /// the standard trick for geodetic coordinates: it holds about 11 mm of
        /// resolution over the whole globe in 32 bits, whereas float32 degrees
        /// would degrade to roughly a meter near the poles and float64 would cost
        /// twice the bandwidth for precision no airframe can fly to.
        pub latitude_deg_1e7: i32,
        /// WGS 84 longitude in units of 1e-7 degrees.
        pub longitude_deg_1e7: i32,
        /// Target altitude above mean sea level, in meters.
        pub altitude_amsl_m: f32,
        /// Ground speed to hold on the leg approaching this waypoint, in meters
        /// per second. A negative value means "unchanged from the previous leg".
        pub speed_mps: f32,
        /// Distance from the waypoint at which it is considered reached, in
        /// meters.
        pub acceptance_radius_m: f32,
        /// Seconds to hold position after reaching the waypoint before
        /// proceeding. Zero means fly through.
        pub loiter_time_s: u16,
        /// The action to perform at this waypoint; one of the constants below.
        pub command: u8,
        /// Short human-readable label shown in the ground station, UTF-8, not
        /// null-terminated. Bounded at 16 bytes because the array is replicated
        /// up to 256 times inside a mission plan.
        pub label: crate::dsdl_runtime::DsdlVec<u8>,
    }

    ```

=== "Go"

    ```go
    // A single mission item.
    // 
    // Nested type with no fixed port identifier: waypoints are only ever
    // transported inside MissionPlan or UploadMission. It is delimited
    // rather than sealed so that a future minor version may add mission item
    // parameters without forcing a major bump of every container that embeds
    // it.
    // 
    // A delimited type nested inside another type carries its own 32-bit
    // length header, which is what lets the outer type skip over an element
    // it cannot fully parse. That is four bytes per waypoint -- a real cost
    // at 256 waypoints, and the reason this trade is worth stating
    // explicitly rather than defaulting either way.
    type Waypoint_1_0 struct {
      // WGS 84 latitude in units of 1e-7 degrees. This fixed-point encoding is
      // the standard trick for geodetic coordinates: it holds about 11 mm of
      // resolution over the whole globe in 32 bits, whereas float32 degrees
      // would degrade to roughly a meter near the poles and float64 would cost
      // twice the bandwidth for precision no airframe can fly to.
      LatitudeDeg1e7 int32
      // WGS 84 longitude in units of 1e-7 degrees.
      LongitudeDeg1e7 int32
      // Target altitude above mean sea level, in meters.
      AltitudeAmslM float32
      // Ground speed to hold on the leg approaching this waypoint, in meters
      // per second. A negative value means "unchanged from the previous leg".
      SpeedMps float32
      // Distance from the waypoint at which it is considered reached, in
      // meters.
      AcceptanceRadiusM float32
      // Seconds to hold position after reaching the waypoint before
      // proceeding. Zero means fly through.
      LoiterTimeS uint16
      // The action to perform at this waypoint; one of the constants below.
      Command uint8
      // Short human-readable label shown in the ground station, UTF-8, not
      // null-terminated. Bounded at 16 bytes because the array is replicated
      // up to 256 times inside a mission plan.
      Label []uint8
    }

    ```

=== "TypeScript"

    ```typescript
    // A single mission item.
    // 
    // Nested type with no fixed port identifier: waypoints are only ever
    // transported inside MissionPlan or UploadMission. It is delimited
    // rather than sealed so that a future minor version may add mission item
    // parameters without forcing a major bump of every container that embeds
    // it.
    // 
    // A delimited type nested inside another type carries its own 32-bit
    // length header, which is what lets the outer type skip over an element
    // it cannot fully parse. That is four bytes per waypoint -- a real cost
    // at 256 waypoints, and the reason this trade is worth stating
    // explicitly rather than defaulting either way.
    export interface Waypoint_1_0 {
      // WGS 84 latitude in units of 1e-7 degrees. This fixed-point encoding is
      // the standard trick for geodetic coordinates: it holds about 11 mm of
      // resolution over the whole globe in 32 bits, whereas float32 degrees
      // would degrade to roughly a meter near the poles and float64 would cost
      // twice the bandwidth for precision no airframe can fly to.
      latitude_deg_1e7: number;
      // WGS 84 longitude in units of 1e-7 degrees.
      longitude_deg_1e7: number;
      // Target altitude above mean sea level, in meters.
      altitude_amsl_m: number;
      // Ground speed to hold on the leg approaching this waypoint, in meters
      // per second. A negative value means "unchanged from the previous leg".
      speed_mps: number;
      // Distance from the waypoint at which it is considered reached, in
      // meters.
      acceptance_radius_m: number;
      // Seconds to hold position after reaching the waypoint before
      // proceeding. Zero means fly through.
      loiter_time_s: number;
      // The action to perform at this waypoint; one of the constants below.
      command: number;
      // Short human-readable label shown in the ground station, UTF-8, not
      // null-terminated. Bounded at 16 bytes because the array is replicated
      // up to 256 times inside a mission plan.
      label: Array<number>;
    }

    ```

=== "Python"

    ```python
    # A single mission item.
    # 
    # Nested type with no fixed port identifier: waypoints are only ever
    # transported inside MissionPlan or UploadMission. It is delimited
    # rather than sealed so that a future minor version may add mission item
    # parameters without forcing a major bump of every container that embeds
    # it.
    # 
    # A delimited type nested inside another type carries its own 32-bit
    # length header, which is what lets the outer type skip over an element
    # it cannot fully parse. That is four bytes per waypoint -- a real cost
    # at 256 waypoints, and the reason this trade is worth stating
    # explicitly rather than defaulting either way.
    @dataclass(slots=True)
    class Waypoint_1_0:
        # WGS 84 latitude in units of 1e-7 degrees. This fixed-point encoding is
        # the standard trick for geodetic coordinates: it holds about 11 mm of
        # resolution over the whole globe in 32 bits, whereas float32 degrees
        # would degrade to roughly a meter near the poles and float64 would cost
        # twice the bandwidth for precision no airframe can fly to.
        latitude_deg_1e7: int = 0
        # WGS 84 longitude in units of 1e-7 degrees.
        longitude_deg_1e7: int = 0
        # Target altitude above mean sea level, in meters.
        altitude_amsl_m: float = 0.0
        # Ground speed to hold on the leg approaching this waypoint, in meters
        # per second. A negative value means "unchanged from the previous leg".
        speed_mps: float = 0.0
        # Distance from the waypoint at which it is considered reached, in
        # meters.
        acceptance_radius_m: float = 0.0
        # Seconds to hold position after reaching the waypoint before
        # proceeding. Zero means fly through.
        loiter_time_s: int = 0
        # The action to perform at this waypoint; one of the constants below.
        command: int = 0
        # Short human-readable label shown in the ground station, UTF-8, not
        # null-terminated. Bounded at 16 bytes because the array is replicated
        # up to 256 times inside a mission plan.
        label: list[int] = field(default_factory=list)

    ```
