# lanyard.payload.CameraFrameMetadata.1.0

Per-frame metadata emitted by the imaging payload alongside the video

| | |
|---|---|
| Full name | `lanyard.payload.CameraFrameMetadata` |
| Version | 1.0 |
| Kind | Message |
| Fixed port ID | 6231 |
| Transport tier | Cyphal/UDP ONLY |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Message | 4096 | 2073 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# Per-frame metadata emitted by the imaging payload alongside the video
# stream.
#
# TRANSPORT TIER: Cyphal/UDP ONLY.
#
# The EXIF blob at the end can reach two kilobytes, which rules out any
# CAN transport. This message rides the same Ethernet segment as the
# video itself, where a two-kilobyte datagram is unremarkable.
#
# This definition also carries the showroom's cast-mode demonstration.
# Every scalar field in DSDL has a cast mode that decides what happens
# when a value is too large for its field. `saturated` -- the default,
# applied to every field in this namespace that does not say otherwise
# -- clamps to the nearest representable value. `truncated` discards the
# high bits and wraps. Saturation is almost always what a measurement
# wants; truncation is what a counter that is meant to wrap wants, and
# stating it explicitly documents the intent to everyone reading the
# generated code.

uavcan.time.SynchronizedTimestamp.1.0 timestamp
# Network-synchronized moment of the shutter midpoint, used to register
# the frame against the navigation solution.

truncated uint32 frame_index
# Free-running frame counter. Explicitly truncated: this counter is
# designed to wrap at 2^32 and be interpreted modulo that, and
# saturating it at the maximum would turn a wrap into a permanent stall
# at 4294967295 that a consumer computing frame deltas would never
# recover from.

uint16 width_px
# Frame width in pixels.

uint16 height_px
# Frame height in pixels.

saturated uint32 exposure_us
# Exposure time in microseconds. Explicitly saturated even though
# saturation is the default, because the contrast with `frame_index`
# above is the point: an over-range exposure should clamp to the longest
# representable exposure, never wrap around to a near-zero one.

saturated float32 gain_db
# Sensor analog gain in decibels.

uint8[<=2048] exif
# Raw EXIF/XMP metadata block as written into the stored image, passed
# through verbatim so that consumers are not restricted to the subset of
# fields this definition happens to model.

