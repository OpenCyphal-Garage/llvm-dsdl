# lanyard.nav.GlobalPosition.1.0

Fused navigation solution published by the GNSS/INS estimator at 10

| | |
|---|---|
| Full name | `lanyard.nav.GlobalPosition` |
| Version | 1.0 |
| Kind | Message |
| Fixed port ID | 6220 |
| Transport tier | CAN FD |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Message | 96 | 43 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# Fused navigation solution published by the GNSS/INS estimator at 10
# Hz.
#
# TRANSPORT TIER: CAN FD.

uavcan.time.SynchronizedTimestamp.1.0 timestamp
# The network-synchronized moment this solution is valid for.

int32 latitude_deg_1e7
# WGS 84 latitude in units of 1e-7 degrees.

int32 longitude_deg_1e7
# WGS 84 longitude in units of 1e-7 degrees.

uavcan.si.unit.length.Scalar.1.0 altitude_amsl
# Altitude above mean sea level, in meters.

uavcan.si.unit.length.Scalar.1.0 altitude_ellipsoid
# Altitude above the WGS 84 reference ellipsoid, in meters. Both
# altitudes are carried because the geoid separation is not constant and
# consumers differ in which datum they expect.

float16[9] position_covariance
# Row-major 3x3 position covariance in the North-East-Down frame, in
# meters squared.
#
# A fixed-size array, unlike the variable-length arrays elsewhere in
# this namespace: the length is part of the type, so nothing is
# transmitted to describe it and generated code exposes it as a plain
# nine-element array rather than a length-carrying container. float16 is
# adequate here because a covariance is only ever consumed as an
# order-of-magnitude confidence estimate.

uint8 satellites_visible
# Number of satellites contributing to the solution.

uint4 fix_type
# Quality of the GNSS fix; one of the constants below.

uint4 FIX_NONE = 0
# No position solution.

uint4 FIX_2D = 1
# Horizontal position only; altitude is dead-reckoned.

uint4 FIX_3D = 2
# Full three-dimensional position solution.

uint4 FIX_DGPS = 3
# Three-dimensional solution with differential corrections applied.

uint4 FIX_RTK_FLOAT = 4
# Real-time kinematic solution with an unresolved carrier phase
# ambiguity, decimeter class.

uint4 FIX_RTK_FIXED = 5
# Real-time kinematic solution with a fixed carrier phase ambiguity,
# centimeter class.

void4
# Padding to a byte boundary.

@assert _offset_.max <= 63 * 8
# Must remain a single CAN FD frame; the covariance array is the field
# most likely to break this.

@print _offset_.max / 8
# Compile-time diagnostic: prints the worst-case serialized size of this
# definition, in bytes, while the namespace is being compiled. @print is
# evaluated by the frontend and contributes nothing to the generated
# code -- it is a build-log message, useful when tuning a layout against
# a frame budget.

