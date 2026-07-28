# lanyard.propulsion.ThrottleCommand.1.0

Group throttle setpoint for the propulsion array.

| | |
|---|---|
| Full name | `lanyard.propulsion.ThrottleCommand` |
| Version | 1.0 |
| Kind | Message |
| Fixed port ID | 6203 |
| Transport tier | CAN FD |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Message | 18 | 18 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# Group throttle setpoint for the propulsion array.
#
# VERSIONING: the stabilized successor to ThrottleCommand.0.1. Promoting
# a 0.x definition to 1.0 is not a "non-breaking change" -- it is the
# moment the definition acquires a compatibility promise for the first
# time. The field set was allowed to change on the way (a sequence
# counter and an emergency-stop flag were added) precisely because 0.1
# promised nothing.
#
# WHY SEALED: this message is published at the inner-loop rate on every
# armed flight. It is latency critical and never grows, so it pays
# neither the delimiter header nor the extent slack. When the field set
# does need to change, this definition will be superseded by 2.0 on a
# new port.
#
# TRANSPORT TIER: CAN FD.

uint4 MAX_MOTORS = 8
# Largest propulsion array this definition addresses. Exposed as a
# constant so that generated code carries the bound as a named value
# rather than a literal duplicated at every call site.

float16[<=8] ratio
# Normalized throttle command per motor, 0.0 (idle) to 1.0 (full),
# indexed by motor number.

uint7 sequence
# Free-running command counter, incremented once per published message
# and wrapping at 128. A controller that observes a gap knows it missed
# a command and may enter its own failsafe rather than continuing to
# actuate a stale setpoint.

bool emergency_stop
# When true, every controller shall cut drive current immediately and
# ignore the ratio array. This is a commanded stop, not a failsafe: it
# is asserted deliberately by the flight controller.

@assert _offset_.max <= 63 * 8
# The CAN FD single-frame budget: 63 payload bytes plus the Cyphal/CAN
# tail byte.

