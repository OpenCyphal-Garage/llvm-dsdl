# lanyard.link.RcInput.1.0

Decoded pilot control input from the radio receiver, published at 50

| | |
|---|---|
| Full name | `lanyard.link.RcInput` |
| Version | 1.0 |
| Kind | Message |
| Fixed port ID | 6240 |
| Transport tier | CAN FD |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Message | 24 | 24 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# Decoded pilot control input from the radio receiver, published at 50
# Hz.
#
# TRANSPORT TIER: CAN FD.
#
# WHY SEALED: this message sits on the manual-control path, where a lost
# or late frame is felt directly by the pilot. It is fixed-size,
# single-frame, and never grows.

uint11[16] channel
# Sixteen channel values in raw receiver counts, 0 to 2047, in channel
# order.
#
# A fixed-size array of a non-byte-aligned type: the sixteen eleven-bit
# values are packed end to end across byte boundaries with no padding
# between them, for 22 bytes rather than the 32 that sixteen uint16
# fields would cost. Eleven bits is the native resolution of the
# receiver's serial protocol, so nothing is lost. Generated code exposes
# this as a plain sixteen-element array and hides the cross-byte packing
# inside the serializer.

uint8 rssi
# Received signal strength as a normalized 0 to 255 value, where 255 is
# the strongest reading the receiver can report. Not in dBm: the scale
# is receiver-specific and only meaningful as a trend.

bool failsafe
# True when the receiver has lost the transmitter and is emitting its
# configured failsafe positions. Consumers shall treat the channel array
# as invalid for manual control while this flag is set.

bool frame_lost
# True when at least one radio frame was missed since the previous
# message. Distinct from failsafe: an occasional lost frame is normal at
# range, whereas failsafe means the link is gone.

void6
# Padding to a byte boundary.

@assert _offset_.max <= 63 * 8
# Must remain a single CAN FD frame.

