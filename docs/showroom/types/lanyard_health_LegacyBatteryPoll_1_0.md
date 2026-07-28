# lanyard.health.LegacyBatteryPoll.1.0

Read the state of one battery pack. DEPRECATED -- use the

| | |
|---|---|
| Full name | `lanyard.health.LegacyBatteryPoll` |
| Version | 1.0 |
| Kind | Service |
| Fixed port ID | 258 |
| Transport tier | Classic CAN |

## Wire layout

| Section | Extent (bytes) | Max serialized (bytes) |
|---|---:|---:|
| Request | 1 | 1 |
| Response | 6 | 6 |

A sealed type reports its extent as its exact serialized size; a delimited type reports the declared `@extent`, which bounds what a reader must be prepared to receive.

## Definition

```python
# Read the state of one battery pack. DEPRECATED -- use the
# BatteryStatus.2.0 message instead.
#
# TRANSPORT TIER: Classic CAN.
#
# WHY IT IS STILL HERE: this is the showroom's deprecation example. The
# @deprecated directive marks a definition as scheduled for removal
# without deleting it, so that nodes still speaking it keep working
# while integrators migrate.
#
# WHAT THE GENERATED CODE CARRIES: every backend appends a `Deprecated:
# ...` notice to this comment block and emits an IS_DEPRECATED constant,
# so the marking is visible to a developer who never opens the DSDL. Go
# picks it up as a real deprecation (its toolchain keys on a
# `Deprecated: ` doc paragraph), and TypeScript additionally gets a `/**
# @deprecated */` JSDoc block, which is what tsc and editors read.
#
# Compile-time enforcement in C, C++, and Rust is opt-in behind `dsdlc
# --emit-deprecation-attributes`, which adds
# `__attribute__((deprecated))`, `[[deprecated]]`, and `#[deprecated]`
# respectively. It is not the default because it turns documentation
# into a build diagnostic, and enough of the standard uavcan namespace
# is deprecated that switching it on unannounced would break -Werror
# builds.
#
# Note the placement: @deprecated appears in the request section, ahead
# of the first attribute, and applies to the whole service. The language
# accepts it nowhere else -- not after a field, and not in the response
# section -- because a service is deprecated or not as a unit; you
# cannot retire half a request/response pair.
#
# MIGRATION: subscribe to lanyard.health.BatteryStatus.2.0 (port 6251),
# which the pack publishes at 2 Hz on its own. Polling cost the bus a
# request and a response per pack per interval and gave the flight
# controller a scheduling problem it did not need.

@deprecated

uint8 battery_index
# Zero-based index of the pack to read.

@sealed

---

uint16 voltage_mv
# Pack terminal voltage in millivolts.

int16 current_ca
# Pack current in centiamperes. Positive is discharge.

uint8 remaining_pct
# State of charge as a percentage, 0 to 100.

int8 temperature_c
# Hottest cell temperature in whole degrees Celsius. Note that the
# replacement message reports temperature in kelvin via the standard SI
# type, so a migration is not a straight field copy.

