# lanyard.nav.MissionPlan.1.0

A complete stored mission, published once after upload and on request.

| | |
|---|---|
| Full name | `lanyard.nav.MissionPlan` |
| Version | 1.0 |
| Kind | Message |
| Fixed port ID | 6221 |
| Transport tier | Cyphal/UDP ONLY. This message does not fit any CAN |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Message | 24576 | 17423 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# A complete stored mission, published once after upload and on request.
#
# TRANSPORT TIER: Cyphal/UDP ONLY. This message does not fit any CAN
# frame and is not intended to.
#
# It exists in this showroom as the counterweight to EscStatus.1.0: the
# same compiler, the same language, and the same generated-code surface,
# but sized for a transport with a 64 KiB datagram rather than a 7-byte
# frame. On CAN this would fragment into hundreds of frames and
# monopolize the bus; on UDP it is one unremarkable datagram. The
# transport, not the compiler, is what makes a definition of this shape
# reasonable or not.
#
# On a mixed vehicle the CAN nodes never subscribe to this subject. They
# receive mission items one chunk at a time through the UploadMission
# service instead.

uavcan.time.SynchronizedTimestamp.1.0 timestamp
# The moment this plan was committed to storage.

uint32 plan_id
# Monotonic identifier of the stored plan, incremented on every
# successful upload. A consumer uses it to detect that the plan changed
# without re-reading the item list.

uint16 current_item
# Index into `item` of the waypoint the vehicle is currently navigating
# toward.

lanyard.nav.Waypoint.1.0[<=256] item
# The mission items in execution order.
#
# Each element is a delimited composite and therefore carries its own
# four-byte length header. Note that the bound is the waypoint's 64-byte
# *extent*, not the 36 bytes a 1.0 waypoint actually occupies: a reader
# must be prepared for any 1.x waypoint, and the extent is what states
# how large that can be. So the worst case is 256 * (64 + 4) plus the
# array's own length prefix, which puts this message a little over 17
# KiB -- roughly 2500 classic-CAN frames, and one UDP datagram.

