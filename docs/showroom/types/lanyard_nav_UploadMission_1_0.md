# lanyard.nav.UploadMission.1.0

Upload a mission to the vehicle one chunk at a time.

| | |
|---|---|
| Full name | `lanyard.nav.UploadMission` |
| Version | 1.0 |
| Kind | Service |
| Fixed port ID | 256 |
| Transport tier | CAN FD and Cyphal/UDP |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Request | 640 | 548 |
| Response | 128 | 69 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# Upload a mission to the vehicle one chunk at a time.
#
# TRANSPORT TIER: CAN FD and Cyphal/UDP.
#
# WHY A SERVICE: mission upload needs an acknowledgement per chunk so
# that the ground station can retry a lost chunk rather than restarting
# the transfer. A service definition carries a request and a response in
# one file, separated by the `---` marker, and the two sections are
# independent: they have their own fields, their own documentation, and
# -- as here -- their own sealing decision.
#
# Chunking exists so that this service can be flown over CAN FD as well
# as UDP. The MissionPlan message carries an entire plan at once and is
# UDP-only; this service carries the same content in pieces small enough
# for either transport, at the cost of a round trip per chunk.

uint16 first_item_index
# Index within the destination plan at which `item` should be written.
# The ground station uploads ascending chunks; the vehicle rejects a
# chunk that does not begin where the previous one ended.

bool is_last
# True on the final chunk. The vehicle commits the plan to storage and
# increments its plan identifier only when a chunk with this flag is
# accepted.

void7
# Padding to a byte boundary.

lanyard.nav.Waypoint.1.0[<=8] item
# The mission items in this chunk. Bounded at eight so that a chunk
# stays within a short CAN FD multi-frame transfer: 548 bytes worst
# case, against a little over 17 KiB for a whole plan.

@extent 640 * 8
# Eight waypoints at the 64-byte waypoint extent plus a four-byte
# delimiter header each, plus the array length prefix and the leading
# fields. The extent must cover the worst case, not the typical one: the
# compiler rejects an extent smaller than the maximal serialized length.

---

bool accepted
# True when the chunk was written to the staging buffer.

void7
# Padding to a byte boundary.

uint16 next_expected_index
# The index the vehicle expects the next chunk to begin at. On rejection
# this tells the ground station where to resume rather than forcing it
# to restart the upload.

uint8 error
# Reason for rejection; zero when `accepted` is true.

uint8 ERROR_NONE = 0
# No error.

uint8 ERROR_OUT_OF_SEQUENCE = 1
# first_item_index did not match next_expected_index.

uint8 ERROR_CAPACITY = 2
# The plan would exceed the vehicle's stored mission capacity.

uint8 ERROR_INVALID_ITEM = 3
# A waypoint in the chunk failed validation, for example an altitude
# outside the configured envelope.

uint8 ERROR_BUSY = 4
# The vehicle is armed or otherwise unwilling to accept a mission change
# right now.

uint8[<=64] detail
# Optional human-readable elaboration on `error`, UTF-8, for display in
# the ground station log. The response section is delimited so that this
# field and future diagnostics may grow independently of the request
# section, which is size-critical.

