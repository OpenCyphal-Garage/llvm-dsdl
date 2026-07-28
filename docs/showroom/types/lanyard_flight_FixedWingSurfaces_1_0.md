# lanyard.flight.FixedWingSurfaces.1.0

Post-mixer actuator commands for a fixed-wing airframe.

| | |
|---|---|
| Full name | `lanyard.flight.FixedWingSurfaces` |
| Version | 1.0 |
| Kind | Message |
| Fixed port ID | none (nested type) |
| Transport tier | unspecified |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Message | 10 | 10 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# Post-mixer actuator commands for a fixed-wing airframe.
#
# Nested type with no fixed port identifier; published as one option of
# ControlSurfaces.1.0.

float16 aileron_left
# Left aileron deflection, -1.0 (full down) to +1.0 (full up).

float16 aileron_right
# Right aileron deflection, -1.0 (full down) to +1.0 (full up). Carried
# separately from the left surface rather than as a single roll command
# so that flaperon and differential-aileron mixes can be expressed
# without a second message.

float16 elevator
# Elevator deflection, -1.0 (full down) to +1.0 (full up).

float16 rudder
# Rudder deflection, -1.0 (full left) to +1.0 (full right).

float16 throttle
# Normalized thrust command, 0.0 (idle) to 1.0 (full).

