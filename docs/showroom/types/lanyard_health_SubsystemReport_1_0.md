# lanyard.health.SubsystemReport.1.0

Health of one subsystem, as reported by the node that owns it.

| | |
|---|---|
| Full name | `lanyard.health.SubsystemReport` |
| Version | 1.0 |
| Kind | Message |
| Fixed port ID | none (nested type) |
| Transport tier | unspecified |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Message | 30 | 30 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# Health of one subsystem, as reported by the node that owns it.
#
# Nested type with no fixed port identifier; carried as an array element
# inside SystemHealth.1.0.
#
# The two leading fields are standard UAVCAN types rather than local
# enumerations. That is the point of reusing the regulated namespace: a
# generic health monitor, a logger, or a ground station can interpret
# the severity and health of a vendor-specific subsystem without knowing
# anything about this vendor, because those two fields mean exactly what
# they mean everywhere else on the network.

uavcan.node.Health.1.0 health
# Aggregate health of the subsystem, using the standard four-level
# scale: NOMINAL, ADVISORY, CAUTION, WARNING.

uavcan.diagnostic.Severity.1.0 severity
# Severity of the most significant active condition, on the standard
# eight-level scale.

void3
# Padding to a byte boundary. The two standard types above occupy two
# and three bits respectively.

uint16 fault_code
# Vendor-specific code identifying the active condition, or zero when
# there is none. Meaningful only in combination with the subsystem name
# below.

uint8[<=24] name
# Subsystem name, UTF-8, for example "gnss" or "esc.3". Bounded because
# up to eight of these are carried in a single SystemHealth message.