@sealed
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* Decoded pilot control input from the radio receiver, published at 50 */
    /* Hz. */
    /*  */
    /* TRANSPORT TIER: CAN FD. */
    /*  */
    /* WHY SEALED: this message sits on the manual-control path, where a lost */
    /* or late frame is felt directly by the pilot. It is fixed-size, */
    /* single-frame, and never grows. */
    typedef struct lanyard__link__RcInput {
      /* Sixteen channel values in raw receiver counts, 0 to 2047, in channel */
      /* order. */
      /*  */
      /* A fixed-size array of a non-byte-aligned type: the sixteen eleven-bit */
      /* values are packed end to end across byte boundaries with no padding */
      /* between them, for 22 bytes rather than the 32 that sixteen uint16 */
      /* fields would cost. Eleven bits is the native resolution of the */
      /* receiver's serial protocol, so nothing is lost. Generated code exposes */
      /* this as a plain sixteen-element array and hides the cross-byte packing */
      /* inside the serializer. */
      uint16_t channel[16U];
      /* Received signal strength as a normalized 0 to 255 value, where 255 is */
      /* the strongest reading the receiver can report. Not in dBm: the scale */
      /* is receiver-specific and only meaningful as a trend. */
      uint8_t rssi;
      /* True when the receiver has lost the transmitter and is emitting its */
      /* configured failsafe positions. Consumers shall treat the channel array */
      /* as invalid for manual control while this flag is set. */
      bool failsafe;
      /* True when at least one radio frame was missed since the previous */
      /* message. Distinct from failsafe: an occasional lost frame is normal at */
      /* range, whereas failsafe means the link is gone. */
      bool frame_lost;
    } lanyard__link__RcInput;

    ```

=== "C++ (std)"

    ```cpp
    // Decoded pilot control input from the radio receiver, published at 50
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY SEALED: this message sits on the manual-control path, where a lost
    // or late frame is felt directly by the pilot. It is fixed-size,
    // single-frame, and never grows.
    struct RcInput {
      // Sixteen channel values in raw receiver counts, 0 to 2047, in channel
      // order.
      // 
      // A fixed-size array of a non-byte-aligned type: the sixteen eleven-bit
      // values are packed end to end across byte boundaries with no padding
      // between them, for 22 bytes rather than the 32 that sixteen uint16
      // fields would cost. Eleven bits is the native resolution of the
      // receiver's serial protocol, so nothing is lost. Generated code exposes
      // this as a plain sixteen-element array and hides the cross-byte packing
      // inside the serializer.
      std::array<std::uint16_t, 16U> channel{};
      // Received signal strength as a normalized 0 to 255 value, where 255 is
      // the strongest reading the receiver can report. Not in dBm: the scale
      // is receiver-specific and only meaningful as a trend.
      std::uint8_t rssi{};
      // True when the receiver has lost the transmitter and is emitting its
      // configured failsafe positions. Consumers shall treat the channel array
      // as invalid for manual control while this flag is set.
      bool failsafe{};
      // True when at least one radio frame was missed since the previous
      // message. Distinct from failsafe: an occasional lost frame is normal at
      // range, whereas failsafe means the link is gone.
      bool frame_lost{};
      static constexpr const char* FULL_NAME = "lanyard.link.RcInput";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.link.RcInput.1.0";
      static constexpr std::size_t EXTENT_BYTES = 24U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 24U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      static constexpr std::size_t CHANNEL_ARRAY_CAPACITY = 16U;
      static constexpr bool CHANNEL_ARRAY_IS_VARIABLE_LENGTH = false;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return RcInput__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return RcInput__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return RcInput__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return RcInput__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // Decoded pilot control input from the radio receiver, published at 50
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY SEALED: this message sits on the manual-control path, where a lost
    // or late frame is felt directly by the pilot. It is fixed-size,
    // single-frame, and never grows.
    struct RcInput {
      // Sixteen channel values in raw receiver counts, 0 to 2047, in channel
      // order.
      // 
      // A fixed-size array of a non-byte-aligned type: the sixteen eleven-bit
      // values are packed end to end across byte boundaries with no padding
      // between them, for 22 bytes rather than the 32 that sixteen uint16
      // fields would cost. Eleven bits is the native resolution of the
      // receiver's serial protocol, so nothing is lost. Generated code exposes
      // this as a plain sixteen-element array and hides the cross-byte packing
      // inside the serializer.
      std::array<std::uint16_t, 16U> channel{};
      // Received signal strength as a normalized 0 to 255 value, where 255 is
      // the strongest reading the receiver can report. Not in dBm: the scale
      // is receiver-specific and only meaningful as a trend.
      std::uint8_t rssi{};
      // True when the receiver has lost the transmitter and is emitting its
      // configured failsafe positions. Consumers shall treat the channel array
      // as invalid for manual control while this flag is set.
      bool failsafe{};
      // True when at least one radio frame was missed since the previous
      // message. Distinct from failsafe: an occasional lost frame is normal at
      // range, whereas failsafe means the link is gone.
      bool frame_lost{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      RcInput() = default;
      explicit RcInput(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
      }
      static constexpr const char* FULL_NAME = "lanyard.link.RcInput";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.link.RcInput.1.0";
      static constexpr std::size_t EXTENT_BYTES = 24U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 24U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      static constexpr std::size_t CHANNEL_ARRAY_CAPACITY = 16U;
      static constexpr bool CHANNEL_ARRAY_IS_VARIABLE_LENGTH = false;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return RcInput__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return RcInput__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return RcInput__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return RcInput__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return RcInput__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return RcInput__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // Decoded pilot control input from the radio receiver, published at 50
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY SEALED: this message sits on the manual-control path, where a lost
    // or late frame is felt directly by the pilot. It is fixed-size,
    // single-frame, and never grows.
    struct RcInput {
      // Sixteen channel values in raw receiver counts, 0 to 2047, in channel
      // order.
      // 
      // A fixed-size array of a non-byte-aligned type: the sixteen eleven-bit
      // values are packed end to end across byte boundaries with no padding
      // between them, for 22 bytes rather than the 32 that sixteen uint16
      // fields would cost. Eleven bits is the native resolution of the
      // receiver's serial protocol, so nothing is lost. Generated code exposes
      // this as a plain sixteen-element array and hides the cross-byte packing
      // inside the serializer.
      std::array<std::uint16_t, 16U> channel{};
      // Received signal strength as a normalized 0 to 255 value, where 255 is
      // the strongest reading the receiver can report. Not in dBm: the scale
      // is receiver-specific and only meaningful as a trend.
      std::uint8_t rssi{};
      // True when the receiver has lost the transmitter and is emitting its
      // configured failsafe positions. Consumers shall treat the channel array
      // as invalid for manual control while this flag is set.
      bool failsafe{};
      // True when at least one radio frame was missed since the previous
      // message. Distinct from failsafe: an occasional lost frame is normal at
      // range, whereas failsafe means the link is gone.
      bool frame_lost{};
      static constexpr const char* FULL_NAME = "lanyard.link.RcInput";
      static constexpr bool IS_DEPRECATED = false;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.link.RcInput.1.0";
      static constexpr std::size_t EXTENT_BYTES = 24U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 24U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = false;
      static constexpr const char* ZOH_ALIAS_REASON = "sub-byte-field";
      static constexpr std::size_t CHANNEL_ARRAY_CAPACITY = 16U;
      static constexpr bool CHANNEL_ARRAY_IS_VARIABLE_LENGTH = false;
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return RcInput__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return RcInput__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return RcInput__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return RcInput__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// Decoded pilot control input from the radio receiver, published at 50
    /// Hz.
    /// 
    /// TRANSPORT TIER: CAN FD.
    /// 
    /// WHY SEALED: this message sits on the manual-control path, where a lost
    /// or late frame is felt directly by the pilot. It is fixed-size,
    /// single-frame, and never grows.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_link_RcInput_1_0 {
        /// Sixteen channel values in raw receiver counts, 0 to 2047, in channel
        /// order.
        /// 
        /// A fixed-size array of a non-byte-aligned type: the sixteen eleven-bit
        /// values are packed end to end across byte boundaries with no padding
        /// between them, for 22 bytes rather than the 32 that sixteen uint16
        /// fields would cost. Eleven bits is the native resolution of the
        /// receiver's serial protocol, so nothing is lost. Generated code exposes
        /// this as a plain sixteen-element array and hides the cross-byte packing
        /// inside the serializer.
        pub channel: crate::dsdl_runtime::DsdlVec<u16>,
        /// Received signal strength as a normalized 0 to 255 value, where 255 is
        /// the strongest reading the receiver can report. Not in dBm: the scale
        /// is receiver-specific and only meaningful as a trend.
        pub rssi: u8,
        /// True when the receiver has lost the transmitter and is emitting its
        /// configured failsafe positions. Consumers shall treat the channel array
        /// as invalid for manual control while this flag is set.
        pub failsafe: bool,
        /// True when at least one radio frame was missed since the previous
        /// message. Distinct from failsafe: an occasional lost frame is normal at
        /// range, whereas failsafe means the link is gone.
        pub frame_lost: bool,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// Decoded pilot control input from the radio receiver, published at 50
    /// Hz.
    /// 
    /// TRANSPORT TIER: CAN FD.
    /// 
    /// WHY SEALED: this message sits on the manual-control path, where a lost
    /// or late frame is felt directly by the pilot. It is fixed-size,
    /// single-frame, and never grows.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_link_RcInput_1_0 {
        /// Sixteen channel values in raw receiver counts, 0 to 2047, in channel
        /// order.
        /// 
        /// A fixed-size array of a non-byte-aligned type: the sixteen eleven-bit
        /// values are packed end to end across byte boundaries with no padding
        /// between them, for 22 bytes rather than the 32 that sixteen uint16
        /// fields would cost. Eleven bits is the native resolution of the
        /// receiver's serial protocol, so nothing is lost. Generated code exposes
        /// this as a plain sixteen-element array and hides the cross-byte packing
        /// inside the serializer.
        pub channel: crate::dsdl_runtime::DsdlVec<u16>,
        /// Received signal strength as a normalized 0 to 255 value, where 255 is
        /// the strongest reading the receiver can report. Not in dBm: the scale
        /// is receiver-specific and only meaningful as a trend.
        pub rssi: u8,
        /// True when the receiver has lost the transmitter and is emitting its
        /// configured failsafe positions. Consumers shall treat the channel array
        /// as invalid for manual control while this flag is set.
        pub failsafe: bool,
        /// True when at least one radio frame was missed since the previous
        /// message. Distinct from failsafe: an occasional lost frame is normal at
        /// range, whereas failsafe means the link is gone.
        pub frame_lost: bool,
    }

    ```

=== "Go"

    ```go
    // Decoded pilot control input from the radio receiver, published at 50
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY SEALED: this message sits on the manual-control path, where a lost
    // or late frame is felt directly by the pilot. It is fixed-size,
    // single-frame, and never grows.
    type RcInput_1_0 struct {
      // Sixteen channel values in raw receiver counts, 0 to 2047, in channel
      // order.
      // 
      // A fixed-size array of a non-byte-aligned type: the sixteen eleven-bit
      // values are packed end to end across byte boundaries with no padding
      // between them, for 22 bytes rather than the 32 that sixteen uint16
      // fields would cost. Eleven bits is the native resolution of the
      // receiver's serial protocol, so nothing is lost. Generated code exposes
      // this as a plain sixteen-element array and hides the cross-byte packing
      // inside the serializer.
      Channel [16]uint16
      // Received signal strength as a normalized 0 to 255 value, where 255 is
      // the strongest reading the receiver can report. Not in dBm: the scale
      // is receiver-specific and only meaningful as a trend.
      Rssi uint8
      // True when the receiver has lost the transmitter and is emitting its
      // configured failsafe positions. Consumers shall treat the channel array
      // as invalid for manual control while this flag is set.
      Failsafe bool
      // True when at least one radio frame was missed since the previous
      // message. Distinct from failsafe: an occasional lost frame is normal at
      // range, whereas failsafe means the link is gone.
      FrameLost bool
    }

    ```

=== "TypeScript"

    ```typescript
    // Decoded pilot control input from the radio receiver, published at 50
    // Hz.
    // 
    // TRANSPORT TIER: CAN FD.
    // 
    // WHY SEALED: this message sits on the manual-control path, where a lost
    // or late frame is felt directly by the pilot. It is fixed-size,
    // single-frame, and never grows.
    export interface RcInput_1_0 {
      // Sixteen channel values in raw receiver counts, 0 to 2047, in channel
      // order.
      // 
      // A fixed-size array of a non-byte-aligned type: the sixteen eleven-bit
      // values are packed end to end across byte boundaries with no padding
      // between them, for 22 bytes rather than the 32 that sixteen uint16
      // fields would cost. Eleven bits is the native resolution of the
      // receiver's serial protocol, so nothing is lost. Generated code exposes
      // this as a plain sixteen-element array and hides the cross-byte packing
      // inside the serializer.
      channel: Array<number>;
      // Received signal strength as a normalized 0 to 255 value, where 255 is
      // the strongest reading the receiver can report. Not in dBm: the scale
      // is receiver-specific and only meaningful as a trend.
      rssi: number;
      // True when the receiver has lost the transmitter and is emitting its
      // configured failsafe positions. Consumers shall treat the channel array
      // as invalid for manual control while this flag is set.
      failsafe: boolean;
      // True when at least one radio frame was missed since the previous
      // message. Distinct from failsafe: an occasional lost frame is normal at
      // range, whereas failsafe means the link is gone.
      frame_lost: boolean;
    }

    ```

=== "Python"

    ```python
    # Decoded pilot control input from the radio receiver, published at 50
    # Hz.
    # 
    # TRANSPORT TIER: CAN FD.
    # 
    # WHY SEALED: this message sits on the manual-control path, where a lost
    # or late frame is felt directly by the pilot. It is fixed-size,
    # single-frame, and never grows.
    @dataclass(slots=True)
    class RcInput_1_0:
        # Sixteen channel values in raw receiver counts, 0 to 2047, in channel
        # order.
        # 
        # A fixed-size array of a non-byte-aligned type: the sixteen eleven-bit
        # values are packed end to end across byte boundaries with no padding
        # between them, for 22 bytes rather than the 32 that sixteen uint16
        # fields would cost. Eleven bits is the native resolution of the
        # receiver's serial protocol, so nothing is lost. Generated code exposes
        # this as a plain sixteen-element array and hides the cross-byte packing
        # inside the serializer.
        channel: list[int] = field(default_factory=list)
        # Received signal strength as a normalized 0 to 255 value, where 255 is
        # the strongest reading the receiver can report. Not in dBm: the scale
        # is receiver-specific and only meaningful as a trend.
        rssi: int = 0
        # True when the receiver has lost the transmitter and is emitting its
        # configured failsafe positions. Consumers shall treat the channel array
        # as invalid for manual control while this flag is set.
        failsafe: bool = False
        # True when at least one radio frame was missed since the previous
        # message. Distinct from failsafe: an occasional lost frame is normal at
        # range, whereas failsafe means the link is gone.
        frame_lost: bool = False

    ```