@extent 96 * 8
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* Fused navigation solution published by the GNSS/INS estimator at 10 */
    /* Hz. */
    /*  */
    /* TRANSPORT TIER: CAN FD. */
    typedef struct lanyard__nav__GlobalPosition {
      /* The network-synchronized moment this solution is valid for. */
      uavcan__time__SynchronizedTimestamp timestamp;
      /* WGS 84 latitude in units of 1e-7 degrees. */
      int32_t latitude_deg_1e7;
      /* WGS 84 longitude in units of 1e-7 degrees. */
      int32_t longitude_deg_1e7;
      /* Altitude above mean sea level, in meters. */
      uavcan__si__unit__length__Scalar altitude_amsl;
      /* Altitude above the WGS 84 reference ellipsoid, in meters. Both */
      /* altitudes are carried because the geoid separation is not constant and */
      /* consumers differ in which datum they expect. */
      uavcan__si__unit__length__Scalar altitude_ellipsoid;
      /* Row-major 3x3 position covariance in the North-East-Down frame, in */
      /* meters squared. */
      /*  */
      /* A fixed-size array, unlike the variable-length arrays elsewhere in */
      /* this namespace: the length is part of the type, so nothing is */
      /* transmitted to describe it and generated code exposes it as a plain */
      /* nine-element array rather than a length-carrying container. float16 is */
      /* adequate here because a covariance is only ever consumed as an */
      /* order-of-magnitude confidence estimate. */
      float position_covariance[9U];
      /* Number of satellites contributing to the solution. */
      uint8_t satellites_visible;
      /* Quality of the GNSS fix; one of the constants below. */
      uint8_t fix_type;
    } lanyard__nav__GlobalPosition;

    ```

=== "C++ (std)"

    ```cpp
    // Fused navigation solution published by the GNSS/INS estimator at 10
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    struct GlobalPosition {
      // The network-synchronized moment this solution is valid for.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // WGS 84 latitude in units of 1e-7 degrees.
      std::int32_t latitude_deg_1e7{};
      // WGS 84 longitude in units of 1e-7 degrees.
      std::int32_t longitude_deg_1e7{};
      // Altitude above mean sea level, in meters.
      ::uavcan::si::unit::length::Scalar altitude_amsl{};
      // Altitude above the WGS 84 reference ellipsoid, in meters. Both
      // altitudes are carried because the geoid separation is not constant and
      // consumers differ in which datum they expect.
      ::uavcan::si::unit::length::Scalar altitude_ellipsoid{};
      // Row-major 3x3 position covariance in the North-East-Down frame, in
      // meters squared.
      // 
      // A fixed-size array, unlike the variable-length arrays elsewhere in
      // this namespace: the length is part of the type, so nothing is
      // transmitted to describe it and generated code exposes it as a plain
      // nine-element array rather than a length-carrying container. float16 is
      // adequate here because a covariance is only ever consumed as an
      // order-of-magnitude confidence estimate.
      std::array<float, 9U> position_covariance{};
      // Number of satellites contributing to the solution.
      std::uint8_t satellites_visible{};
      // Quality of the GNSS fix; one of the constants below.
      std::uint8_t fix_type{};
      static constexpr const char* FULL_NAME = "lanyard.nav.GlobalPosition";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.nav.GlobalPosition.1.0";
      static constexpr std::size_t EXTENT_BYTES = 96U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 43U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      // No position solution.
      static constexpr auto FIX_NONE = 0;
      // Horizontal position only; altitude is dead-reckoned.
      static constexpr auto FIX_2D = 1;
      // Full three-dimensional position solution.
      static constexpr auto FIX_3D = 2;
      // Three-dimensional solution with differential corrections applied.
      static constexpr auto FIX_DGPS = 3;
      // Real-time kinematic solution with an unresolved carrier phase
      // ambiguity, decimeter class.
      static constexpr auto FIX_RTK_FLOAT = 4;
      // Real-time kinematic solution with a fixed carrier phase ambiguity,
      // centimeter class.
      static constexpr auto FIX_RTK_FIXED = 5;
      static constexpr std::size_t POSITION_COVARIANCE_ARRAY_CAPACITY = 9U;
      static constexpr bool POSITION_COVARIANCE_ARRAY_IS_VARIABLE_LENGTH = false;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return GlobalPosition__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return GlobalPosition__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return GlobalPosition__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return GlobalPosition__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // Fused navigation solution published by the GNSS/INS estimator at 10
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    struct GlobalPosition {
      // The network-synchronized moment this solution is valid for.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // WGS 84 latitude in units of 1e-7 degrees.
      std::int32_t latitude_deg_1e7{};
      // WGS 84 longitude in units of 1e-7 degrees.
      std::int32_t longitude_deg_1e7{};
      // Altitude above mean sea level, in meters.
      ::uavcan::si::unit::length::Scalar altitude_amsl{};
      // Altitude above the WGS 84 reference ellipsoid, in meters. Both
      // altitudes are carried because the geoid separation is not constant and
      // consumers differ in which datum they expect.
      ::uavcan::si::unit::length::Scalar altitude_ellipsoid{};
      // Row-major 3x3 position covariance in the North-East-Down frame, in
      // meters squared.
      // 
      // A fixed-size array, unlike the variable-length arrays elsewhere in
      // this namespace: the length is part of the type, so nothing is
      // transmitted to describe it and generated code exposes it as a plain
      // nine-element array rather than a length-carrying container. float16 is
      // adequate here because a covariance is only ever consumed as an
      // order-of-magnitude confidence estimate.
      std::array<float, 9U> position_covariance{};
      // Number of satellites contributing to the solution.
      std::uint8_t satellites_visible{};
      // Quality of the GNSS fix; one of the constants below.
      std::uint8_t fix_type{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      GlobalPosition() = default;
      explicit GlobalPosition(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
        timestamp.set_memory_resource(_memory_resource);
        altitude_amsl.set_memory_resource(_memory_resource);
        altitude_ellipsoid.set_memory_resource(_memory_resource);
      }
      static constexpr const char* FULL_NAME = "lanyard.nav.GlobalPosition";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.nav.GlobalPosition.1.0";
      static constexpr std::size_t EXTENT_BYTES = 96U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 43U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      // No position solution.
      static constexpr auto FIX_NONE = 0;
      // Horizontal position only; altitude is dead-reckoned.
      static constexpr auto FIX_2D = 1;
      // Full three-dimensional position solution.
      static constexpr auto FIX_3D = 2;
      // Three-dimensional solution with differential corrections applied.
      static constexpr auto FIX_DGPS = 3;
      // Real-time kinematic solution with an unresolved carrier phase
      // ambiguity, decimeter class.
      static constexpr auto FIX_RTK_FLOAT = 4;
      // Real-time kinematic solution with a fixed carrier phase ambiguity,
      // centimeter class.
      static constexpr auto FIX_RTK_FIXED = 5;
      static constexpr std::size_t POSITION_COVARIANCE_ARRAY_CAPACITY = 9U;
      static constexpr bool POSITION_COVARIANCE_ARRAY_IS_VARIABLE_LENGTH = false;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return GlobalPosition__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return GlobalPosition__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return GlobalPosition__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return GlobalPosition__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return GlobalPosition__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return GlobalPosition__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // Fused navigation solution published by the GNSS/INS estimator at 10
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    struct GlobalPosition {
      // The network-synchronized moment this solution is valid for.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // WGS 84 latitude in units of 1e-7 degrees.
      std::int32_t latitude_deg_1e7{};
      // WGS 84 longitude in units of 1e-7 degrees.
      std::int32_t longitude_deg_1e7{};
      // Altitude above mean sea level, in meters.
      ::uavcan::si::unit::length::Scalar altitude_amsl{};
      // Altitude above the WGS 84 reference ellipsoid, in meters. Both
      // altitudes are carried because the geoid separation is not constant and
      // consumers differ in which datum they expect.
      ::uavcan::si::unit::length::Scalar altitude_ellipsoid{};
      // Row-major 3x3 position covariance in the North-East-Down frame, in
      // meters squared.
      // 
      // A fixed-size array, unlike the variable-length arrays elsewhere in
      // this namespace: the length is part of the type, so nothing is
      // transmitted to describe it and generated code exposes it as a plain
      // nine-element array rather than a length-carrying container. float16 is
      // adequate here because a covariance is only ever consumed as an
      // order-of-magnitude confidence estimate.
      std::array<float, 9U> position_covariance{};
      // Number of satellites contributing to the solution.
      std::uint8_t satellites_visible{};
      // Quality of the GNSS fix; one of the constants below.
      std::uint8_t fix_type{};
      static constexpr const char* FULL_NAME = "lanyard.nav.GlobalPosition";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.nav.GlobalPosition.1.0";
      static constexpr std::size_t EXTENT_BYTES = 96U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 43U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      // No position solution.
      static constexpr auto FIX_NONE = 0;
      // Horizontal position only; altitude is dead-reckoned.
      static constexpr auto FIX_2D = 1;
      // Full three-dimensional position solution.
      static constexpr auto FIX_3D = 2;
      // Three-dimensional solution with differential corrections applied.
      static constexpr auto FIX_DGPS = 3;
      // Real-time kinematic solution with an unresolved carrier phase
      // ambiguity, decimeter class.
      static constexpr auto FIX_RTK_FLOAT = 4;
      // Real-time kinematic solution with a fixed carrier phase ambiguity,
      // centimeter class.
      static constexpr auto FIX_RTK_FIXED = 5;
      static constexpr std::size_t POSITION_COVARIANCE_ARRAY_CAPACITY = 9U;
      static constexpr bool POSITION_COVARIANCE_ARRAY_IS_VARIABLE_LENGTH = false;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return GlobalPosition__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return GlobalPosition__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return GlobalPosition__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return GlobalPosition__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// Fused navigation solution published by the GNSS/INS estimator at 10
    /// Hz.
    /// 
    /// TRANSPORT TIER: CAN FD.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_nav_GlobalPosition_1_0 {
        /// The network-synchronized moment this solution is valid for.
        pub timestamp: uavcan_time_SynchronizedTimestamp_1_0,
        /// WGS 84 latitude in units of 1e-7 degrees.
        pub latitude_deg_1e7: i32,
        /// WGS 84 longitude in units of 1e-7 degrees.
        pub longitude_deg_1e7: i32,
        /// Altitude above mean sea level, in meters.
        pub altitude_amsl: uavcan_si_unit_length_Scalar_1_0,
        /// Altitude above the WGS 84 reference ellipsoid, in meters. Both
        /// altitudes are carried because the geoid separation is not constant and
        /// consumers differ in which datum they expect.
        pub altitude_ellipsoid: uavcan_si_unit_length_Scalar_1_0,
        /// Row-major 3x3 position covariance in the North-East-Down frame, in
        /// meters squared.
        /// 
        /// A fixed-size array, unlike the variable-length arrays elsewhere in
        /// this namespace: the length is part of the type, so nothing is
        /// transmitted to describe it and generated code exposes it as a plain
        /// nine-element array rather than a length-carrying container. float16 is
        /// adequate here because a covariance is only ever consumed as an
        /// order-of-magnitude confidence estimate.
        pub position_covariance: crate::dsdl_runtime::DsdlVec<f32>,
        /// Number of satellites contributing to the solution.
        pub satellites_visible: u8,
        /// Quality of the GNSS fix; one of the constants below.
        pub fix_type: u8,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// Fused navigation solution published by the GNSS/INS estimator at 10
    /// Hz.
    /// 
    /// TRANSPORT TIER: CAN FD.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_nav_GlobalPosition_1_0 {
        /// The network-synchronized moment this solution is valid for.
        pub timestamp: uavcan_time_SynchronizedTimestamp_1_0,
        /// WGS 84 latitude in units of 1e-7 degrees.
        pub latitude_deg_1e7: i32,
        /// WGS 84 longitude in units of 1e-7 degrees.
        pub longitude_deg_1e7: i32,
        /// Altitude above mean sea level, in meters.
        pub altitude_amsl: uavcan_si_unit_length_Scalar_1_0,
        /// Altitude above the WGS 84 reference ellipsoid, in meters. Both
        /// altitudes are carried because the geoid separation is not constant and
        /// consumers differ in which datum they expect.
        pub altitude_ellipsoid: uavcan_si_unit_length_Scalar_1_0,
        /// Row-major 3x3 position covariance in the North-East-Down frame, in
        /// meters squared.
        /// 
        /// A fixed-size array, unlike the variable-length arrays elsewhere in
        /// this namespace: the length is part of the type, so nothing is
        /// transmitted to describe it and generated code exposes it as a plain
        /// nine-element array rather than a length-carrying container. float16 is
        /// adequate here because a covariance is only ever consumed as an
        /// order-of-magnitude confidence estimate.
        pub position_covariance: crate::dsdl_runtime::DsdlVec<f32>,
        /// Number of satellites contributing to the solution.
        pub satellites_visible: u8,
        /// Quality of the GNSS fix; one of the constants below.
        pub fix_type: u8,
    }

    ```

=== "Go"

    ```go
    // Fused navigation solution published by the GNSS/INS estimator at 10
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    type GlobalPosition_1_0 struct {
      // The network-synchronized moment this solution is valid for.
      Timestamp pkg_uavcan_time.SynchronizedTimestamp_1_0
      // WGS 84 latitude in units of 1e-7 degrees.
      LatitudeDeg1e7 int32
      // WGS 84 longitude in units of 1e-7 degrees.
      LongitudeDeg1e7 int32
      // Altitude above mean sea level, in meters.
      AltitudeAmsl pkg_uavcan_si_unit_length.Scalar_1_0
      // Altitude above the WGS 84 reference ellipsoid, in meters. Both
      // altitudes are carried because the geoid separation is not constant and
      // consumers differ in which datum they expect.
      AltitudeEllipsoid pkg_uavcan_si_unit_length.Scalar_1_0
      // Row-major 3x3 position covariance in the North-East-Down frame, in
      // meters squared.
      // 
      // A fixed-size array, unlike the variable-length arrays elsewhere in
      // this namespace: the length is part of the type, so nothing is
      // transmitted to describe it and generated code exposes it as a plain
      // nine-element array rather than a length-carrying container. float16 is
      // adequate here because a covariance is only ever consumed as an
      // order-of-magnitude confidence estimate.
      PositionCovariance [9]float32
      // Number of satellites contributing to the solution.
      SatellitesVisible uint8
      // Quality of the GNSS fix; one of the constants below.
      FixType uint8
    }

    ```

=== "TypeScript"

    ```typescript
    // Fused navigation solution published by the GNSS/INS estimator at 10
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    export interface GlobalPosition_1_0 {
      // The network-synchronized moment this solution is valid for.
      timestamp: SynchronizedTimestamp_1_0;
      // WGS 84 latitude in units of 1e-7 degrees.
      latitude_deg_1e7: number;
      // WGS 84 longitude in units of 1e-7 degrees.
      longitude_deg_1e7: number;
      // Altitude above mean sea level, in meters.
      altitude_amsl: Scalar_1_0;
      // Altitude above the WGS 84 reference ellipsoid, in meters. Both
      // altitudes are carried because the geoid separation is not constant and
      // consumers differ in which datum they expect.
      altitude_ellipsoid: Scalar_1_0;
      // Row-major 3x3 position covariance in the North-East-Down frame, in
      // meters squared.
      // 
      // A fixed-size array, unlike the variable-length arrays elsewhere in
      // this namespace: the length is part of the type, so nothing is
      // transmitted to describe it and generated code exposes it as a plain
      // nine-element array rather than a length-carrying container. float16 is
      // adequate here because a covariance is only ever consumed as an
      // order-of-magnitude confidence estimate.
      position_covariance: Array<number>;
      // Number of satellites contributing to the solution.
      satellites_visible: number;
      // Quality of the GNSS fix; one of the constants below.
      fix_type: number;
    }

    ```

=== "Python"

    ```python
    # Fused navigation solution published by the GNSS/INS estimator at 10
    # Hz.
    # 
    # TRANSPORT TIER: CAN FD.
    @dataclass(slots=True)
    class GlobalPosition_1_0:
        # The network-synchronized moment this solution is valid for.
        timestamp: SynchronizedTimestamp_1_0 = field(default_factory=SynchronizedTimestamp_1_0)
        # WGS 84 latitude in units of 1e-7 degrees.
        latitude_deg_1e7: int = 0
        # WGS 84 longitude in units of 1e-7 degrees.
        longitude_deg_1e7: int = 0
        # Altitude above mean sea level, in meters.
        altitude_amsl: Scalar_1_0 = field(default_factory=Scalar_1_0)
        # Altitude above the WGS 84 reference ellipsoid, in meters. Both
        # altitudes are carried because the geoid separation is not constant and
        # consumers differ in which datum they expect.
        altitude_ellipsoid: Scalar_1_0 = field(default_factory=Scalar_1_0)
        # Row-major 3x3 position covariance in the North-East-Down frame, in
        # meters squared.
        # 
        # A fixed-size array, unlike the variable-length arrays elsewhere in
        # this namespace: the length is part of the type, so nothing is
        # transmitted to describe it and generated code exposes it as a plain
        # nine-element array rather than a length-carrying container. float16 is
        # adequate here because a covariance is only ever consumed as an
        # order-of-magnitude confidence estimate.
        position_covariance: list[float] = field(default_factory=list)
        # Number of satellites contributing to the solution.
        satellites_visible: int = 0
        # Quality of the GNSS fix; one of the constants below.
        fix_type: int = 0

    ```
