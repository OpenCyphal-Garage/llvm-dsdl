# lanyard.health.SystemHealth.1.0

Vehicle-level health summary, published at 1 Hz by the flight

| | |
|---|---|
| Full name | `lanyard.health.SystemHealth` |
| Version | 1.0 |
| Kind | Message |
| Fixed port ID | 6250 |
| Transport tier | CAN FD |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Message | 512 | 250 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# Vehicle-level health summary, published at 1 Hz by the flight
# controller.
#
# TRANSPORT TIER: CAN FD.
#
# WHY DELIMITED: the subsystem list grows as the airframe gains
# hardware, and the ground station is the slowest thing in the fleet to
# be updated. An extent lets the ground station keep decoding this
# message across several vehicle software releases.

uavcan.time.SynchronizedTimestamp.1.0 timestamp
# Moment the summary was assembled.

uavcan.node.Health.1.0 aggregate_health
# Worst health level among all subsystems below. Precomputed so that a
# consumer that only needs the top-level state -- an annunciator light,
# say -- does not have to walk the array.

void6
# Padding to a byte boundary.

lanyard.health.SubsystemReport.1.0[<=8] subsystem
# One report per monitored subsystem. Sealed elements, so unlike the
# waypoint array in MissionPlan these carry no per-element delimiter
# header; the trade is that SubsystemReport can never gain a field
# without a major bump of its own.