@extent 4096 * 8
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* Per-frame metadata emitted by the imaging payload alongside the video */
    /* stream. */
    /*  */
    /* TRANSPORT TIER: Cyphal/UDP ONLY. */
    /*  */
    /* The EXIF blob at the end can reach two kilobytes, which rules out any */
    /* CAN transport. This message rides the same Ethernet segment as the */
    /* video itself, where a two-kilobyte datagram is unremarkable. */
    /*  */
    /* This definition also carries the showroom's cast-mode demonstration. */
    /* Every scalar field in DSDL has a cast mode that decides what happens */
    /* when a value is too large for its field. `saturated` -- the default, */
    /* applied to every field in this namespace that does not say otherwise */
    /* -- clamps to the nearest representable value. `truncated` discards the */
    /* high bits and wraps. Saturation is almost always what a measurement */
    /* wants; truncation is what a counter that is meant to wrap wants, and */
    /* stating it explicitly documents the intent to everyone reading the */
    /* generated code. */
    typedef struct lanyard__payload__CameraFrameMetadata {
      /* Network-synchronized moment of the shutter midpoint, used to register */
      /* the frame against the navigation solution. */
      uavcan__time__SynchronizedTimestamp timestamp;
      /* Free-running frame counter. Explicitly truncated: this counter is */
      /* designed to wrap at 2^32 and be interpreted modulo that, and */
      /* saturating it at the maximum would turn a wrap into a permanent stall */
      /* at 4294967295 that a consumer computing frame deltas would never */
      /* recover from. */
      uint32_t frame_index;
      /* Frame width in pixels. */
      uint16_t width_px;
      /* Frame height in pixels. */
      uint16_t height_px;
      /* Exposure time in microseconds. Explicitly saturated even though */
      /* saturation is the default, because the contrast with `frame_index` */
      /* above is the point: an over-range exposure should clamp to the longest */
      /* representable exposure, never wrap around to a near-zero one. */
      uint32_t exposure_us;
      /* Sensor analog gain in decibels. */
      float gain_db;
      /* Raw EXIF/XMP metadata block as written into the stored image, passed */
      /* through verbatim so that consumers are not restricted to the subset of */
      /* fields this definition happens to model. */
      struct {
        uint8_t elements[2048U];
        size_t count;
      } exif;
    } lanyard__payload__CameraFrameMetadata;

    ```

=== "C++ (std)"

    ```cpp
    // Per-frame metadata emitted by the imaging payload alongside the video
    // stream.
    // 
    // TRANSPORT TIER: Cyphal/UDP ONLY.
    // 
    // The EXIF blob at the end can reach two kilobytes, which rules out any
    // CAN transport. This message rides the same Ethernet segment as the
    // video itself, where a two-kilobyte datagram is unremarkable.
    // 
    // This definition also carries the showroom's cast-mode demonstration.
    // Every scalar field in DSDL has a cast mode that decides what happens
    // when a value is too large for its field. `saturated` -- the default,
    // applied to every field in this namespace that does not say otherwise
    // -- clamps to the nearest representable value. `truncated` discards the
    // high bits and wraps. Saturation is almost always what a measurement
    // wants; truncation is what a counter that is meant to wrap wants, and
    // stating it explicitly documents the intent to everyone reading the
    // generated code.
    struct CameraFrameMetadata {
      // Network-synchronized moment of the shutter midpoint, used to register
      // the frame against the navigation solution.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // Free-running frame counter. Explicitly truncated: this counter is
      // designed to wrap at 2^32 and be interpreted modulo that, and
      // saturating it at the maximum would turn a wrap into a permanent stall
      // at 4294967295 that a consumer computing frame deltas would never
      // recover from.
      std::uint32_t frame_index{};
      // Frame width in pixels.
      std::uint16_t width_px{};
      // Frame height in pixels.
      std::uint16_t height_px{};
      // Exposure time in microseconds. Explicitly saturated even though
      // saturation is the default, because the contrast with `frame_index`
      // above is the point: an over-range exposure should clamp to the longest
      // representable exposure, never wrap around to a near-zero one.
      std::uint32_t exposure_us{};
      // Sensor analog gain in decibels.
      float gain_db{};
      // Raw EXIF/XMP metadata block as written into the stored image, passed
      // through verbatim so that consumers are not restricted to the subset of
      // fields this definition happens to model.
      std::vector<std::uint8_t> exif{};
      static constexpr const char* FULL_NAME = "lanyard.payload.CameraFrameMetadata";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.payload.CameraFrameMetadata.1.0";
      static constexpr std::size_t EXTENT_BYTES = 4096U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 2073U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      static constexpr std::size_t EXIF_ARRAY_CAPACITY = 2048U;
      static constexpr bool EXIF_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return CameraFrameMetadata__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return CameraFrameMetadata__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return CameraFrameMetadata__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return CameraFrameMetadata__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // Per-frame metadata emitted by the imaging payload alongside the video
    // stream.
    // 
    // TRANSPORT TIER: Cyphal/UDP ONLY.
    // 
    // The EXIF blob at the end can reach two kilobytes, which rules out any
    // CAN transport. This message rides the same Ethernet segment as the
    // video itself, where a two-kilobyte datagram is unremarkable.
    // 
    // This definition also carries the showroom's cast-mode demonstration.
    // Every scalar field in DSDL has a cast mode that decides what happens
    // when a value is too large for its field. `saturated` -- the default,
    // applied to every field in this namespace that does not say otherwise
    // -- clamps to the nearest representable value. `truncated` discards the
    // high bits and wraps. Saturation is almost always what a measurement
    // wants; truncation is what a counter that is meant to wrap wants, and
    // stating it explicitly documents the intent to everyone reading the
    // generated code.
    struct CameraFrameMetadata {
      // Network-synchronized moment of the shutter midpoint, used to register
      // the frame against the navigation solution.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // Free-running frame counter. Explicitly truncated: this counter is
      // designed to wrap at 2^32 and be interpreted modulo that, and
      // saturating it at the maximum would turn a wrap into a permanent stall
      // at 4294967295 that a consumer computing frame deltas would never
      // recover from.
      std::uint32_t frame_index{};
      // Frame width in pixels.
      std::uint16_t width_px{};
      // Frame height in pixels.
      std::uint16_t height_px{};
      // Exposure time in microseconds. Explicitly saturated even though
      // saturation is the default, because the contrast with `frame_index`
      // above is the point: an over-range exposure should clamp to the longest
      // representable exposure, never wrap around to a near-zero one.
      std::uint32_t exposure_us{};
      // Sensor analog gain in decibels.
      float gain_db{};
      // Raw EXIF/XMP metadata block as written into the stored image, passed
      // through verbatim so that consumers are not restricted to the subset of
      // fields this definition happens to model.
      std::pmr::vector<std::uint8_t> exif{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      CameraFrameMetadata() = default;
      explicit CameraFrameMetadata(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
        exif = decltype(exif)(_memory_resource);
        timestamp.set_memory_resource(_memory_resource);
      }
      static constexpr const char* FULL_NAME = "lanyard.payload.CameraFrameMetadata";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.payload.CameraFrameMetadata.1.0";
      static constexpr std::size_t EXTENT_BYTES = 4096U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 2073U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      static constexpr std::size_t EXIF_ARRAY_CAPACITY = 2048U;
      static constexpr bool EXIF_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return CameraFrameMetadata__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return CameraFrameMetadata__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return CameraFrameMetadata__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return CameraFrameMetadata__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return CameraFrameMetadata__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return CameraFrameMetadata__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // Per-frame metadata emitted by the imaging payload alongside the video
    // stream.
    // 
    // TRANSPORT TIER: Cyphal/UDP ONLY.
    // 
    // The EXIF blob at the end can reach two kilobytes, which rules out any
    // CAN transport. This message rides the same Ethernet segment as the
    // video itself, where a two-kilobyte datagram is unremarkable.
    // 
    // This definition also carries the showroom's cast-mode demonstration.
    // Every scalar field in DSDL has a cast mode that decides what happens
    // when a value is too large for its field. `saturated` -- the default,
    // applied to every field in this namespace that does not say otherwise
    // -- clamps to the nearest representable value. `truncated` discards the
    // high bits and wraps. Saturation is almost always what a measurement
    // wants; truncation is what a counter that is meant to wrap wants, and
    // stating it explicitly documents the intent to everyone reading the
    // generated code.
    struct CameraFrameMetadata {
      // Network-synchronized moment of the shutter midpoint, used to register
      // the frame against the navigation solution.
      ::uavcan::time::SynchronizedTimestamp timestamp{};
      // Free-running frame counter. Explicitly truncated: this counter is
      // designed to wrap at 2^32 and be interpreted modulo that, and
      // saturating it at the maximum would turn a wrap into a permanent stall
      // at 4294967295 that a consumer computing frame deltas would never
      // recover from.
      std::uint32_t frame_index{};
      // Frame width in pixels.
      std::uint16_t width_px{};
      // Frame height in pixels.
      std::uint16_t height_px{};
      // Exposure time in microseconds. Explicitly saturated even though
      // saturation is the default, because the contrast with `frame_index`
      // above is the point: an over-range exposure should clamp to the longest
      // representable exposure, never wrap around to a near-zero one.
      std::uint32_t exposure_us{};
      // Sensor analog gain in decibels.
      float gain_db{};
      // Raw EXIF/XMP metadata block as written into the stored image, passed
      // through verbatim so that consumers are not restricted to the subset of
      // fields this definition happens to model.
      ::llvmdsdl::cpp::autosar::BoundedVector<std::uint8_t, 2048U> exif{};
      static constexpr const char* FULL_NAME = "lanyard.payload.CameraFrameMetadata";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.payload.CameraFrameMetadata.1.0";
      static constexpr std::size_t EXTENT_BYTES = 4096U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 2073U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "invalid-bit-length";
      static constexpr std::size_t EXIF_ARRAY_CAPACITY = 2048U;
      static constexpr bool EXIF_ARRAY_IS_VARIABLE_LENGTH = true;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return CameraFrameMetadata__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return CameraFrameMetadata__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return CameraFrameMetadata__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return CameraFrameMetadata__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// Per-frame metadata emitted by the imaging payload alongside the video
    /// stream.
    /// 
    /// TRANSPORT TIER: Cyphal/UDP ONLY.
    /// 
    /// The EXIF blob at the end can reach two kilobytes, which rules out any
    /// CAN transport. This message rides the same Ethernet segment as the
    /// video itself, where a two-kilobyte datagram is unremarkable.
    /// 
    /// This definition also carries the showroom's cast-mode demonstration.
    /// Every scalar field in DSDL has a cast mode that decides what happens
    /// when a value is too large for its field. `saturated` -- the default,
    /// applied to every field in this namespace that does not say otherwise
    /// -- clamps to the nearest representable value. `truncated` discards the
    /// high bits and wraps. Saturation is almost always what a measurement
    /// wants; truncation is what a counter that is meant to wrap wants, and
    /// stating it explicitly documents the intent to everyone reading the
    /// generated code.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_payload_CameraFrameMetadata_1_0 {
        /// Network-synchronized moment of the shutter midpoint, used to register
        /// the frame against the navigation solution.
        pub timestamp: uavcan_time_SynchronizedTimestamp_1_0,
        /// Free-running frame counter. Explicitly truncated: this counter is
        /// designed to wrap at 2^32 and be interpreted modulo that, and
        /// saturating it at the maximum would turn a wrap into a permanent stall
        /// at 4294967295 that a consumer computing frame deltas would never
        /// recover from.
        pub frame_index: u32,
        /// Frame width in pixels.
        pub width_px: u16,
        /// Frame height in pixels.
        pub height_px: u16,
        /// Exposure time in microseconds. Explicitly saturated even though
        /// saturation is the default, because the contrast with `frame_index`
        /// above is the point: an over-range exposure should clamp to the longest
        /// representable exposure, never wrap around to a near-zero one.
        pub exposure_us: u32,
        /// Sensor analog gain in decibels.
        pub gain_db: f32,
        /// Raw EXIF/XMP metadata block as written into the stored image, passed
        /// through verbatim so that consumers are not restricted to the subset of
        /// fields this definition happens to model.
        pub exif: crate::dsdl_runtime::DsdlVec<u8>,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// Per-frame metadata emitted by the imaging payload alongside the video
    /// stream.
    /// 
    /// TRANSPORT TIER: Cyphal/UDP ONLY.
    /// 
    /// The EXIF blob at the end can reach two kilobytes, which rules out any
    /// CAN transport. This message rides the same Ethernet segment as the
    /// video itself, where a two-kilobyte datagram is unremarkable.
    /// 
    /// This definition also carries the showroom's cast-mode demonstration.
    /// Every scalar field in DSDL has a cast mode that decides what happens
    /// when a value is too large for its field. `saturated` -- the default,
    /// applied to every field in this namespace that does not say otherwise
    /// -- clamps to the nearest representable value. `truncated` discards the
    /// high bits and wraps. Saturation is almost always what a measurement
    /// wants; truncation is what a counter that is meant to wrap wants, and
    /// stating it explicitly documents the intent to everyone reading the
    /// generated code.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_payload_CameraFrameMetadata_1_0 {
        /// Network-synchronized moment of the shutter midpoint, used to register
        /// the frame against the navigation solution.
        pub timestamp: uavcan_time_SynchronizedTimestamp_1_0,
        /// Free-running frame counter. Explicitly truncated: this counter is
        /// designed to wrap at 2^32 and be interpreted modulo that, and
        /// saturating it at the maximum would turn a wrap into a permanent stall
        /// at 4294967295 that a consumer computing frame deltas would never
        /// recover from.
        pub frame_index: u32,
        /// Frame width in pixels.
        pub width_px: u16,
        /// Frame height in pixels.
        pub height_px: u16,
        /// Exposure time in microseconds. Explicitly saturated even though
        /// saturation is the default, because the contrast with `frame_index`
        /// above is the point: an over-range exposure should clamp to the longest
        /// representable exposure, never wrap around to a near-zero one.
        pub exposure_us: u32,
        /// Sensor analog gain in decibels.
        pub gain_db: f32,
        /// Raw EXIF/XMP metadata block as written into the stored image, passed
        /// through verbatim so that consumers are not restricted to the subset of
        /// fields this definition happens to model.
        pub exif: crate::dsdl_runtime::DsdlVec<u8>,
    }

    ```

=== "Go"

    ```go
    // Per-frame metadata emitted by the imaging payload alongside the video
    // stream.
    // 
    // TRANSPORT TIER: Cyphal/UDP ONLY.
    // 
    // The EXIF blob at the end can reach two kilobytes, which rules out any
    // CAN transport. This message rides the same Ethernet segment as the
    // video itself, where a two-kilobyte datagram is unremarkable.
    // 
    // This definition also carries the showroom's cast-mode demonstration.
    // Every scalar field in DSDL has a cast mode that decides what happens
    // when a value is too large for its field. `saturated` -- the default,
    // applied to every field in this namespace that does not say otherwise
    // -- clamps to the nearest representable value. `truncated` discards the
    // high bits and wraps. Saturation is almost always what a measurement
    // wants; truncation is what a counter that is meant to wrap wants, and
    // stating it explicitly documents the intent to everyone reading the
    // generated code.
    type CameraFrameMetadata_1_0 struct {
      // Network-synchronized moment of the shutter midpoint, used to register
      // the frame against the navigation solution.
      Timestamp pkg_uavcan_time.SynchronizedTimestamp_1_0
      // Free-running frame counter. Explicitly truncated: this counter is
      // designed to wrap at 2^32 and be interpreted modulo that, and
      // saturating it at the maximum would turn a wrap into a permanent stall
      // at 4294967295 that a consumer computing frame deltas would never
      // recover from.
      FrameIndex uint32
      // Frame width in pixels.
      WidthPx uint16
      // Frame height in pixels.
      HeightPx uint16
      // Exposure time in microseconds. Explicitly saturated even though
      // saturation is the default, because the contrast with `frame_index`
      // above is the point: an over-range exposure should clamp to the longest
      // representable exposure, never wrap around to a near-zero one.
      ExposureUs uint32
      // Sensor analog gain in decibels.
      GainDb float32
      // Raw EXIF/XMP metadata block as written into the stored image, passed
      // through verbatim so that consumers are not restricted to the subset of
      // fields this definition happens to model.
      Exif []uint8
    }

    ```

=== "TypeScript"

    ```typescript
    // Per-frame metadata emitted by the imaging payload alongside the video
    // stream.
    // 
    // TRANSPORT TIER: Cyphal/UDP ONLY.
    // 
    // The EXIF blob at the end can reach two kilobytes, which rules out any
    // CAN transport. This message rides the same Ethernet segment as the
    // video itself, where a two-kilobyte datagram is unremarkable.
    // 
    // This definition also carries the showroom's cast-mode demonstration.
    // Every scalar field in DSDL has a cast mode that decides what happens
    // when a value is too large for its field. `saturated` -- the default,
    // applied to every field in this namespace that does not say otherwise
    // -- clamps to the nearest representable value. `truncated` discards the
    // high bits and wraps. Saturation is almost always what a measurement
    // wants; truncation is what a counter that is meant to wrap wants, and
    // stating it explicitly documents the intent to everyone reading the
    // generated code.
    export interface CameraFrameMetadata_1_0 {
      // Network-synchronized moment of the shutter midpoint, used to register
      // the frame against the navigation solution.
      timestamp: SynchronizedTimestamp_1_0;
      // Free-running frame counter. Explicitly truncated: this counter is
      // designed to wrap at 2^32 and be interpreted modulo that, and
      // saturating it at the maximum would turn a wrap into a permanent stall
      // at 4294967295 that a consumer computing frame deltas would never
      // recover from.
      frame_index: number;
      // Frame width in pixels.
      width_px: number;
      // Frame height in pixels.
      height_px: number;
      // Exposure time in microseconds. Explicitly saturated even though
      // saturation is the default, because the contrast with `frame_index`
      // above is the point: an over-range exposure should clamp to the longest
      // representable exposure, never wrap around to a near-zero one.
      exposure_us: number;
      // Sensor analog gain in decibels.
      gain_db: number;
      // Raw EXIF/XMP metadata block as written into the stored image, passed
      // through verbatim so that consumers are not restricted to the subset of
      // fields this definition happens to model.
      exif: Array<number>;
    }

    ```

=== "Python"

    ```python
    # Per-frame metadata emitted by the imaging payload alongside the video
    # stream.
    # 
    # TRANSPORT TIER: Cyphal/UDP ONLY.
    # 
    # The EXIF blob at the end can reach two kilobytes, which rules out any
    # CAN transport. This message rides the same Ethernet segment as the
    # video itself, where a two-kilobyte datagram is unremarkable.
    # 
    # This definition also carries the showroom's cast-mode demonstration.
    # Every scalar field in DSDL has a cast mode that decides what happens
    # when a value is too large for its field. `saturated` -- the default,
    # applied to every field in this namespace that does not say otherwise
    # -- clamps to the nearest representable value. `truncated` discards the
    # high bits and wraps. Saturation is almost always what a measurement
    # wants; truncation is what a counter that is meant to wrap wants, and
    # stating it explicitly documents the intent to everyone reading the
    # generated code.
    @dataclass(slots=True)
    class CameraFrameMetadata_1_0:
        # Network-synchronized moment of the shutter midpoint, used to register
        # the frame against the navigation solution.
        timestamp: SynchronizedTimestamp_1_0 = field(default_factory=SynchronizedTimestamp_1_0)
        # Free-running frame counter. Explicitly truncated: this counter is
        # designed to wrap at 2^32 and be interpreted modulo that, and
        # saturating it at the maximum would turn a wrap into a permanent stall
        # at 4294967295 that a consumer computing frame deltas would never
        # recover from.
        frame_index: int = 0
        # Frame width in pixels.
        width_px: int = 0
        # Frame height in pixels.
        height_px: int = 0
        # Exposure time in microseconds. Explicitly saturated even though
        # saturation is the default, because the contrast with `frame_index`
        # above is the point: an over-range exposure should clamp to the longest
        # representable exposure, never wrap around to a near-zero one.
        exposure_us: int = 0
        # Sensor analog gain in decibels.
        gain_db: float = 0.0
        # Raw EXIF/XMP metadata block as written into the stored image, passed
        # through verbatim so that consumers are not restricted to the subset of
        # fields this definition happens to model.
        exif: list[int] = field(default_factory=list)

    ```
