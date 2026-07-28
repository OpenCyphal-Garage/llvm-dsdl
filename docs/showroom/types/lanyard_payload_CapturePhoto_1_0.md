# lanyard.payload.CapturePhoto.1.0

Command the payload to capture one or more still images.

| | |
|---|---|
| Full name | `lanyard.payload.CapturePhoto` |
| Version | 1.0 |
| Kind | Service |
| Fixed port ID | 257 |
| Transport tier | CAN FD and Cyphal/UDP |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Request | 5 | 5 |
| Response | 64 | 40 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# Command the payload to capture one or more still images.
#
# TRANSPORT TIER: CAN FD and Cyphal/UDP.
#
# WHY THE TWO SECTIONS SEAL DIFFERENTLY: the request is a fixed
# five-byte command issued from the flight controller's real-time loop
# when a survey trigger fires, so it is @sealed and pays no delimiter
# header. The response is consumed by a ground station that tolerates
# latency and benefits from richer diagnostics over time, so it is
# delimited and may grow in later minor versions.
#
# Request and response are independent definitions that happen to share
# a file and a port. Nothing requires them to make the same sealing,
# extent, or size decision, and here they do not.

uint16 count
# Number of images to capture. Zero cancels an in-progress sequence.

float16 interval_s
# Seconds between exposures when count is greater than one. Ignored when
# count is one.

uint2 mode
# Capture mode; one of the constants below.

uint2 MODE_SINGLE = 0
# One exposure at the current settings.

uint2 MODE_BRACKET = 1
# An exposure bracket around the metered value, for high-dynamic-range
# compositing.

uint2 MODE_SURVEY = 2
# Capture at a fixed ground sample distance, with the payload computing
# the interval from vehicle ground speed and altitude. `interval_s` is
# ignored.

void6
# Padding to a byte boundary.

@assert _offset_.max == 40
# Five bytes: the request is issued from a real-time loop and its size
# is part of that contract.

@sealed

---

bool accepted
# True when the payload has queued the capture.

void7
# Padding to a byte boundary.

uint32 first_image_id
# Identifier assigned to the first image of the sequence. Subsequent
# images increment from it, so a ground station can name every file in
# the sequence from this one value.

uint16 remaining_capacity_images
# Images that will still fit on the payload's storage medium after this
# sequence completes.

uint8[<=32] detail
# Optional human-readable elaboration, UTF-8. Empty on success.