@extent 512 * 8
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* Vehicle-level health summary, published at 1 Hz by the flight */
    /* controller. */
    /*  */
    /* TRANSPORT TIER: CAN FD. */
    /*  */
    /* WHY DELIMITED: the subsystem list grows as the airframe gains */
    /* hardware, and the ground station is the slowest thing in the fleet to */
    /* be updated. An extent lets the ground station keep decoding this */
    /* message across several vehicle software releases. */
    typedef struct lanyard__health__SystemHealth {
      /* Moment the summary was assembled. */
      uavcan__time__SynchronizedTimestamp timestamp;
      /* Worst health level among all subsystems below. Precomputed so that a */
      /* consumer that only needs the top-level state -- an annunciator light, */
      /* say -- does not have to walk the array. */
      uavcan__node__Health aggregate_health;
      /* One report per monitored subsystem. Sealed elements, so unlike the */
      /* waypoint array in MissionPlan these carry no per-element delimiter */
      /* header; the trade is that SubsystemReport can never gain a field */
      /* without a major bump of its own. */
      struct {
        lanyard__health__SubsystemReport elements[8U];
        size_t count;
      } subsystem;
    } lanyard__health__SystemHealth;

    ```

=== "C++ (std)"

    ```cpp
    // Vehicle-level health summary, published at 1 Hz by the flight
    // controller.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY DELIMITED: the subsystem list grows as the airframe gains
    // hardware, and the ground station is the slowest thing in the fleet to
    // be updated. An extent lets the ground station keep decoding this
    // message across several vehicle software releases.
    struct SystemHealth {
      // Moment the summary was assembled.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // Worst health level among all subsystems below. Precomputed so that a
      // consumer that only needs the top-level state -- an annunciator light,
      // say -- does not have to walk the array.
      ::uavcan::node::Health aggregate_health{};
      // One report per monitored subsystem. Sealed elements, so unlike the
      // waypoint array in MissionPlan these carry no per-element delimiter
      // header; the trade is that SubsystemReport can never gain a field
      // without a major bump of its own.
      std::vector<::lanyard::health::SubsystemReport> subsystem{};
      static constexpr const char* FULL_NAME = "lanyard.health.SystemHealth";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.health.SystemHealth.1.0";
      static constexpr std::size_t EXTENT_BYTES = 512U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 250U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      static constexpr std::size_t SUBSYSTEM_ARRAY_CAPACITY = 8U;
      static constexpr bool SUBSYSTEM_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return SystemHealth__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return SystemHealth__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return SystemHealth__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return SystemHealth__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // Vehicle-level health summary, published at 1 Hz by the flight
    // controller.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY DELIMITED: the subsystem list grows as the airframe gains
    // hardware, and the ground station is the slowest thing in the fleet to
    // be updated. An extent lets the ground station keep decoding this
    // message across several vehicle software releases.
    struct SystemHealth {
      // Moment the summary was assembled.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // Worst health level among all subsystems below. Precomputed so that a
      // consumer that only needs the top-level state -- an annunciator light,
      // say -- does not have to walk the array.
      ::uavcan::node::Health aggregate_health{};
      // One report per monitored subsystem. Sealed elements, so unlike the
      // waypoint array in MissionPlan these carry no per-element delimiter
      // header; the trade is that SubsystemReport can never gain a field
      // without a major bump of its own.
      std::pmr::vector<::lanyard::health::SubsystemReport> subsystem{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      SystemHealth() = default;
      explicit SystemHealth(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
        subsystem = decltype(subsystem)(_memory_resource);
        timestamp.set_memory_resource(_memory_resource);
        aggregate_health.set_memory_resource(_memory_resource);
        for (std::size_t subsystem_index = 0U; subsystem_index < subsystem.size(); ++subsystem_index) {
          subsystem[subsystem_index].set_memory_resource(_memory_resource);
        }
      }
      static constexpr const char* FULL_NAME = "lanyard.health.SystemHealth";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.health.SystemHealth.1.0";
      static constexpr std::size_t EXTENT_BYTES = 512U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 250U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      static constexpr std::size_t SUBSYSTEM_ARRAY_CAPACITY = 8U;
      static constexpr bool SUBSYSTEM_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return SystemHealth__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return SystemHealth__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return SystemHealth__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return SystemHealth__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return SystemHealth__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return SystemHealth__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // Vehicle-level health summary, published at 1 Hz by the flight
    // controller.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY DELIMITED: the subsystem list grows as the airframe gains
    // hardware, and the ground station is the slowest thing in the fleet to
    // be updated. An extent lets the ground station keep decoding this
    // message across several vehicle software releases.
    struct SystemHealth {
      // Moment the summary was assembled.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // Worst health level among all subsystems below. Precomputed so that a
      // consumer that only needs the top-level state -- an annunciator light,
      // say -- does not have to walk the array.
      ::uavcan::node::Health aggregate_health{};
      // One report per monitored subsystem. Sealed elements, so unlike the
      // waypoint array in MissionPlan these carry no per-element delimiter
      // header; the trade is that SubsystemReport can never gain a field
      // without a major bump of its own.
      ::llvmdsdl::cpp::autosar::BoundedVector<::lanyard::health::SubsystemReport, 8U> subsystem{};
      static constexpr const char* FULL_NAME = "lanyard.health.SystemHealth";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.health.SystemHealth.1.0";
      static constexpr std::size_t EXTENT_BYTES = 512U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 250U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      static constexpr std::size_t SUBSYSTEM_ARRAY_CAPACITY = 8U;
      static constexpr bool SUBSYSTEM_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return SystemHealth__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return SystemHealth__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return SystemHealth__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return SystemHealth__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// Vehicle-level health summary, published at 1 Hz by the flight
    /// controller.
    /// 
    /// TRANSPORT TIER: CAN FD.
    /// 
    /// WHY DELIMITED: the subsystem list grows as the airframe gains
    /// hardware, and the ground station is the slowest thing in the fleet to
    /// be updated. An extent lets the ground station keep decoding this
    /// message across several vehicle software releases.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_health_SystemHealth_1_0 {
        /// Moment the summary was assembled.
        pub timestamp: uavcan_time_SynchronizedTimestamp_1_0,
        /// Worst health level among all subsystems below. Precomputed so that a
        /// consumer that only needs the top-level state -- an annunciator light,
        /// say -- does not have to walk the array.
        pub aggregate_health: uavcan_node_Health_1_0,
        /// One report per monitored subsystem. Sealed elements, so unlike the
        /// waypoint array in MissionPlan these carry no per-element delimiter
        /// header; the trade is that SubsystemReport can never gain a field
        /// without a major bump of its own.
        pub subsystem: crate::dsdl_runtime::DsdlVec<lanyard_health_SubsystemReport_1_0>,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// Vehicle-level health summary, published at 1 Hz by the flight
    /// controller.
    /// 
    /// TRANSPORT TIER: CAN FD.
    /// 
    /// WHY DELIMITED: the subsystem list grows as the airframe gains
    /// hardware, and the ground station is the slowest thing in the fleet to
    /// be updated. An extent lets the ground station keep decoding this
    /// message across several vehicle software releases.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_health_SystemHealth_1_0 {
        /// Moment the summary was assembled.
        pub timestamp: uavcan_time_SynchronizedTimestamp_1_0,
        /// Worst health level among all subsystems below. Precomputed so that a
        /// consumer that only needs the top-level state -- an annunciator light,
        /// say -- does not have to walk the array.
        pub aggregate_health: uavcan_node_Health_1_0,
        /// One report per monitored subsystem. Sealed elements, so unlike the
        /// waypoint array in MissionPlan these carry no per-element delimiter
        /// header; the trade is that SubsystemReport can never gain a field
        /// without a major bump of its own.
        pub subsystem: crate::dsdl_runtime::DsdlVec<lanyard_health_SubsystemReport_1_0>,
    }

    ```

=== "Go"

    ```go
    // Vehicle-level health summary, published at 1 Hz by the flight
    // controller.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY DELIMITED: the subsystem list grows as the airframe gains
    // hardware, and the ground station is the slowest thing in the fleet to
    // be updated. An extent lets the ground station keep decoding this
    // message across several vehicle software releases.
    type SystemHealth_1_0 struct {
      // Moment the summary was assembled.
      Timestamp pkg_uavcan_time.SynchronizedTimestamp_1_0
      // Worst health level among all subsystems below. Precomputed so that a
      // consumer that only needs the top-level state -- an annunciator light,
      // say -- does not have to walk the array.
      AggregateHealth pkg_uavcan_node.Health_1_0
      // One report per monitored subsystem. Sealed elements, so unlike the
      // waypoint array in MissionPlan these carry no per-element delimiter
      // header; the trade is that SubsystemReport can never gain a field
      // without a major bump of its own.
      Subsystem []SubsystemReport_1_0
    }

    ```

=== "TypeScript"

    ```typescript
    // Vehicle-level health summary, published at 1 Hz by the flight
    // controller.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY DELIMITED: the subsystem list grows as the airframe gains
    // hardware, and the ground station is the slowest thing in the fleet to
    // be updated. An extent lets the ground station keep decoding this
    // message across several vehicle software releases.
    export interface SystemHealth_1_0 {
      // Moment the summary was assembled.
      timestamp: SynchronizedTimestamp_1_0;
      // Worst health level among all subsystems below. Precomputed so that a
      // consumer that only needs the top-level state -- an annunciator light,
      // say -- does not have to walk the array.
      aggregate_health: Health_1_0;
      // One report per monitored subsystem. Sealed elements, so unlike the
      // waypoint array in MissionPlan these carry no per-element delimiter
      // header; the trade is that SubsystemReport can never gain a field
      // without a major bump of its own.
      subsystem: Array<SubsystemReport_1_0>;
    }

    ```

=== "Python"

    ```python
    # Vehicle-level health summary, published at 1 Hz by the flight
    # controller.
    # 
    # TRANSPORT TIER: CAN FD.
    # 
    # WHY DELIMITED: the subsystem list grows as the airframe gains
    # hardware, and the ground station is the slowest thing in the fleet to
    # be updated. An extent lets the ground station keep decoding this
    # message across several vehicle software releases.
    @dataclass(slots=True)
    class SystemHealth_1_0:
        # Moment the summary was assembled.
        timestamp: SynchronizedTimestamp_1_0 = field(default_factory=SynchronizedTimestamp_1_0)
        # Worst health level among all subsystems below. Precomputed so that a
        # consumer that only needs the top-level state -- an annunciator light,
        # say -- does not have to walk the array.
        aggregate_health: Health_1_0 = field(default_factory=Health_1_0)
        # One report per monitored subsystem. Sealed elements, so unlike the
        # waypoint array in MissionPlan these carry no per-element delimiter
        # header; the trade is that SubsystemReport can never gain a field
        # without a major bump of its own.
        subsystem: list[SubsystemReport_1_0] = field(default_factory=list)

    ```