@sealed
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* Post-mixer actuator commands for a fixed-wing airframe. */
    /*  */
    /* Nested type with no fixed port identifier; published as one option of */
    /* ControlSurfaces.1.0. */
    typedef struct lanyard__flight__FixedWingSurfaces {
      /* Left aileron deflection, -1.0 (full down) to +1.0 (full up). */
      float aileron_left;
      /* Right aileron deflection, -1.0 (full down) to +1.0 (full up). Carried */
      /* separately from the left surface rather than as a single roll command */
      /* so that flaperon and differential-aileron mixes can be expressed */
      /* without a second message. */
      float aileron_right;
      /* Elevator deflection, -1.0 (full down) to +1.0 (full up). */
      float elevator;
      /* Rudder deflection, -1.0 (full left) to +1.0 (full right). */
      float rudder;
      /* Normalized thrust command, 0.0 (idle) to 1.0 (full). */
      float throttle;
    } lanyard__flight__FixedWingSurfaces;

    ```

=== "C++ (std)"

    ```cpp
    // Post-mixer actuator commands for a fixed-wing airframe.
    // 
    // Nested type with no fixed port identifier; published as one option of
    // ControlSurfaces.1.0.
    struct FixedWingSurfaces {
      // Left aileron deflection, -1.0 (full down) to +1.0 (full up).
      float aileron_left{};
      // Right aileron deflection, -1.0 (full down) to +1.0 (full up). Carried
      // separately from the left surface rather than as a single roll command
      // so that flaperon and differential-aileron mixes can be expressed
      // without a second message.
      float aileron_right{};
      // Elevator deflection, -1.0 (full down) to +1.0 (full up).
      float elevator{};
      // Rudder deflection, -1.0 (full left) to +1.0 (full right).
      float rudder{};
      // Normalized thrust command, 0.0 (idle) to 1.0 (full).
      float throttle{};
      static constexpr const char* FULL_NAME = "lanyard.flight.FixedWingSurfaces";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.flight.FixedWingSurfaces.1.0";
      static constexpr std::size_t EXTENT_BYTES = 10U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 10U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = true;
      static constexpr const char* ZOH_ALIAS_REASON = "eligible";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return FixedWingSurfaces__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return FixedWingSurfaces__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return FixedWingSurfaces__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return FixedWingSurfaces__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // Post-mixer actuator commands for a fixed-wing airframe.
    // 
    // Nested type with no fixed port identifier; published as one option of
    // ControlSurfaces.1.0.
    struct FixedWingSurfaces {
      // Left aileron deflection, -1.0 (full down) to +1.0 (full up).
      float aileron_left{};
      // Right aileron deflection, -1.0 (full down) to +1.0 (full up). Carried
      // separately from the left surface rather than as a single roll command
      // so that flaperon and differential-aileron mixes can be expressed
      // without a second message.
      float aileron_right{};
      // Elevator deflection, -1.0 (full down) to +1.0 (full up).
      float elevator{};
      // Rudder deflection, -1.0 (full left) to +1.0 (full right).
      float rudder{};
      // Normalized thrust command, 0.0 (idle) to 1.0 (full).
      float throttle{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      FixedWingSurfaces() = default;
      explicit FixedWingSurfaces(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
      }
      static constexpr const char* FULL_NAME = "lanyard.flight.FixedWingSurfaces";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.flight.FixedWingSurfaces.1.0";
      static constexpr std::size_t EXTENT_BYTES = 10U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 10U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = true;
      static constexpr const char* ZOH_ALIAS_REASON = "eligible";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return FixedWingSurfaces__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return FixedWingSurfaces__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return FixedWingSurfaces__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return FixedWingSurfaces__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return FixedWingSurfaces__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return FixedWingSurfaces__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // Post-mixer actuator commands for a fixed-wing airframe.
    // 
    // Nested type with no fixed port identifier; published as one option of
    // ControlSurfaces.1.0.
    struct FixedWingSurfaces {
      // Left aileron deflection, -1.0 (full down) to +1.0 (full up).
      float aileron_left{};
      // Right aileron deflection, -1.0 (full down) to +1.0 (full up). Carried
      // separately from the left surface rather than as a single roll command
      // so that flaperon and differential-aileron mixes can be expressed
      // without a second message.
      float aileron_right{};
      // Elevator deflection, -1.0 (full down) to +1.0 (full up).
      float elevator{};
      // Rudder deflection, -1.0 (full left) to +1.0 (full right).
      float rudder{};
      // Normalized thrust command, 0.0 (idle) to 1.0 (full).
      float throttle{};
      static constexpr const char* FULL_NAME = "lanyard.flight.FixedWingSurfaces";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.flight.FixedWingSurfaces.1.0";
      static constexpr std::size_t EXTENT_BYTES = 10U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 10U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = true;
      static constexpr const char* ZOH_ALIAS_REASON = "eligible";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return FixedWingSurfaces__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return FixedWingSurfaces__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return FixedWingSurfaces__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return FixedWingSurfaces__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// Post-mixer actuator commands for a fixed-wing airframe.
    /// 
    /// Nested type with no fixed port identifier; published as one option of
    /// ControlSurfaces.1.0.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_flight_FixedWingSurfaces_1_0 {
        /// Left aileron deflection, -1.0 (full down) to +1.0 (full up).
        pub aileron_left: f32,
        /// Right aileron deflection, -1.0 (full down) to +1.0 (full up). Carried
        /// separately from the left surface rather than as a single roll command
        /// so that flaperon and differential-aileron mixes can be expressed
        /// without a second message.
        pub aileron_right: f32,
        /// Elevator deflection, -1.0 (full down) to +1.0 (full up).
        pub elevator: f32,
        /// Rudder deflection, -1.0 (full left) to +1.0 (full right).
        pub rudder: f32,
        /// Normalized thrust command, 0.0 (idle) to 1.0 (full).
        pub throttle: f32,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// Post-mixer actuator commands for a fixed-wing airframe.
    /// 
    /// Nested type with no fixed port identifier; published as one option of
    /// ControlSurfaces.1.0.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_flight_FixedWingSurfaces_1_0 {
        /// Left aileron deflection, -1.0 (full down) to +1.0 (full up).
        pub aileron_left: f32,
        /// Right aileron deflection, -1.0 (full down) to +1.0 (full up). Carried
        /// separately from the left surface rather than as a single roll command
        /// so that flaperon and differential-aileron mixes can be expressed
        /// without a second message.
        pub aileron_right: f32,
        /// Elevator deflection, -1.0 (full down) to +1.0 (full up).
        pub elevator: f32,
        /// Rudder deflection, -1.0 (full left) to +1.0 (full right).
        pub rudder: f32,
        /// Normalized thrust command, 0.0 (idle) to 1.0 (full).
        pub throttle: f32,
    }

    ```

=== "Go"

    ```go
    // Post-mixer actuator commands for a fixed-wing airframe.
    // 
    // Nested type with no fixed port identifier; published as one option of
    // ControlSurfaces.1.0.
    type FixedWingSurfaces_1_0 struct {
      // Left aileron deflection, -1.0 (full down) to +1.0 (full up).
      AileronLeft float32
      // Right aileron deflection, -1.0 (full down) to +1.0 (full up). Carried
      // separately from the left surface rather than as a single roll command
      // so that flaperon and differential-aileron mixes can be expressed
      // without a second message.
      AileronRight float32
      // Elevator deflection, -1.0 (full down) to +1.0 (full up).
      Elevator float32
      // Rudder deflection, -1.0 (full left) to +1.0 (full right).
      Rudder float32
      // Normalized thrust command, 0.0 (idle) to 1.0 (full).
      Throttle float32
    }

    ```

=== "TypeScript"

    ```typescript
    // Post-mixer actuator commands for a fixed-wing airframe.
    // 
    // Nested type with no fixed port identifier; published as one option of
    // ControlSurfaces.1.0.
    export interface FixedWingSurfaces_1_0 {
      // Left aileron deflection, -1.0 (full down) to +1.0 (full up).
      aileron_left: number;
      // Right aileron deflection, -1.0 (full down) to +1.0 (full up). Carried
      // separately from the left surface rather than as a single roll command
      // so that flaperon and differential-aileron mixes can be expressed
      // without a second message.
      aileron_right: number;
      // Elevator deflection, -1.0 (full down) to +1.0 (full up).
      elevator: number;
      // Rudder deflection, -1.0 (full left) to +1.0 (full right).
      rudder: number;
      // Normalized thrust command, 0.0 (idle) to 1.0 (full).
      throttle: number;
    }

    ```

=== "Python"

    ```python
    # Post-mixer actuator commands for a fixed-wing airframe.
    # 
    # Nested type with no fixed port identifier; published as one option of
    # ControlSurfaces.1.0.
    @dataclass(slots=True)
    class FixedWingSurfaces_1_0:
        # Left aileron deflection, -1.0 (full down) to +1.0 (full up).
        aileron_left: float = 0.0
        # Right aileron deflection, -1.0 (full down) to +1.0 (full up). Carried
        # separately from the left surface rather than as a single roll command
        # so that flaperon and differential-aileron mixes can be expressed
        # without a second message.
        aileron_right: float = 0.0
        # Elevator deflection, -1.0 (full down) to +1.0 (full up).
        elevator: float = 0.0
        # Rudder deflection, -1.0 (full left) to +1.0 (full right).
        rudder: float = 0.0
        # Normalized thrust command, 0.0 (idle) to 1.0 (full).
        throttle: float = 0.0

    ```
