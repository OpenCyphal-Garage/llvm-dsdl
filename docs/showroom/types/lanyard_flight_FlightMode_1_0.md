# lanyard.flight.FlightMode.1.0

The commanded flight mode of the vehicle.

| | |
|---|---|
| Full name | `lanyard.flight.FlightMode` |
| Version | 1.0 |
| Kind | Message |
| Fixed port ID | none (nested type) |
| Transport tier | unspecified |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Message | 1 | 1 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# The commanded flight mode of the vehicle.
#
# This is a nested type: it has no fixed port identifier and is never
# published on its own. It exists so that the mode enumeration is
# defined exactly once and embedded by value into every message that
# needs it, rather than being redeclared as a bare integer in each.
#
# The DSDL idiom for an enumeration is a value field followed by a set
# of constants of the same type. Constants are compile-time only -- they
# occupy no bits on the wire -- and every backend surfaces them as named
# values in the generated code, together with the documentation attached
# here.

uint8 value
# The active mode, one of the constants below. Values not listed are
# reserved and shall be treated by a receiver as equivalent to FAILSAFE.

uint8 MANUAL = 0
# Direct passthrough of pilot stick positions to the mixer. No
# stabilization.

uint8 STABILIZE = 1
# Attitude is stabilized; stick position commands lean angle. Altitude
# is not held.

uint8 ALTITUDE_HOLD = 2
# As STABILIZE, plus barometric altitude hold when the throttle stick is
# centered.

uint8 POSITION_HOLD = 3
# Full three-axis position hold using the navigation solution. Requires
# a valid GNSS fix.

uint8 AUTO_MISSION = 4
# The vehicle is flying a stored mission plan without pilot input.

uint8 RETURN_TO_LAUNCH = 5
# The vehicle is navigating autonomously back to its recorded launch
# position.

uint8 LAND = 6
# Controlled descent to touchdown at the current horizontal position.

uint8 FAILSAFE = 7
# The flight controller has lost a resource it requires (link, GNSS, or
# power margin) and is running its configured contingency behavior.