@extent 24576 * 8
# 24 KiB. Deliberately not accompanied by an @assert on _offset_: the
# value set of a 256-element array of delimited composites is far too
# large to enumerate, and the analyzer will say so. Bounding a
# definition of this shape is the extent's job. See GlobalPosition.1.0
# for the compile-time size check that is worth writing, on a definition
# whose layout is small enough to evaluate.
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* A complete stored mission, published once after upload and on request. */
    /*  */
    /* TRANSPORT TIER: Cyphal/UDP ONLY. This message does not fit any CAN */
    /* frame and is not intended to. */
    /*  */
    /* It exists in this showroom as the counterweight to EscStatus.1.0: the */
    /* same compiler, the same language, and the same generated-code surface, */
    /* but sized for a transport with a 64 KiB datagram rather than a 7-byte */
    /* frame. On CAN this would fragment into hundreds of frames and */
    /* monopolize the bus; on UDP it is one unremarkable datagram. The */
    /* transport, not the compiler, is what makes a definition of this shape */
    /* reasonable or not. */
    /*  */
    /* On a mixed vehicle the CAN nodes never subscribe to this subject. They */
    /* receive mission items one chunk at a time through the UploadMission */
    /* service instead. */
    typedef struct lanyard__nav__MissionPlan {
      /* The moment this plan was committed to storage. */
      uavcan__time__SynchronizedTimestamp timestamp;
      /* Monotonic identifier of the stored plan, incremented on every */
      /* successful upload. A consumer uses it to detect that the plan changed */
      /* without re-reading the item list. */
      uint32_t plan_id;
      /* Index into `item` of the waypoint the vehicle is currently navigating */
      /* toward. */
      uint16_t current_item;
      /* The mission items in execution order. */
      /*  */
      /* Each element is a delimited composite and therefore carries its own */
      /* four-byte length header. Note that the bound is the waypoint's 64-byte */
      /* *extent*, not the 36 bytes a 1.0 waypoint actually occupies: a reader */
      /* must be prepared for any 1.x waypoint, and the extent is what states */
      /* how large that can be. So the worst case is 256 * (64 + 4) plus the */
      /* array's own length prefix, which puts this message a little over 17 */
      /* KiB -- roughly 2500 classic-CAN frames, and one UDP datagram. */
      struct {
        lanyard__nav__Waypoint elements[256U];
        size_t count;
      } item;
    } lanyard__nav__MissionPlan;

    ```

=== "C++ (std)"

    ```cpp
    // A complete stored mission, published once after upload and on request.
    // 
    // TRANSPORT TIER: Cyphal/UDP ONLY. This message does not fit any CAN
    // frame and is not intended to.
    // 
    // It exists in this showroom as the counterweight to EscStatus.1.0: the
    // same compiler, the same language, and the same generated-code surface,
    // but sized for a transport with a 64 KiB datagram rather than a 7-byte
    // frame. On CAN this would fragment into hundreds of frames and
    // monopolize the bus; on UDP it is one unremarkable datagram. The
    // transport, not the compiler, is what makes a definition of this shape
    // reasonable or not.
    // 
    // On a mixed vehicle the CAN nodes never subscribe to this subject. They
    // receive mission items one chunk at a time through the UploadMission
    // service instead.
    struct MissionPlan {
      // The moment this plan was committed to storage.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // Monotonic identifier of the stored plan, incremented on every
      // successful upload. A consumer uses it to detect that the plan changed
      // without re-reading the item list.
      std::uint32_t plan_id{};
      // Index into `item` of the waypoint the vehicle is currently navigating
      // toward.
      std::uint16_t current_item{};
      // The mission items in execution order.
      // 
      // Each element is a delimited composite and therefore carries its own
      // four-byte length header. Note that the bound is the waypoint's 64-byte
      // *extent*, not the 36 bytes a 1.0 waypoint actually occupies: a reader
      // must be prepared for any 1.x waypoint, and the extent is what states
      // how large that can be. So the worst case is 256 * (64 + 4) plus the
      // array's own length prefix, which puts this message a little over 17
      // KiB -- roughly 2500 classic-CAN frames, and one UDP datagram.
      std::vector<::lanyard::nav::Waypoint> item{};
      static constexpr const char* FULL_NAME = "lanyard.nav.MissionPlan";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.nav.MissionPlan.1.0";
      static constexpr std::size_t EXTENT_BYTES = 24576U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 17423U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      static constexpr std::size_t ITEM_ARRAY_CAPACITY = 256U;
      static constexpr bool ITEM_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return MissionPlan__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return MissionPlan__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return MissionPlan__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return MissionPlan__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // A complete stored mission, published once after upload and on request.
    // 
    // TRANSPORT TIER: Cyphal/UDP ONLY. This message does not fit any CAN
    // frame and is not intended to.
    // 
    // It exists in this showroom as the counterweight to EscStatus.1.0: the
    // same compiler, the same language, and the same generated-code surface,
    // but sized for a transport with a 64 KiB datagram rather than a 7-byte
    // frame. On CAN this would fragment into hundreds of frames and
    // monopolize the bus; on UDP it is one unremarkable datagram. The
    // transport, not the compiler, is what makes a definition of this shape
    // reasonable or not.
    // 
    // On a mixed vehicle the CAN nodes never subscribe to this subject. They
    // receive mission items one chunk at a time through the UploadMission
    // service instead.
    struct MissionPlan {
      // The moment this plan was committed to storage.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // Monotonic identifier of the stored plan, incremented on every
      // successful upload. A consumer uses it to detect that the plan changed
      // without re-reading the item list.
      std::uint32_t plan_id{};
      // Index into `item` of the waypoint the vehicle is currently navigating
      // toward.
      std::uint16_t current_item{};
      // The mission items in execution order.
      // 
      // Each element is a delimited composite and therefore carries its own
      // four-byte length header. Note that the bound is the waypoint's 64-byte
      // *extent*, not the 36 bytes a 1.0 waypoint actually occupies: a reader
      // must be prepared for any 1.x waypoint, and the extent is what states
      // how large that can be. So the worst case is 256 * (64 + 4) plus the
      // array's own length prefix, which puts this message a little over 17
      // KiB -- roughly 2500 classic-CAN frames, and one UDP datagram.
      std::pmr::vector<::lanyard::nav::Waypoint> item{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      MissionPlan() = default;
      explicit MissionPlan(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
        item = decltype(item)(_memory_resource);
        timestamp.set_memory_resource(_memory_resource);
        for (std::size_t item_index = 0U; item_index < item.size(); ++item_index) {
          item[item_index].set_memory_resource(_memory_resource);
        }
      }
      static constexpr const char* FULL_NAME = "lanyard.nav.MissionPlan";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.nav.MissionPlan.1.0";
      static constexpr std::size_t EXTENT_BYTES = 24576U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 17423U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      static constexpr std::size_t ITEM_ARRAY_CAPACITY = 256U;
      static constexpr bool ITEM_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return MissionPlan__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return MissionPlan__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return MissionPlan__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return MissionPlan__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return MissionPlan__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return MissionPlan__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // A complete stored mission, published once after upload and on request.
    // 
    // TRANSPORT TIER: Cyphal/UDP ONLY. This message does not fit any CAN
    // frame and is not intended to.
    // 
    // It exists in this showroom as the counterweight to EscStatus.1.0: the
    // same compiler, the same language, and the same generated-code surface,
    // but sized for a transport with a 64 KiB datagram rather than a 7-byte
    // frame. On CAN this would fragment into hundreds of frames and
    // monopolize the bus; on UDP it is one unremarkable datagram. The
    // transport, not the compiler, is what makes a definition of this shape
    // reasonable or not.
    // 
    // On a mixed vehicle the CAN nodes never subscribe to this subject. They
    // receive mission items one chunk at a time through the UploadMission
    // service instead.
    struct MissionPlan {
      // The moment this plan was committed to storage.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // Monotonic identifier of the stored plan, incremented on every
      // successful upload. A consumer uses it to detect that the plan changed
      // without re-reading the item list.
      std::uint32_t plan_id{};
      // Index into `item` of the waypoint the vehicle is currently navigating
      // toward.
      std::uint16_t current_item{};
      // The mission items in execution order.
      // 
      // Each element is a delimited composite and therefore carries its own
      // four-byte length header. Note that the bound is the waypoint's 64-byte
      // *extent*, not the 36 bytes a 1.0 waypoint actually occupies: a reader
      // must be prepared for any 1.x waypoint, and the extent is what states
      // how large that can be. So the worst case is 256 * (64 + 4) plus the
      // array's own length prefix, which puts this message a little over 17
      // KiB -- roughly 2500 classic-CAN frames, and one UDP datagram.
      ::llvmdsdl::cpp::autosar::BoundedVector<::lanyard::nav::Waypoint, 256U> item{};
      static constexpr const char* FULL_NAME = "lanyard.nav.MissionPlan";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.nav.MissionPlan.1.0";
      static constexpr std::size_t EXTENT_BYTES = 24576U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 17423U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      static constexpr std::size_t ITEM_ARRAY_CAPACITY = 256U;
      static constexpr bool ITEM_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return MissionPlan__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return MissionPlan__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return MissionPlan__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return MissionPlan__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// A complete stored mission, published once after upload and on request.
    /// 
    /// TRANSPORT TIER: Cyphal/UDP ONLY. This message does not fit any CAN
    /// frame and is not intended to.
    /// 
    /// It exists in this showroom as the counterweight to EscStatus.1.0: the
    /// same compiler, the same language, and the same generated-code surface,
    /// but sized for a transport with a 64 KiB datagram rather than a 7-byte
    /// frame. On CAN this would fragment into hundreds of frames and
    /// monopolize the bus; on UDP it is one unremarkable datagram. The
    /// transport, not the compiler, is what makes a definition of this shape
    /// reasonable or not.
    /// 
    /// On a mixed vehicle the CAN nodes never subscribe to this subject. They
    /// receive mission items one chunk at a time through the UploadMission
    /// service instead.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_nav_MissionPlan_1_0 {
        /// The moment this plan was committed to storage.
        pub timestamp: uavcan_time_SynchronizedTimestamp_1_0,
        /// Monotonic identifier of the stored plan, incremented on every
        /// successful upload. A consumer uses it to detect that the plan changed
        /// without re-reading the item list.
        pub plan_id: u32,
        /// Index into `item` of the waypoint the vehicle is currently navigating
        /// toward.
        pub current_item: u16,
        /// The mission items in execution order.
        /// 
        /// Each element is a delimited composite and therefore carries its own
        /// four-byte length header. Note that the bound is the waypoint's 64-byte
        /// *extent*, not the 36 bytes a 1.0 waypoint actually occupies: a reader
        /// must be prepared for any 1.x waypoint, and the extent is what states
        /// how large that can be. So the worst case is 256 * (64 + 4) plus the
        /// array's own length prefix, which puts this message a little over 17
        /// KiB -- roughly 2500 classic-CAN frames, and one UDP datagram.
        pub item: crate::dsdl_runtime::DsdlVec<lanyard_nav_Waypoint_1_0>,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// A complete stored mission, published once after upload and on request.
    /// 
    /// TRANSPORT TIER: Cyphal/UDP ONLY. This message does not fit any CAN
    /// frame and is not intended to.
    /// 
    /// It exists in this showroom as the counterweight to EscStatus.1.0: the
    /// same compiler, the same language, and the same generated-code surface,
    /// but sized for a transport with a 64 KiB datagram rather than a 7-byte
    /// frame. On CAN this would fragment into hundreds of frames and
    /// monopolize the bus; on UDP it is one unremarkable datagram. The
    /// transport, not the compiler, is what makes a definition of this shape
    /// reasonable or not.
    /// 
    /// On a mixed vehicle the CAN nodes never subscribe to this subject. They
    /// receive mission items one chunk at a time through the UploadMission
    /// service instead.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_nav_MissionPlan_1_0 {
        /// The moment this plan was committed to storage.
        pub timestamp: uavcan_time_SynchronizedTimestamp_1_0,
        /// Monotonic identifier of the stored plan, incremented on every
        /// successful upload. A consumer uses it to detect that the plan changed
        /// without re-reading the item list.
        pub plan_id: u32,
        /// Index into `item` of the waypoint the vehicle is currently navigating
        /// toward.
        pub current_item: u16,
        /// The mission items in execution order.
        /// 
        /// Each element is a delimited composite and therefore carries its own
        /// four-byte length header. Note that the bound is the waypoint's 64-byte
        /// *extent*, not the 36 bytes a 1.0 waypoint actually occupies: a reader
        /// must be prepared for any 1.x waypoint, and the extent is what states
        /// how large that can be. So the worst case is 256 * (64 + 4) plus the
        /// array's own length prefix, which puts this message a little over 17
        /// KiB -- roughly 2500 classic-CAN frames, and one UDP datagram.
        pub item: crate::dsdl_runtime::DsdlVec<lanyard_nav_Waypoint_1_0>,
    }

    ```

=== "Go"

    ```go
    // A complete stored mission, published once after upload and on request.
    // 
    // TRANSPORT TIER: Cyphal/UDP ONLY. This message does not fit any CAN
    // frame and is not intended to.
    // 
    // It exists in this showroom as the counterweight to EscStatus.1.0: the
    // same compiler, the same language, and the same generated-code surface,
    // but sized for a transport with a 64 KiB datagram rather than a 7-byte
    // frame. On CAN this would fragment into hundreds of frames and
    // monopolize the bus; on UDP it is one unremarkable datagram. The
    // transport, not the compiler, is what makes a definition of this shape
    // reasonable or not.
    // 
    // On a mixed vehicle the CAN nodes never subscribe to this subject. They
    // receive mission items one chunk at a time through the UploadMission
    // service instead.
    type MissionPlan_1_0 struct {
      // The moment this plan was committed to storage.
      Timestamp pkg_uavcan_time.SynchronizedTimestamp_1_0
      // Monotonic identifier of the stored plan, incremented on every
      // successful upload. A consumer uses it to detect that the plan changed
      // without re-reading the item list.
      PlanId uint32
      // Index into `item` of the waypoint the vehicle is currently navigating
      // toward.
      CurrentItem uint16
      // The mission items in execution order.
      // 
      // Each element is a delimited composite and therefore carries its own
      // four-byte length header. Note that the bound is the waypoint's 64-byte
      // *extent*, not the 36 bytes a 1.0 waypoint actually occupies: a reader
      // must be prepared for any 1.x waypoint, and the extent is what states
      // how large that can be. So the worst case is 256 * (64 + 4) plus the
      // array's own length prefix, which puts this message a little over 17
      // KiB -- roughly 2500 classic-CAN frames, and one UDP datagram.
      Item []Waypoint_1_0
    }

    ```

=== "TypeScript"

    ```typescript
    // A complete stored mission, published once after upload and on request.
    // 
    // TRANSPORT TIER: Cyphal/UDP ONLY. This message does not fit any CAN
    // frame and is not intended to.
    // 
    // It exists in this showroom as the counterweight to EscStatus.1.0: the
    // same compiler, the same language, and the same generated-code surface,
    // but sized for a transport with a 64 KiB datagram rather than a 7-byte
    // frame. On CAN this would fragment into hundreds of frames and
    // monopolize the bus; on UDP it is one unremarkable datagram. The
    // transport, not the compiler, is what makes a definition of this shape
    // reasonable or not.
    // 
    // On a mixed vehicle the CAN nodes never subscribe to this subject. They
    // receive mission items one chunk at a time through the UploadMission
    // service instead.
    export interface MissionPlan_1_0 {
      // The moment this plan was committed to storage.
      timestamp: SynchronizedTimestamp_1_0;
      // Monotonic identifier of the stored plan, incremented on every
      // successful upload. A consumer uses it to detect that the plan changed
      // without re-reading the item list.
      plan_id: number;
      // Index into `item` of the waypoint the vehicle is currently navigating
      // toward.
      current_item: number;
      // The mission items in execution order.
      // 
      // Each element is a delimited composite and therefore carries its own
      // four-byte length header. Note that the bound is the waypoint's 64-byte
      // *extent*, not the 36 bytes a 1.0 waypoint actually occupies: a reader
      // must be prepared for any 1.x waypoint, and the extent is what states
      // how large that can be. So the worst case is 256 * (64 + 4) plus the
      // array's own length prefix, which puts this message a little over 17
      // KiB -- roughly 2500 classic-CAN frames, and one UDP datagram.
      item: Array<Waypoint_1_0>;
    }

    ```

=== "Python"

    ```python
    # A complete stored mission, published once after upload and on request.
    # 
    # TRANSPORT TIER: Cyphal/UDP ONLY. This message does not fit any CAN
    # frame and is not intended to.
    # 
    # It exists in this showroom as the counterweight to EscStatus.1.0: the
    # same compiler, the same language, and the same generated-code surface,
    # but sized for a transport with a 64 KiB datagram rather than a 7-byte
    # frame. On CAN this would fragment into hundreds of frames and
    # monopolize the bus; on UDP it is one unremarkable datagram. The
    # transport, not the compiler, is what makes a definition of this shape
    # reasonable or not.
    # 
    # On a mixed vehicle the CAN nodes never subscribe to this subject. They
    # receive mission items one chunk at a time through the UploadMission
    # service instead.
    @dataclass(slots=True)
    class MissionPlan_1_0:
        # The moment this plan was committed to storage.
        timestamp: SynchronizedTimestamp_1_0 = field(default_factory=SynchronizedTimestamp_1_0)
        # Monotonic identifier of the stored plan, incremented on every
        # successful upload. A consumer uses it to detect that the plan changed
        # without re-reading the item list.
        plan_id: int = 0
        # Index into `item` of the waypoint the vehicle is currently navigating
        # toward.
        current_item: int = 0
        # The mission items in execution order.
        # 
        # Each element is a delimited composite and therefore carries its own
        # four-byte length header. Note that the bound is the waypoint's 64-byte
        # *extent*, not the 36 bytes a 1.0 waypoint actually occupies: a reader
        # must be prepared for any 1.x waypoint, and the extent is what states
        # how large that can be. So the worst case is 256 * (64 + 4) plus the
        # array's own length prefix, which puts this message a little over 17
        # KiB -- roughly 2500 classic-CAN frames, and one UDP datagram.
        item: list[Waypoint_1_0] = field(default_factory=list)

    ```