@sealed
```

## Generated code

Declaration excerpts only -- the serialization bodies are omitted for length. Build the `showroom` target for the complete output in every language and profile.

=== "C"

    ```c
    /* Read the state of one battery pack. DEPRECATED -- use the */
    /* BatteryStatus.2.0 message instead. */
    /*  */
    /* TRANSPORT TIER: Classic CAN. */
    /*  */
    /* WHY IT IS STILL HERE: this is the showroom's deprecation example. The */
    /* @deprecated directive marks a definition as scheduled for removal */
    /* without deleting it, so that nodes still speaking it keep working */
    /* while integrators migrate. */
    /*  */
    /* WHAT THE GENERATED CODE CARRIES: every backend appends a `Deprecated: */
    /* ...` notice to this comment block and emits an IS_DEPRECATED constant, */
    /* so the marking is visible to a developer who never opens the DSDL. Go */
    /* picks it up as a real deprecation (its toolchain keys on a */
    /* `Deprecated: ` doc paragraph), and TypeScript additionally gets a `/** */
    /* @deprecated * /` JSDoc block, which is what tsc and editors read. */
    /*  */
    /* Compile-time enforcement in C, C++, and Rust is opt-in behind `dsdlc */
    /* --emit-deprecation-attributes`, which adds */
    /* `__attribute__((deprecated))`, `[[deprecated]]`, and `#[deprecated]` */
    /* respectively. It is not the default because it turns documentation */
    /* into a build diagnostic, and enough of the standard uavcan namespace */
    /* is deprecated that switching it on unannounced would break -Werror */
    /* builds. */
    /*  */
    /* Note the placement: @deprecated appears in the request section, ahead */
    /* of the first attribute, and applies to the whole service. The language */
    /* accepts it nowhere else -- not after a field, and not in the response */
    /* section -- because a service is deprecated or not as a unit; you */
    /* cannot retire half a request/response pair. */
    /*  */
    /* MIGRATION: subscribe to lanyard.health.BatteryStatus.2.0 (port 6251), */
    /* which the pack publishes at 2 Hz on its own. Polling cost the bus a */
    /* request and a response per pack per interval and gave the flight */
    /* controller a scheduling problem it did not need. */
    /*  */
    /* Deprecated: lanyard.health.LegacyBatteryPoll.1.0 is marked @deprecated */
    /* in its DSDL definition and is scheduled for removal; migrate to a newer */
    /* version of this type. */
    typedef struct lanyard__health__LegacyBatteryPoll__Request {
      /* Zero-based index of the pack to read. */
      uint8_t battery_index;
    } lanyard__health__LegacyBatteryPoll__Request;

    /* Read the state of one battery pack. DEPRECATED -- use the */
    /* BatteryStatus.2.0 message instead. */
    /*  */
    /* TRANSPORT TIER: Classic CAN. */
    /*  */
    /* WHY IT IS STILL HERE: this is the showroom's deprecation example. The */
    /* @deprecated directive marks a definition as scheduled for removal */
    /* without deleting it, so that nodes still speaking it keep working */
    /* while integrators migrate. */
    /*  */
    /* WHAT THE GENERATED CODE CARRIES: every backend appends a `Deprecated: */
    /* ...` notice to this comment block and emits an IS_DEPRECATED constant, */
    /* so the marking is visible to a developer who never opens the DSDL. Go */
    /* picks it up as a real deprecation (its toolchain keys on a */
    /* `Deprecated: ` doc paragraph), and TypeScript additionally gets a `/** */
    /* @deprecated * /` JSDoc block, which is what tsc and editors read. */
    /*  */
    /* Compile-time enforcement in C, C++, and Rust is opt-in behind `dsdlc */
    /* --emit-deprecation-attributes`, which adds */
    /* `__attribute__((deprecated))`, `[[deprecated]]`, and `#[deprecated]` */
    /* respectively. It is not the default because it turns documentation */
    /* into a build diagnostic, and enough of the standard uavcan namespace */
    /* is deprecated that switching it on unannounced would break -Werror */
    /* builds. */
    /*  */
    /* Note the placement: @deprecated appears in the request section, ahead */
    /* of the first attribute, and applies to the whole service. The language */
    /* accepts it nowhere else -- not after a field, and not in the response */
    /* section -- because a service is deprecated or not as a unit; you */
    /* cannot retire half a request/response pair. */
    /*  */
    /* MIGRATION: subscribe to lanyard.health.BatteryStatus.2.0 (port 6251), */
    /* which the pack publishes at 2 Hz on its own. Polling cost the bus a */
    /* request and a response per pack per interval and gave the flight */
    /* controller a scheduling problem it did not need. */
    /*  */
    /* Deprecated: lanyard.health.LegacyBatteryPoll.1.0 is marked @deprecated */
    /* in its DSDL definition and is scheduled for removal; migrate to a newer */
    /* version of this type. */
    typedef struct lanyard__health__LegacyBatteryPoll__Response {
      /* Pack terminal voltage in millivolts. */
      uint16_t voltage_mv;
      /* Pack current in centiamperes. Positive is discharge. */
      int16_t current_ca;
      /* State of charge as a percentage, 0 to 100. */
      uint8_t remaining_pct;
      /* Hottest cell temperature in whole degrees Celsius. Note that the */
      /* replacement message reports temperature in kelvin via the standard SI */
      /* type, so a migration is not a straight field copy. */
      int8_t temperature_c;
    } lanyard__health__LegacyBatteryPoll__Response;

    ```

=== "C++ (std)"

    ```cpp
    // Read the state of one battery pack. DEPRECATED -- use the
    // BatteryStatus.2.0 message instead.
    // 
    // TRANSPORT TIER: Classic CAN.
    // 
    // WHY IT IS STILL HERE: this is the showroom's deprecation example. The
    // @deprecated directive marks a definition as scheduled for removal
    // without deleting it, so that nodes still speaking it keep working
    // while integrators migrate.
    // 
    // WHAT THE GENERATED CODE CARRIES: every backend appends a `Deprecated:
    // ...` notice to this comment block and emits an IS_DEPRECATED constant,
    // so the marking is visible to a developer who never opens the DSDL. Go
    // picks it up as a real deprecation (its toolchain keys on a
    // `Deprecated: ` doc paragraph), and TypeScript additionally gets a `/**
    // @deprecated */` JSDoc block, which is what tsc and editors read.
    // 
    // Compile-time enforcement in C, C++, and Rust is opt-in behind `dsdlc
    // --emit-deprecation-attributes`, which adds
    // `__attribute__((deprecated))`, `[[deprecated]]`, and `#[deprecated]`
    // respectively. It is not the default because it turns documentation
    // into a build diagnostic, and enough of the standard uavcan namespace
    // is deprecated that switching it on unannounced would break -Werror
    // builds.
    // 
    // Note the placement: @deprecated appears in the request section, ahead
    // of the first attribute, and applies to the whole service. The language
    // accepts it nowhere else -- not after a field, and not in the response
    // section -- because a service is deprecated or not as a unit; you
    // cannot retire half a request/response pair.
    // 
    // MIGRATION: subscribe to lanyard.health.BatteryStatus.2.0 (port 6251),
    // which the pack publishes at 2 Hz on its own. Polling cost the bus a
    // request and a response per pack per interval and gave the flight
    // controller a scheduling problem it did not need.
    // 
    // Deprecated: lanyard.health.LegacyBatteryPoll.1.0 is marked @deprecated
    // in its DSDL definition and is scheduled for removal; migrate to a newer
    // version of this type.
    struct LegacyBatteryPoll__Request {
      // Zero-based index of the pack to read.
      std::uint8_t battery_index{};
      static constexpr const char* FULL_NAME = "lanyard.health.LegacyBatteryPoll.Request";
      static constexpr bool IS_DEPRECATED = true;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.health.LegacyBatteryPoll.Request.1.0";
      static constexpr std::size_t EXTENT_BYTES = 1U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 1U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = true;
      static constexpr const char* ZOH_ALIAS_REASON = "eligible";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return LegacyBatteryPoll__Request__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return LegacyBatteryPoll__Request__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return LegacyBatteryPoll__Request__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return LegacyBatteryPoll__Request__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    // Read the state of one battery pack. DEPRECATED -- use the
    // BatteryStatus.2.0 message instead.
    // 
    // TRANSPORT TIER: Classic CAN.
    // 
    // WHY IT IS STILL HERE: this is the showroom's deprecation example. The
    // @deprecated directive marks a definition as scheduled for removal
    // without deleting it, so that nodes still speaking it keep working
    // while integrators migrate.
    // 
    // WHAT THE GENERATED CODE CARRIES: every backend appends a `Deprecated:
    // ...` notice to this comment block and emits an IS_DEPRECATED constant,
    // so the marking is visible to a developer who never opens the DSDL. Go
    // picks it up as a real deprecation (its toolchain keys on a
    // `Deprecated: ` doc paragraph), and TypeScript additionally gets a `/**
    // @deprecated */` JSDoc block, which is what tsc and editors read.
    // 
    // Compile-time enforcement in C, C++, and Rust is opt-in behind `dsdlc
    // --emit-deprecation-attributes`, which adds
    // `__attribute__((deprecated))`, `[[deprecated]]`, and `#[deprecated]`
    // respectively. It is not the default because it turns documentation
    // into a build diagnostic, and enough of the standard uavcan namespace
    // is deprecated that switching it on unannounced would break -Werror
    // builds.
    // 
    // Note the placement: @deprecated appears in the request section, ahead
    // of the first attribute, and applies to the whole service. The language
    // accepts it nowhere else -- not after a field, and not in the response
    // section -- because a service is deprecated or not as a unit; you
    // cannot retire half a request/response pair.
    // 
    // MIGRATION: subscribe to lanyard.health.BatteryStatus.2.0 (port 6251),
    // which the pack publishes at 2 Hz on its own. Polling cost the bus a
    // request and a response per pack per interval and gave the flight
    // controller a scheduling problem it did not need.
    // 
    // Deprecated: lanyard.health.LegacyBatteryPoll.1.0 is marked @deprecated
    // in its DSDL definition and is scheduled for removal; migrate to a newer
    // version of this type.
    struct LegacyBatteryPoll__Response {
      // Pack terminal voltage in millivolts.
      std::uint16_t voltage_mv{};
      // Pack current in centiamperes. Positive is discharge.
      std::int16_t current_ca{};
      // State of charge as a percentage, 0 to 100.
      std::uint8_t remaining_pct{};
      // Hottest cell temperature in whole degrees Celsius. Note that the
      // replacement message reports temperature in kelvin via the standard SI
      // type, so a migration is not a straight field copy.
      std::int8_t temperature_c{};
      static constexpr const char* FULL_NAME = "lanyard.health.LegacyBatteryPoll.Response";
      static constexpr bool IS_DEPRECATED = true;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.health.LegacyBatteryPoll.Response.1.0";
      static constexpr std::size_t EXTENT_BYTES = 6U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 6U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = true;
      static constexpr const char* ZOH_ALIAS_REASON = "eligible";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return LegacyBatteryPoll__Response__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return LegacyBatteryPoll__Response__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return LegacyBatteryPoll__Response__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return LegacyBatteryPoll__Response__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "C++ (pmr)"

    Polymorphic-allocator profile: variable-length fields route through std::pmr.

    ```cpp
    // Read the state of one battery pack. DEPRECATED -- use the
    // BatteryStatus.2.0 message instead.
    // 
    // TRANSPORT TIER: Classic CAN.
    // 
    // WHY IT IS STILL HERE: this is the showroom's deprecation example. The
    // @deprecated directive marks a definition as scheduled for removal
    // without deleting it, so that nodes still speaking it keep working
    // while integrators migrate.
    // 
    // WHAT THE GENERATED CODE CARRIES: every backend appends a `Deprecated:
    // ...` notice to this comment block and emits an IS_DEPRECATED constant,
    // so the marking is visible to a developer who never opens the DSDL. Go
    // picks it up as a real deprecation (its toolchain keys on a
    // `Deprecated: ` doc paragraph), and TypeScript additionally gets a `/**
    // @deprecated */` JSDoc block, which is what tsc and editors read.
    // 
    // Compile-time enforcement in C, C++, and Rust is opt-in behind `dsdlc
    // --emit-deprecation-attributes`, which adds
    // `__attribute__((deprecated))`, `[[deprecated]]`, and `#[deprecated]`
    // respectively. It is not the default because it turns documentation
    // into a build diagnostic, and enough of the standard uavcan namespace
    // is deprecated that switching it on unannounced would break -Werror
    // builds.
    // 
    // Note the placement: @deprecated appears in the request section, ahead
    // of the first attribute, and applies to the whole service. The language
    // accepts it nowhere else -- not after a field, and not in the response
    // section -- because a service is deprecated or not as a unit; you
    // cannot retire half a request/response pair.
    // 
    // MIGRATION: subscribe to lanyard.health.BatteryStatus.2.0 (port 6251),
    // which the pack publishes at 2 Hz on its own. Polling cost the bus a
    // request and a response per pack per interval and gave the flight
    // controller a scheduling problem it did not need.
    // 
    // Deprecated: lanyard.health.LegacyBatteryPoll.1.0 is marked @deprecated
    // in its DSDL definition and is scheduled for removal; migrate to a newer
    // version of this type.
    struct LegacyBatteryPoll__Request {
      // Zero-based index of the pack to read.
      std::uint8_t battery_index{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      LegacyBatteryPoll__Request() = default;
      explicit LegacyBatteryPoll__Request(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
      }
      static constexpr const char* FULL_NAME = "lanyard.health.LegacyBatteryPoll.Request";
      static constexpr bool IS_DEPRECATED = true;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.health.LegacyBatteryPoll.Request.1.0";
      static constexpr std::size_t EXTENT_BYTES = 1U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 1U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = true;
      static constexpr const char* ZOH_ALIAS_REASON = "eligible";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return LegacyBatteryPoll__Request__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return LegacyBatteryPoll__Request__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return LegacyBatteryPoll__Request__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return LegacyBatteryPoll__Request__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return LegacyBatteryPoll__Request__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return LegacyBatteryPoll__Request__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    // Read the state of one battery pack. DEPRECATED -- use the
    // BatteryStatus.2.0 message instead.
    // 
    // TRANSPORT TIER: Classic CAN.
    // 
    // WHY IT IS STILL HERE: this is the showroom's deprecation example. The
    // @deprecated directive marks a definition as scheduled for removal
    // without deleting it, so that nodes still speaking it keep working
    // while integrators migrate.
    // 
    // WHAT THE GENERATED CODE CARRIES: every backend appends a `Deprecated:
    // ...` notice to this comment block and emits an IS_DEPRECATED constant,
    // so the marking is visible to a developer who never opens the DSDL. Go
    // picks it up as a real deprecation (its toolchain keys on a
    // `Deprecated: ` doc paragraph), and TypeScript additionally gets a `/**
    // @deprecated */` JSDoc block, which is what tsc and editors read.
    // 
    // Compile-time enforcement in C, C++, and Rust is opt-in behind `dsdlc
    // --emit-deprecation-attributes`, which adds
    // `__attribute__((deprecated))`, `[[deprecated]]`, and `#[deprecated]`
    // respectively. It is not the default because it turns documentation
    // into a build diagnostic, and enough of the standard uavcan namespace
    // is deprecated that switching it on unannounced would break -Werror
    // builds.
    // 
    // Note the placement: @deprecated appears in the request section, ahead
    // of the first attribute, and applies to the whole service. The language
    // accepts it nowhere else -- not after a field, and not in the response
    // section -- because a service is deprecated or not as a unit; you
    // cannot retire half a request/response pair.
    // 
    // MIGRATION: subscribe to lanyard.health.BatteryStatus.2.0 (port 6251),
    // which the pack publishes at 2 Hz on its own. Polling cost the bus a
    // request and a response per pack per interval and gave the flight
    // controller a scheduling problem it did not need.
    // 
    // Deprecated: lanyard.health.LegacyBatteryPoll.1.0 is marked @deprecated
    // in its DSDL definition and is scheduled for removal; migrate to a newer
    // version of this type.
    struct LegacyBatteryPoll__Response {
      // Pack terminal voltage in millivolts.
      std::uint16_t voltage_mv{};
      // Pack current in centiamperes. Positive is discharge.
      std::int16_t current_ca{};
      // State of charge as a percentage, 0 to 100.
      std::uint8_t remaining_pct{};
      // Hottest cell temperature in whole degrees Celsius. Note that the
      // replacement message reports temperature in kelvin via the standard SI
      // type, so a migration is not a straight field copy.
      std::int8_t temperature_c{};
      ::llvmdsdl::cpp::MemoryResource* _memory_resource{::llvmdsdl::cpp::default_memory_resource()};
      LegacyBatteryPoll__Response() = default;
      explicit LegacyBatteryPoll__Response(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
      void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
        _memory_resource = (memory_resource != nullptr) ? memory_resource : ::llvmdsdl::cpp::default_memory_resource();
      }
      static constexpr const char* FULL_NAME = "lanyard.health.LegacyBatteryPoll.Response";
      static constexpr bool IS_DEPRECATED = true;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.health.LegacyBatteryPoll.Response.1.0";
      static constexpr std::size_t EXTENT_BYTES = 6U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 6U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = true;
      static constexpr const char* ZOH_ALIAS_REASON = "eligible";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return LegacyBatteryPoll__Response__serialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return LegacyBatteryPoll__Response__deserialize_(this, buffer, inout_buffer_size_bytes, _memory_resource);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return LegacyBatteryPoll__Response__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return LegacyBatteryPoll__Response__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) const {
        return LegacyBatteryPoll__Response__serialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, ::llvmdsdl::cpp::MemoryResource* memory_resource) {
        return LegacyBatteryPoll__Response__deserialize_(this, buffer, inout_buffer_size_bytes, memory_resource);
      }
    };

    ```

=== "C++ (autosar)"

    AUTOSAR C++14 subset profile.

    ```cpp
    // Read the state of one battery pack. DEPRECATED -- use the
    // BatteryStatus.2.0 message instead.
    // 
    // TRANSPORT TIER: Classic CAN.
    // 
    // WHY IT IS STILL HERE: this is the showroom's deprecation example. The
    // @deprecated directive marks a definition as scheduled for removal
    // without deleting it, so that nodes still speaking it keep working
    // while integrators migrate.
    // 
    // WHAT THE GENERATED CODE CARRIES: every backend appends a `Deprecated:
    // ...` notice to this comment block and emits an IS_DEPRECATED constant,
    // so the marking is visible to a developer who never opens the DSDL. Go
    // picks it up as a real deprecation (its toolchain keys on a
    // `Deprecated: ` doc paragraph), and TypeScript additionally gets a `/**
    // @deprecated */` JSDoc block, which is what tsc and editors read.
    // 
    // Compile-time enforcement in C, C++, and Rust is opt-in behind `dsdlc
    // --emit-deprecation-attributes`, which adds
    // `__attribute__((deprecated))`, `[[deprecated]]`, and `#[deprecated]`
    // respectively. It is not the default because it turns documentation
    // into a build diagnostic, and enough of the standard uavcan namespace
    // is deprecated that switching it on unannounced would break -Werror
    // builds.
    // 
    // Note the placement: @deprecated appears in the request section, ahead
    // of the first attribute, and applies to the whole service. The language
    // accepts it nowhere else -- not after a field, and not in the response
    // section -- because a service is deprecated or not as a unit; you
    // cannot retire half a request/response pair.
    // 
    // MIGRATION: subscribe to lanyard.health.BatteryStatus.2.0 (port 6251),
    // which the pack publishes at 2 Hz on its own. Polling cost the bus a
    // request and a response per pack per interval and gave the flight
    // controller a scheduling problem it did not need.
    // 
    // Deprecated: lanyard.health.LegacyBatteryPoll.1.0 is marked @deprecated
    // in its DSDL definition and is scheduled for removal; migrate to a newer
    // version of this type.
    struct LegacyBatteryPoll__Request {
      // Zero-based index of the pack to read.
      std::uint8_t battery_index{};
      static constexpr const char* FULL_NAME = "lanyard.health.LegacyBatteryPoll.Request";
      static constexpr bool IS_DEPRECATED = true;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.health.LegacyBatteryPoll.Request.1.0";
      static constexpr std::size_t EXTENT_BYTES = 1U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 1U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = true;
      static constexpr const char* ZOH_ALIAS_REASON = "eligible";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return LegacyBatteryPoll__Request__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return LegacyBatteryPoll__Request__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return LegacyBatteryPoll__Request__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return LegacyBatteryPoll__Request__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    // Read the state of one battery pack. DEPRECATED -- use the
    // BatteryStatus.2.0 message instead.
    // 
    // TRANSPORT TIER: Classic CAN.
    // 
    // WHY IT IS STILL HERE: this is the showroom's deprecation example. The
    // @deprecated directive marks a definition as scheduled for removal
    // without deleting it, so that nodes still speaking it keep working
    // while integrators migrate.
    // 
    // WHAT THE GENERATED CODE CARRIES: every backend appends a `Deprecated:
    // ...` notice to this comment block and emits an IS_DEPRECATED constant,
    // so the marking is visible to a developer who never opens the DSDL. Go
    // picks it up as a real deprecation (its toolchain keys on a
    // `Deprecated: ` doc paragraph), and TypeScript additionally gets a `/**
    // @deprecated */` JSDoc block, which is what tsc and editors read.
    // 
    // Compile-time enforcement in C, C++, and Rust is opt-in behind `dsdlc
    // --emit-deprecation-attributes`, which adds
    // `__attribute__((deprecated))`, `[[deprecated]]`, and `#[deprecated]`
    // respectively. It is not the default because it turns documentation
    // into a build diagnostic, and enough of the standard uavcan namespace
    // is deprecated that switching it on unannounced would break -Werror
    // builds.
    // 
    // Note the placement: @deprecated appears in the request section, ahead
    // of the first attribute, and applies to the whole service. The language
    // accepts it nowhere else -- not after a field, and not in the response
    // section -- because a service is deprecated or not as a unit; you
    // cannot retire half a request/response pair.
    // 
    // MIGRATION: subscribe to lanyard.health.BatteryStatus.2.0 (port 6251),
    // which the pack publishes at 2 Hz on its own. Polling cost the bus a
    // request and a response per pack per interval and gave the flight
    // controller a scheduling problem it did not need.
    // 
    // Deprecated: lanyard.health.LegacyBatteryPoll.1.0 is marked @deprecated
    // in its DSDL definition and is scheduled for removal; migrate to a newer
    // version of this type.
    struct LegacyBatteryPoll__Response {
      // Pack terminal voltage in millivolts.
      std::uint16_t voltage_mv{};
      // Pack current in centiamperes. Positive is discharge.
      std::int16_t current_ca{};
      // State of charge as a percentage, 0 to 100.
      std::uint8_t remaining_pct{};
      // Hottest cell temperature in whole degrees Celsius. Note that the
      // replacement message reports temperature in kelvin via the standard SI
      // type, so a migration is not a straight field copy.
      std::int8_t temperature_c{};
      static constexpr const char* FULL_NAME = "lanyard.health.LegacyBatteryPoll.Response";
      static constexpr bool IS_DEPRECATED = true;
      static constexpr const char* FULL_NAME_AND_VERSION = "lanyard.health.LegacyBatteryPoll.Response.1.0";
      static constexpr std::size_t EXTENT_BYTES = 6U;
      static constexpr std::size_t SERIALIZATION_BUFFER_SIZE_BYTES = 6U;
      static constexpr bool ZOH_ALIAS_ELIGIBLE = true;
      static constexpr const char* ZOH_ALIAS_REASON = "eligible";
      LLVMDSDL_NODISCARD inline std::int8_t serialize(std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) const {
        return LegacyBatteryPoll__Response__serialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD inline std::int8_t deserialize(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return LegacyBatteryPoll__Response__deserialize_(this, buffer, inout_buffer_size_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_deserialize_view(const std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes, const std::uint8_t** out_view_bytes) {
        return LegacyBatteryPoll__Response__try_deserialize_view_(buffer, inout_buffer_size_bytes, out_view_bytes);
      }
      LLVMDSDL_NODISCARD static inline std::int8_t try_serialize_view(const std::uint8_t* view_bytes, std::size_t view_size_bytes, std::uint8_t* buffer, std::size_t* inout_buffer_size_bytes) {
        return LegacyBatteryPoll__Response__try_serialize_view_(view_bytes, view_size_bytes, buffer, inout_buffer_size_bytes);
      }
    };

    ```

=== "Rust (std)"

    ```rust
    /// Read the state of one battery pack. DEPRECATED -- use the
    /// BatteryStatus.2.0 message instead.
    /// 
    /// TRANSPORT TIER: Classic CAN.
    /// 
    /// WHY IT IS STILL HERE: this is the showroom's deprecation example. The
    /// @deprecated directive marks a definition as scheduled for removal
    /// without deleting it, so that nodes still speaking it keep working
    /// while integrators migrate.
    /// 
    /// WHAT THE GENERATED CODE CARRIES: every backend appends a `Deprecated:
    /// ...` notice to this comment block and emits an IS_DEPRECATED constant,
    /// so the marking is visible to a developer who never opens the DSDL. Go
    /// picks it up as a real deprecation (its toolchain keys on a
    /// `Deprecated: ` doc paragraph), and TypeScript additionally gets a `/**
    /// @deprecated */` JSDoc block, which is what tsc and editors read.
    /// 
    /// Compile-time enforcement in C, C++, and Rust is opt-in behind `dsdlc
    /// --emit-deprecation-attributes`, which adds
    /// `__attribute__((deprecated))`, `[[deprecated]]`, and `#[deprecated]`
    /// respectively. It is not the default because it turns documentation
    /// into a build diagnostic, and enough of the standard uavcan namespace
    /// is deprecated that switching it on unannounced would break -Werror
    /// builds.
    /// 
    /// Note the placement: @deprecated appears in the request section, ahead
    /// of the first attribute, and applies to the whole service. The language
    /// accepts it nowhere else -- not after a field, and not in the response
    /// section -- because a service is deprecated or not as a unit; you
    /// cannot retire half a request/response pair.
    /// 
    /// MIGRATION: subscribe to lanyard.health.BatteryStatus.2.0 (port 6251),
    /// which the pack publishes at 2 Hz on its own. Polling cost the bus a
    /// request and a response per pack per interval and gave the flight
    /// controller a scheduling problem it did not need.
    /// 
    /// Deprecated: lanyard.health.LegacyBatteryPoll.1.0 is marked @deprecated
    /// in its DSDL definition and is scheduled for removal; migrate to a newer
    /// version of this type.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_health_LegacyBatteryPoll_1_0_Request {
        /// Zero-based index of the pack to read.
        pub battery_index: u8,
    }

    /// Read the state of one battery pack. DEPRECATED -- use the
    /// BatteryStatus.2.0 message instead.
    /// 
    /// TRANSPORT TIER: Classic CAN.
    /// 
    /// WHY IT IS STILL HERE: this is the showroom's deprecation example. The
    /// @deprecated directive marks a definition as scheduled for removal
    /// without deleting it, so that nodes still speaking it keep working
    /// while integrators migrate.
    /// 
    /// WHAT THE GENERATED CODE CARRIES: every backend appends a `Deprecated:
    /// ...` notice to this comment block and emits an IS_DEPRECATED constant,
    /// so the marking is visible to a developer who never opens the DSDL. Go
    /// picks it up as a real deprecation (its toolchain keys on a
    /// `Deprecated: ` doc paragraph), and TypeScript additionally gets a `/**
    /// @deprecated */` JSDoc block, which is what tsc and editors read.
    /// 
    /// Compile-time enforcement in C, C++, and Rust is opt-in behind `dsdlc
    /// --emit-deprecation-attributes`, which adds
    /// `__attribute__((deprecated))`, `[[deprecated]]`, and `#[deprecated]`
    /// respectively. It is not the default because it turns documentation
    /// into a build diagnostic, and enough of the standard uavcan namespace
    /// is deprecated that switching it on unannounced would break -Werror
    /// builds.
    /// 
    /// Note the placement: @deprecated appears in the request section, ahead
    /// of the first attribute, and applies to the whole service. The language
    /// accepts it nowhere else -- not after a field, and not in the response
    /// section -- because a service is deprecated or not as a unit; you
    /// cannot retire half a request/response pair.
    /// 
    /// MIGRATION: subscribe to lanyard.health.BatteryStatus.2.0 (port 6251),
    /// which the pack publishes at 2 Hz on its own. Polling cost the bus a
    /// request and a response per pack per interval and gave the flight
    /// controller a scheduling problem it did not need.
    /// 
    /// Deprecated: lanyard.health.LegacyBatteryPoll.1.0 is marked @deprecated
    /// in its DSDL definition and is scheduled for removal; migrate to a newer
    /// version of this type.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_health_LegacyBatteryPoll_1_0_Response {
        /// Pack terminal voltage in millivolts.
        pub voltage_mv: u16,
        /// Pack current in centiamperes. Positive is discharge.
        pub current_ca: i16,
        /// State of charge as a percentage, 0 to 100.
        pub remaining_pct: u8,
        /// Hottest cell temperature in whole degrees Celsius. Note that the
        /// replacement message reports temperature in kelvin via the standard SI
        /// type, so a migration is not a straight field copy.
        pub temperature_c: i8,
    }

    ```

=== "Rust (no-std)"

    no_std + alloc profile, as a flight-controller firmware build would use.

    ```rust
    /// Read the state of one battery pack. DEPRECATED -- use the
    /// BatteryStatus.2.0 message instead.
    /// 
    /// TRANSPORT TIER: Classic CAN.
    /// 
    /// WHY IT IS STILL HERE: this is the showroom's deprecation example. The
    /// @deprecated directive marks a definition as scheduled for removal
    /// without deleting it, so that nodes still speaking it keep working
    /// while integrators migrate.
    /// 
    /// WHAT THE GENERATED CODE CARRIES: every backend appends a `Deprecated:
    /// ...` notice to this comment block and emits an IS_DEPRECATED constant,
    /// so the marking is visible to a developer who never opens the DSDL. Go
    /// picks it up as a real deprecation (its toolchain keys on a
    /// `Deprecated: ` doc paragraph), and TypeScript additionally gets a `/**
    /// @deprecated */` JSDoc block, which is what tsc and editors read.
    /// 
    /// Compile-time enforcement in C, C++, and Rust is opt-in behind `dsdlc
    /// --emit-deprecation-attributes`, which adds
    /// `__attribute__((deprecated))`, `[[deprecated]]`, and `#[deprecated]`
    /// respectively. It is not the default because it turns documentation
    /// into a build diagnostic, and enough of the standard uavcan namespace
    /// is deprecated that switching it on unannounced would break -Werror
    /// builds.
    /// 
    /// Note the placement: @deprecated appears in the request section, ahead
    /// of the first attribute, and applies to the whole service. The language
    /// accepts it nowhere else -- not after a field, and not in the response
    /// section -- because a service is deprecated or not as a unit; you
    /// cannot retire half a request/response pair.
    /// 
    /// MIGRATION: subscribe to lanyard.health.BatteryStatus.2.0 (port 6251),
    /// which the pack publishes at 2 Hz on its own. Polling cost the bus a
    /// request and a response per pack per interval and gave the flight
    /// controller a scheduling problem it did not need.
    /// 
    /// Deprecated: lanyard.health.LegacyBatteryPoll.1.0 is marked @deprecated
    /// in its DSDL definition and is scheduled for removal; migrate to a newer
    /// version of this type.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_health_LegacyBatteryPoll_1_0_Request {
        /// Zero-based index of the pack to read.
        pub battery_index: u8,
    }

    /// Read the state of one battery pack. DEPRECATED -- use the
    /// BatteryStatus.2.0 message instead.
    /// 
    /// TRANSPORT TIER: Classic CAN.
    /// 
    /// WHY IT IS STILL HERE: this is the showroom's deprecation example. The
    /// @deprecated directive marks a definition as scheduled for removal
    /// without deleting it, so that nodes still speaking it keep working
    /// while integrators migrate.
    /// 
    /// WHAT THE GENERATED CODE CARRIES: every backend appends a `Deprecated:
    /// ...` notice to this comment block and emits an IS_DEPRECATED constant,
    /// so the marking is visible to a developer who never opens the DSDL. Go
    /// picks it up as a real deprecation (its toolchain keys on a
    /// `Deprecated: ` doc paragraph), and TypeScript additionally gets a `/**
    /// @deprecated */` JSDoc block, which is what tsc and editors read.
    /// 
    /// Compile-time enforcement in C, C++, and Rust is opt-in behind `dsdlc
    /// --emit-deprecation-attributes`, which adds
    /// `__attribute__((deprecated))`, `[[deprecated]]`, and `#[deprecated]`
    /// respectively. It is not the default because it turns documentation
    /// into a build diagnostic, and enough of the standard uavcan namespace
    /// is deprecated that switching it on unannounced would break -Werror
    /// builds.
    /// 
    /// Note the placement: @deprecated appears in the request section, ahead
    /// of the first attribute, and applies to the whole service. The language
    /// accepts it nowhere else -- not after a field, and not in the response
    /// section -- because a service is deprecated or not as a unit; you
    /// cannot retire half a request/response pair.
    /// 
    /// MIGRATION: subscribe to lanyard.health.BatteryStatus.2.0 (port 6251),
    /// which the pack publishes at 2 Hz on its own. Polling cost the bus a
    /// request and a response per pack per interval and gave the flight
    /// controller a scheduling problem it did not need.
    /// 
    /// Deprecated: lanyard.health.LegacyBatteryPoll.1.0 is marked @deprecated
    /// in its DSDL definition and is scheduled for removal; migrate to a newer
    /// version of this type.
    #[derive(Clone, Debug, PartialEq)]
    pub struct lanyard_health_LegacyBatteryPoll_1_0_Response {
        /// Pack terminal voltage in millivolts.
        pub voltage_mv: u16,
        /// Pack current in centiamperes. Positive is discharge.
        pub current_ca: i16,
        /// State of charge as a percentage, 0 to 100.
        pub remaining_pct: u8,
        /// Hottest cell temperature in whole degrees Celsius. Note that the
        /// replacement message reports temperature in kelvin via the standard SI
        /// type, so a migration is not a straight field copy.
        pub temperature_c: i8,
    }

    ```

=== "Go"

    ```go
    // Read the state of one battery pack. DEPRECATED -- use the
    // BatteryStatus.2.0 message instead.
    // 
    // TRANSPORT TIER: Classic CAN.
    // 
    // WHY IT IS STILL HERE: this is the showroom's deprecation example. The
    // @deprecated directive marks a definition as scheduled for removal
    // without deleting it, so that nodes still speaking it keep working
    // while integrators migrate.
    // 
    // WHAT THE GENERATED CODE CARRIES: every backend appends a `Deprecated:
    // ...` notice to this comment block and emits an IS_DEPRECATED constant,
    // so the marking is visible to a developer who never opens the DSDL. Go
    // picks it up as a real deprecation (its toolchain keys on a
    // `Deprecated: ` doc paragraph), and TypeScript additionally gets a `/**
    // @deprecated */` JSDoc block, which is what tsc and editors read.
    // 
    // Compile-time enforcement in C, C++, and Rust is opt-in behind `dsdlc
    // --emit-deprecation-attributes`, which adds
    // `__attribute__((deprecated))`, `[[deprecated]]`, and `#[deprecated]`
    // respectively. It is not the default because it turns documentation
    // into a build diagnostic, and enough of the standard uavcan namespace
    // is deprecated that switching it on unannounced would break -Werror
    // builds.
    // 
    // Note the placement: @deprecated appears in the request section, ahead
    // of the first attribute, and applies to the whole service. The language
    // accepts it nowhere else -- not after a field, and not in the response
    // section -- because a service is deprecated or not as a unit; you
    // cannot retire half a request/response pair.
    // 
    // MIGRATION: subscribe to lanyard.health.BatteryStatus.2.0 (port 6251),
    // which the pack publishes at 2 Hz on its own. Polling cost the bus a
    // request and a response per pack per interval and gave the flight
    // controller a scheduling problem it did not need.
    // 
    // Deprecated: lanyard.health.LegacyBatteryPoll.1.0 is marked @deprecated
    // in its DSDL definition and is scheduled for removal; migrate to a newer
    // version of this type.
    type LegacyBatteryPoll_1_0_Request struct {
      // Zero-based index of the pack to read.
      BatteryIndex uint8
    }

    // Read the state of one battery pack. DEPRECATED -- use the
    // BatteryStatus.2.0 message instead.
    // 
    // TRANSPORT TIER: Classic CAN.
    // 
    // WHY IT IS STILL HERE: this is the showroom's deprecation example. The
    // @deprecated directive marks a definition as scheduled for removal
    // without deleting it, so that nodes still speaking it keep working
    // while integrators migrate.
    // 
    // WHAT THE GENERATED CODE CARRIES: every backend appends a `Deprecated:
    // ...` notice to this comment block and emits an IS_DEPRECATED constant,
    // so the marking is visible to a developer who never opens the DSDL. Go
    // picks it up as a real deprecation (its toolchain keys on a
    // `Deprecated: ` doc paragraph), and TypeScript additionally gets a `/**
    // @deprecated */` JSDoc block, which is what tsc and editors read.
    // 
    // Compile-time enforcement in C, C++, and Rust is opt-in behind `dsdlc
    // --emit-deprecation-attributes`, which adds
    // `__attribute__((deprecated))`, `[[deprecated]]`, and `#[deprecated]`
    // respectively. It is not the default because it turns documentation
    // into a build diagnostic, and enough of the standard uavcan namespace
    // is deprecated that switching it on unannounced would break -Werror
    // builds.
    // 
    // Note the placement: @deprecated appears in the request section, ahead
    // of the first attribute, and applies to the whole service. The language
    // accepts it nowhere else -- not after a field, and not in the response
    // section -- because a service is deprecated or not as a unit; you
    // cannot retire half a request/response pair.
    // 
    // MIGRATION: subscribe to lanyard.health.BatteryStatus.2.0 (port 6251),
    // which the pack publishes at 2 Hz on its own. Polling cost the bus a
    // request and a response per pack per interval and gave the flight
    // controller a scheduling problem it did not need.
    // 
    // Deprecated: lanyard.health.LegacyBatteryPoll.1.0 is marked @deprecated
    // in its DSDL definition and is scheduled for removal; migrate to a newer
    // version of this type.
    type LegacyBatteryPoll_1_0_Response struct {
      // Pack terminal voltage in millivolts.
      VoltageMv uint16
      // Pack current in centiamperes. Positive is discharge.
      CurrentCa int16
      // State of charge as a percentage, 0 to 100.
      RemainingPct uint8
      // Hottest cell temperature in whole degrees Celsius. Note that the
      // replacement message reports temperature in kelvin via the standard SI
      // type, so a migration is not a straight field copy.
      TemperatureC int8
    }

    ```

=== "TypeScript"

    ```typescript
    // Read the state of one battery pack. DEPRECATED -- use the
    // BatteryStatus.2.0 message instead.
    // 
    // TRANSPORT TIER: Classic CAN.
    // 
    // WHY IT IS STILL HERE: this is the showroom's deprecation example. The
    // @deprecated directive marks a definition as scheduled for removal
    // without deleting it, so that nodes still speaking it keep working
    // while integrators migrate.
    // 
    // WHAT THE GENERATED CODE CARRIES: every backend appends a `Deprecated:
    // ...` notice to this comment block and emits an IS_DEPRECATED constant,
    // so the marking is visible to a developer who never opens the DSDL. Go
    // picks it up as a real deprecation (its toolchain keys on a
    // `Deprecated: ` doc paragraph), and TypeScript additionally gets a `/**
    // @deprecated */` JSDoc block, which is what tsc and editors read.
    // 
    // Compile-time enforcement in C, C++, and Rust is opt-in behind `dsdlc
    // --emit-deprecation-attributes`, which adds
    // `__attribute__((deprecated))`, `[[deprecated]]`, and `#[deprecated]`
    // respectively. It is not the default because it turns documentation
    // into a build diagnostic, and enough of the standard uavcan namespace
    // is deprecated that switching it on unannounced would break -Werror
    // builds.
    // 
    // Note the placement: @deprecated appears in the request section, ahead
    // of the first attribute, and applies to the whole service. The language
    // accepts it nowhere else -- not after a field, and not in the response
    // section -- because a service is deprecated or not as a unit; you
    // cannot retire half a request/response pair.
    // 
    // MIGRATION: subscribe to lanyard.health.BatteryStatus.2.0 (port 6251),
    // which the pack publishes at 2 Hz on its own. Polling cost the bus a
    // request and a response per pack per interval and gave the flight
    // controller a scheduling problem it did not need.
    /**
     * @deprecated lanyard.health.LegacyBatteryPoll.1.0 is marked @deprecated in its
     *   DSDL definition and is scheduled for removal; migrate to a newer
     *   version of this type.
     */
    export interface LegacyBatteryPoll_1_0_Request {
      // Zero-based index of the pack to read.
      battery_index: number;
    }

    // Read the state of one battery pack. DEPRECATED -- use the
    // BatteryStatus.2.0 message instead.
    // 
    // TRANSPORT TIER: Classic CAN.
    // 
    // WHY IT IS STILL HERE: this is the showroom's deprecation example. The
    // @deprecated directive marks a definition as scheduled for removal
    // without deleting it, so that nodes still speaking it keep working
    // while integrators migrate.
    // 
    // WHAT THE GENERATED CODE CARRIES: every backend appends a `Deprecated:
    // ...` notice to this comment block and emits an IS_DEPRECATED constant,
    // so the marking is visible to a developer who never opens the DSDL. Go
    // picks it up as a real deprecation (its toolchain keys on a
    // `Deprecated: ` doc paragraph), and TypeScript additionally gets a `/**
    // @deprecated */` JSDoc block, which is what tsc and editors read.
    // 
    // Compile-time enforcement in C, C++, and Rust is opt-in behind `dsdlc
    // --emit-deprecation-attributes`, which adds
    // `__attribute__((deprecated))`, `[[deprecated]]`, and `#[deprecated]`
    // respectively. It is not the default because it turns documentation
    // into a build diagnostic, and enough of the standard uavcan namespace
    // is deprecated that switching it on unannounced would break -Werror
    // builds.
    // 
    // Note the placement: @deprecated appears in the request section, ahead
    // of the first attribute, and applies to the whole service. The language
    // accepts it nowhere else -- not after a field, and not in the response
    // section -- because a service is deprecated or not as a unit; you
    // cannot retire half a request/response pair.
    // 
    // MIGRATION: subscribe to lanyard.health.BatteryStatus.2.0 (port 6251),
    // which the pack publishes at 2 Hz on its own. Polling cost the bus a
    // request and a response per pack per interval and gave the flight
    // controller a scheduling problem it did not need.
    /**
     * @deprecated lanyard.health.LegacyBatteryPoll.1.0 is marked @deprecated in its
     *   DSDL definition and is scheduled for removal; migrate to a newer
     *   version of this type.
     */
    export interface LegacyBatteryPoll_1_0_Response {
      // Pack terminal voltage in millivolts.
      voltage_mv: number;
      // Pack current in centiamperes. Positive is discharge.
      current_ca: number;
      // State of charge as a percentage, 0 to 100.
      remaining_pct: number;
      // Hottest cell temperature in whole degrees Celsius. Note that the
      // replacement message reports temperature in kelvin via the standard SI
      // type, so a migration is not a straight field copy.
      temperature_c: number;
    }

    export type LegacyBatteryPoll_1_0 = LegacyBatteryPoll_1_0_Request;

    ```

=== "Python"

    ```python
    # Read the state of one battery pack. DEPRECATED -- use the
    # BatteryStatus.2.0 message instead.
    # 
    # TRANSPORT TIER: Classic CAN.
    # 
    # WHY IT IS STILL HERE: this is the showroom's deprecation example. The
    # @deprecated directive marks a definition as scheduled for removal
    # without deleting it, so that nodes still speaking it keep working
    # while integrators migrate.
    # 
    # WHAT THE GENERATED CODE CARRIES: every backend appends a `Deprecated:
    # ...` notice to this comment block and emits an IS_DEPRECATED constant,
    # so the marking is visible to a developer who never opens the DSDL. Go
    # picks it up as a real deprecation (its toolchain keys on a
    # `Deprecated: ` doc paragraph), and TypeScript additionally gets a `/**
    # @deprecated */` JSDoc block, which is what tsc and editors read.
    # 
    # Compile-time enforcement in C, C++, and Rust is opt-in behind `dsdlc
    # --emit-deprecation-attributes`, which adds
    # `__attribute__((deprecated))`, `[[deprecated]]`, and `#[deprecated]`
    # respectively. It is not the default because it turns documentation
    # into a build diagnostic, and enough of the standard uavcan namespace
    # is deprecated that switching it on unannounced would break -Werror
    # builds.
    # 
    # Note the placement: @deprecated appears in the request section, ahead
    # of the first attribute, and applies to the whole service. The language
    # accepts it nowhere else -- not after a field, and not in the response
    # section -- because a service is deprecated or not as a unit; you
    # cannot retire half a request/response pair.
    # 
    # MIGRATION: subscribe to lanyard.health.BatteryStatus.2.0 (port 6251),
    # which the pack publishes at 2 Hz on its own. Polling cost the bus a
    # request and a response per pack per interval and gave the flight
    # controller a scheduling problem it did not need.
    # 
    # Deprecated: lanyard.health.LegacyBatteryPoll.1.0 is marked @deprecated
    # in its DSDL definition and is scheduled for removal; migrate to a newer
    # version of this type.
    @dataclass(slots=True)
    class LegacyBatteryPoll_1_0_Request:
        # Zero-based index of the pack to read.
        battery_index: int = 0

    # Read the state of one battery pack. DEPRECATED -- use the
    # BatteryStatus.2.0 message instead.
    # 
    # TRANSPORT TIER: Classic CAN.
    # 
    # WHY IT IS STILL HERE: this is the showroom's deprecation example. The
    # @deprecated directive marks a definition as scheduled for removal
    # without deleting it, so that nodes still speaking it keep working
    # while integrators migrate.
    # 
    # WHAT THE GENERATED CODE CARRIES: every backend appends a `Deprecated:
    # ...` notice to this comment block and emits an IS_DEPRECATED constant,
    # so the marking is visible to a developer who never opens the DSDL. Go
    # picks it up as a real deprecation (its toolchain keys on a
    # `Deprecated: ` doc paragraph), and TypeScript additionally gets a `/**
    # @deprecated */` JSDoc block, which is what tsc and editors read.
    # 
    # Compile-time enforcement in C, C++, and Rust is opt-in behind `dsdlc
    # --emit-deprecation-attributes`, which adds
    # `__attribute__((deprecated))`, `[[deprecated]]`, and `#[deprecated]`
    # respectively. It is not the default because it turns documentation
    # into a build diagnostic, and enough of the standard uavcan namespace
    # is deprecated that switching it on unannounced would break -Werror
    # builds.
    # 
    # Note the placement: @deprecated appears in the request section, ahead
    # of the first attribute, and applies to the whole service. The language
    # accepts it nowhere else -- not after a field, and not in the response
    # section -- because a service is deprecated or not as a unit; you
    # cannot retire half a request/response pair.
    # 
    # MIGRATION: subscribe to lanyard.health.BatteryStatus.2.0 (port 6251),
    # which the pack publishes at 2 Hz on its own. Polling cost the bus a
    # request and a response per pack per interval and gave the flight
    # controller a scheduling problem it did not need.
    # 
    # Deprecated: lanyard.health.LegacyBatteryPoll.1.0 is marked @deprecated
    # in its DSDL definition and is scheduled for removal; migrate to a newer
    # version of this type.
    @dataclass(slots=True)
    class LegacyBatteryPoll_1_0_Response:
        # Pack terminal voltage in millivolts.
        voltage_mv: int = 0
        # Pack current in centiamperes. Positive is discharge.
        current_ca: int = 0
        # State of charge as a percentage, 0 to 100.
        remaining_pct: int = 0
        # Hottest cell temperature in whole degrees Celsius. Note that the
        # replacement message reports temperature in kelvin via the standard SI
        # type, so a migration is not a straight field copy.
        temperature_c: int = 0

    ```