@sealed
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* The commanded flight mode of the vehicle. */
    /*  */
    /* This is a nested type: it has no fixed port identifier and is never */
    /* published on its own. It exists so that the mode enumeration is */
    /* defined exactly once and embedded by value into every message that */
    /* needs it, rather than being redeclared as a bare integer in each. */
    /*  */
    /* The DSDL idiom for an enumeration is a value field followed by a set */
    /* of constants of the same type. Constants are compile-time only -- they */
    /* occupy no bits on the wire -- and every backend surfaces them as named */
    /* values in the generated code, together with the documentation attached */
    /* here. */
    typedef struct lanyard__flight__FlightMode {
      /* The active mode, one of the constants below. Values not listed are */
      /* reserved and shall be treated by a receiver as equivalent to FAILSAFE. */
      uint8_t value;
    } lanyard__flight__FlightMode;

    ```

=== "C++ (std)"

    ```cpp
    // The commanded flight mode of the vehicle.
    // 
    // This is a nested type: it has no fixed port identifier and is never
    // published on its own. It exists so that the mode enumeration is
    // defined exactly once and embedded by value into every message that
    // needs it, rather than being redeclared as a bare integer in each.
    // 
    // The DSDL idiom for an enumeration is a value field followed by a set
    // of constants of the same type. Constants are compile-time only -- they
    // occupy no bits on the wire -- and every backend surfaces them as named
    // values in the generated code, together with the documentation attached
    // here.
    struct FlightMode {
      // The active mode, one of the constants below. Values not listed are
      // reserved and shall be treated by a receiver as equivalent to FAILSAFE.
      std::uint8_t value{};
      static constexpr const char* FULL_NAME = "lanyard.flight.FlightMode";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.flight.FlightMode.1.0";
      static constexpr std::size_t EXTENT_BYTES = 1U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 1U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = true;
      static constexpr const char* ZOH_ALIAS_REASON = "eligible";
      // Direct passthrough of pilot stick positions to the mixer. No
      // stabilization.
      static constexpr auto MANUAL = 0;
      // Attitude is stabilized; stick position commands lean angle. Altitude
      // is not held.
      static constexpr auto STABILIZE = 1;
      // As STABILIZE, plus barometric altitude hold when the throttle stick is
      // centered.
      static constexpr auto ALTITUDE_HOLD = 2;
      // Full three-axis position hold using the navigation solution. Requires
      // a valid GNSS fix.
      static constexpr auto POSITION_HOLD = 3;
      // The vehicle is flying a stored mission plan without pilot input.
      static constexpr auto AUTO_MISSION = 4;
      // The vehicle is navigating autonomously back to its recorded launch
      // position.
      static constexpr auto RETURN_TO_LAUNCH = 5;
      // Controlled descent to touchdown at the current horizontal position.
      static constexpr auto LAND = 6;
      // The flight controller has lost a resource it requires (link, GNSS, or
      // power margin) and is running its configured contingency behavior.
      static constexpr auto FAILSAFE = 7;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return FlightMode__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return FlightMode__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return FlightMode__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return FlightMode__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // The commanded flight mode of the vehicle.
    // 
    // This is a nested type: it has no fixed port identifier and is never
    // published on its own. It exists so that the mode enumeration is
    // defined exactly once and embedded by value into every message that
    // needs it, rather than being redeclared as a bare integer in each.
    // 
    // The DSDL idiom for an enumeration is a value field followed by a set
    // of constants of the same type. Constants are compile-time only -- they
    // occupy no bits on the wire -- and every backend surfaces them as named
    // values in the generated code, together with the documentation attached
    // here.
    struct FlightMode {
      // The active mode, one of the constants below. Values not listed are
      // reserved and shall be treated by a receiver as equivalent to FAILSAFE.
      std::uint8_t value{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      FlightMode() = default;
      explicit FlightMode(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
      }
      static constexpr const char* FULL_NAME = "lanyard.flight.FlightMode";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.flight.FlightMode.1.0";
      static constexpr std::size_t EXTENT_BYTES = 1U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 1U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = true;
      static constexpr const char* ZOH_ALIAS_REASON = "eligible";
      // Direct passthrough of pilot stick positions to the mixer. No
      // stabilization.
      static constexpr auto MANUAL = 0;
      // Attitude is stabilized; stick position commands lean angle. Altitude
      // is not held.
      static constexpr auto STABILIZE = 1;
      // As STABILIZE, plus barometric altitude hold when the throttle stick is
      // centered.
      static constexpr auto ALTITUDE_HOLD = 2;
      // Full three-axis position hold using the navigation solution. Requires
      // a valid GNSS fix.
      static constexpr auto POSITION_HOLD = 3;
      // The vehicle is flying a stored mission plan without pilot input.
      static constexpr auto AUTO_MISSION = 4;
      // The vehicle is navigating autonomously back to its recorded launch
      // position.
      static constexpr auto RETURN_TO_LAUNCH = 5;
      // Controlled descent to touchdown at the current horizontal position.
      static constexpr auto LAND = 6;
      // The flight controller has lost a resource it requires (link, GNSS, or
      // power margin) and is running its configured contingency behavior.
      static constexpr auto FAILSAFE = 7;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return FlightMode__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return FlightMode__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return FlightMode__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return FlightMode__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return FlightMode__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return FlightMode__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // The commanded flight mode of the vehicle.
    // 
    // This is a nested type: it has no fixed port identifier and is never
    // published on its own. It exists so that the mode enumeration is
    // defined exactly once and embedded by value into every message that
    // needs it, rather than being redeclared as a bare integer in each.
    // 
    // The DSDL idiom for an enumeration is a value field followed by a set
    // of constants of the same type. Constants are compile-time only -- they
    // occupy no bits on the wire -- and every backend surfaces them as named
    // values in the generated code, together with the documentation attached
    // here.
    struct FlightMode {
      // The active mode, one of the constants below. Values not listed are
      // reserved and shall be treated by a receiver as equivalent to FAILSAFE.
      std::uint8_t value{};
      static constexpr const char* FULL_NAME = "lanyard.flight.FlightMode";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.flight.FlightMode.1.0";
      static constexpr std::size_t EXTENT_BYTES = 1U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 1U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = true;
      static constexpr const char* ZOH_ALIAS_REASON = "eligible";
      // Direct passthrough of pilot stick positions to the mixer. No
      // stabilization.
      static constexpr auto MANUAL = 0;
      // Attitude is stabilized; stick position commands lean angle. Altitude
      // is not held.
      static constexpr auto STABILIZE = 1;
      // As STABILIZE, plus barometric altitude hold when the throttle stick is
      // centered.
      static constexpr auto ALTITUDE_HOLD = 2;
      // Full three-axis position hold using the navigation solution. Requires
      // a valid GNSS fix.
      static constexpr auto POSITION_HOLD = 3;
      // The vehicle is flying a stored mission plan without pilot input.
      static constexpr auto AUTO_MISSION = 4;
      // The vehicle is navigating autonomously back to its recorded launch
      // position.
      static constexpr auto RETURN_TO_LAUNCH = 5;
      // Controlled descent to touchdown at the current horizontal position.
      static constexpr auto LAND = 6;
      // The flight controller has lost a resource it requires (link, GNSS, or
      // power margin) and is running its configured contingency behavior.
      static constexpr auto FAILSAFE = 7;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return FlightMode__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return FlightMode__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return FlightMode__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return FlightMode__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// The commanded flight mode of the vehicle.
    /// 
    /// This is a nested type: it has no fixed port identifier and is never
    /// published on its own. It exists so that the mode enumeration is
    /// defined exactly once and embedded by value into every message that
    /// needs it, rather than being redeclared as a bare integer in each.
    /// 
    /// The DSDL idiom for an enumeration is a value field followed by a set
    /// of constants of the same type. Constants are compile-time only -- they
    /// occupy no bits on the wire -- and every backend surfaces them as named
    /// values in the generated code, together with the documentation attached
    /// here.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_flight_FlightMode_1_0 {
        /// The active mode, one of the constants below. Values not listed are
        /// reserved and shall be treated by a receiver as equivalent to FAILSAFE.
        pub value: u8,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// The commanded flight mode of the vehicle.
    /// 
    /// This is a nested type: it has no fixed port identifier and is never
    /// published on its own. It exists so that the mode enumeration is
    /// defined exactly once and embedded by value into every message that
    /// needs it, rather than being redeclared as a bare integer in each.
    /// 
    /// The DSDL idiom for an enumeration is a value field followed by a set
    /// of constants of the same type. Constants are compile-time only -- they
    /// occupy no bits on the wire -- and every backend surfaces them as named
    /// values in the generated code, together with the documentation attached
    /// here.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_flight_FlightMode_1_0 {
        /// The active mode, one of the constants below. Values not listed are
        /// reserved and shall be treated by a receiver as equivalent to FAILSAFE.
        pub value: u8,
    }

    ```

=== "Go"

    ```go
    // The commanded flight mode of the vehicle.
    // 
    // This is a nested type: it has no fixed port identifier and is never
    // published on its own. It exists so that the mode enumeration is
    // defined exactly once and embedded by value into every message that
    // needs it, rather than being redeclared as a bare integer in each.
    // 
    // The DSDL idiom for an enumeration is a value field followed by a set
    // of constants of the same type. Constants are compile-time only -- they
    // occupy no bits on the wire -- and every backend surfaces them as named
    // values in the generated code, together with the documentation attached
    // here.
    type FlightMode_1_0 struct {
      // The active mode, one of the constants below. Values not listed are
      // reserved and shall be treated by a receiver as equivalent to FAILSAFE.
      Value uint8
    }

    ```

=== "TypeScript"

    ```typescript
    // The commanded flight mode of the vehicle.
    // 
    // This is a nested type: it has no fixed port identifier and is never
    // published on its own. It exists so that the mode enumeration is
    // defined exactly once and embedded by value into every message that
    // needs it, rather than being redeclared as a bare integer in each.
    // 
    // The DSDL idiom for an enumeration is a value field followed by a set
    // of constants of the same type. Constants are compile-time only -- they
    // occupy no bits on the wire -- and every backend surfaces them as named
    // values in the generated code, together with the documentation attached
    // here.
    export interface FlightMode_1_0 {
      // The active mode, one of the constants below. Values not listed are
      // reserved and shall be treated by a receiver as equivalent to FAILSAFE.
      value: number;
    }

    ```

=== "Python"

    ```python
    # The commanded flight mode of the vehicle.
    # 
    # This is a nested type: it has no fixed port identifier and is never
    # published on its own. It exists so that the mode enumeration is
    # defined exactly once and embedded by value into every message that
    # needs it, rather than being redeclared as a bare integer in each.
    # 
    # The DSDL idiom for an enumeration is a value field followed by a set
    # of constants of the same type. Constants are compile-time only -- they
    # occupy no bits on the wire -- and every backend surfaces them as named
    # values in the generated code, together with the documentation attached
    # here.
    @dataclass(slots=True)
    class FlightMode_1_0:
        # The active mode, one of the constants below. Values not listed are
        # reserved and shall be treated by a receiver as equivalent to FAILSAFE.
        value: int = 0

    ```