@sealed
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* Health of one subsystem, as reported by the node that owns it. */
    /*  */
    /* Nested type with no fixed port identifier; carried as an array element */
    /* inside SystemHealth.1.0. */
    /*  */
    /* The two leading fields are standard UAVCAN types rather than local */
    /* enumerations. That is the point of reusing the regulated namespace: a */
    /* generic health monitor, a logger, or a ground station can interpret */
    /* the severity and health of a vendor-specific subsystem without knowing */
    /* anything about this vendor, because those two fields mean exactly what */
    /* they mean everywhere else on the network. */
    typedef struct lanyard__health__SubsystemReport {
      /* Aggregate health of the subsystem, using the standard four-level */
      /* scale: NOMINAL, ADVISORY, CAUTION, WARNING. */
      uavcan__node__Health health;
      /* Severity of the most significant active condition, on the standard */
      /* eight-level scale. */
      uavcan__diagnostic__Severity severity;
      /* Vendor-specific code identifying the active condition, or zero when */
      /* there is none. Meaningful only in combination with the subsystem name */
      /* below. */
      uint16_t fault_code;
      /* Subsystem name, UTF-8, for example "gnss" or "esc.3". Bounded because */
      /* up to eight of these are carried in a single SystemHealth message. */
      struct {
        uint8_t elements[24U];
        size_t count;
      } name;
    } lanyard__health__SubsystemReport;

    ```

=== "C++ (std)"

    ```cpp
    // Health of one subsystem, as reported by the node that owns it.
    // 
    // Nested type with no fixed port identifier; carried as an array element
    // inside SystemHealth.1.0.
    // 
    // The two leading fields are standard UAVCAN types rather than local
    // enumerations. That is the point of reusing the regulated namespace: a
    // generic health monitor, a logger, or a ground station can interpret
    // the severity and health of a vendor-specific subsystem without knowing
    // anything about this vendor, because those two fields mean exactly what
    // they mean everywhere else on the network.
    struct SubsystemReport {
      // Aggregate health of the subsystem, using the standard four-level
      // scale: NOMINAL, ADVISORY, CAUTION, WARNING.
      ::uavcan::node::Health health{};
      // Severity of the most significant active condition, on the standard
      // eight-level scale.
      ::uavcan::diagnostic::Severity severity{};
      // Vendor-specific code identifying the active condition, or zero when
      // there is none. Meaningful only in combination with the subsystem name
      // below.
      std::uint16_t fault_code{};
      // Subsystem name, UTF-8, for example "gnss" or "esc.3". Bounded because
      // up to eight of these are carried in a single SystemHealth message.
      std::vector<std::uint8_t> name{};
      static constexpr const char* FULL_NAME = "lanyard.health.SubsystemReport";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.health.SubsystemReport.1.0";
      static constexpr std::size_t EXTENT_BYTES = 30U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 30U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      static constexpr std::size_t NAME_ARRAY_CAPACITY = 24U;
      static constexpr bool NAME_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return SubsystemReport__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return SubsystemReport__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return SubsystemReport__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return SubsystemReport__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // Health of one subsystem, as reported by the node that owns it.
    // 
    // Nested type with no fixed port identifier; carried as an array element
    // inside SystemHealth.1.0.
    // 
    // The two leading fields are standard UAVCAN types rather than local
    // enumerations. That is the point of reusing the regulated namespace: a
    // generic health monitor, a logger, or a ground station can interpret
    // the severity and health of a vendor-specific subsystem without knowing
    // anything about this vendor, because those two fields mean exactly what
    // they mean everywhere else on the network.
    struct SubsystemReport {
      // Aggregate health of the subsystem, using the standard four-level
      // scale: NOMINAL, ADVISORY, CAUTION, WARNING.
      ::uavcan::node::Health health{};
      // Severity of the most significant active condition, on the standard
      // eight-level scale.
      ::uavcan::diagnostic::Severity severity{};
      // Vendor-specific code identifying the active condition, or zero when
      // there is none. Meaningful only in combination with the subsystem name
      // below.
      std::uint16_t fault_code{};
      // Subsystem name, UTF-8, for example "gnss" or "esc.3". Bounded because
      // up to eight of these are carried in a single SystemHealth message.
      std::pmr::vector<std::uint8_t> name{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      SubsystemReport() = default;
      explicit SubsystemReport(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
        name = decltype(name)(_memory_resource);
        health.set_memory_resource(_memory_resource);
        severity.set_memory_resource(_memory_resource);
      }
      static constexpr const char* FULL_NAME = "lanyard.health.SubsystemReport";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.health.SubsystemReport.1.0";
      static constexpr std::size_t EXTENT_BYTES = 30U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 30U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      static constexpr std::size_t NAME_ARRAY_CAPACITY = 24U;
      static constexpr bool NAME_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return SubsystemReport__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return SubsystemReport__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return SubsystemReport__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return SubsystemReport__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return SubsystemReport__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return SubsystemReport__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // Health of one subsystem, as reported by the node that owns it.
    // 
    // Nested type with no fixed port identifier; carried as an array element
    // inside SystemHealth.1.0.
    // 
    // The two leading fields are standard UAVCAN types rather than local
    // enumerations. That is the point of reusing the regulated namespace: a
    // generic health monitor, a logger, or a ground station can interpret
    // the severity and health of a vendor-specific subsystem without knowing
    // anything about this vendor, because those two fields mean exactly what
    // they mean everywhere else on the network.
    struct SubsystemReport {
      // Aggregate health of the subsystem, using the standard four-level
      // scale: NOMINAL, ADVISORY, CAUTION, WARNING.
      ::uavcan::node::Health health{};
      // Severity of the most significant active condition, on the standard
      // eight-level scale.
      ::uavcan::diagnostic::Severity severity{};
      // Vendor-specific code identifying the active condition, or zero when
      // there is none. Meaningful only in combination with the subsystem name
      // below.
      std::uint16_t fault_code{};
      // Subsystem name, UTF-8, for example "gnss" or "esc.3". Bounded because
      // up to eight of these are carried in a single SystemHealth message.
      ::llvmdsdl::cpp::autosar::BoundedVector<std::uint8_t, 24U> name{};
      static constexpr const char* FULL_NAME = "lanyard.health.SubsystemReport";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.health.SubsystemReport.1.0";
      static constexpr std::size_t EXTENT_BYTES = 30U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 30U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      static constexpr std::size_t NAME_ARRAY_CAPACITY = 24U;
      static constexpr bool NAME_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return SubsystemReport__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return SubsystemReport__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return SubsystemReport__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return SubsystemReport__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// Health of one subsystem, as reported by the node that owns it.
    /// 
    /// Nested type with no fixed port identifier; carried as an array element
    /// inside SystemHealth.1.0.
    /// 
    /// The two leading fields are standard UAVCAN types rather than local
    /// enumerations. That is the point of reusing the regulated namespace: a
    /// generic health monitor, a logger, or a ground station can interpret
    /// the severity and health of a vendor-specific subsystem without knowing
    /// anything about this vendor, because those two fields mean exactly what
    /// they mean everywhere else on the network.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_health_SubsystemReport_1_0 {
        /// Aggregate health of the subsystem, using the standard four-level
        /// scale: NOMINAL, ADVISORY, CAUTION, WARNING.
        pub health: uavcan_node_Health_1_0,
        /// Severity of the most significant active condition, on the standard
        /// eight-level scale.
        pub severity: uavcan_diagnostic_Severity_1_0,
        /// Vendor-specific code identifying the active condition, or zero when
        /// there is none. Meaningful only in combination with the subsystem name
        /// below.
        pub fault_code: u16,
        /// Subsystem name, UTF-8, for example "gnss" or "esc.3". Bounded because
        /// up to eight of these are carried in a single SystemHealth message.
        pub name: crate::dsdl_runtime::DsdlVec<u8>,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// Health of one subsystem, as reported by the node that owns it.
    /// 
    /// Nested type with no fixed port identifier; carried as an array element
    /// inside SystemHealth.1.0.
    /// 
    /// The two leading fields are standard UAVCAN types rather than local
    /// enumerations. That is the point of reusing the regulated namespace: a
    /// generic health monitor, a logger, or a ground station can interpret
    /// the severity and health of a vendor-specific subsystem without knowing
    /// anything about this vendor, because those two fields mean exactly what
    /// they mean everywhere else on the network.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_health_SubsystemReport_1_0 {
        /// Aggregate health of the subsystem, using the standard four-level
        /// scale: NOMINAL, ADVISORY, CAUTION, WARNING.
        pub health: uavcan_node_Health_1_0,
        /// Severity of the most significant active condition, on the standard
        /// eight-level scale.
        pub severity: uavcan_diagnostic_Severity_1_0,
        /// Vendor-specific code identifying the active condition, or zero when
        /// there is none. Meaningful only in combination with the subsystem name
        /// below.
        pub fault_code: u16,
        /// Subsystem name, UTF-8, for example "gnss" or "esc.3". Bounded because
        /// up to eight of these are carried in a single SystemHealth message.
        pub name: crate::dsdl_runtime::DsdlVec<u8>,
    }

    ```

=== "Go"

    ```go
    // Health of one subsystem, as reported by the node that owns it.
    // 
    // Nested type with no fixed port identifier; carried as an array element
    // inside SystemHealth.1.0.
    // 
    // The two leading fields are standard UAVCAN types rather than local
    // enumerations. That is the point of reusing the regulated namespace: a
    // generic health monitor, a logger, or a ground station can interpret
    // the severity and health of a vendor-specific subsystem without knowing
    // anything about this vendor, because those two fields mean exactly what
    // they mean everywhere else on the network.
    type SubsystemReport_1_0 struct {
      // Aggregate health of the subsystem, using the standard four-level
      // scale: NOMINAL, ADVISORY, CAUTION, WARNING.
      Health pkg_uavcan_node.Health_1_0
      // Severity of the most significant active condition, on the standard
      // eight-level scale.
      Severity pkg_uavcan_diagnostic.Severity_1_0
      // Vendor-specific code identifying the active condition, or zero when
      // there is none. Meaningful only in combination with the subsystem name
      // below.
      FaultCode uint16
      // Subsystem name, UTF-8, for example "gnss" or "esc.3". Bounded because
      // up to eight of these are carried in a single SystemHealth message.
      Name []uint8
    }

    ```

=== "TypeScript"

    ```typescript
    // Health of one subsystem, as reported by the node that owns it.
    // 
    // Nested type with no fixed port identifier; carried as an array element
    // inside SystemHealth.1.0.
    // 
    // The two leading fields are standard UAVCAN types rather than local
    // enumerations. That is the point of reusing the regulated namespace: a
    // generic health monitor, a logger, or a ground station can interpret
    // the severity and health of a vendor-specific subsystem without knowing
    // anything about this vendor, because those two fields mean exactly what
    // they mean everywhere else on the network.
    export interface SubsystemReport_1_0 {
      // Aggregate health of the subsystem, using the standard four-level
      // scale: NOMINAL, ADVISORY, CAUTION, WARNING.
      health: Health_1_0;
      // Severity of the most significant active condition, on the standard
      // eight-level scale.
      severity: Severity_1_0;
      // Vendor-specific code identifying the active condition, or zero when
      // there is none. Meaningful only in combination with the subsystem name
      // below.
      fault_code: number;
      // Subsystem name, UTF-8, for example "gnss" or "esc.3". Bounded because
      // up to eight of these are carried in a single SystemHealth message.
      name: Array<number>;
    }

    ```

=== "Python"

    ```python
    # Health of one subsystem, as reported by the node that owns it.
    # 
    # Nested type with no fixed port identifier; carried as an array element
    # inside SystemHealth.1.0.
    # 
    # The two leading fields are standard UAVCAN types rather than local
    # enumerations. That is the point of reusing the regulated namespace: a
    # generic health monitor, a logger, or a ground station can interpret
    # the severity and health of a vendor-specific subsystem without knowing
    # anything about this vendor, because those two fields mean exactly what
    # they mean everywhere else on the network.
    @dataclass(slots=True)
    class SubsystemReport_1_0:
        # Aggregate health of the subsystem, using the standard four-level
        # scale: NOMINAL, ADVISORY, CAUTION, WARNING.
        health: Health_1_0 = field(default_factory=Health_1_0)
        # Severity of the most significant active condition, on the standard
        # eight-level scale.
        severity: Severity_1_0 = field(default_factory=Severity_1_0)
        # Vendor-specific code identifying the active condition, or zero when
        # there is none. Meaningful only in combination with the subsystem name
        # below.
        fault_code: int = 0
        # Subsystem name, UTF-8, for example "gnss" or "esc.3". Bounded because
        # up to eight of these are carried in a single SystemHealth message.
        name: list[int] = field(default_factory=list)

    ```