@sealed
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* Group throttle setpoint for the propulsion array. */
    /*  */
    /* VERSIONING: the stabilized successor to ThrottleCommand.0.1. Promoting */
    /* a 0.x definition to 1.0 is not a "non-breaking change" -- it is the */
    /* moment the definition acquires a compatibility promise for the first */
    /* time. The field set was allowed to change on the way (a sequence */
    /* counter and an emergency-stop flag were added) precisely because 0.1 */
    /* promised nothing. */
    /*  */
    /* WHY SEALED: this message is published at the inner-loop rate on every */
    /* armed flight. It is latency critical and never grows, so it pays */
    /* neither the delimiter header nor the extent slack. When the field set */
    /* does need to change, this definition will be superseded by 2.0 on a */
    /* new port. */
    /*  */
    /* TRANSPORT TIER: CAN FD. */
    typedef struct lanyard__propulsion__ThrottleCommand {
      /* Normalized throttle command per motor, 0.0 (idle) to 1.0 (full), */
      /* indexed by motor number. */
      struct {
        float elements[8U];
        size_t count;
      } ratio;
      /* Free-running command counter, incremented once per published message */
      /* and wrapping at 128. A controller that observes a gap knows it missed */
      /* a command and may enter its own failsafe rather than continuing to */
      /* actuate a stale setpoint. */
      uint8_t sequence;
      /* When true, every controller shall cut drive current immediately and */
      /* ignore the ratio array. This is a commanded stop, not a failsafe: it */
      /* is asserted deliberately by the flight controller. */
      bool emergency_stop;
    } lanyard__propulsion__ThrottleCommand;

    ```

=== "C++ (std)"

    ```cpp
    // Group throttle setpoint for the propulsion array.
    // 
    // VERSIONING: the stabilized successor to ThrottleCommand.0.1. Promoting
    // a 0.x definition to 1.0 is not a "non-breaking change" -- it is the
    // moment the definition acquires a compatibility promise for the first
    // time. The field set was allowed to change on the way (a sequence
    // counter and an emergency-stop flag were added) precisely because 0.1
    // promised nothing.
    // 
    // WHY SEALED: this message is published at the inner-loop rate on every
    // armed flight. It is latency critical and never grows, so it pays
    // neither the delimiter header nor the extent slack. When the field set
    // does need to change, this definition will be superseded by 2.0 on a
    // new port.
    // 
    // TRANSPORT TIER: CAN FD.
    struct ThrottleCommand_1_0 {
      // Normalized throttle command per motor, 0.0 (idle) to 1.0 (full),
      // indexed by motor number.
      std::vector<float> ratio{};
      // Free-running command counter, incremented once per published message
      // and wrapping at 128. A controller that observes a gap knows it missed
      // a command and may enter its own failsafe rather than continuing to
      // actuate a stale setpoint.
      std::uint8_t sequence{};
      // When true, every controller shall cut drive current immediately and
      // ignore the ratio array. This is a commanded stop, not a failsafe: it
      // is asserted deliberately by the flight controller.
      bool emergency_stop{};
      static constexpr const char* FULL_NAME = "lanyard.propulsion.ThrottleCommand";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.propulsion.ThrottleCommand.1.0";
      static constexpr std::size_t EXTENT_BYTES = 18U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 18U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "variable-array";
      // Largest propulsion array this definition addresses. Exposed as a
      // constant so that generated code carries the bound as a named value
      // rather than a literal duplicated at every call site.
      static constexpr auto MAX_MOTORS = 8;
      static constexpr std::size_t RATIO_ARRAY_CAPACITY = 8U;
      static constexpr bool RATIO_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return ThrottleCommand_1_0__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return ThrottleCommand_1_0__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return ThrottleCommand_1_0__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return ThrottleCommand_1_0__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // Group throttle setpoint for the propulsion array.
    // 
    // VERSIONING: the stabilized successor to ThrottleCommand.0.1. Promoting
    // a 0.x definition to 1.0 is not a "non-breaking change" -- it is the
    // moment the definition acquires a compatibility promise for the first
    // time. The field set was allowed to change on the way (a sequence
    // counter and an emergency-stop flag were added) precisely because 0.1
    // promised nothing.
    // 
    // WHY SEALED: this message is published at the inner-loop rate on every
    // armed flight. It is latency critical and never grows, so it pays
    // neither the delimiter header nor the extent slack. When the field set
    // does need to change, this definition will be superseded by 2.0 on a
    // new port.
    // 
    // TRANSPORT TIER: CAN FD.
    struct ThrottleCommand_1_0 {
      // Normalized throttle command per motor, 0.0 (idle) to 1.0 (full),
      // indexed by motor number.
      std::pmr::vector<float> ratio{};
      // Free-running command counter, incremented once per published message
      // and wrapping at 128. A controller that observes a gap knows it missed
      // a command and may enter its own failsafe rather than continuing to
      // actuate a stale setpoint.
      std::uint8_t sequence{};
      // When true, every controller shall cut drive current immediately and
      // ignore the ratio array. This is a commanded stop, not a failsafe: it
      // is asserted deliberately by the flight controller.
      bool emergency_stop{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      ThrottleCommand_1_0() = default;
      explicit ThrottleCommand_1_0(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
        ratio = decltype(ratio)(_memory_resource);
      }
      static constexpr const char* FULL_NAME = "lanyard.propulsion.ThrottleCommand";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.propulsion.ThrottleCommand.1.0";
      static constexpr std::size_t EXTENT_BYTES = 18U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 18U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "variable-array";
      // Largest propulsion array this definition addresses. Exposed as a
      // constant so that generated code carries the bound as a named value
      // rather than a literal duplicated at every call site.
      static constexpr auto MAX_MOTORS = 8;
      static constexpr std::size_t RATIO_ARRAY_CAPACITY = 8U;
      static constexpr bool RATIO_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return ThrottleCommand_1_0__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return ThrottleCommand_1_0__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return ThrottleCommand_1_0__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return ThrottleCommand_1_0__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return ThrottleCommand_1_0__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return ThrottleCommand_1_0__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // Group throttle setpoint for the propulsion array.
    // 
    // VERSIONING: the stabilized successor to ThrottleCommand.0.1. Promoting
    // a 0.x definition to 1.0 is not a "non-breaking change" -- it is the
    // moment the definition acquires a compatibility promise for the first
    // time. The field set was allowed to change on the way (a sequence
    // counter and an emergency-stop flag were added) precisely because 0.1
    // promised nothing.
    // 
    // WHY SEALED: this message is published at the inner-loop rate on every
    // armed flight. It is latency critical and never grows, so it pays
    // neither the delimiter header nor the extent slack. When the field set
    // does need to change, this definition will be superseded by 2.0 on a
    // new port.
    // 
    // TRANSPORT TIER: CAN FD.
    struct ThrottleCommand_1_0 {
      // Normalized throttle command per motor, 0.0 (idle) to 1.0 (full),
      // indexed by motor number.
      ::llvmdsdl::cpp::autosar::BoundedVector<float, 8U> ratio{};
      // Free-running command counter, incremented once per published message
      // and wrapping at 128. A controller that observes a gap knows it missed
      // a command and may enter its own failsafe rather than continuing to
      // actuate a stale setpoint.
      std::uint8_t sequence{};
      // When true, every controller shall cut drive current immediately and
      // ignore the ratio array. This is a commanded stop, not a failsafe: it
      // is asserted deliberately by the flight controller.
      bool emergency_stop{};
      static constexpr const char* FULL_NAME = "lanyard.propulsion.ThrottleCommand";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.propulsion.ThrottleCommand.1.0";
      static constexpr std::size_t EXTENT_BYTES = 18U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 18U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "variable-array";
      // Largest propulsion array this definition addresses. Exposed as a
      // constant so that generated code carries the bound as a named value
      // rather than a literal duplicated at every call site.
      static constexpr auto MAX_MOTORS = 8;
      static constexpr std::size_t RATIO_ARRAY_CAPACITY = 8U;
      static constexpr bool RATIO_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return ThrottleCommand_1_0__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return ThrottleCommand_1_0__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return ThrottleCommand_1_0__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return ThrottleCommand_1_0__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// Group throttle setpoint for the propulsion array.
    /// 
    /// VERSIONING: the stabilized successor to ThrottleCommand.0.1. Promoting
    /// a 0.x definition to 1.0 is not a "non-breaking change" -- it is the
    /// moment the definition acquires a compatibility promise for the first
    /// time. The field set was allowed to change on the way (a sequence
    /// counter and an emergency-stop flag were added) precisely because 0.1
    /// promised nothing.
    /// 
    /// WHY SEALED: this message is published at the inner-loop rate on every
    /// armed flight. It is latency critical and never grows, so it pays
    /// neither the delimiter header nor the extent slack. When the field set
    /// does need to change, this definition will be superseded by 2.0 on a
    /// new port.
    /// 
    /// TRANSPORT TIER: CAN FD.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_propulsion_ThrottleCommand_1_0 {
        /// Normalized throttle command per motor, 0.0 (idle) to 1.0 (full),
        /// indexed by motor number.
        pub ratio: crate::dsdl_runtime::DsdlVec<f32>,
        /// Free-running command counter, incremented once per published message
        /// and wrapping at 128. A controller that observes a gap knows it missed
        /// a command and may enter its own failsafe rather than continuing to
        /// actuate a stale setpoint.
        pub sequence: u8,
        /// When true, every controller shall cut drive current immediately and
        /// ignore the ratio array. This is a commanded stop, not a failsafe: it
        /// is asserted deliberately by the flight controller.
        pub emergency_stop: bool,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// Group throttle setpoint for the propulsion array.
    /// 
    /// VERSIONING: the stabilized successor to ThrottleCommand.0.1. Promoting
    /// a 0.x definition to 1.0 is not a "non-breaking change" -- it is the
    /// moment the definition acquires a compatibility promise for the first
    /// time. The field set was allowed to change on the way (a sequence
    /// counter and an emergency-stop flag were added) precisely because 0.1
    /// promised nothing.
    /// 
    /// WHY SEALED: this message is published at the inner-loop rate on every
    /// armed flight. It is latency critical and never grows, so it pays
    /// neither the delimiter header nor the extent slack. When the field set
    /// does need to change, this definition will be superseded by 2.0 on a
    /// new port.
    /// 
    /// TRANSPORT TIER: CAN FD.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_propulsion_ThrottleCommand_1_0 {
        /// Normalized throttle command per motor, 0.0 (idle) to 1.0 (full),
        /// indexed by motor number.
        pub ratio: crate::dsdl_runtime::DsdlVec<f32>,
        /// Free-running command counter, incremented once per published message
        /// and wrapping at 128. A controller that observes a gap knows it missed
        /// a command and may enter its own failsafe rather than continuing to
        /// actuate a stale setpoint.
        pub sequence: u8,
        /// When true, every controller shall cut drive current immediately and
        /// ignore the ratio array. This is a commanded stop, not a failsafe: it
        /// is asserted deliberately by the flight controller.
        pub emergency_stop: bool,
    }

    ```

=== "Go"

    ```go
    // Group throttle setpoint for the propulsion array.
    // 
    // VERSIONING: the stabilized successor to ThrottleCommand.0.1. Promoting
    // a 0.x definition to 1.0 is not a "non-breaking change" -- it is the
    // moment the definition acquires a compatibility promise for the first
    // time. The field set was allowed to change on the way (a sequence
    // counter and an emergency-stop flag were added) precisely because 0.1
    // promised nothing.
    // 
    // WHY SEALED: this message is published at the inner-loop rate on every
    // armed flight. It is latency critical and never grows, so it pays
    // neither the delimiter header nor the extent slack. When the field set
    // does need to change, this definition will be superseded by 2.0 on a
    // new port.
    // 
    // TRANSPORT TIER: CAN FD.
    type ThrottleCommand_1_0 struct {
      // Normalized throttle command per motor, 0.0 (idle) to 1.0 (full),
      // indexed by motor number.
      Ratio []float32
      // Free-running command counter, incremented once per published message
      // and wrapping at 128. A controller that observes a gap knows it missed
      // a command and may enter its own failsafe rather than continuing to
      // actuate a stale setpoint.
      Sequence uint8
      // When true, every controller shall cut drive current immediately and
      // ignore the ratio array. This is a commanded stop, not a failsafe: it
      // is asserted deliberately by the flight controller.
      EmergencyStop bool
    }

    ```

=== "TypeScript"

    ```typescript
    // Group throttle setpoint for the propulsion array.
    // 
    // VERSIONING: the stabilized successor to ThrottleCommand.0.1. Promoting
    // a 0.x definition to 1.0 is not a "non-breaking change" -- it is the
    // moment the definition acquires a compatibility promise for the first
    // time. The field set was allowed to change on the way (a sequence
    // counter and an emergency-stop flag were added) precisely because 0.1
    // promised nothing.
    // 
    // WHY SEALED: this message is published at the inner-loop rate on every
    // armed flight. It is latency critical and never grows, so it pays
    // neither the delimiter header nor the extent slack. When the field set
    // does need to change, this definition will be superseded by 2.0 on a
    // new port.
    // 
    // TRANSPORT TIER: CAN FD.
    export interface ThrottleCommand_1_0 {
      // Normalized throttle command per motor, 0.0 (idle) to 1.0 (full),
      // indexed by motor number.
      ratio: Array<number>;
      // Free-running command counter, incremented once per published message
      // and wrapping at 128. A controller that observes a gap knows it missed
      // a command and may enter its own failsafe rather than continuing to
      // actuate a stale setpoint.
      sequence: number;
      // When true, every controller shall cut drive current immediately and
      // ignore the ratio array. This is a commanded stop, not a failsafe: it
      // is asserted deliberately by the flight controller.
      emergency_stop: boolean;
    }

    ```

=== "Python"

    ```python
    # Group throttle setpoint for the propulsion array.
    # 
    # VERSIONING: the stabilized successor to ThrottleCommand.0.1. Promoting
    # a 0.x definition to 1.0 is not a "non-breaking change" -- it is the
    # moment the definition acquires a compatibility promise for the first
    # time. The field set was allowed to change on the way (a sequence
    # counter and an emergency-stop flag were added) precisely because 0.1
    # promised nothing.
    # 
    # WHY SEALED: this message is published at the inner-loop rate on every
    # armed flight. It is latency critical and never grows, so it pays
    # neither the delimiter header nor the extent slack. When the field set
    # does need to change, this definition will be superseded by 2.0 on a
    # new port.
    # 
    # TRANSPORT TIER: CAN FD.
    @dataclass(slots=True)
    class ThrottleCommand_1_0:
        # Normalized throttle command per motor, 0.0 (idle) to 1.0 (full),
        # indexed by motor number.
        ratio: list[float] = field(default_factory=list)
        # Free-running command counter, incremented once per published message
        # and wrapping at 128. A controller that observes a gap knows it missed
        # a command and may enter its own failsafe rather than continuing to
        # actuate a stale setpoint.
        sequence: int = 0
        # When true, every controller shall cut drive current immediately and
        # ignore the ratio array. This is a commanded stop, not a failsafe: it
        # is asserted deliberately by the flight controller.
        emergency_stop: bool = False

    ```