@extent 128 * 8
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* Upload a mission to the vehicle one chunk at a time. */
    /*  */
    /* TRANSPORT TIER: CAN FD and Cyphal/UDP. */
    /*  */
    /* WHY A SERVICE: mission upload needs an acknowledgement per chunk so */
    /* that the ground station can retry a lost chunk rather than restarting */
    /* the transfer. A service definition carries a request and a response in */
    /* one file, separated by the `---` marker, and the two sections are */
    /* independent: they have their own fields, their own documentation, and */
    /* -- as here -- their own sealing decision. */
    /*  */
    /* Chunking exists so that this service can be flown over CAN FD as well */
    /* as UDP. The MissionPlan message carries an entire plan at once and is */
    /* UDP-only; this service carries the same content in pieces small enough */
    /* for either transport, at the cost of a round trip per chunk. */
    typedef struct lanyard__nav__UploadMission__Request {
      /* Index within the destination plan at which `item` should be written. */
      /* The ground station uploads ascending chunks; the vehicle rejects a */
      /* chunk that does not begin where the previous one ended. */
      uint16_t first_item_index;
      /* True on the final chunk. The vehicle commits the plan to storage and */
      /* increments its plan identifier only when a chunk with this flag is */
      /* accepted. */
      bool is_last;
      /* The mission items in this chunk. Bounded at eight so that a chunk */
      /* stays within a short CAN FD multi-frame transfer: 548 bytes worst */
      /* case, against a little over 17 KiB for a whole plan. */
      struct {
        lanyard__nav__Waypoint elements[8U];
        size_t count;
      } item;
    } lanyard__nav__UploadMission__Request;

    /* Upload a mission to the vehicle one chunk at a time. */
    /*  */
    /* TRANSPORT TIER: CAN FD and Cyphal/UDP. */
    /*  */
    /* WHY A SERVICE: mission upload needs an acknowledgement per chunk so */
    /* that the ground station can retry a lost chunk rather than restarting */
    /* the transfer. A service definition carries a request and a response in */
    /* one file, separated by the `---` marker, and the two sections are */
    /* independent: they have their own fields, their own documentation, and */
    /* -- as here -- their own sealing decision. */
    /*  */
    /* Chunking exists so that this service can be flown over CAN FD as well */
    /* as UDP. The MissionPlan message carries an entire plan at once and is */
    /* UDP-only; this service carries the same content in pieces small enough */
    /* for either transport, at the cost of a round trip per chunk. */
    typedef struct lanyard__nav__UploadMission__Response {
      /* True when the chunk was written to the staging buffer. */
      bool accepted;
      /* The index the vehicle expects the next chunk to begin at. On rejection */
      /* this tells the ground station where to resume rather than forcing it */
      /* to restart the upload. */
      uint16_t next_expected_index;
      /* Reason for rejection; zero when `accepted` is true. */
      uint8_t error;
      /* Optional human-readable elaboration on `error`, UTF-8, for display in */
      /* the ground station log. The response section is delimited so that this */
      /* field and future diagnostics may grow independently of the request */
      /* section, which is size-critical. */
      struct {
        uint8_t elements[64U];
        size_t count;
      } detail;
    } lanyard__nav__UploadMission__Response;

    ```

=== "C++ (std)"

    ```cpp
    // Upload a mission to the vehicle one chunk at a time.
    // 
    // TRANSPORT TIER: CAN FD and Cyphal/UDP.
    // 
    // WHY A SERVICE: mission upload needs an acknowledgement per chunk so
    // that the ground station can retry a lost chunk rather than restarting
    // the transfer. A service definition carries a request and a response in
    // one file, separated by the `---` marker, and the two sections are
    // independent: they have their own fields, their own documentation, and
    // -- as here -- their own sealing decision.
    // 
    // Chunking exists so that this service can be flown over CAN FD as well
    // as UDP. The MissionPlan message carries an entire plan at once and is
    // UDP-only; this service carries the same content in pieces small enough
    // for either transport, at the cost of a round trip per chunk.
    struct UploadMission__Request {
      // Index within the destination plan at which `item` should be written.
      // The ground station uploads ascending chunks; the vehicle rejects a
      // chunk that does not begin where the previous one ended.
      std::uint16_t first_item_index{};
      // True on the final chunk. The vehicle commits the plan to storage and
      // increments its plan identifier only when a chunk with this flag is
      // accepted.
      bool is_last{};
      // The mission items in this chunk. Bounded at eight so that a chunk
      // stays within a short CAN FD multi-frame transfer: 548 bytes worst
      // case, against a little over 17 KiB for a whole plan.
      std::vector<::lanyard::nav::Waypoint> item{};
      static constexpr const char* FULL_NAME = "lanyard.nav.UploadMission.Request";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.nav.UploadMission.Request.1.0";
      static constexpr std::size_t EXTENT_BYTES = 640U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 548U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      static constexpr std::size_t ITEM_ARRAY_CAPACITY = 8U;
      static constexpr bool ITEM_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return UploadMission__Request__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return UploadMission__Request__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return UploadMission__Request__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return UploadMission__Request__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    // Upload a mission to the vehicle one chunk at a time.
    // 
    // TRANSPORT TIER: CAN FD and Cyphal/UDP.
    // 
    // WHY A SERVICE: mission upload needs an acknowledgement per chunk so
    // that the ground station can retry a lost chunk rather than restarting
    // the transfer. A service definition carries a request and a response in
    // one file, separated by the `---` marker, and the two sections are
    // independent: they have their own fields, their own documentation, and
    // -- as here -- their own sealing decision.
    // 
    // Chunking exists so that this service can be flown over CAN FD as well
    // as UDP. The MissionPlan message carries an entire plan at once and is
    // UDP-only; this service carries the same content in pieces small enough
    // for either transport, at the cost of a round trip per chunk.
    struct UploadMission__Response {
      // True when the chunk was written to the staging buffer.
      bool accepted{};
      // The index the vehicle expects the next chunk to begin at. On rejection
      // this tells the ground station where to resume rather than forcing it
      // to restart the upload.
      std::uint16_t next_expected_index{};
      // Reason for rejection; zero when `accepted` is true.
      std::uint8_t error{};
      // Optional human-readable elaboration on `error`, UTF-8, for display in
      // the ground station log. The response section is delimited so that this
      // field and future diagnostics may grow independently of the request
      // section, which is size-critical.
      std::vector<std::uint8_t> detail{};
      static constexpr const char* FULL_NAME = "lanyard.nav.UploadMission.Response";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.nav.UploadMission.Response.1.0";
      static constexpr std::size_t EXTENT_BYTES = 128U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 69U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      // No error.
      static constexpr auto ERROR_NONE = 0;
      // first_item_index did not match next_expected_index.
      static constexpr auto ERROR_OUT_OF_SEQUENCE = 1;
      // The plan would exceed the vehicle's stored mission capacity.
      static constexpr auto ERROR_CAPACITY = 2;
      // A waypoint in the chunk failed validation, for example an altitude
      // outside the configured envelope.
      static constexpr auto ERROR_INVALID_ITEM = 3;
      // The vehicle is armed or otherwise unwilling to accept a mission change
      // right now.
      static constexpr auto ERROR_BUSY = 4;
      static constexpr std::size_t DETAIL_ARRAY_CAPACITY = 64U;
      static constexpr bool DETAIL_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return UploadMission__Response__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return UploadMission__Response__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return UploadMission__Response__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return UploadMission__Response__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // Upload a mission to the vehicle one chunk at a time.
    // 
    // TRANSPORT TIER: CAN FD and Cyphal/UDP.
    // 
    // WHY A SERVICE: mission upload needs an acknowledgement per chunk so
    // that the ground station can retry a lost chunk rather than restarting
    // the transfer. A service definition carries a request and a response in
    // one file, separated by the `---` marker, and the two sections are
    // independent: they have their own fields, their own documentation, and
    // -- as here -- their own sealing decision.
    // 
    // Chunking exists so that this service can be flown over CAN FD as well
    // as UDP. The MissionPlan message carries an entire plan at once and is
    // UDP-only; this service carries the same content in pieces small enough
    // for either transport, at the cost of a round trip per chunk.
    struct UploadMission__Request {
      // Index within the destination plan at which `item` should be written.
      // The ground station uploads ascending chunks; the vehicle rejects a
      // chunk that does not begin where the previous one ended.
      std::uint16_t first_item_index{};
      // True on the final chunk. The vehicle commits the plan to storage and
      // increments its plan identifier only when a chunk with this flag is
      // accepted.
      bool is_last{};
      // The mission items in this chunk. Bounded at eight so that a chunk
      // stays within a short CAN FD multi-frame transfer: 548 bytes worst
      // case, against a little over 17 KiB for a whole plan.
      std::pmr::vector<::lanyard::nav::Waypoint> item{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      UploadMission__Request() = default;
      explicit UploadMission__Request(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
        item = decltype(item)(_memory_resource);
        for (std::size_t item_index = 0U; item_index < item.size(); ++item_index) {
          item[item_index].set_memory_resource(_memory_resource);
        }
      }
      static constexpr const char* FULL_NAME = "lanyard.nav.UploadMission.Request";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.nav.UploadMission.Request.1.0";
      static constexpr std::size_t EXTENT_BYTES = 640U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 548U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      static constexpr std::size_t ITEM_ARRAY_CAPACITY = 8U;
      static constexpr bool ITEM_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return UploadMission__Request__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return UploadMission__Request__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return UploadMission__Request__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return UploadMission__Request__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return UploadMission__Request__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return UploadMission__Request__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    // Upload a mission to the vehicle one chunk at a time.
    // 
    // TRANSPORT TIER: CAN FD and Cyphal/UDP.
    // 
    // WHY A SERVICE: mission upload needs an acknowledgement per chunk so
    // that the ground station can retry a lost chunk rather than restarting
    // the transfer. A service definition carries a request and a response in
    // one file, separated by the `---` marker, and the two sections are
    // independent: they have their own fields, their own documentation, and
    // -- as here -- their own sealing decision.
    // 
    // Chunking exists so that this service can be flown over CAN FD as well
    // as UDP. The MissionPlan message carries an entire plan at once and is
    // UDP-only; this service carries the same content in pieces small enough
    // for either transport, at the cost of a round trip per chunk.
    struct UploadMission__Response {
      // True when the chunk was written to the staging buffer.
      bool accepted{};
      // The index the vehicle expects the next chunk to begin at. On rejection
      // this tells the ground station where to resume rather than forcing it
      // to restart the upload.
      std::uint16_t next_expected_index{};
      // Reason for rejection; zero when `accepted` is true.
      std::uint8_t error{};
      // Optional human-readable elaboration on `error`, UTF-8, for display in
      // the ground station log. The response section is delimited so that this
      // field and future diagnostics may grow independently of the request
      // section, which is size-critical.
      std::pmr::vector<std::uint8_t> detail{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      UploadMission__Response() = default;
      explicit UploadMission__Response(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
        detail = decltype(detail)(_memory_resource);
      }
      static constexpr const char* FULL_NAME = "lanyard.nav.UploadMission.Response";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.nav.UploadMission.Response.1.0";
      static constexpr std::size_t EXTENT_BYTES = 128U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 69U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      // No error.
      static constexpr auto ERROR_NONE = 0;
      // first_item_index did not match next_expected_index.
      static constexpr auto ERROR_OUT_OF_SEQUENCE = 1;
      // The plan would exceed the vehicle's stored mission capacity.
      static constexpr auto ERROR_CAPACITY = 2;
      // A waypoint in the chunk failed validation, for example an altitude
      // outside the configured envelope.
      static constexpr auto ERROR_INVALID_ITEM = 3;
      // The vehicle is armed or otherwise unwilling to accept a mission change
      // right now.
      static constexpr auto ERROR_BUSY = 4;
      static constexpr std::size_t DETAIL_ARRAY_CAPACITY = 64U;
      static constexpr bool DETAIL_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return UploadMission__Response__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return UploadMission__Response__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return UploadMission__Response__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return UploadMission__Response__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return UploadMission__Response__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return UploadMission__Response__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // Upload a mission to the vehicle one chunk at a time.
    // 
    // TRANSPORT TIER: CAN FD and Cyphal/UDP.
    // 
    // WHY A SERVICE: mission upload needs an acknowledgement per chunk so
    // that the ground station can retry a lost chunk rather than restarting
    // the transfer. A service definition carries a request and a response in
    // one file, separated by the `---` marker, and the two sections are
    // independent: they have their own fields, their own documentation, and
    // -- as here -- their own sealing decision.
    // 
    // Chunking exists so that this service can be flown over CAN FD as well
    // as UDP. The MissionPlan message carries an entire plan at once and is
    // UDP-only; this service carries the same content in pieces small enough
    // for either transport, at the cost of a round trip per chunk.
    struct UploadMission__Request {
      // Index within the destination plan at which `item` should be written.
      // The ground station uploads ascending chunks; the vehicle rejects a
      // chunk that does not begin where the previous one ended.
      std::uint16_t first_item_index{};
      // True on the final chunk. The vehicle commits the plan to storage and
      // increments its plan identifier only when a chunk with this flag is
      // accepted.
      bool is_last{};
      // The mission items in this chunk. Bounded at eight so that a chunk
      // stays within a short CAN FD multi-frame transfer: 548 bytes worst
      // case, against a little over 17 KiB for a whole plan.
      ::llvmdsdl::cpp::autosar::BoundedVector<::lanyard::nav::Waypoint, 8U> item{};
      static constexpr const char* FULL_NAME = "lanyard.nav.UploadMission.Request";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.nav.UploadMission.Request.1.0";
      static constexpr std::size_t EXTENT_BYTES = 640U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 548U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      static constexpr std::size_t ITEM_ARRAY_CAPACITY = 8U;
      static constexpr bool ITEM_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return UploadMission__Request__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return UploadMission__Request__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return UploadMission__Request__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return UploadMission__Request__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    // Upload a mission to the vehicle one chunk at a time.
    // 
    // TRANSPORT TIER: CAN FD and Cyphal/UDP.
    // 
    // WHY A SERVICE: mission upload needs an acknowledgement per chunk so
    // that the ground station can retry a lost chunk rather than restarting
    // the transfer. A service definition carries a request and a response in
    // one file, separated by the `---` marker, and the two sections are
    // independent: they have their own fields, their own documentation, and
    // -- as here -- their own sealing decision.
    // 
    // Chunking exists so that this service can be flown over CAN FD as well
    // as UDP. The MissionPlan message carries an entire plan at once and is
    // UDP-only; this service carries the same content in pieces small enough
    // for either transport, at the cost of a round trip per chunk.
    struct UploadMission__Response {
      // True when the chunk was written to the staging buffer.
      bool accepted{};
      // The index the vehicle expects the next chunk to begin at. On rejection
      // this tells the ground station where to resume rather than forcing it
      // to restart the upload.
      std::uint16_t next_expected_index{};
      // Reason for rejection; zero when `accepted` is true.
      std::uint8_t error{};
      // Optional human-readable elaboration on `error`, UTF-8, for display in
      // the ground station log. The response section is delimited so that this
      // field and future diagnostics may grow independently of the request
      // section, which is size-critical.
      ::llvmdsdl::cpp::autosar::BoundedVector<std::uint8_t, 64U> detail{};
      static constexpr const char* FULL_NAME = "lanyard.nav.UploadMission.Response";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.nav.UploadMission.Response.1.0";
      static constexpr std::size_t EXTENT_BYTES = 128U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 69U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      // No error.
      static constexpr auto ERROR_NONE = 0;
      // first_item_index did not match next_expected_index.
      static constexpr auto ERROR_OUT_OF_SEQUENCE = 1;
      // The plan would exceed the vehicle's stored mission capacity.
      static constexpr auto ERROR_CAPACITY = 2;
      // A waypoint in the chunk failed validation, for example an altitude
      // outside the configured envelope.
      static constexpr auto ERROR_INVALID_ITEM = 3;
      // The vehicle is armed or otherwise unwilling to accept a mission change
      // right now.
      static constexpr auto ERROR_BUSY = 4;
      static constexpr std::size_t DETAIL_ARRAY_CAPACITY = 64U;
      static constexpr bool DETAIL_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return UploadMission__Response__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return UploadMission__Response__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return UploadMission__Response__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return UploadMission__Response__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// Upload a mission to the vehicle one chunk at a time.
    /// 
    /// TRANSPORT TIER: CAN FD and Cyphal/UDP.
    /// 
    /// WHY A SERVICE: mission upload needs an acknowledgement per chunk so
    /// that the ground station can retry a lost chunk rather than restarting
    /// the transfer. A service definition carries a request and a response in
    /// one file, separated by the `---` marker, and the two sections are
    /// independent: they have their own fields, their own documentation, and
    /// -- as here -- their own sealing decision.
    /// 
    /// Chunking exists so that this service can be flown over CAN FD as well
    /// as UDP. The MissionPlan message carries an entire plan at once and is
    /// UDP-only; this service carries the same content in pieces small enough
    /// for either transport, at the cost of a round trip per chunk.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_nav_UploadMission_1_0_Request {
        /// Index within the destination plan at which `item` should be written.
        /// The ground station uploads ascending chunks; the vehicle rejects a
        /// chunk that does not begin where the previous one ended.
        pub first_item_index: u16,
        /// True on the final chunk. The vehicle commits the plan to storage and
        /// increments its plan identifier only when a chunk with this flag is
        /// accepted.
        pub is_last: bool,
        /// The mission items in this chunk. Bounded at eight so that a chunk
        /// stays within a short CAN FD multi-frame transfer: 548 bytes worst
        /// case, against a little over 17 KiB for a whole plan.
        pub item: crate::dsdl_runtime::DsdlVec<lanyard_nav_Waypoint_1_0>,
    }

    /// Upload a mission to the vehicle one chunk at a time.
    /// 
    /// TRANSPORT TIER: CAN FD and Cyphal/UDP.
    /// 
    /// WHY A SERVICE: mission upload needs an acknowledgement per chunk so
    /// that the ground station can retry a lost chunk rather than restarting
    /// the transfer. A service definition carries a request and a response in
    /// one file, separated by the `---` marker, and the two sections are
    /// independent: they have their own fields, their own documentation, and
    /// -- as here -- their own sealing decision.
    /// 
    /// Chunking exists so that this service can be flown over CAN FD as well
    /// as UDP. The MissionPlan message carries an entire plan at once and is
    /// UDP-only; this service carries the same content in pieces small enough
    /// for either transport, at the cost of a round trip per chunk.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_nav_UploadMission_1_0_Response {
        /// True when the chunk was written to the staging buffer.
        pub accepted: bool,
        /// The index the vehicle expects the next chunk to begin at. On rejection
        /// this tells the ground station where to resume rather than forcing it
        /// to restart the upload.
        pub next_expected_index: u16,
        /// Reason for rejection; zero when `accepted` is true.
        pub error: u8,
        /// Optional human-readable elaboration on `error`, UTF-8, for display in
        /// the ground station log. The response section is delimited so that this
        /// field and future diagnostics may grow independently of the request
        /// section, which is size-critical.
        pub detail: crate::dsdl_runtime::DsdlVec<u8>,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// Upload a mission to the vehicle one chunk at a time.
    /// 
    /// TRANSPORT TIER: CAN FD and Cyphal/UDP.
    /// 
    /// WHY A SERVICE: mission upload needs an acknowledgement per chunk so
    /// that the ground station can retry a lost chunk rather than restarting
    /// the transfer. A service definition carries a request and a response in
    /// one file, separated by the `---` marker, and the two sections are
    /// independent: they have their own fields, their own documentation, and
    /// -- as here -- their own sealing decision.
    /// 
    /// Chunking exists so that this service can be flown over CAN FD as well
    /// as UDP. The MissionPlan message carries an entire plan at once and is
    /// UDP-only; this service carries the same content in pieces small enough
    /// for either transport, at the cost of a round trip per chunk.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_nav_UploadMission_1_0_Request {
        /// Index within the destination plan at which `item` should be written.
        /// The ground station uploads ascending chunks; the vehicle rejects a
        /// chunk that does not begin where the previous one ended.
        pub first_item_index: u16,
        /// True on the final chunk. The vehicle commits the plan to storage and
        /// increments its plan identifier only when a chunk with this flag is
        /// accepted.
        pub is_last: bool,
        /// The mission items in this chunk. Bounded at eight so that a chunk
        /// stays within a short CAN FD multi-frame transfer: 548 bytes worst
        /// case, against a little over 17 KiB for a whole plan.
        pub item: crate::dsdl_runtime::DsdlVec<lanyard_nav_Waypoint_1_0>,
    }

    /// Upload a mission to the vehicle one chunk at a time.
    /// 
    /// TRANSPORT TIER: CAN FD and Cyphal/UDP.
    /// 
    /// WHY A SERVICE: mission upload needs an acknowledgement per chunk so
    /// that the ground station can retry a lost chunk rather than restarting
    /// the transfer. A service definition carries a request and a response in
    /// one file, separated by the `---` marker, and the two sections are
    /// independent: they have their own fields, their own documentation, and
    /// -- as here -- their own sealing decision.
    /// 
    /// Chunking exists so that this service can be flown over CAN FD as well
    /// as UDP. The MissionPlan message carries an entire plan at once and is
    /// UDP-only; this service carries the same content in pieces small enough
    /// for either transport, at the cost of a round trip per chunk.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_nav_UploadMission_1_0_Response {
        /// True when the chunk was written to the staging buffer.
        pub accepted: bool,
        /// The index the vehicle expects the next chunk to begin at. On rejection
        /// this tells the ground station where to resume rather than forcing it
        /// to restart the upload.
        pub next_expected_index: u16,
        /// Reason for rejection; zero when `accepted` is true.
        pub error: u8,
        /// Optional human-readable elaboration on `error`, UTF-8, for display in
        /// the ground station log. The response section is delimited so that this
        /// field and future diagnostics may grow independently of the request
        /// section, which is size-critical.
        pub detail: crate::dsdl_runtime::DsdlVec<u8>,
    }

    ```

=== "Go"

    ```go
    // Upload a mission to the vehicle one chunk at a time.
    // 
    // TRANSPORT TIER: CAN FD and Cyphal/UDP.
    // 
    // WHY A SERVICE: mission upload needs an acknowledgement per chunk so
    // that the ground station can retry a lost chunk rather than restarting
    // the transfer. A service definition carries a request and a response in
    // one file, separated by the `---` marker, and the two sections are
    // independent: they have their own fields, their own documentation, and
    // -- as here -- their own sealing decision.
    // 
    // Chunking exists so that this service can be flown over CAN FD as well
    // as UDP. The MissionPlan message carries an entire plan at once and is
    // UDP-only; this service carries the same content in pieces small enough
    // for either transport, at the cost of a round trip per chunk.
    type UploadMission_1_0_Request struct {
      // Index within the destination plan at which `item` should be written.
      // The ground station uploads ascending chunks; the vehicle rejects a
      // chunk that does not begin where the previous one ended.
      FirstItemIndex uint16
      // True on the final chunk. The vehicle commits the plan to storage and
      // increments its plan identifier only when a chunk with this flag is
      // accepted.
      IsLast bool
      // The mission items in this chunk. Bounded at eight so that a chunk
      // stays within a short CAN FD multi-frame transfer: 548 bytes worst
      // case, against a little over 17 KiB for a whole plan.
      Item []Waypoint_1_0
    }

    // Upload a mission to the vehicle one chunk at a time.
    // 
    // TRANSPORT TIER: CAN FD and Cyphal/UDP.
    // 
    // WHY A SERVICE: mission upload needs an acknowledgement per chunk so
    // that the ground station can retry a lost chunk rather than restarting
    // the transfer. A service definition carries a request and a response in
    // one file, separated by the `---` marker, and the two sections are
    // independent: they have their own fields, their own documentation, and
    // -- as here -- their own sealing decision.
    // 
    // Chunking exists so that this service can be flown over CAN FD as well
    // as UDP. The MissionPlan message carries an entire plan at once and is
    // UDP-only; this service carries the same content in pieces small enough
    // for either transport, at the cost of a round trip per chunk.
    type UploadMission_1_0_Response struct {
      // True when the chunk was written to the staging buffer.
      Accepted bool
      // The index the vehicle expects the next chunk to begin at. On rejection
      // this tells the ground station where to resume rather than forcing it
      // to restart the upload.
      NextExpectedIndex uint16
      // Reason for rejection; zero when `accepted` is true.
      Error uint8
      // Optional human-readable elaboration on `error`, UTF-8, for display in
      // the ground station log. The response section is delimited so that this
      // field and future diagnostics may grow independently of the request
      // section, which is size-critical.
      Detail []uint8
    }

    ```

=== "TypeScript"

    ```typescript
    // Upload a mission to the vehicle one chunk at a time.
    // 
    // TRANSPORT TIER: CAN FD and Cyphal/UDP.
    // 
    // WHY A SERVICE: mission upload needs an acknowledgement per chunk so
    // that the ground station can retry a lost chunk rather than restarting
    // the transfer. A service definition carries a request and a response in
    // one file, separated by the `---` marker, and the two sections are
    // independent: they have their own fields, their own documentation, and
    // -- as here -- their own sealing decision.
    // 
    // Chunking exists so that this service can be flown over CAN FD as well
    // as UDP. The MissionPlan message carries an entire plan at once and is
    // UDP-only; this service carries the same content in pieces small enough
    // for either transport, at the cost of a round trip per chunk.
    export interface UploadMission_1_0_Request {
      // Index within the destination plan at which `item` should be written.
      // The ground station uploads ascending chunks; the vehicle rejects a
      // chunk that does not begin where the previous one ended.
      first_item_index: number;
      // True on the final chunk. The vehicle commits the plan to storage and
      // increments its plan identifier only when a chunk with this flag is
      // accepted.
      is_last: boolean;
      // The mission items in this chunk. Bounded at eight so that a chunk
      // stays within a short CAN FD multi-frame transfer: 548 bytes worst
      // case, against a little over 17 KiB for a whole plan.
      item: Array<Waypoint_1_0>;
    }

    // Upload a mission to the vehicle one chunk at a time.
    // 
    // TRANSPORT TIER: CAN FD and Cyphal/UDP.
    // 
    // WHY A SERVICE: mission upload needs an acknowledgement per chunk so
    // that the ground station can retry a lost chunk rather than restarting
    // the transfer. A service definition carries a request and a response in
    // one file, separated by the `---` marker, and the two sections are
    // independent: they have their own fields, their own documentation, and
    // -- as here -- their own sealing decision.
    // 
    // Chunking exists so that this service can be flown over CAN FD as well
    // as UDP. The MissionPlan message carries an entire plan at once and is
    // UDP-only; this service carries the same content in pieces small enough
    // for either transport, at the cost of a round trip per chunk.
    export interface UploadMission_1_0_Response {
      // True when the chunk was written to the staging buffer.
      accepted: boolean;
      // The index the vehicle expects the next chunk to begin at. On rejection
      // this tells the ground station where to resume rather than forcing it
      // to restart the upload.
      next_expected_index: number;
      // Reason for rejection; zero when `accepted` is true.
      error: number;
      // Optional human-readable elaboration on `error`, UTF-8, for display in
      // the ground station log. The response section is delimited so that this
      // field and future diagnostics may grow independently of the request
      // section, which is size-critical.
      detail: Array<number>;
    }

    export type UploadMission_1_0 = UploadMission_1_0_Request;

    ```

=== "Python"

    ```python
    # Upload a mission to the vehicle one chunk at a time.
    # 
    # TRANSPORT TIER: CAN FD and Cyphal/UDP.
    # 
    # WHY A SERVICE: mission upload needs an acknowledgement per chunk so
    # that the ground station can retry a lost chunk rather than restarting
    # the transfer. A service definition carries a request and a response in
    # one file, separated by the `---` marker, and the two sections are
    # independent: they have their own fields, their own documentation, and
    # -- as here -- their own sealing decision.
    # 
    # Chunking exists so that this service can be flown over CAN FD as well
    # as UDP. The MissionPlan message carries an entire plan at once and is
    # UDP-only; this service carries the same content in pieces small enough
    # for either transport, at the cost of a round trip per chunk.
    @dataclass(slots=True)
    class UploadMission_1_0_Request:
        # Index within the destination plan at which `item` should be written.
        # The ground station uploads ascending chunks; the vehicle rejects a
        # chunk that does not begin where the previous one ended.
        first_item_index: int = 0
        # True on the final chunk. The vehicle commits the plan to storage and
        # increments its plan identifier only when a chunk with this flag is
        # accepted.
        is_last: bool = False
        # The mission items in this chunk. Bounded at eight so that a chunk
        # stays within a short CAN FD multi-frame transfer: 548 bytes worst
        # case, against a little over 17 KiB for a whole plan.
        item: list[Waypoint_1_0] = field(default_factory=list)

    # Upload a mission to the vehicle one chunk at a time.
    # 
    # TRANSPORT TIER: CAN FD and Cyphal/UDP.
    # 
    # WHY A SERVICE: mission upload needs an acknowledgement per chunk so
    # that the ground station can retry a lost chunk rather than restarting
    # the transfer. A service definition carries a request and a response in
    # one file, separated by the `---` marker, and the two sections are
    # independent: they have their own fields, their own documentation, and
    # -- as here -- their own sealing decision.
    # 
    # Chunking exists so that this service can be flown over CAN FD as well
    # as UDP. The MissionPlan message carries an entire plan at once and is
    # UDP-only; this service carries the same content in pieces small enough
    # for either transport, at the cost of a round trip per chunk.
    @dataclass(slots=True)
    class UploadMission_1_0_Response:
        # True when the chunk was written to the staging buffer.
        accepted: bool = False
        # The index the vehicle expects the next chunk to begin at. On rejection
        # this tells the ground station where to resume rather than forcing it
        # to restart the upload.
        next_expected_index: int = 0
        # Reason for rejection; zero when `accepted` is true.
        error: int = 0
        # Optional human-readable elaboration on `error`, UTF-8, for display in
        # the ground station log. The response section is delimited so that this
        # field and future diagnostics may grow independently of the request
        # section, which is size-critical.
        detail: list[int] = field(default_factory=list)

    ```