@extent 64 * 8
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* Command the payload to capture one or more still images. */
    /*  */
    /* TRANSPORT TIER: CAN FD and Cyphal/UDP. */
    /*  */
    /* WHY THE TWO SECTIONS SEAL DIFFERENTLY: the request is a fixed */
    /* five-byte command issued from the flight controller's real-time loop */
    /* when a survey trigger fires, so it is @sealed and pays no delimiter */
    /* header. The response is consumed by a ground station that tolerates */
    /* latency and benefits from richer diagnostics over time, so it is */
    /* delimited and may grow in later minor versions. */
    /*  */
    /* Request and response are independent definitions that happen to share */
    /* a file and a port. Nothing requires them to make the same sealing, */
    /* extent, or size decision, and here they do not. */
    typedef struct lanyard__payload__CapturePhoto__Request {
      /* Number of images to capture. Zero cancels an in-progress sequence. */
      uint16_t count;
      /* Seconds between exposures when count is greater than one. Ignored when */
      /* count is one. */
      float interval_s;
      /* Capture mode; one of the constants below. */
      uint8_t mode;
    } lanyard__payload__CapturePhoto__Request;

    /* Command the payload to capture one or more still images. */
    /*  */
    /* TRANSPORT TIER: CAN FD and Cyphal/UDP. */
    /*  */
    /* WHY THE TWO SECTIONS SEAL DIFFERENTLY: the request is a fixed */
    /* five-byte command issued from the flight controller's real-time loop */
    /* when a survey trigger fires, so it is @sealed and pays no delimiter */
    /* header. The response is consumed by a ground station that tolerates */
    /* latency and benefits from richer diagnostics over time, so it is */
    /* delimited and may grow in later minor versions. */
    /*  */
    /* Request and response are independent definitions that happen to share */
    /* a file and a port. Nothing requires them to make the same sealing, */
    /* extent, or size decision, and here they do not. */
    typedef struct lanyard__payload__CapturePhoto__Response {
      /* True when the payload has queued the capture. */
      bool accepted;
      /* Identifier assigned to the first image of the sequence. Subsequent */
      /* images increment from it, so a ground station can name every file in */
      /* the sequence from this one value. */
      uint32_t first_image_id;
      /* Images that will still fit on the payload's storage medium after this */
      /* sequence completes. */
      uint16_t remaining_capacity_images;
      /* Optional human-readable elaboration, UTF-8. Empty on success. */
      struct {
        uint8_t elements[32U];
        size_t count;
      } detail;
    } lanyard__payload__CapturePhoto__Response;

    ```

=== "C++ (std)"

    ```cpp
    // Command the payload to capture one or more still images.
    // 
    // TRANSPORT TIER: CAN FD and Cyphal/UDP.
    // 
    // WHY THE TWO SECTIONS SEAL DIFFERENTLY: the request is a fixed
    // five-byte command issued from the flight controller's real-time loop
    // when a survey trigger fires, so it is @sealed and pays no delimiter
    // header. The response is consumed by a ground station that tolerates
    // latency and benefits from richer diagnostics over time, so it is
    // delimited and may grow in later minor versions.
    // 
    // Request and response are independent definitions that happen to share
    // a file and a port. Nothing requires them to make the same sealing,
    // extent, or size decision, and here they do not.
    struct CapturePhoto__Request {
      // Number of images to capture. Zero cancels an in-progress sequence.
      std::uint16_t count{};
      // Seconds between exposures when count is greater than one. Ignored when
      // count is one.
      float interval_s{};
      // Capture mode; one of the constants below.
      std::uint8_t mode{};
      static constexpr const char* FULL_NAME = "lanyard.payload.CapturePhoto.Request";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.payload.CapturePhoto.Request.1.0";
      static constexpr std::size_t EXTENT_BYTES = 5U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 5U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      // One exposure at the current settings.
      static constexpr auto MODE_SINGLE = 0;
      // An exposure bracket around the metered value, for high-dynamic-range
      // compositing.
      static constexpr auto MODE_BRACKET = 1;
      // Capture at a fixed ground sample distance, with the payload computing
      // the interval from vehicle ground speed and altitude. `interval_s` is
      // ignored.
      static constexpr auto MODE_SURVEY = 2;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return CapturePhoto__Request__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return CapturePhoto__Request__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return CapturePhoto__Request__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return CapturePhoto__Request__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    // Command the payload to capture one or more still images.
    // 
    // TRANSPORT TIER: CAN FD and Cyphal/UDP.
    // 
    // WHY THE TWO SECTIONS SEAL DIFFERENTLY: the request is a fixed
    // five-byte command issued from the flight controller's real-time loop
    // when a survey trigger fires, so it is @sealed and pays no delimiter
    // header. The response is consumed by a ground station that tolerates
    // latency and benefits from richer diagnostics over time, so it is
    // delimited and may grow in later minor versions.
    // 
    // Request and response are independent definitions that happen to share
    // a file and a port. Nothing requires them to make the same sealing,
    // extent, or size decision, and here they do not.
    struct CapturePhoto__Response {
      // True when the payload has queued the capture.
      bool accepted{};
      // Identifier assigned to the first image of the sequence. Subsequent
      // images increment from it, so a ground station can name every file in
      // the sequence from this one value.
      std::uint32_t first_image_id{};
      // Images that will still fit on the payload's storage medium after this
      // sequence completes.
      std::uint16_t remaining_capacity_images{};
      // Optional human-readable elaboration, UTF-8. Empty on success.
      std::vector<std::uint8_t> detail{};
      static constexpr const char* FULL_NAME = "lanyard.payload.CapturePhoto.Response";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.payload.CapturePhoto.Response.1.0";
      static constexpr std::size_t EXTENT_BYTES = 64U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 40U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      static constexpr std::size_t DETAIL_ARRAY_CAPACITY = 32U;
      static constexpr bool DETAIL_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return CapturePhoto__Response__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return CapturePhoto__Response__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return CapturePhoto__Response__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return CapturePhoto__Response__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // Command the payload to capture one or more still images.
    // 
    // TRANSPORT TIER: CAN FD and Cyphal/UDP.
    // 
    // WHY THE TWO SECTIONS SEAL DIFFERENTLY: the request is a fixed
    // five-byte command issued from the flight controller's real-time loop
    // when a survey trigger fires, so it is @sealed and pays no delimiter
    // header. The response is consumed by a ground station that tolerates
    // latency and benefits from richer diagnostics over time, so it is
    // delimited and may grow in later minor versions.
    // 
    // Request and response are independent definitions that happen to share
    // a file and a port. Nothing requires them to make the same sealing,
    // extent, or size decision, and here they do not.
    struct CapturePhoto__Request {
      // Number of images to capture. Zero cancels an in-progress sequence.
      std::uint16_t count{};
      // Seconds between exposures when count is greater than one. Ignored when
      // count is one.
      float interval_s{};
      // Capture mode; one of the constants below.
      std::uint8_t mode{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      CapturePhoto__Request() = default;
      explicit CapturePhoto__Request(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
      }
      static constexpr const char* FULL_NAME = "lanyard.payload.CapturePhoto.Request";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.payload.CapturePhoto.Request.1.0";
      static constexpr std::size_t EXTENT_BYTES = 5U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 5U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      // One exposure at the current settings.
      static constexpr auto MODE_SINGLE = 0;
      // An exposure bracket around the metered value, for high-dynamic-range
      // compositing.
      static constexpr auto MODE_BRACKET = 1;
      // Capture at a fixed ground sample distance, with the payload computing
      // the interval from vehicle ground speed and altitude. `interval_s` is
      // ignored.
      static constexpr auto MODE_SURVEY = 2;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return CapturePhoto__Request__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return CapturePhoto__Request__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return CapturePhoto__Request__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return CapturePhoto__Request__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return CapturePhoto__Request__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return CapturePhoto__Request__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    // Command the payload to capture one or more still images.
    // 
    // TRANSPORT TIER: CAN FD and Cyphal/UDP.
    // 
    // WHY THE TWO SECTIONS SEAL DIFFERENTLY: the request is a fixed
    // five-byte command issued from the flight controller's real-time loop
    // when a survey trigger fires, so it is @sealed and pays no delimiter
    // header. The response is consumed by a ground station that tolerates
    // latency and benefits from richer diagnostics over time, so it is
    // delimited and may grow in later minor versions.
    // 
    // Request and response are independent definitions that happen to share
    // a file and a port. Nothing requires them to make the same sealing,
    // extent, or size decision, and here they do not.
    struct CapturePhoto__Response {
      // True when the payload has queued the capture.
      bool accepted{};
      // Identifier assigned to the first image of the sequence. Subsequent
      // images increment from it, so a ground station can name every file in
      // the sequence from this one value.
      std::uint32_t first_image_id{};
      // Images that will still fit on the payload's storage medium after this
      // sequence completes.
      std::uint16_t remaining_capacity_images{};
      // Optional human-readable elaboration, UTF-8. Empty on success.
      std::pmr::vector<std::uint8_t> detail{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      CapturePhoto__Response() = default;
      explicit CapturePhoto__Response(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
        detail = decltype(detail)(_memory_resource);
      }
      static constexpr const char* FULL_NAME = "lanyard.payload.CapturePhoto.Response";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.payload.CapturePhoto.Response.1.0";
      static constexpr std::size_t EXTENT_BYTES = 64U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 40U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      static constexpr std::size_t DETAIL_ARRAY_CAPACITY = 32U;
      static constexpr bool DETAIL_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return CapturePhoto__Response__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return CapturePhoto__Response__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return CapturePhoto__Response__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return CapturePhoto__Response__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return CapturePhoto__Response__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return CapturePhoto__Response__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // Command the payload to capture one or more still images.
    // 
    // TRANSPORT TIER: CAN FD and Cyphal/UDP.
    // 
    // WHY THE TWO SECTIONS SEAL DIFFERENTLY: the request is a fixed
    // five-byte command issued from the flight controller's real-time loop
    // when a survey trigger fires, so it is @sealed and pays no delimiter
    // header. The response is consumed by a ground station that tolerates
    // latency and benefits from richer diagnostics over time, so it is
    // delimited and may grow in later minor versions.
    // 
    // Request and response are independent definitions that happen to share
    // a file and a port. Nothing requires them to make the same sealing,
    // extent, or size decision, and here they do not.
    struct CapturePhoto__Request {
      // Number of images to capture. Zero cancels an in-progress sequence.
      std::uint16_t count{};
      // Seconds between exposures when count is greater than one. Ignored when
      // count is one.
      float interval_s{};
      // Capture mode; one of the constants below.
      std::uint8_t mode{};
      static constexpr const char* FULL_NAME = "lanyard.payload.CapturePhoto.Request";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.payload.CapturePhoto.Request.1.0";
      static constexpr std::size_t EXTENT_BYTES = 5U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 5U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      // One exposure at the current settings.
      static constexpr auto MODE_SINGLE = 0;
      // An exposure bracket around the metered value, for high-dynamic-range
      // compositing.
      static constexpr auto MODE_BRACKET = 1;
      // Capture at a fixed ground sample distance, with the payload computing
      // the interval from vehicle ground speed and altitude. `interval_s` is
      // ignored.
      static constexpr auto MODE_SURVEY = 2;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return CapturePhoto__Request__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return CapturePhoto__Request__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return CapturePhoto__Request__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return CapturePhoto__Request__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    // Command the payload to capture one or more still images.
    // 
    // TRANSPORT TIER: CAN FD and Cyphal/UDP.
    // 
    // WHY THE TWO SECTIONS SEAL DIFFERENTLY: the request is a fixed
    // five-byte command issued from the flight controller's real-time loop
    // when a survey trigger fires, so it is @sealed and pays no delimiter
    // header. The response is consumed by a ground station that tolerates
    // latency and benefits from richer diagnostics over time, so it is
    // delimited and may grow in later minor versions.
    // 
    // Request and response are independent definitions that happen to share
    // a file and a port. Nothing requires them to make the same sealing,
    // extent, or size decision, and here they do not.
    struct CapturePhoto__Response {
      // True when the payload has queued the capture.
      bool accepted{};
      // Identifier assigned to the first image of the sequence. Subsequent
      // images increment from it, so a ground station can name every file in
      // the sequence from this one value.
      std::uint32_t first_image_id{};
      // Images that will still fit on the payload's storage medium after this
      // sequence completes.
      std::uint16_t remaining_capacity_images{};
      // Optional human-readable elaboration, UTF-8. Empty on success.
      ::llvmdsdl::cpp::autosar::BoundedVector<std::uint8_t, 32U> detail{};
      static constexpr const char* FULL_NAME = "lanyard.payload.CapturePhoto.Response";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.payload.CapturePhoto.Response.1.0";
      static constexpr std::size_t EXTENT_BYTES = 64U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 40U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      static constexpr std::size_t DETAIL_ARRAY_CAPACITY = 32U;
      static constexpr bool DETAIL_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return CapturePhoto__Response__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return CapturePhoto__Response__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return CapturePhoto__Response__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return CapturePhoto__Response__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// Command the payload to capture one or more still images.
    /// 
    /// TRANSPORT TIER: CAN FD and Cyphal/UDP.
    /// 
    /// WHY THE TWO SECTIONS SEAL DIFFERENTLY: the request is a fixed
    /// five-byte command issued from the flight controller's real-time loop
    /// when a survey trigger fires, so it is @sealed and pays no delimiter
    /// header. The response is consumed by a ground station that tolerates
    /// latency and benefits from richer diagnostics over time, so it is
    /// delimited and may grow in later minor versions.
    /// 
    /// Request and response are independent definitions that happen to share
    /// a file and a port. Nothing requires them to make the same sealing,
    /// extent, or size decision, and here they do not.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_payload_CapturePhoto_1_0_Request {
        /// Number of images to capture. Zero cancels an in-progress sequence.
        pub count: u16,
        /// Seconds between exposures when count is greater than one. Ignored when
        /// count is one.
        pub interval_s: f32,
        /// Capture mode; one of the constants below.
        pub mode: u8,
    }

    /// Command the payload to capture one or more still images.
    /// 
    /// TRANSPORT TIER: CAN FD and Cyphal/UDP.
    /// 
    /// WHY THE TWO SECTIONS SEAL DIFFERENTLY: the request is a fixed
    /// five-byte command issued from the flight controller's real-time loop
    /// when a survey trigger fires, so it is @sealed and pays no delimiter
    /// header. The response is consumed by a ground station that tolerates
    /// latency and benefits from richer diagnostics over time, so it is
    /// delimited and may grow in later minor versions.
    /// 
    /// Request and response are independent definitions that happen to share
    /// a file and a port. Nothing requires them to make the same sealing,
    /// extent, or size decision, and here they do not.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_payload_CapturePhoto_1_0_Response {
        /// True when the payload has queued the capture.
        pub accepted: bool,
        /// Identifier assigned to the first image of the sequence. Subsequent
        /// images increment from it, so a ground station can name every file in
        /// the sequence from this one value.
        pub first_image_id: u32,
        /// Images that will still fit on the payload's storage medium after this
        /// sequence completes.
        pub remaining_capacity_images: u16,
        /// Optional human-readable elaboration, UTF-8. Empty on success.
        pub detail: crate::dsdl_runtime::DsdlVec<u8>,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// Command the payload to capture one or more still images.
    /// 
    /// TRANSPORT TIER: CAN FD and Cyphal/UDP.
    /// 
    /// WHY THE TWO SECTIONS SEAL DIFFERENTLY: the request is a fixed
    /// five-byte command issued from the flight controller's real-time loop
    /// when a survey trigger fires, so it is @sealed and pays no delimiter
    /// header. The response is consumed by a ground station that tolerates
    /// latency and benefits from richer diagnostics over time, so it is
    /// delimited and may grow in later minor versions.
    /// 
    /// Request and response are independent definitions that happen to share
    /// a file and a port. Nothing requires them to make the same sealing,
    /// extent, or size decision, and here they do not.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_payload_CapturePhoto_1_0_Request {
        /// Number of images to capture. Zero cancels an in-progress sequence.
        pub count: u16,
        /// Seconds between exposures when count is greater than one. Ignored when
        /// count is one.
        pub interval_s: f32,
        /// Capture mode; one of the constants below.
        pub mode: u8,
    }

    /// Command the payload to capture one or more still images.
    /// 
    /// TRANSPORT TIER: CAN FD and Cyphal/UDP.
    /// 
    /// WHY THE TWO SECTIONS SEAL DIFFERENTLY: the request is a fixed
    /// five-byte command issued from the flight controller's real-time loop
    /// when a survey trigger fires, so it is @sealed and pays no delimiter
    /// header. The response is consumed by a ground station that tolerates
    /// latency and benefits from richer diagnostics over time, so it is
    /// delimited and may grow in later minor versions.
    /// 
    /// Request and response are independent definitions that happen to share
    /// a file and a port. Nothing requires them to make the same sealing,
    /// extent, or size decision, and here they do not.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_payload_CapturePhoto_1_0_Response {
        /// True when the payload has queued the capture.
        pub accepted: bool,
        /// Identifier assigned to the first image of the sequence. Subsequent
        /// images increment from it, so a ground station can name every file in
        /// the sequence from this one value.
        pub first_image_id: u32,
        /// Images that will still fit on the payload's storage medium after this
        /// sequence completes.
        pub remaining_capacity_images: u16,
        /// Optional human-readable elaboration, UTF-8. Empty on success.
        pub detail: crate::dsdl_runtime::DsdlVec<u8>,
    }

    ```

=== "Go"

    ```go
    // Command the payload to capture one or more still images.
    // 
    // TRANSPORT TIER: CAN FD and Cyphal/UDP.
    // 
    // WHY THE TWO SECTIONS SEAL DIFFERENTLY: the request is a fixed
    // five-byte command issued from the flight controller's real-time loop
    // when a survey trigger fires, so it is @sealed and pays no delimiter
    // header. The response is consumed by a ground station that tolerates
    // latency and benefits from richer diagnostics over time, so it is
    // delimited and may grow in later minor versions.
    // 
    // Request and response are independent definitions that happen to share
    // a file and a port. Nothing requires them to make the same sealing,
    // extent, or size decision, and here they do not.
    type CapturePhoto_1_0_Request struct {
      // Number of images to capture. Zero cancels an in-progress sequence.
      Count uint16
      // Seconds between exposures when count is greater than one. Ignored when
      // count is one.
      IntervalS float32
      // Capture mode; one of the constants below.
      Mode uint8
    }

    // Command the payload to capture one or more still images.
    // 
    // TRANSPORT TIER: CAN FD and Cyphal/UDP.
    // 
    // WHY THE TWO SECTIONS SEAL DIFFERENTLY: the request is a fixed
    // five-byte command issued from the flight controller's real-time loop
    // when a survey trigger fires, so it is @sealed and pays no delimiter
    // header. The response is consumed by a ground station that tolerates
    // latency and benefits from richer diagnostics over time, so it is
    // delimited and may grow in later minor versions.
    // 
    // Request and response are independent definitions that happen to share
    // a file and a port. Nothing requires them to make the same sealing,
    // extent, or size decision, and here they do not.
    type CapturePhoto_1_0_Response struct {
      // True when the payload has queued the capture.
      Accepted bool
      // Identifier assigned to the first image of the sequence. Subsequent
      // images increment from it, so a ground station can name every file in
      // the sequence from this one value.
      FirstImageId uint32
      // Images that will still fit on the payload's storage medium after this
      // sequence completes.
      RemainingCapacityImages uint16
      // Optional human-readable elaboration, UTF-8. Empty on success.
      Detail []uint8
    }

    ```

=== "TypeScript"

    ```typescript
    // Command the payload to capture one or more still images.
    // 
    // TRANSPORT TIER: CAN FD and Cyphal/UDP.
    // 
    // WHY THE TWO SECTIONS SEAL DIFFERENTLY: the request is a fixed
    // five-byte command issued from the flight controller's real-time loop
    // when a survey trigger fires, so it is @sealed and pays no delimiter
    // header. The response is consumed by a ground station that tolerates
    // latency and benefits from richer diagnostics over time, so it is
    // delimited and may grow in later minor versions.
    // 
    // Request and response are independent definitions that happen to share
    // a file and a port. Nothing requires them to make the same sealing,
    // extent, or size decision, and here they do not.
    export interface CapturePhoto_1_0_Request {
      // Number of images to capture. Zero cancels an in-progress sequence.
      count: number;
      // Seconds between exposures when count is greater than one. Ignored when
      // count is one.
      interval_s: number;
      // Capture mode; one of the constants below.
      mode: number;
    }

    // Command the payload to capture one or more still images.
    // 
    // TRANSPORT TIER: CAN FD and Cyphal/UDP.
    // 
    // WHY THE TWO SECTIONS SEAL DIFFERENTLY: the request is a fixed
    // five-byte command issued from the flight controller's real-time loop
    // when a survey trigger fires, so it is @sealed and pays no delimiter
    // header. The response is consumed by a ground station that tolerates
    // latency and benefits from richer diagnostics over time, so it is
    // delimited and may grow in later minor versions.
    // 
    // Request and response are independent definitions that happen to share
    // a file and a port. Nothing requires them to make the same sealing,
    // extent, or size decision, and here they do not.
    export interface CapturePhoto_1_0_Response {
      // True when the payload has queued the capture.
      accepted: boolean;
      // Identifier assigned to the first image of the sequence. Subsequent
      // images increment from it, so a ground station can name every file in
      // the sequence from this one value.
      first_image_id: number;
      // Images that will still fit on the payload's storage medium after this
      // sequence completes.
      remaining_capacity_images: number;
      // Optional human-readable elaboration, UTF-8. Empty on success.
      detail: Array<number>;
    }

    export type CapturePhoto_1_0 = CapturePhoto_1_0_Request;

    ```

=== "Python"

    ```python
    # Command the payload to capture one or more still images.
    # 
    # TRANSPORT TIER: CAN FD and Cyphal/UDP.
    # 
    # WHY THE TWO SECTIONS SEAL DIFFERENTLY: the request is a fixed
    # five-byte command issued from the flight controller's real-time loop
    # when a survey trigger fires, so it is @sealed and pays no delimiter
    # header. The response is consumed by a ground station that tolerates
    # latency and benefits from richer diagnostics over time, so it is
    # delimited and may grow in later minor versions.
    # 
    # Request and response are independent definitions that happen to share
    # a file and a port. Nothing requires them to make the same sealing,
    # extent, or size decision, and here they do not.
    @dataclass(slots=True)
    class CapturePhoto_1_0_Request:
        # Number of images to capture. Zero cancels an in-progress sequence.
        count: int = 0
        # Seconds between exposures when count is greater than one. Ignored when
        # count is one.
        interval_s: float = 0.0
        # Capture mode; one of the constants below.
        mode: int = 0

    # Command the payload to capture one or more still images.
    # 
    # TRANSPORT TIER: CAN FD and Cyphal/UDP.
    # 
    # WHY THE TWO SECTIONS SEAL DIFFERENTLY: the request is a fixed
    # five-byte command issued from the flight controller's real-time loop
    # when a survey trigger fires, so it is @sealed and pays no delimiter
    # header. The response is consumed by a ground station that tolerates
    # latency and benefits from richer diagnostics over time, so it is
    # delimited and may grow in later minor versions.
    # 
    # Request and response are independent definitions that happen to share
    # a file and a port. Nothing requires them to make the same sealing,
    # extent, or size decision, and here they do not.
    @dataclass(slots=True)
    class CapturePhoto_1_0_Response:
        # True when the payload has queued the capture.
        accepted: bool = False
        # Identifier assigned to the first image of the sequence. Subsequent
        # images increment from it, so a ground station can name every file in
        # the sequence from this one value.
        first_image_id: int = 0
        # Images that will still fit on the payload's storage medium after this
        # sequence completes.
        remaining_capacity_images: int = 0
        # Optional human-readable elaboration, UTF-8. Empty on success.
        detail: list[int] = field(default_factory=list)

    ```
