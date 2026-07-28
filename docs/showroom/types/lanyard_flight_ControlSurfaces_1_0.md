# lanyard.flight.ControlSurfaces.1.0

Post-mixer actuator commands, discriminated by airframe class.

| | |
|---|---|
| Full name | `lanyard.flight.ControlSurfaces` |
| Version | 1.0 |
| Kind | Message |
| Fixed port ID | 6212 |
| Transport tier | CAN FD |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Message | 64 | 26 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# Post-mixer actuator commands, discriminated by airframe class.
#
# TRANSPORT TIER: CAN FD.
#
# WHY A UNION: the three airframe classes in the fleet need disjoint
# actuator sets, and a message that carried all of them would waste
# bandwidth on every flight and would leave a receiver guessing which
# half of the payload is meaningful. A @union serializes a tag plus
# exactly one option, so the wire cost is that of the selected variant
# rather than the sum of all three.
#
# A union's fields are options, not members: exactly one is present in
# any given value. The tag is implicit in the definition and is emitted
# ahead of the selected option.
#
# WHAT THE GENERATED CODE LOOKS LIKE: this is the one construct whose
# rendering genuinely differs between backends, so it is worth comparing
# them side by side in the showroom.
#
#   - C, C++, Rust, and Go emit a plain aggregate holding all three
#     options at once, plus a tag field naming the active one. It needs
#     no allocation and its layout does not depend on which option is
#     set, but the aggregate is as large as its largest option and
#     reading the wrong one is a runtime mistake, not a compile-time
#     one: consumers must branch on the tag first.
#   - Python emits the same shape with the inactive options typed as
#     None, so an unset option at least reads as absent rather than as a
#     zeroed value.
#   - TypeScript emits a true discriminated union -- three object types
#     joined by `|`, each carrying its own `_tag` literal -- so the
#     compiler refuses to let you read an option you have not narrowed
#     to.
#
# None of this changes the wire format, which is a tag followed by the
# selected option in every case.

@union

lanyard.flight.MultirotorMix.1.0 multirotor
# Selected for quadrotor, hexrotor, and coaxial airframes.

lanyard.flight.FixedWingSurfaces.1.0 fixed_wing
# Selected for conventional and flying-wing airframes.

lanyard.flight.MultirotorMix.1.0 vtol_hover
# Selected for the hover phase of a VTOL transition. Structurally
# identical to the multirotor option but semantically distinct: a
# receiver uses the tag to decide whether the forward-flight surfaces
# should be held at their transition schedule or released to the
# autopilot.

