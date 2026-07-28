# lanyard.propulsion.ThrottleCommand.0.1

Group throttle setpoint for the propulsion array -- PRE-RELEASE, DO

| | |
|---|---|
| Full name | `lanyard.propulsion.ThrottleCommand` |
| Version | 0.1 |
| Kind | Message |
| Fixed port ID | 6202 |
| Transport tier | CAN FD |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Message | 17 | 17 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# Group throttle setpoint for the propulsion array -- PRE-RELEASE, DO
# NOT DEPLOY.
#
# VERSIONING: this is a 0.x definition. Major version zero carries no
# compatibility guarantee whatsoever: 0.1 and 0.2 may differ
# arbitrarily, and neither is required to interoperate with the other or
# with any 1.x release. Zero-major types exist so that a design can be
# flown on a bench and iterated on without burning major version numbers
# or promising anything to downstream integrators.
#
# It is retained here alongside ThrottleCommand.1.0 to show the
# stabilization step: once the field set settled, the definition was
# promoted to 1.0 on a different port identifier, and this one was
# frozen. Production nodes subscribe to 1.0.
#
# TRANSPORT TIER: CAN FD.

float16[<=8] ratio
# Normalized throttle command per motor, in the range 0.0 (idle) to 1.0
# (full). The array length implicitly declares how many motors the
# flight controller believes are present; a controller whose index
# exceeds the array length holds its last commanded value until the
# arming timeout expires.
#
# A variable-length array serializes as an implicit length prefix
# followed by the elements, so a quadrotor pays for four elements and an
# octorotor for eight, out of the same definition.

@sealed
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* Group throttle setpoint for the propulsion array -- PRE-RELEASE, DO */
    /* NOT DEPLOY. */
    /*  */
    /* VERSIONING: this is a 0.x definition. Major version zero carries no */
    /* compatibility guarantee whatsoever: 0.1 and 0.2 may differ */
    /* arbitrarily, and neither is required to interoperate with the other or */
    /* with any 1.x release. Zero-major types exist so that a design can be */
    /* flown on a bench and iterated on without burning major version numbers */
    /* or promising anything to downstream integrators. */
    /*  */
    /* It is retained here alongside ThrottleCommand.1.0 to show the */
    /* stabilization step: once the field set settled, the definition was */
    /* promoted to 1.0 on a different port identifier, and this one was */
    /* frozen. Production nodes subscribe to 1.0. */
    /*  */
    /* TRANSPORT TIER: CAN FD. */
    typedef struct lanyard__propulsion__ThrottleCommand {
      /* Normalized throttle command per motor, in the range 0.0 (idle) to 1.0 */
      /* (full). The array length implicitly declares how many motors the */
      /* flight controller believes are present; a controller whose index */
      /* exceeds the array length holds its last commanded value until the */
      /* arming timeout expires. */
      /*  */
      /* A variable-length array serializes as an implicit length prefix */
      /* followed by the elements, so a quadrotor pays for four elements and an */
      /* octorotor for eight, out of the same definition. */
      struct {
        float elements[8U];
        size_t count;
      } ratio;
    } lanyard__propulsion__ThrottleCommand;

    ```

=== "C++ (std)"

    ```cpp
    // Group throttle setpoint for the propulsion array -- PRE-RELEASE, DO
    // NOT DEPLOY.
    // 
    // VERSIONING: this is a 0.x definition. Major version zero carries no
    // compatibility guarantee whatsoever: 0.1 and 0.2 may differ
    // arbitrarily, and neither is required to interoperate with the other or
    // with any 1.x release. Zero-major types exist so that a design can be
    // flown on a bench and iterated on without burning major version numbers
    // or promising anything to downstream integrators.
    // 
    // It is retained here alongside ThrottleCommand.1.0 to show the
    // stabilization step: once the field set settled, the definition was
    // promoted to 1.0 on a different port identifier, and this one was
    // frozen. Production nodes subscribe to 1.0.
    // 
    // TRANSPORT TIER: CAN FD.
    struct ThrottleCommand_0_1 {
      // Normalized throttle command per motor, in the range 0.0 (idle) to 1.0
      // (full). The array length implicitly declares how many motors the
      // flight controller believes are present; a controller whose index
      // exceeds the array length holds its last commanded value until the
      // arming timeout expires.
      // 
      // A variable-length array serializes as an implicit length prefix
      // followed by the elements, so a quadrotor pays for four elements and an
      // octorotor for eight, out of the same definition.
      std::vector<float> ratio{};
      static constexpr const char* FULL_NAME = "lanyard.propulsion.ThrottleCommand";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.propulsion.ThrottleCommand.0.1";
      static constexpr std::size_t EXTENT_BYTES = 17U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 17U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "variable-array";
      static constexpr std::size_t RATIO_ARRAY_CAPACITY = 8U;
      static constexpr bool RATIO_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return ThrottleCommand_0_1__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return ThrottleCommand_0_1__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return ThrottleCommand_0_1__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return ThrottleCommand_0_1__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // Group throttle setpoint for the propulsion array -- PRE-RELEASE, DO
    // NOT DEPLOY.
    // 
    // VERSIONING: this is a 0.x definition. Major version zero carries no
    // compatibility guarantee whatsoever: 0.1 and 0.2 may differ
    // arbitrarily, and neither is required to interoperate with the other or
    // with any 1.x release. Zero-major types exist so that a design can be
    // flown on a bench and iterated on without burning major version numbers
    // or promising anything to downstream integrators.
    // 
    // It is retained here alongside ThrottleCommand.1.0 to show the
    // stabilization step: once the field set settled, the definition was
    // promoted to 1.0 on a different port identifier, and this one was
    // frozen. Production nodes subscribe to 1.0.
    // 
    // TRANSPORT TIER: CAN FD.
    struct ThrottleCommand_0_1 {
      // Normalized throttle command per motor, in the range 0.0 (idle) to 1.0
      // (full). The array length implicitly declares how many motors the
      // flight controller believes are present; a controller whose index
      // exceeds the array length holds its last commanded value until the
      // arming timeout expires.
      // 
      // A variable-length array serializes as an implicit length prefix
      // followed by the elements, so a quadrotor pays for four elements and an
      // octorotor for eight, out of the same definition.
      std::pmr::vector<float> ratio{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      ThrottleCommand_0_1() = default;
      explicit ThrottleCommand_0_1(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
        ratio = decltype(ratio)(_memory_resource);
      }
      static constexpr const char* FULL_NAME = "lanyard.propulsion.ThrottleCommand";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.propulsion.ThrottleCommand.0.1";
      static constexpr std::size_t EXTENT_BYTES = 17U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 17U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "variable-array";
      static constexpr std::size_t RATIO_ARRAY_CAPACITY = 8U;
      static constexpr bool RATIO_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return ThrottleCommand_0_1__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return ThrottleCommand_0_1__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return ThrottleCommand_0_1__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return ThrottleCommand_0_1__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return ThrottleCommand_0_1__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return ThrottleCommand_0_1__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // Group throttle setpoint for the propulsion array -- PRE-RELEASE, DO
    // NOT DEPLOY.
    // 
    // VERSIONING: this is a 0.x definition. Major version zero carries no
    // compatibility guarantee whatsoever: 0.1 and 0.2 may differ
    // arbitrarily, and neither is required to interoperate with the other or
    // with any 1.x release. Zero-major types exist so that a design can be
    // flown on a bench and iterated on without burning major version numbers
    // or promising anything to downstream integrators.
    // 
    // It is retained here alongside ThrottleCommand.1.0 to show the
    // stabilization step: once the field set settled, the definition was
    // promoted to 1.0 on a different port identifier, and this one was
    // frozen. Production nodes subscribe to 1.0.
    // 
    // TRANSPORT TIER: CAN FD.
    struct ThrottleCommand_0_1 {
      // Normalized throttle command per motor, in the range 0.0 (idle) to 1.0
      // (full). The array length implicitly declares how many motors the
      // flight controller believes are present; a controller whose index
      // exceeds the array length holds its last commanded value until the
      // arming timeout expires.
      // 
      // A variable-length array serializes as an implicit length prefix
      // followed by the elements, so a quadrotor pays for four elements and an
      // octorotor for eight, out of the same definition.
      ::llvmdsdl::cpp::autosar::BoundedVector<float, 8U> ratio{};
      static constexpr const char* FULL_NAME = "lanyard.propulsion.ThrottleCommand";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.propulsion.ThrottleCommand.0.1";
      static constexpr std::size_t EXTENT_BYTES = 17U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 17U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "variable-array";
      static constexpr std::size_t RATIO_ARRAY_CAPACITY = 8U;
      static constexpr bool RATIO_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return ThrottleCommand_0_1__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return ThrottleCommand_0_1__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return ThrottleCommand_0_1__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return ThrottleCommand_0_1__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// Group throttle setpoint for the propulsion array -- PRE-RELEASE, DO
    /// NOT DEPLOY.
    /// 
    /// VERSIONING: this is a 0.x definition. Major version zero carries no
    /// compatibility guarantee whatsoever: 0.1 and 0.2 may differ
    /// arbitrarily, and neither is required to interoperate with the other or
    /// with any 1.x release. Zero-major types exist so that a design can be
    /// flown on a bench and iterated on without burning major version numbers
    /// or promising anything to downstream integrators.
    /// 
    /// It is retained here alongside ThrottleCommand.1.0 to show the
    /// stabilization step: once the field set settled, the definition was
    /// promoted to 1.0 on a different port identifier, and this one was
    /// frozen. Production nodes subscribe to 1.0.
    /// 
    /// TRANSPORT TIER: CAN FD.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_propulsion_ThrottleCommand_0_1 {
        /// Normalized throttle command per motor, in the range 0.0 (idle) to 1.0
        /// (full). The array length implicitly declares how many motors the
        /// flight controller believes are present; a controller whose index
        /// exceeds the array length holds its last commanded value until the
        /// arming timeout expires.
        /// 
        /// A variable-length array serializes as an implicit length prefix
        /// followed by the elements, so a quadrotor pays for four elements and an
        /// octorotor for eight, out of the same definition.
        pub ratio: crate::dsdl_runtime::DsdlVec<f32>,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// Group throttle setpoint for the propulsion array -- PRE-RELEASE, DO
    /// NOT DEPLOY.
    /// 
    /// VERSIONING: this is a 0.x definition. Major version zero carries no
    /// compatibility guarantee whatsoever: 0.1 and 0.2 may differ
    /// arbitrarily, and neither is required to interoperate with the other or
    /// with any 1.x release. Zero-major types exist so that a design can be
    /// flown on a bench and iterated on without burning major version numbers
    /// or promising anything to downstream integrators.
    /// 
    /// It is retained here alongside ThrottleCommand.1.0 to show the
    /// stabilization step: once the field set settled, the definition was
    /// promoted to 1.0 on a different port identifier, and this one was
    /// frozen. Production nodes subscribe to 1.0.
    /// 
    /// TRANSPORT TIER: CAN FD.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_propulsion_ThrottleCommand_0_1 {
        /// Normalized throttle command per motor, in the range 0.0 (idle) to 1.0
        /// (full). The array length implicitly declares how many motors the
        /// flight controller believes are present; a controller whose index
        /// exceeds the array length holds its last commanded value until the
        /// arming timeout expires.
        /// 
        /// A variable-length array serializes as an implicit length prefix
        /// followed by the elements, so a quadrotor pays for four elements and an
        /// octorotor for eight, out of the same definition.
        pub ratio: crate::dsdl_runtime::DsdlVec<f32>,
    }

    ```

=== "Go"

    ```go
    // Group throttle setpoint for the propulsion array -- PRE-RELEASE, DO
    // NOT DEPLOY.
    // 
    // VERSIONING: this is a 0.x definition. Major version zero carries no
    // compatibility guarantee whatsoever: 0.1 and 0.2 may differ
    // arbitrarily, and neither is required to interoperate with the other or
    // with any 1.x release. Zero-major types exist so that a design can be
    // flown on a bench and iterated on without burning major version numbers
    // or promising anything to downstream integrators.
    // 
    // It is retained here alongside ThrottleCommand.1.0 to show the
    // stabilization step: once the field set settled, the definition was
    // promoted to 1.0 on a different port identifier, and this one was
    // frozen. Production nodes subscribe to 1.0.
    // 
    // TRANSPORT TIER: CAN FD.
    type ThrottleCommand_0_1 struct {
      // Normalized throttle command per motor, in the range 0.0 (idle) to 1.0
      // (full). The array length implicitly declares how many motors the
      // flight controller believes are present; a controller whose index
      // exceeds the array length holds its last commanded value until the
      // arming timeout expires.
      // 
      // A variable-length array serializes as an implicit length prefix
      // followed by the elements, so a quadrotor pays for four elements and an
      // octorotor for eight, out of the same definition.
      Ratio []float32
    }

    ```

=== "TypeScript"

    ```typescript
    // Group throttle setpoint for the propulsion array -- PRE-RELEASE, DO
    // NOT DEPLOY.
    // 
    // VERSIONING: this is a 0.x definition. Major version zero carries no
    // compatibility guarantee whatsoever: 0.1 and 0.2 may differ
    // arbitrarily, and neither is required to interoperate with the other or
    // with any 1.x release. Zero-major types exist so that a design can be
    // flown on a bench and iterated on without burning major version numbers
    // or promising anything to downstream integrators.
    // 
    // It is retained here alongside ThrottleCommand.1.0 to show the
    // stabilization step: once the field set settled, the definition was
    // promoted to 1.0 on a different port identifier, and this one was
    // frozen. Production nodes subscribe to 1.0.
    // 
    // TRANSPORT TIER: CAN FD.
    export interface ThrottleCommand_0_1 {
      // Normalized throttle command per motor, in the range 0.0 (idle) to 1.0
      // (full). The array length implicitly declares how many motors the
      // flight controller believes are present; a controller whose index
      // exceeds the array length holds its last commanded value until the
      // arming timeout expires.
      // 
      // A variable-length array serializes as an implicit length prefix
      // followed by the elements, so a quadrotor pays for four elements and an
      // octorotor for eight, out of the same definition.
      ratio: Array<number>;
    }

    ```

=== "Python"

    ```python
    # Group throttle setpoint for the propulsion array -- PRE-RELEASE, DO
    # NOT DEPLOY.
    # 
    # VERSIONING: this is a 0.x definition. Major version zero carries no
    # compatibility guarantee whatsoever: 0.1 and 0.2 may differ
    # arbitrarily, and neither is required to interoperate with the other or
    # with any 1.x release. Zero-major types exist so that a design can be
    # flown on a bench and iterated on without burning major version numbers
    # or promising anything to downstream integrators.
    # 
    # It is retained here alongside ThrottleCommand.1.0 to show the
    # stabilization step: once the field set settled, the definition was
    # promoted to 1.0 on a different port identifier, and this one was
    # frozen. Production nodes subscribe to 1.0.
    # 
    # TRANSPORT TIER: CAN FD.
    @dataclass(slots=True)
    class ThrottleCommand_0_1:
        # Normalized throttle command per motor, in the range 0.0 (idle) to 1.0
        # (full). The array length implicitly declares how many motors the
        # flight controller believes are present; a controller whose index
        # exceeds the array length holds its last commanded value until the
        # arming timeout expires.
        # 
        # A variable-length array serializes as an implicit length prefix
        # followed by the elements, so a quadrotor pays for four elements and an
        # octorotor for eight, out of the same definition.
        ratio: list[float] = field(default_factory=list)

    ```