@extent 64 * 8
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* Post-mixer actuator commands, discriminated by airframe class. */
    /*  */
    /* TRANSPORT TIER: CAN FD. */
    /*  */
    /* WHY A UNION: the three airframe classes in the fleet need disjoint */
    /* actuator sets, and a message that carried all of them would waste */
    /* bandwidth on every flight and would leave a receiver guessing which */
    /* half of the payload is meaningful. A @union serializes a tag plus */
    /* exactly one option, so the wire cost is that of the selected variant */
    /* rather than the sum of all three. */
    /*  */
    /* A union's fields are options, not members: exactly one is present in */
    /* any given value. The tag is implicit in the definition and is emitted */
    /* ahead of the selected option. */
    /*  */
    /* WHAT THE GENERATED CODE LOOKS LIKE: this is the one construct whose */
    /* rendering genuinely differs between backends, so it is worth comparing */
    /* them side by side in the showroom. */
    /*  */
    /*   - C, C++, Rust, and Go emit a plain aggregate holding all three */
    /*     options at once, plus a tag field naming the active one. It needs */
    /*     no allocation and its layout does not depend on which option is */
    /*     set, but the aggregate is as large as its largest option and */
    /*     reading the wrong one is a runtime mistake, not a compile-time */
    /*     one: consumers must branch on the tag first. */
    /*   - Python emits the same shape with the inactive options typed as */
    /*     None, so an unset option at least reads as absent rather than as a */
    /*     zeroed value. */
    /*   - TypeScript emits a true discriminated union -- three object types */
    /*     joined by `|`, each carrying its own `_tag` literal -- so the */
    /*     compiler refuses to let you read an option you have not narrowed */
    /*     to. */
    /*  */
    /* None of this changes the wire format, which is a tag followed by the */
    /* selected option in every case. */
    typedef struct lanyard__flight__ControlSurfaces {
      /* Selected for quadrotor, hexrotor, and coaxial airframes. */
      lanyard__flight__MultirotorMix multirotor;
      /* Selected for conventional and flying-wing airframes. */
      lanyard__flight__FixedWingSurfaces fixed_wing;
      /* Selected for the hover phase of a VTOL transition. Structurally */
      /* identical to the multirotor option but semantically distinct: a */
      /* receiver uses the tag to decide whether the forward-flight surfaces */
      /* should be held at their transition schedule or released to the */
      /* autopilot. */
      lanyard__flight__MultirotorMix vtol_hover;
      uint8_t _tag_;
    } lanyard__flight__ControlSurfaces;

    ```

=== "C++ (std)"

    ```cpp
    // Post-mixer actuator commands, discriminated by airframe class.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY A UNION: the three airframe classes in the fleet need disjoint
    // actuator sets, and a message that carried all of them would waste
    // bandwidth on every flight and would leave a receiver guessing which
    // half of the payload is meaningful. A @union serializes a tag plus
    // exactly one option, so the wire cost is that of the selected variant
    // rather than the sum of all three.
    // 
    // A union's fields are options, not members: exactly one is present in
    // any given value. The tag is implicit in the definition and is emitted
    // ahead of the selected option.
    // 
    // WHAT THE GENERATED CODE LOOKS LIKE: this is the one construct whose
    // rendering genuinely differs between backends, so it is worth comparing
    // them side by side in the showroom.
    // 
    //   - C, C++, Rust, and Go emit a plain aggregate holding all three
    //     options at once, plus a tag field naming the active one. It needs
    //     no allocation and its layout does not depend on which option is
    //     set, but the aggregate is as large as its largest option and
    //     reading the wrong one is a runtime mistake, not a compile-time
    //     one: consumers must branch on the tag first.
    //   - Python emits the same shape with the inactive options typed as
    //     None, so an unset option at least reads as absent rather than as a
    //     zeroed value.
    //   - TypeScript emits a true discriminated union -- three object types
    //     joined by `|`, each carrying its own `_tag` literal -- so the
    //     compiler refuses to let you read an option you have not narrowed
    //     to.
    // 
    // None of this changes the wire format, which is a tag followed by the
    // selected option in every case.
    struct ControlSurfaces {
      // Selected for quadrotor, hexrotor, and coaxial airframes.
      ::lanyard::flight::MultirotorMix multirotor{};
      // Selected for conventional and flying-wing airframes.
      ::lanyard::flight::FixedWingSurfaces fixed_wing{};
      // Selected for the hover phase of a VTOL transition. Structurally
      // identical to the multirotor option but semantically distinct: a
      // receiver uses the tag to decide whether the forward-flight surfaces
      // should be held at their transition schedule or released to the
      // autopilot.
      ::lanyard::flight::MultirotorMix vtol_hover{};
      std::uint8_t _tag_{0U};
      static constexpr const char* FULL_NAME = "lanyard.flight.ControlSurfaces";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.flight.ControlSurfaces.1.0";
      static constexpr std::size_t EXTENT_BYTES = 64U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 26U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      static constexpr std::size_t UNION_OPTION_COUNT = 3U;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return ControlSurfaces__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return ControlSurfaces__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return ControlSurfaces__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return ControlSurfaces__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // Post-mixer actuator commands, discriminated by airframe class.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY A UNION: the three airframe classes in the fleet need disjoint
    // actuator sets, and a message that carried all of them would waste
    // bandwidth on every flight and would leave a receiver guessing which
    // half of the payload is meaningful. A @union serializes a tag plus
    // exactly one option, so the wire cost is that of the selected variant
    // rather than the sum of all three.
    // 
    // A union's fields are options, not members: exactly one is present in
    // any given value. The tag is implicit in the definition and is emitted
    // ahead of the selected option.
    // 
    // WHAT THE GENERATED CODE LOOKS LIKE: this is the one construct whose
    // rendering genuinely differs between backends, so it is worth comparing
    // them side by side in the showroom.
    // 
    //   - C, C++, Rust, and Go emit a plain aggregate holding all three
    //     options at once, plus a tag field naming the active one. It needs
    //     no allocation and its layout does not depend on which option is
    //     set, but the aggregate is as large as its largest option and
    //     reading the wrong one is a runtime mistake, not a compile-time
    //     one: consumers must branch on the tag first.
    //   - Python emits the same shape with the inactive options typed as
    //     None, so an unset option at least reads as absent rather than as a
    //     zeroed value.
    //   - TypeScript emits a true discriminated union -- three object types
    //     joined by `|`, each carrying its own `_tag` literal -- so the
    //     compiler refuses to let you read an option you have not narrowed
    //     to.
    // 
    // None of this changes the wire format, which is a tag followed by the
    // selected option in every case.
    struct ControlSurfaces {
      // Selected for quadrotor, hexrotor, and coaxial airframes.
      ::lanyard::flight::MultirotorMix multirotor{};
      // Selected for conventional and flying-wing airframes.
      ::lanyard::flight::FixedWingSurfaces fixed_wing{};
      // Selected for the hover phase of a VTOL transition. Structurally
      // identical to the multirotor option but semantically distinct: a
      // receiver uses the tag to decide whether the forward-flight surfaces
      // should be held at their transition schedule or released to the
      // autopilot.
      ::lanyard::flight::MultirotorMix vtol_hover{};
      std::uint8_t _tag_{0U};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      ControlSurfaces() = default;
      explicit ControlSurfaces(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
        multirotor.set_memory_resource(_memory_resource);
        fixed_wing.set_memory_resource(_memory_resource);
        vtol_hover.set_memory_resource(_memory_resource);
      }
      static constexpr const char* FULL_NAME = "lanyard.flight.ControlSurfaces";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.flight.ControlSurfaces.1.0";
      static constexpr std::size_t EXTENT_BYTES = 64U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 26U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      static constexpr std::size_t UNION_OPTION_COUNT = 3U;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return ControlSurfaces__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return ControlSurfaces__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return ControlSurfaces__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return ControlSurfaces__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return ControlSurfaces__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return ControlSurfaces__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // Post-mixer actuator commands, discriminated by airframe class.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY A UNION: the three airframe classes in the fleet need disjoint
    // actuator sets, and a message that carried all of them would waste
    // bandwidth on every flight and would leave a receiver guessing which
    // half of the payload is meaningful. A @union serializes a tag plus
    // exactly one option, so the wire cost is that of the selected variant
    // rather than the sum of all three.
    // 
    // A union's fields are options, not members: exactly one is present in
    // any given value. The tag is implicit in the definition and is emitted
    // ahead of the selected option.
    // 
    // WHAT THE GENERATED CODE LOOKS LIKE: this is the one construct whose
    // rendering genuinely differs between backends, so it is worth comparing
    // them side by side in the showroom.
    // 
    //   - C, C++, Rust, and Go emit a plain aggregate holding all three
    //     options at once, plus a tag field naming the active one. It needs
    //     no allocation and its layout does not depend on which option is
    //     set, but the aggregate is as large as its largest option and
    //     reading the wrong one is a runtime mistake, not a compile-time
    //     one: consumers must branch on the tag first.
    //   - Python emits the same shape with the inactive options typed as
    //     None, so an unset option at least reads as absent rather than as a
    //     zeroed value.
    //   - TypeScript emits a true discriminated union -- three object types
    //     joined by `|`, each carrying its own `_tag` literal -- so the
    //     compiler refuses to let you read an option you have not narrowed
    //     to.
    // 
    // None of this changes the wire format, which is a tag followed by the
    // selected option in every case.
    struct ControlSurfaces {
      // Selected for quadrotor, hexrotor, and coaxial airframes.
      ::lanyard::flight::MultirotorMix multirotor{};
      // Selected for conventional and flying-wing airframes.
      ::lanyard::flight::FixedWingSurfaces fixed_wing{};
      // Selected for the hover phase of a VTOL transition. Structurally
      // identical to the multirotor option but semantically distinct: a
      // receiver uses the tag to decide whether the forward-flight surfaces
      // should be held at their transition schedule or released to the
      // autopilot.
      ::lanyard::flight::MultirotorMix vtol_hover{};
      std::uint8_t _tag_{0U};
      static constexpr const char* FULL_NAME = "lanyard.flight.ControlSurfaces";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.flight.ControlSurfaces.1.0";
      static constexpr std::size_t EXTENT_BYTES = 64U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 26U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      static constexpr std::size_t UNION_OPTION_COUNT = 3U;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return ControlSurfaces__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return ControlSurfaces__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return ControlSurfaces__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return ControlSurfaces__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// Post-mixer actuator commands, discriminated by airframe class.
    /// 
    /// TRANSPORT TIER: CAN FD.
    /// 
    /// WHY A UNION: the three airframe classes in the fleet need disjoint
    /// actuator sets, and a message that carried all of them would waste
    /// bandwidth on every flight and would leave a receiver guessing which
    /// half of the payload is meaningful. A @union serializes a tag plus
    /// exactly one option, so the wire cost is that of the selected variant
    /// rather than the sum of all three.
    /// 
    /// A union's fields are options, not members: exactly one is present in
    /// any given value. The tag is implicit in the definition and is emitted
    /// ahead of the selected option.
    /// 
    /// WHAT THE GENERATED CODE LOOKS LIKE: this is the one construct whose
    /// rendering genuinely differs between backends, so it is worth comparing
    /// them side by side in the showroom.
    /// 
    ///   - C, C++, Rust, and Go emit a plain aggregate holding all three
    ///     options at once, plus a tag field naming the active one. It needs
    ///     no allocation and its layout does not depend on which option is
    ///     set, but the aggregate is as large as its largest option and
    ///     reading the wrong one is a runtime mistake, not a compile-time
    ///     one: consumers must branch on the tag first.
    ///   - Python emits the same shape with the inactive options typed as
    ///     None, so an unset option at least reads as absent rather than as a
    ///     zeroed value.
    ///   - TypeScript emits a true discriminated union -- three object types
    ///     joined by `|`, each carrying its own `_tag` literal -- so the
    ///     compiler refuses to let you read an option you have not narrowed
    ///     to.
    /// 
    /// None of this changes the wire format, which is a tag followed by the
    /// selected option in every case.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_flight_ControlSurfaces_1_0 {
        /// Selected for quadrotor, hexrotor, and coaxial airframes.
        pub multirotor: lanyard_flight_MultirotorMix_1_0,
        /// Selected for conventional and flying-wing airframes.
        pub fixed_wing: lanyard_flight_FixedWingSurfaces_1_0,
        /// Selected for the hover phase of a VTOL transition. Structurally
        /// identical to the multirotor option but semantically distinct: a
        /// receiver uses the tag to decide whether the forward-flight surfaces
        /// should be held at their transition schedule or released to the
        /// autopilot.
        pub vtol_hover: lanyard_flight_MultirotorMix_1_0,
        pub _tag_: u8,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// Post-mixer actuator commands, discriminated by airframe class.
    /// 
    /// TRANSPORT TIER: CAN FD.
    /// 
    /// WHY A UNION: the three airframe classes in the fleet need disjoint
    /// actuator sets, and a message that carried all of them would waste
    /// bandwidth on every flight and would leave a receiver guessing which
    /// half of the payload is meaningful. A @union serializes a tag plus
    /// exactly one option, so the wire cost is that of the selected variant
    /// rather than the sum of all three.
    /// 
    /// A union's fields are options, not members: exactly one is present in
    /// any given value. The tag is implicit in the definition and is emitted
    /// ahead of the selected option.
    /// 
    /// WHAT THE GENERATED CODE LOOKS LIKE: this is the one construct whose
    /// rendering genuinely differs between backends, so it is worth comparing
    /// them side by side in the showroom.
    /// 
    ///   - C, C++, Rust, and Go emit a plain aggregate holding all three
    ///     options at once, plus a tag field naming the active one. It needs
    ///     no allocation and its layout does not depend on which option is
    ///     set, but the aggregate is as large as its largest option and
    ///     reading the wrong one is a runtime mistake, not a compile-time
    ///     one: consumers must branch on the tag first.
    ///   - Python emits the same shape with the inactive options typed as
    ///     None, so an unset option at least reads as absent rather than as a
    ///     zeroed value.
    ///   - TypeScript emits a true discriminated union -- three object types
    ///     joined by `|`, each carrying its own `_tag` literal -- so the
    ///     compiler refuses to let you read an option you have not narrowed
    ///     to.
    /// 
    /// None of this changes the wire format, which is a tag followed by the
    /// selected option in every case.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_flight_ControlSurfaces_1_0 {
        /// Selected for quadrotor, hexrotor, and coaxial airframes.
        pub multirotor: lanyard_flight_MultirotorMix_1_0,
        /// Selected for conventional and flying-wing airframes.
        pub fixed_wing: lanyard_flight_FixedWingSurfaces_1_0,
        /// Selected for the hover phase of a VTOL transition. Structurally
        /// identical to the multirotor option but semantically distinct: a
        /// receiver uses the tag to decide whether the forward-flight surfaces
        /// should be held at their transition schedule or released to the
        /// autopilot.
        pub vtol_hover: lanyard_flight_MultirotorMix_1_0,
        pub _tag_: u8,
    }

    ```

=== "Go"

    ```go
    // Post-mixer actuator commands, discriminated by airframe class.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY A UNION: the three airframe classes in the fleet need disjoint
    // actuator sets, and a message that carried all of them would waste
    // bandwidth on every flight and would leave a receiver guessing which
    // half of the payload is meaningful. A @union serializes a tag plus
    // exactly one option, so the wire cost is that of the selected variant
    // rather than the sum of all three.
    // 
    // A union's fields are options, not members: exactly one is present in
    // any given value. The tag is implicit in the definition and is emitted
    // ahead of the selected option.
    // 
    // WHAT THE GENERATED CODE LOOKS LIKE: this is the one construct whose
    // rendering genuinely differs between backends, so it is worth comparing
    // them side by side in the showroom.
    // 
    //   - C, C++, Rust, and Go emit a plain aggregate holding all three
    //     options at once, plus a tag field naming the active one. It needs
    //     no allocation and its layout does not depend on which option is
    //     set, but the aggregate is as large as its largest option and
    //     reading the wrong one is a runtime mistake, not a compile-time
    //     one: consumers must branch on the tag first.
    //   - Python emits the same shape with the inactive options typed as
    //     None, so an unset option at least reads as absent rather than as a
    //     zeroed value.
    //   - TypeScript emits a true discriminated union -- three object types
    //     joined by `|`, each carrying its own `_tag` literal -- so the
    //     compiler refuses to let you read an option you have not narrowed
    //     to.
    // 
    // None of this changes the wire format, which is a tag followed by the
    // selected option in every case.
    type ControlSurfaces_1_0 struct {
      // Selected for quadrotor, hexrotor, and coaxial airframes.
      Multirotor MultirotorMix_1_0
      // Selected for conventional and flying-wing airframes.
      FixedWing FixedWingSurfaces_1_0
      // Selected for the hover phase of a VTOL transition. Structurally
      // identical to the multirotor option but semantically distinct: a
      // receiver uses the tag to decide whether the forward-flight surfaces
      // should be held at their transition schedule or released to the
      // autopilot.
      VtolHover MultirotorMix_1_0
      Tag uint8
    }

    ```

=== "TypeScript"

    ```typescript
    // Post-mixer actuator commands, discriminated by airframe class.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY A UNION: the three airframe classes in the fleet need disjoint
    // actuator sets, and a message that carried all of them would waste
    // bandwidth on every flight and would leave a receiver guessing which
    // half of the payload is meaningful. A @union serializes a tag plus
    // exactly one option, so the wire cost is that of the selected variant
    // rather than the sum of all three.
    // 
    // A union's fields are options, not members: exactly one is present in
    // any given value. The tag is implicit in the definition and is emitted
    // ahead of the selected option.
    // 
    // WHAT THE GENERATED CODE LOOKS LIKE: this is the one construct whose
    // rendering genuinely differs between backends, so it is worth comparing
    // them side by side in the showroom.
    // 
    //   - C, C++, Rust, and Go emit a plain aggregate holding all three
    //     options at once, plus a tag field naming the active one. It needs
    //     no allocation and its layout does not depend on which option is
    //     set, but the aggregate is as large as its largest option and
    //     reading the wrong one is a runtime mistake, not a compile-time
    //     one: consumers must branch on the tag first.
    //   - Python emits the same shape with the inactive options typed as
    //     None, so an unset option at least reads as absent rather than as a
    //     zeroed value.
    //   - TypeScript emits a true discriminated union -- three object types
    //     joined by `|`, each carrying its own `_tag` literal -- so the
    //     compiler refuses to let you read an option you have not narrowed
    //     to.
    // 
    // None of this changes the wire format, which is a tag followed by the
    // selected option in every case.
    export type ControlSurfaces_1_0 =
    // Selected for quadrotor, hexrotor, and coaxial airframes.
      | { _tag: 0; multirotor: MultirotorMix_1_0; }
    // Selected for conventional and flying-wing airframes.
      | { _tag: 1; fixed_wing: FixedWingSurfaces_1_0; }
    // Selected for the hover phase of a VTOL transition. Structurally
    // identical to the multirotor option but semantically distinct: a
    // receiver uses the tag to decide whether the forward-flight surfaces
    // should be held at their transition schedule or released to the
    // autopilot.
      | { _tag: 2; vtol_hover: MultirotorMix_1_0; };

    ```

=== "Python"

    ```python
    # Post-mixer actuator commands, discriminated by airframe class.
    # 
    # TRANSPORT TIER: CAN FD.
    # 
    # WHY A UNION: the three airframe classes in the fleet need disjoint
    # actuator sets, and a message that carried all of them would waste
    # bandwidth on every flight and would leave a receiver guessing which
    # half of the payload is meaningful. A @union serializes a tag plus
    # exactly one option, so the wire cost is that of the selected variant
    # rather than the sum of all three.
    # 
    # A union's fields are options, not members: exactly one is present in
    # any given value. The tag is implicit in the definition and is emitted
    # ahead of the selected option.
    # 
    # WHAT THE GENERATED CODE LOOKS LIKE: this is the one construct whose
    # rendering genuinely differs between backends, so it is worth comparing
    # them side by side in the showroom.
    # 
    #   - C, C++, Rust, and Go emit a plain aggregate holding all three
    #     options at once, plus a tag field naming the active one. It needs
    #     no allocation and its layout does not depend on which option is
    #     set, but the aggregate is as large as its largest option and
    #     reading the wrong one is a runtime mistake, not a compile-time
    #     one: consumers must branch on the tag first.
    #   - Python emits the same shape with the inactive options typed as
    #     None, so an unset option at least reads as absent rather than as a
    #     zeroed value.
    #   - TypeScript emits a true discriminated union -- three object types
    #     joined by `|`, each carrying its own `_tag` literal -- so the
    #     compiler refuses to let you read an option you have not narrowed
    #     to.
    # 
    # None of this changes the wire format, which is a tag followed by the
    # selected option in every case.
    @dataclass(slots=True)
    class ControlSurfaces_1_0:
        _tag: int = 0
        # Selected for quadrotor, hexrotor, and coaxial airframes.
        multirotor: MultirotorMix_1_0 | None = None
        # Selected for conventional and flying-wing airframes.
        fixed_wing: FixedWingSurfaces_1_0 | None = None
        # Selected for the hover phase of a VTOL transition. Structurally
        # identical to the multirotor option but semantically distinct: a
        # receiver uses the tag to decide whether the forward-flight surfaces
        # should be held at their transition schedule or released to the
        # autopilot.
        vtol_hover: MultirotorMix_1_0 | None = None

    ```
