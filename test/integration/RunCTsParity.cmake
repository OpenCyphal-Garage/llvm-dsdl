cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC OUT_DIR SOURCE_ROOT C_COMPILER TSC_EXECUTABLE NODE_EXECUTABLE)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

if(NOT EXISTS "${DSDLC}")
  message(FATAL_ERROR "dsdlc executable not found: ${DSDLC}")
endif()
if(NOT EXISTS "${SOURCE_ROOT}")
  message(FATAL_ERROR "source root not found: ${SOURCE_ROOT}")
endif()
if(NOT EXISTS "${C_COMPILER}")
  message(FATAL_ERROR "C compiler not found: ${C_COMPILER}")
endif()
if(NOT EXISTS "${TSC_EXECUTABLE}")
  message(FATAL_ERROR "tsc executable not found: ${TSC_EXECUTABLE}")
endif()
if(NOT EXISTS "${NODE_EXECUTABLE}")
  message(FATAL_ERROR "node executable not found: ${NODE_EXECUTABLE}")
endif()

set(random_iterations 128)

set(dsdlc_extra_args "")
if(DEFINED DSDLC_EXTRA_ARGS AND NOT "${DSDLC_EXTRA_ARGS}" STREQUAL "")
  separate_arguments(dsdlc_extra_args NATIVE_COMMAND "${DSDLC_EXTRA_ARGS}")
endif()

if(NOT DEFINED TS_RUNTIME_SPECIALIZATION OR "${TS_RUNTIME_SPECIALIZATION}" STREQUAL "")
  set(TS_RUNTIME_SPECIALIZATION "portable")
endif()
if(NOT "${TS_RUNTIME_SPECIALIZATION}" STREQUAL "portable" AND
   NOT "${TS_RUNTIME_SPECIALIZATION}" STREQUAL "fast")
  message(FATAL_ERROR "Invalid TS_RUNTIME_SPECIALIZATION value: ${TS_RUNTIME_SPECIALIZATION}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(fixture_root "${OUT_DIR}/demo")
set(c_out "${OUT_DIR}/c")
set(ts_out "${OUT_DIR}/ts")
set(work_dir "${OUT_DIR}/work")
file(MAKE_DIRECTORY "${fixture_root}")
file(MAKE_DIRECTORY "${c_out}")
file(MAKE_DIRECTORY "${ts_out}")
file(MAKE_DIRECTORY "${work_dir}")

file(WRITE
  "${fixture_root}/Scalar.1.0.dsdl"
  "uint16 value\n"
  "@sealed\n"
)
file(WRITE
  "${fixture_root}/Vector.1.0.dsdl"
  "uint8[<=5] values\n"
  "@sealed\n"
)
file(WRITE
  "${fixture_root}/UnionTag.1.0.dsdl"
  "@union\n"
  "uint8 first\n"
  "uint16 second\n"
  "uint8 third\n"
  "@sealed\n"
)
file(WRITE
  "${fixture_root}/Delimited.1.0.dsdl"
  "uint8 value\n"
  "@extent 64\n"
)
file(WRITE
  "${fixture_root}/UsesDelimited.1.0.dsdl"
  "demo.Delimited.1.0 nested\n"
  "@sealed\n"
)
file(WRITE
  "${fixture_root}/Svc.1.0.dsdl"
  "uint16 x\n"
  "@sealed\n"
  "---\n"
  "uint8 y\n"
  "@sealed\n"
)
file(WRITE
  "${fixture_root}/DeepLeaf.1.0.dsdl"
  "uint8[<=3] payload\n"
  "@sealed\n"
)
file(WRITE
  "${fixture_root}/DeepInner.1.0.dsdl"
  "demo.DeepLeaf.1.0 node\n"
  "demo.DeepLeaf.1.0[<=2] nodes\n"
  "@sealed\n"
)
file(WRITE
  "${fixture_root}/DeepOuter.1.0.dsdl"
  "demo.DeepInner.1.0 root\n"
  "demo.DeepInner.1.0[<=2] children\n"
  "@sealed\n"
)

execute_process(
  COMMAND
    "${DSDLC}" --target-language c
      "${fixture_root}"
      ${dsdlc_extra_args}
      --outdir "${c_out}"
  RESULT_VARIABLE c_gen_result
  OUTPUT_VARIABLE c_gen_stdout
  ERROR_VARIABLE c_gen_stderr
)
if(NOT c_gen_result EQUAL 0)
  message(STATUS "dsdlc c stdout:\n${c_gen_stdout}")
  message(STATUS "dsdlc c stderr:\n${c_gen_stderr}")
  message(FATAL_ERROR "failed to generate C output for C/TS parity harness")
endif()

execute_process(
  COMMAND
    "${DSDLC}" --target-language ts --versioned-type-names
      "${fixture_root}"
      ${dsdlc_extra_args}
      --outdir "${ts_out}"
      --ts-module "c_ts_parity"
      --ts-runtime-specialization "${TS_RUNTIME_SPECIALIZATION}"
  RESULT_VARIABLE ts_gen_result
  OUTPUT_VARIABLE ts_gen_stdout
  ERROR_VARIABLE ts_gen_stderr
)
if(NOT ts_gen_result EQUAL 0)
  message(STATUS "dsdlc ts stdout:\n${ts_gen_stdout}")
  message(STATUS "dsdlc ts stderr:\n${ts_gen_stderr}")
  message(FATAL_ERROR "failed to generate TypeScript output for C/TS parity harness")
endif()

set(ts_index "${ts_out}/index.ts")
if(NOT EXISTS "${ts_index}")
  message(FATAL_ERROR "generated TypeScript index missing: ${ts_index}")
endif()
file(READ "${ts_index}" ts_index_content)

string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*scalar_1_0\";" scalar_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate scalar_1_0 export alias in ${ts_index}")
endif()
set(ts_scalar_module "${CMAKE_MATCH_1}")

string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*vector_1_0\";" vector_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate vector_1_0 export alias in ${ts_index}")
endif()
set(ts_vector_module "${CMAKE_MATCH_1}")

string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*union_tag_1_0\";" union_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate union_tag_1_0 export alias in ${ts_index}")
endif()
set(ts_union_module "${CMAKE_MATCH_1}")

string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*uses_delimited_1_0\";" delimited_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate uses_delimited_1_0 export alias in ${ts_index}")
endif()
set(ts_delimited_module "${CMAKE_MATCH_1}")

string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*svc_1_0\";" svc_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate svc_1_0 export alias in ${ts_index}")
endif()
set(ts_svc_module "${CMAKE_MATCH_1}")

string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*deep_outer_1_0\";" deep_outer_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate deep_outer_1_0 export alias in ${ts_index}")
endif()
set(ts_deep_outer_module "${CMAKE_MATCH_1}")

set(c_harness_src "${work_dir}/c_ts_parity_harness.c")
file(WRITE
  "${c_harness_src}"
  [=[
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demo/Scalar_1_0.h"
#include "demo/Vector_1_0.h"
#include "demo/UnionTag_1_0.h"
#include "demo/Delimited_1_0.h"
#include "demo/UsesDelimited_1_0.h"
#include "demo/Svc_1_0.h"
#include "demo/DeepLeaf_1_0.h"
#include "demo/DeepInner_1_0.h"
#include "demo/DeepOuter_1_0.h"

static uint32_t prng_next(uint32_t* state) {
  uint32_t x = *state;
  x ^= x << 13U;
  x ^= x >> 17U;
  x ^= x << 5U;
  *state = x;
  return x;
}

static uint32_t hash_u8(uint32_t h, uint8_t v) {
  return (h ^ (uint32_t) v) * 16777619U;
}

static uint32_t hash_u16(uint32_t h, uint16_t v) {
  h = hash_u8(h, (uint8_t) (v & 0xFFU));
  h = hash_u8(h, (uint8_t) ((v >> 8U) & 0xFFU));
  return h;
}

static uint32_t hash_bytes(uint32_t h, const uint8_t* data, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    h = hash_u8(h, data[i]);
  }
  return h;
}

static int run_scalar_random(size_t iterations, uint32_t* out_checksum) {
  uint32_t state = 0x12345678U;
  uint32_t hash = 2166136261U;
  for (size_t i = 0; i < iterations; ++i) {
    demo__Scalar in_obj = {0};
    in_obj.value = (uint16_t) (prng_next(&state) & 0xFFFFU);

    uint8_t out_bytes[4] = {0};
    size_t out_size = sizeof(out_bytes);
    if (demo__Scalar__serialize_(&in_obj, out_bytes, &out_size) != 0) {
      return 2;
    }
    demo__Scalar out_obj = {0};
    size_t consumed = out_size;
    if (demo__Scalar__deserialize_(&out_obj, out_bytes, &consumed) != 0) {
      return 3;
    }
    if (out_obj.value != in_obj.value || consumed != out_size) {
      return 4;
    }

    hash = hash_u8(hash, (uint8_t) out_size);
    hash = hash_bytes(hash, out_bytes, out_size);
    hash = hash_u16(hash, out_obj.value);
    hash = hash_u8(hash, (uint8_t) consumed);
  }
  *out_checksum = hash;
  return 0;
}

static int run_vector_random(size_t iterations, uint32_t* out_checksum) {
  uint32_t state = 0x87654321U;
  uint32_t hash = 2166136261U;
  for (size_t i = 0; i < iterations; ++i) {
    demo__Vector in_obj = {0};
    in_obj.values.count = (size_t) (prng_next(&state) % 6U);
    for (size_t j = 0; j < in_obj.values.count; ++j) {
      in_obj.values.elements[j] = (uint8_t) (prng_next(&state) & 0xFFU);
    }

    uint8_t out_bytes[16] = {0};
    size_t out_size = sizeof(out_bytes);
    if (demo__Vector__serialize_(&in_obj, out_bytes, &out_size) != 0) {
      return 5;
    }

    demo__Vector out_obj = {0};
    size_t consumed = out_size;
    if (demo__Vector__deserialize_(&out_obj, out_bytes, &consumed) != 0) {
      return 6;
    }
    if (out_obj.values.count != in_obj.values.count || consumed != out_size) {
      return 7;
    }
    for (size_t j = 0; j < in_obj.values.count; ++j) {
      if (out_obj.values.elements[j] != in_obj.values.elements[j]) {
        return 8;
      }
    }

    hash = hash_u8(hash, (uint8_t) out_size);
    hash = hash_bytes(hash, out_bytes, out_size);
    hash = hash_u8(hash, (uint8_t) out_obj.values.count);
    for (size_t j = 0; j < out_obj.values.count; ++j) {
      hash = hash_u8(hash, out_obj.values.elements[j]);
    }
    hash = hash_u8(hash, (uint8_t) consumed);
  }
  *out_checksum = hash;
  return 0;
}

static int run_union_random(size_t iterations, uint32_t* out_checksum) {
  uint32_t state = 0x0BADC0DEU;
  uint32_t hash = 2166136261U;
  for (size_t i = 0; i < iterations; ++i) {
    demo__UnionTag in_obj = {0};
    uint8_t choice = (uint8_t) (prng_next(&state) % 3U);
    in_obj._tag_ = choice;
    if (choice == 0U) {
      in_obj.first = (uint8_t) (prng_next(&state) & 0xFFU);
    } else if (choice == 1U) {
      in_obj.second = (uint16_t) (prng_next(&state) & 0xFFFFU);
    } else {
      in_obj.third = (uint8_t) (prng_next(&state) & 0xFFU);
    }

    uint8_t out_bytes[8] = {0};
    size_t out_size = sizeof(out_bytes);
    if (demo__UnionTag__serialize_(&in_obj, out_bytes, &out_size) != 0) {
      return 9;
    }

    demo__UnionTag out_obj = {0};
    size_t consumed = out_size;
    if (demo__UnionTag__deserialize_(&out_obj, out_bytes, &consumed) != 0) {
      return 10;
    }
    if (out_obj._tag_ != in_obj._tag_ || consumed != out_size) {
      return 11;
    }
    if (out_obj._tag_ == 0U && out_obj.first != in_obj.first) {
      return 12;
    }
    if (out_obj._tag_ == 1U && out_obj.second != in_obj.second) {
      return 13;
    }
    if (out_obj._tag_ == 2U && out_obj.third != in_obj.third) {
      return 14;
    }

    hash = hash_u8(hash, (uint8_t) out_size);
    hash = hash_bytes(hash, out_bytes, out_size);
    hash = hash_u8(hash, out_obj._tag_);
    if (out_obj._tag_ == 0U) {
      hash = hash_u8(hash, out_obj.first);
    } else if (out_obj._tag_ == 1U) {
      hash = hash_u16(hash, out_obj.second);
    } else {
      hash = hash_u8(hash, out_obj.third);
    }
    hash = hash_u8(hash, (uint8_t) consumed);
  }
  *out_checksum = hash;
  return 0;
}

static int run_delimited_random(size_t iterations, uint32_t* out_checksum) {
  uint32_t state = 0x31415926U;
  uint32_t hash = 2166136261U;
  for (size_t i = 0; i < iterations; ++i) {
    demo__UsesDelimited in_obj = {0};
    in_obj.nested.value = (uint8_t) (prng_next(&state) & 0xFFU);

    uint8_t out_bytes[16] = {0};
    size_t out_size = sizeof(out_bytes);
    if (demo__UsesDelimited__serialize_(&in_obj, out_bytes, &out_size) != 0) {
      return 15;
    }

    demo__UsesDelimited out_obj = {0};
    size_t consumed = out_size;
    if (demo__UsesDelimited__deserialize_(&out_obj, out_bytes, &consumed) != 0) {
      return 16;
    }
    if (out_obj.nested.value != in_obj.nested.value || consumed != out_size) {
      return 17;
    }

    hash = hash_u8(hash, (uint8_t) out_size);
    hash = hash_bytes(hash, out_bytes, out_size);
    hash = hash_u8(hash, out_obj.nested.value);
    hash = hash_u8(hash, (uint8_t) consumed);
  }
  *out_checksum = hash;
  return 0;
}

static int run_svc_request_random(size_t iterations, uint32_t* out_checksum) {
  uint32_t state = 0x27182818U;
  uint32_t hash = 2166136261U;
  for (size_t i = 0; i < iterations; ++i) {
    demo__Svc__Request in_obj = {0};
    in_obj.x = (uint16_t) (prng_next(&state) & 0xFFFFU);

    uint8_t out_bytes[8] = {0};
    size_t out_size = sizeof(out_bytes);
    if (demo__Svc__Request__serialize_(&in_obj, out_bytes, &out_size) != 0) {
      return 18;
    }

    demo__Svc__Request out_obj = {0};
    size_t consumed = out_size;
    if (demo__Svc__Request__deserialize_(&out_obj, out_bytes, &consumed) != 0) {
      return 19;
    }
    if (out_obj.x != in_obj.x || consumed != out_size) {
      return 20;
    }

    hash = hash_u8(hash, (uint8_t) out_size);
    hash = hash_bytes(hash, out_bytes, out_size);
    hash = hash_u16(hash, out_obj.x);
    hash = hash_u8(hash, (uint8_t) consumed);
  }
  *out_checksum = hash;
  return 0;
}

static int run_svc_response_random(size_t iterations, uint32_t* out_checksum) {
  uint32_t state = 0x11223344U;
  uint32_t hash = 2166136261U;
  for (size_t i = 0; i < iterations; ++i) {
    demo__Svc__Response in_obj = {0};
    in_obj.y = (uint8_t) (prng_next(&state) & 0xFFU);

    uint8_t out_bytes[8] = {0};
    size_t out_size = sizeof(out_bytes);
    if (demo__Svc__Response__serialize_(&in_obj, out_bytes, &out_size) != 0) {
      return 21;
    }

    demo__Svc__Response out_obj = {0};
    size_t consumed = out_size;
    if (demo__Svc__Response__deserialize_(&out_obj, out_bytes, &consumed) != 0) {
      return 22;
    }
    if (out_obj.y != in_obj.y || consumed != out_size) {
      return 23;
    }

    hash = hash_u8(hash, (uint8_t) out_size);
    hash = hash_bytes(hash, out_bytes, out_size);
    hash = hash_u8(hash, out_obj.y);
    hash = hash_u8(hash, (uint8_t) consumed);
  }
  *out_checksum = hash;
  return 0;
}

static int run_scalar_truncated_input(uint32_t* out_checksum) {
  demo__Scalar in_obj = {0};
  in_obj.value = 0x3456U;
  uint8_t bytes[4] = {0};
  size_t size = sizeof(bytes);
  if (demo__Scalar__serialize_(&in_obj, bytes, &size) != 0 || size != 2U) {
    return 28;
  }

  demo__Scalar out_obj = {0};
  size_t short_size = 1U;
  if (demo__Scalar__deserialize_(&out_obj, bytes, &short_size) != 0) {
    return 29;
  }
  if (out_obj.value != 0x56U || short_size != 1U) {
    return 30;
  }

  uint32_t hash = 2166136261U;
  hash = hash_u16(hash, out_obj.value);
  hash = hash_u8(hash, (uint8_t) short_size);
  *out_checksum = hash;
  return 0;
}

static int run_svc_request_truncated_input(uint32_t* out_checksum) {
  demo__Svc__Request in_obj = {0};
  in_obj.x = 0x4567U;
  uint8_t bytes[4] = {0};
  size_t size = sizeof(bytes);
  if (demo__Svc__Request__serialize_(&in_obj, bytes, &size) != 0 || size != 2U) {
    return 31;
  }

  demo__Svc__Request out_obj = {0};
  size_t short_size = 1U;
  if (demo__Svc__Request__deserialize_(&out_obj, bytes, &short_size) != 0) {
    return 32;
  }
  if (out_obj.x != 0x67U || short_size != 1U) {
    return 33;
  }

  uint32_t hash = 2166136261U;
  hash = hash_u16(hash, out_obj.x);
  hash = hash_u8(hash, (uint8_t) short_size);
  *out_checksum = hash;
  return 0;
}

static int run_vector_invalid_length_deserialize(uint32_t* out_checksum) {
  uint8_t bytes[1] = {0x07U};
  size_t size = 1U;
  demo__Vector out_obj = {0};
  if (demo__Vector__deserialize_(&out_obj, bytes, &size) == 0) {
    return 34;
  }
  uint32_t hash = 2166136261U;
  hash = hash_u8(hash, 1U);
  *out_checksum = hash;
  return 0;
}

static int run_vector_invalid_length_serialize(uint32_t* out_checksum) {
  demo__Vector in_obj = {0};
  in_obj.values.count = 6U;
  uint8_t bytes[16] = {0};
  size_t size = sizeof(bytes);
  if (demo__Vector__serialize_(&in_obj, bytes, &size) == 0) {
    return 35;
  }
  uint32_t hash = 2166136261U;
  hash = hash_u8(hash, 1U);
  *out_checksum = hash;
  return 0;
}

static int run_union_invalid_tag_deserialize(uint32_t* out_checksum) {
  uint8_t bytes[1] = {0x03U};
  size_t size = 1U;
  demo__UnionTag out_obj = {0};
  if (demo__UnionTag__deserialize_(&out_obj, bytes, &size) == 0) {
    return 36;
  }
  uint32_t hash = 2166136261U;
  hash = hash_u8(hash, 1U);
  *out_checksum = hash;
  return 0;
}

static int run_delimiter_bad_header_deserialize(uint32_t* out_checksum) {
  demo__UsesDelimited in_obj = {0};
  in_obj.nested.value = 171U;
  uint8_t bytes[16] = {0};
  size_t size = sizeof(bytes);
  if (demo__UsesDelimited__serialize_(&in_obj, bytes, &size) != 0 || size != 5U) {
    return 37;
  }
  bytes[0] = 6U;
  bytes[1] = 0U;
  bytes[2] = 0U;
  bytes[3] = 0U;

  demo__UsesDelimited out_obj = {0};
  size_t consumed = size;
  if (demo__UsesDelimited__deserialize_(&out_obj, bytes, &consumed) == 0) {
    return 38;
  }
  uint32_t hash = 2166136261U;
  hash = hash_u8(hash, 1U);
  *out_checksum = hash;
  return 0;
}

static int run_deep_outer_invalid_length_serialize(uint32_t* out_checksum) {
  demo__DeepOuter in_obj = {0};
  in_obj.root.node.payload.count = 4U;
  uint8_t bytes[256] = {0};
  size_t size = sizeof(bytes);
  if (demo__DeepOuter__serialize_(&in_obj, bytes, &size) == 0) {
    return 39;
  }
  uint32_t hash = 2166136261U;
  hash = hash_u8(hash, 1U);
  *out_checksum = hash;
  return 0;
}

int main(int argc, char** argv) {
  size_t iterations = 128U;
  if (argc > 1) {
    char* end = NULL;
    const unsigned long parsed = strtoul(argv[1], &end, 10);
    if (end == NULL || *end != '\0' || parsed == 0UL) {
      return 100;
    }
    iterations = (size_t) parsed;
  }

  uint32_t checksum = 0U;
  size_t random_cases = 0U;
  size_t directed_cases = 0U;

  if (run_scalar_random(iterations, &checksum) != 0) {
    return 101;
  }
  printf("PASS scalar random (%zu iterations) checksum=%08x\n", iterations, checksum);
  ++random_cases;

  if (run_vector_random(iterations, &checksum) != 0) {
    return 102;
  }
  printf("PASS vector random (%zu iterations) checksum=%08x\n", iterations, checksum);
  ++random_cases;

  if (run_union_random(iterations, &checksum) != 0) {
    return 103;
  }
  printf("PASS union_tag random (%zu iterations) checksum=%08x\n", iterations, checksum);
  ++random_cases;

  if (run_delimited_random(iterations, &checksum) != 0) {
    return 104;
  }
  printf("PASS uses_delimited random (%zu iterations) checksum=%08x\n", iterations, checksum);
  ++random_cases;

  if (run_svc_request_random(iterations, &checksum) != 0) {
    return 105;
  }
  printf("PASS svc_request random (%zu iterations) checksum=%08x\n", iterations, checksum);
  ++random_cases;

  if (run_svc_response_random(iterations, &checksum) != 0) {
    return 106;
  }
  printf("PASS svc_response random (%zu iterations) checksum=%08x\n", iterations, checksum);
  ++random_cases;

  if (run_scalar_truncated_input(&checksum) != 0) {
    return 107;
  }
  printf("PASS scalar_truncated_input directed checksum=%08x\n", checksum);
  ++directed_cases;

  if (run_svc_request_truncated_input(&checksum) != 0) {
    return 108;
  }
  printf("PASS svc_request_truncated_input directed checksum=%08x\n", checksum);
  ++directed_cases;

  if (run_vector_invalid_length_deserialize(&checksum) != 0) {
    return 109;
  }
  printf("PASS vector_invalid_length_deserialize directed checksum=%08x\n", checksum);
  ++directed_cases;

  if (run_vector_invalid_length_serialize(&checksum) != 0) {
    return 110;
  }
  printf("PASS vector_invalid_length_serialize directed checksum=%08x\n", checksum);
  ++directed_cases;

  if (run_union_invalid_tag_deserialize(&checksum) != 0) {
    return 111;
  }
  printf("PASS union_invalid_tag_deserialize directed checksum=%08x\n", checksum);
  ++directed_cases;

  if (run_delimiter_bad_header_deserialize(&checksum) != 0) {
    return 112;
  }
  printf("PASS delimiter_bad_header_deserialize directed checksum=%08x\n", checksum);
  ++directed_cases;

  if (run_deep_outer_invalid_length_serialize(&checksum) != 0) {
    return 114;
  }
  printf("PASS deep_outer_invalid_length_serialize directed checksum=%08x\n", checksum);
  ++directed_cases;

  printf("PASS c/ts directed coverage union_tag_error=1 delimiter_error=1 length_prefix_error=3 truncation=2\n");
  printf("PASS c/ts inventory random_cases=%zu directed_cases=%zu\n", random_cases, directed_cases);
  printf("PASS c/ts parity random_iterations=%zu random_cases=%zu directed_cases=%zu\n",
         iterations,
         random_cases,
         directed_cases);

  return 0;
}
]=]
)

set(c_harness_bin "${work_dir}/c_ts_parity_harness")
execute_process(
  COMMAND
    "${C_COMPILER}"
      -std=c11
      -Wall
      -Wextra
      -Werror
      -I "${c_out}"
      "${c_harness_src}"
      "${c_out}/demo/Scalar_1_0.c"
      "${c_out}/demo/Vector_1_0.c"
      "${c_out}/demo/UnionTag_1_0.c"
      "${c_out}/demo/Delimited_1_0.c"
      "${c_out}/demo/UsesDelimited_1_0.c"
      "${c_out}/demo/Svc_1_0.c"
      "${c_out}/demo/DeepLeaf_1_0.c"
      "${c_out}/demo/DeepInner_1_0.c"
      "${c_out}/demo/DeepOuter_1_0.c"
      -o "${c_harness_bin}"
  RESULT_VARIABLE c_cc_result
  OUTPUT_VARIABLE c_cc_stdout
  ERROR_VARIABLE c_cc_stderr
)
if(NOT c_cc_result EQUAL 0)
  message(STATUS "C compile stdout:\n${c_cc_stdout}")
  message(STATUS "C compile stderr:\n${c_cc_stderr}")
  message(FATAL_ERROR "failed to compile C/TS parity C harness")
endif()

execute_process(
  COMMAND "${c_harness_bin}" "${random_iterations}"
  RESULT_VARIABLE c_run_result
  OUTPUT_VARIABLE c_run_stdout
  ERROR_VARIABLE c_run_stderr
)
if(NOT c_run_result EQUAL 0)
  message(STATUS "C harness stdout:\n${c_run_stdout}")
  message(STATUS "C harness stderr:\n${c_run_stderr}")
  message(FATAL_ERROR "C/TS parity C harness failed")
endif()

set(ts_harness_template
  [=[
import { @ts_scalar_module@, @ts_vector_module@, @ts_union_module@, @ts_delimited_module@, @ts_svc_module@, @ts_deep_outer_module@ } from "./index";

const randomIterations = @random_iterations@;

function prngNext(state: { value: number }): number {
  let x = state.value >>> 0;
  x = (x ^ ((x << 13) >>> 0)) >>> 0;
  x = (x ^ (x >>> 17)) >>> 0;
  x = (x ^ ((x << 5) >>> 0)) >>> 0;
  state.value = x >>> 0;
  return state.value;
}

function hashU8(h: number, v: number): number {
  return Math.imul((h ^ (v & 0xff)) >>> 0, 16777619) >>> 0;
}

function hashU16(h: number, v: number): number {
  let out = hashU8(h, v & 0xff);
  out = hashU8(out, (v >>> 8) & 0xff);
  return out >>> 0;
}

function hashBytes(h: number, bytes: Uint8Array): number {
  let out = h >>> 0;
  for (const b of bytes) {
    out = hashU8(out, b);
  }
  return out >>> 0;
}

function hex32(v: number): string {
  return (v >>> 0).toString(16).padStart(8, "0");
}

function runScalarRandom(iterations: number): number {
  const state = { value: 0x12345678 >>> 0 };
  let hash = 0x811c9dc5 >>> 0;
  for (let i = 0; i < iterations; ++i) {
    const value = prngNext(state) & 0xffff;
    const bytes = @ts_scalar_module@.serializeScalar_1_0({ value });
    const decoded = @ts_scalar_module@.deserializeScalar_1_0(bytes);
    if (decoded.value.value !== value || decoded.consumed !== bytes.length) {
      throw new Error("scalar random mismatch");
    }
    hash = hashU8(hash, bytes.length);
    hash = hashBytes(hash, bytes);
    hash = hashU16(hash, decoded.value.value);
    hash = hashU8(hash, decoded.consumed);
  }
  return hash >>> 0;
}

function runVectorRandom(iterations: number): number {
  const state = { value: 0x87654321 >>> 0 };
  let hash = 0x811c9dc5 >>> 0;
  for (let i = 0; i < iterations; ++i) {
    const count = prngNext(state) % 6;
    const values: number[] = [];
    for (let j = 0; j < count; ++j) {
      values.push(prngNext(state) & 0xff);
    }
    const bytes = @ts_vector_module@.serializeVector_1_0({ values });
    const decoded = @ts_vector_module@.deserializeVector_1_0(bytes);
    if (decoded.value.values.length !== values.length || decoded.consumed !== bytes.length) {
      throw new Error("vector random size mismatch");
    }
    for (let j = 0; j < values.length; ++j) {
      if (decoded.value.values[j] !== values[j]) {
        throw new Error("vector random element mismatch");
      }
    }
    hash = hashU8(hash, bytes.length);
    hash = hashBytes(hash, bytes);
    hash = hashU8(hash, decoded.value.values.length);
    for (const v of decoded.value.values) {
      hash = hashU8(hash, v);
    }
    hash = hashU8(hash, decoded.consumed);
  }
  return hash >>> 0;
}

function runUnionRandom(iterations: number): number {
  const state = { value: 0x0badc0de >>> 0 };
  let hash = 0x811c9dc5 >>> 0;
  for (let i = 0; i < iterations; ++i) {
    const choice = prngNext(state) % 3;
    let value: @ts_union_module@.UnionTag_1_0;
    if (choice === 0) {
      value = { _tag: 0, first: prngNext(state) & 0xff };
    } else if (choice === 1) {
      value = { _tag: 1, second: prngNext(state) & 0xffff };
    } else {
      value = { _tag: 2, third: prngNext(state) & 0xff };
    }
    const bytes = @ts_union_module@.serializeUnionTag_1_0(value);
    const decoded = @ts_union_module@.deserializeUnionTag_1_0(bytes);
    if (decoded.value._tag !== value._tag || decoded.consumed !== bytes.length) {
      throw new Error("union random tag mismatch");
    }
    if (decoded.value._tag === 0 && (!("first" in decoded.value) || !("first" in value) || decoded.value.first !== value.first)) {
      throw new Error("union random first mismatch");
    }
    if (decoded.value._tag === 1 && (!("second" in decoded.value) || !("second" in value) || decoded.value.second !== value.second)) {
      throw new Error("union random second mismatch");
    }
    if (decoded.value._tag === 2 && (!("third" in decoded.value) || !("third" in value) || decoded.value.third !== value.third)) {
      throw new Error("union random third mismatch");
    }

    hash = hashU8(hash, bytes.length);
    hash = hashBytes(hash, bytes);
    hash = hashU8(hash, decoded.value._tag);
    if (decoded.value._tag === 0 && "first" in decoded.value) {
      hash = hashU8(hash, decoded.value.first);
    } else if (decoded.value._tag === 1 && "second" in decoded.value) {
      hash = hashU16(hash, decoded.value.second);
    } else if (decoded.value._tag === 2 && "third" in decoded.value) {
      hash = hashU8(hash, decoded.value.third);
    }
    hash = hashU8(hash, decoded.consumed);
  }
  return hash >>> 0;
}

function runDelimitedRandom(iterations: number): number {
  const state = { value: 0x31415926 >>> 0 };
  let hash = 0x811c9dc5 >>> 0;
  for (let i = 0; i < iterations; ++i) {
    const value: @ts_delimited_module@.UsesDelimited_1_0 = { nested: { value: prngNext(state) & 0xff } };
    const bytes = @ts_delimited_module@.serializeUsesDelimited_1_0(value);
    const decoded = @ts_delimited_module@.deserializeUsesDelimited_1_0(bytes);
    if (decoded.value.nested.value !== value.nested.value || decoded.consumed !== bytes.length) {
      throw new Error("delimited random mismatch");
    }
    hash = hashU8(hash, bytes.length);
    hash = hashBytes(hash, bytes);
    hash = hashU8(hash, decoded.value.nested.value);
    hash = hashU8(hash, decoded.consumed);
  }
  return hash >>> 0;
}

function runSvcRequestRandom(iterations: number): number {
  const state = { value: 0x27182818 >>> 0 };
  let hash = 0x811c9dc5 >>> 0;
  for (let i = 0; i < iterations; ++i) {
    const value: @ts_svc_module@.Svc_1_0_Request = { x: prngNext(state) & 0xffff };
    const bytes = @ts_svc_module@.serializeSvc_1_0_Request(value);
    const decoded = @ts_svc_module@.deserializeSvc_1_0_Request(bytes);
    if (decoded.value.x !== value.x || decoded.consumed !== bytes.length) {
      throw new Error("svc request random mismatch");
    }
    hash = hashU8(hash, bytes.length);
    hash = hashBytes(hash, bytes);
    hash = hashU16(hash, decoded.value.x);
    hash = hashU8(hash, decoded.consumed);
  }
  return hash >>> 0;
}

function runSvcResponseRandom(iterations: number): number {
  const state = { value: 0x11223344 >>> 0 };
  let hash = 0x811c9dc5 >>> 0;
  for (let i = 0; i < iterations; ++i) {
    const value: @ts_svc_module@.Svc_1_0_Response = { y: prngNext(state) & 0xff };
    const bytes = @ts_svc_module@.serializeSvc_1_0_Response(value);
    const decoded = @ts_svc_module@.deserializeSvc_1_0_Response(bytes);
    if (decoded.value.y !== value.y || decoded.consumed !== bytes.length) {
      throw new Error("svc response random mismatch");
    }
    hash = hashU8(hash, bytes.length);
    hash = hashBytes(hash, bytes);
    hash = hashU8(hash, decoded.value.y);
    hash = hashU8(hash, decoded.consumed);
  }
  return hash >>> 0;
}

function runScalarTruncatedInput(): number {
  const full = @ts_scalar_module@.serializeScalar_1_0({ value: 0x3456 });
  const short = full.subarray(0, 1);
  const decoded = @ts_scalar_module@.deserializeScalar_1_0(short);
  if (decoded.value.value !== 0x56 || decoded.consumed !== 1) {
    throw new Error("scalar truncated mismatch");
  }
  let hash = 0x811c9dc5 >>> 0;
  hash = hashU16(hash, decoded.value.value);
  hash = hashU8(hash, decoded.consumed);
  return hash >>> 0;
}

function runSvcRequestTruncatedInput(): number {
  const full = @ts_svc_module@.serializeSvc_1_0_Request({ x: 0x4567 });
  const short = full.subarray(0, 1);
  const decoded = @ts_svc_module@.deserializeSvc_1_0_Request(short);
  if (decoded.value.x !== 0x67 || decoded.consumed !== 1) {
    throw new Error("svc request truncated mismatch");
  }
  let hash = 0x811c9dc5 >>> 0;
  hash = hashU16(hash, decoded.value.x);
  hash = hashU8(hash, decoded.consumed);
  return hash >>> 0;
}

function runVectorInvalidLengthDeserialize(): number {
  let rejected = false;
  try {
    @ts_vector_module@.deserializeVector_1_0(new Uint8Array([0x07]));
  } catch (_err) {
    rejected = true;
  }
  if (!rejected) {
    throw new Error("vector invalid length deserialize unexpectedly accepted");
  }
  let hash = 0x811c9dc5 >>> 0;
  hash = hashU8(hash, 1);
  return hash >>> 0;
}

function runVectorInvalidLengthSerialize(): number {
  let rejected = false;
  try {
    @ts_vector_module@.serializeVector_1_0({ values: [0, 1, 2, 3, 4, 5] });
  } catch (_err) {
    rejected = true;
  }
  if (!rejected) {
    throw new Error("vector invalid length serialize unexpectedly accepted");
  }
  let hash = 0x811c9dc5 >>> 0;
  hash = hashU8(hash, 1);
  return hash >>> 0;
}

function runUnionInvalidTagDeserialize(): number {
  let rejected = false;
  try {
    @ts_union_module@.deserializeUnionTag_1_0(new Uint8Array([0x03]));
  } catch (_err) {
    rejected = true;
  }
  if (!rejected) {
    throw new Error("union invalid tag deserialize unexpectedly accepted");
  }
  let hash = 0x811c9dc5 >>> 0;
  hash = hashU8(hash, 1);
  return hash >>> 0;
}

function runDelimiterBadHeaderDeserialize(): number {
  const valid = @ts_delimited_module@.serializeUsesDelimited_1_0({ nested: { value: 171 } });
  const invalid = new Uint8Array(valid);
  invalid[0] = 6;
  invalid[1] = 0;
  invalid[2] = 0;
  invalid[3] = 0;
  let rejected = false;
  try {
    @ts_delimited_module@.deserializeUsesDelimited_1_0(invalid);
  } catch (_err) {
    rejected = true;
  }
  if (!rejected) {
    throw new Error("delimiter bad header deserialize unexpectedly accepted");
  }
  let hash = 0x811c9dc5 >>> 0;
  hash = hashU8(hash, 1);
  return hash >>> 0;
}

function runDeepOuterInvalidLengthSerialize(): number {
  const value: @ts_deep_outer_module@.DeepOuter_1_0 = {
    root: {
      node: { payload: [1, 2, 3, 4] },
      nodes: [],
    },
    children: [],
  };
  let rejected = false;
  try {
    @ts_deep_outer_module@.serializeDeepOuter_1_0(value);
  } catch (_err) {
    rejected = true;
  }
  if (!rejected) {
    throw new Error("deep outer invalid length serialize unexpectedly accepted");
  }
  let hash = 0x811c9dc5 >>> 0;
  hash = hashU8(hash, 1);
  return hash >>> 0;
}

let randomCases = 0;
let directedCases = 0;

let checksum = runScalarRandom(randomIterations);
console.log(`PASS scalar random (${randomIterations} iterations) checksum=${hex32(checksum)}`);
randomCases += 1;

checksum = runVectorRandom(randomIterations);
console.log(`PASS vector random (${randomIterations} iterations) checksum=${hex32(checksum)}`);
randomCases += 1;

checksum = runUnionRandom(randomIterations);
console.log(`PASS union_tag random (${randomIterations} iterations) checksum=${hex32(checksum)}`);
randomCases += 1;

checksum = runDelimitedRandom(randomIterations);
console.log(`PASS uses_delimited random (${randomIterations} iterations) checksum=${hex32(checksum)}`);
randomCases += 1;

checksum = runSvcRequestRandom(randomIterations);
console.log(`PASS svc_request random (${randomIterations} iterations) checksum=${hex32(checksum)}`);
randomCases += 1;

checksum = runSvcResponseRandom(randomIterations);
console.log(`PASS svc_response random (${randomIterations} iterations) checksum=${hex32(checksum)}`);
randomCases += 1;

checksum = runScalarTruncatedInput();
console.log(`PASS scalar_truncated_input directed checksum=${hex32(checksum)}`);
directedCases += 1;

checksum = runSvcRequestTruncatedInput();
console.log(`PASS svc_request_truncated_input directed checksum=${hex32(checksum)}`);
directedCases += 1;

checksum = runVectorInvalidLengthDeserialize();
console.log(`PASS vector_invalid_length_deserialize directed checksum=${hex32(checksum)}`);
directedCases += 1;

checksum = runVectorInvalidLengthSerialize();
console.log(`PASS vector_invalid_length_serialize directed checksum=${hex32(checksum)}`);
directedCases += 1;

checksum = runUnionInvalidTagDeserialize();
console.log(`PASS union_invalid_tag_deserialize directed checksum=${hex32(checksum)}`);
directedCases += 1;

checksum = runDelimiterBadHeaderDeserialize();
console.log(`PASS delimiter_bad_header_deserialize directed checksum=${hex32(checksum)}`);
directedCases += 1;

checksum = runDeepOuterInvalidLengthSerialize();
console.log(`PASS deep_outer_invalid_length_serialize directed checksum=${hex32(checksum)}`);
directedCases += 1;

console.log("PASS c/ts directed coverage union_tag_error=1 delimiter_error=1 length_prefix_error=3 truncation=2");
console.log(`PASS c/ts inventory random_cases=${randomCases} directed_cases=${directedCases}`);
console.log(`PASS c/ts parity random_iterations=${randomIterations} random_cases=${randomCases} directed_cases=${directedCases}`);
]=]
)
string(CONFIGURE "${ts_harness_template}" ts_harness_content @ONLY)
file(WRITE "${ts_out}/c_ts_parity_main.ts" "${ts_harness_content}")

file(WRITE
  "${ts_out}/tsconfig-c-ts-parity.json"
  "{\n"
  "  \"compilerOptions\": {\n"
  "    \"target\": \"ES2022\",\n"
  "    \"module\": \"CommonJS\",\n"
  "    \"moduleResolution\": \"Node\",\n"
  "    \"strict\": true,\n"
  "    \"skipLibCheck\": true,\n"
  "    \"outDir\": \"./js\"\n"
  "  },\n"
  "  \"include\": [\"./**/*.ts\"]\n"
  "}\n"
)

execute_process(
  COMMAND "${TSC_EXECUTABLE}" -p "${ts_out}/tsconfig-c-ts-parity.json" --pretty false
  WORKING_DIRECTORY "${ts_out}"
  RESULT_VARIABLE tsc_result
  OUTPUT_VARIABLE tsc_stdout
  ERROR_VARIABLE tsc_stderr
)
if(NOT tsc_result EQUAL 0)
  message(STATUS "tsc stdout:\n${tsc_stdout}")
  message(STATUS "tsc stderr:\n${tsc_stderr}")
  message(FATAL_ERROR "failed to compile C/TS parity TypeScript harness")
endif()

file(WRITE "${ts_out}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")

execute_process(
  COMMAND "${NODE_EXECUTABLE}" "${ts_out}/js/c_ts_parity_main.js"
  RESULT_VARIABLE ts_run_result
  OUTPUT_VARIABLE ts_run_stdout
  ERROR_VARIABLE ts_run_stderr
)
if(NOT ts_run_result EQUAL 0)
  message(STATUS "TypeScript harness stdout:\n${ts_run_stdout}")
  message(STATUS "TypeScript harness stderr:\n${ts_run_stderr}")
  message(FATAL_ERROR "C/TS parity TypeScript harness failed")
endif()

string(STRIP "${c_run_stdout}" c_output)
string(STRIP "${ts_run_stdout}" ts_output)
if(NOT c_output STREQUAL ts_output)
  file(WRITE "${OUT_DIR}/c-output.txt" "${c_output}\n")
  file(WRITE "${OUT_DIR}/ts-output.txt" "${ts_output}\n")
  message(FATAL_ERROR
    "C/TS parity mismatch. See ${OUT_DIR}/c-output.txt and ${OUT_DIR}/ts-output.txt.")
endif()

set(parity_output "${c_output}")

set(min_random_iterations 128)
set(expected_random_cases 6)
set(expected_directed_cases 7)
string(REGEX MATCH
  "PASS c/ts parity random_iterations=([0-9]+) random_cases=([0-9]+) directed_cases=([0-9]+)"
  parity_summary_line
  "${parity_output}")
if(NOT parity_summary_line)
  message(FATAL_ERROR "failed to parse C/TS parity summary line from harness output")
endif()
set(observed_random_iterations "${CMAKE_MATCH_1}")
set(observed_random_cases "${CMAKE_MATCH_2}")
set(observed_directed_cases "${CMAKE_MATCH_3}")
if(observed_random_iterations LESS min_random_iterations)
  message(FATAL_ERROR
    "C/TS parity random-iteration regression: observed=${observed_random_iterations}, required>=${min_random_iterations}")
endif()
if(NOT observed_random_cases EQUAL expected_random_cases)
  message(FATAL_ERROR
    "C/TS parity random-case drift: observed=${observed_random_cases}, expected=${expected_random_cases}")
endif()
if(NOT observed_directed_cases EQUAL expected_directed_cases)
  message(FATAL_ERROR
    "C/TS parity directed-case drift: observed=${observed_directed_cases}, expected=${expected_directed_cases}")
endif()

string(REGEX MATCH
  "PASS c/ts inventory random_cases=([0-9]+) directed_cases=([0-9]+)"
  inventory_summary_match
  "${parity_output}")
if(NOT inventory_summary_match)
  message(FATAL_ERROR "missing C/TS parity inventory summary marker")
endif()
set(inventory_random_cases "${CMAKE_MATCH_1}")
set(inventory_directed_cases "${CMAKE_MATCH_2}")
if(NOT inventory_random_cases EQUAL observed_random_cases OR
   NOT inventory_directed_cases EQUAL observed_directed_cases)
  message(FATAL_ERROR
    "C/TS parity inventory mismatch: inventory random=${inventory_random_cases}, "
    "inventory directed=${inventory_directed_cases}, summary random=${observed_random_cases}, "
    "summary directed=${observed_directed_cases}")
endif()

string(REGEX MATCHALL
  "PASS [A-Za-z0-9_]+ random \\([0-9]+ iterations\\) checksum=[0-9a-f]+"
  random_pass_lines
  "${parity_output}")
list(LENGTH random_pass_lines observed_random_pass_lines)
if(NOT observed_random_pass_lines EQUAL observed_random_cases)
  message(FATAL_ERROR
    "C/TS random execution count mismatch: pass-lines=${observed_random_pass_lines}, summary random=${observed_random_cases}")
endif()

string(REGEX MATCHALL
  "PASS [A-Za-z0-9_]+ directed checksum=[0-9a-f]+"
  directed_pass_lines
  "${parity_output}")
list(LENGTH directed_pass_lines observed_directed_pass_lines)
if(NOT observed_directed_pass_lines EQUAL observed_directed_cases)
  message(FATAL_ERROR
    "C/TS directed execution count mismatch: pass-lines=${observed_directed_pass_lines}, summary directed=${observed_directed_cases}")
endif()

set(required_markers
  "PASS scalar random ("
  "PASS vector random ("
  "PASS union_tag random ("
  "PASS uses_delimited random ("
  "PASS svc_request random ("
  "PASS svc_response random ("
  "PASS scalar_truncated_input directed checksum="
  "PASS svc_request_truncated_input directed checksum="
  "PASS vector_invalid_length_deserialize directed checksum="
  "PASS vector_invalid_length_serialize directed checksum="
  "PASS union_invalid_tag_deserialize directed checksum="
  "PASS delimiter_bad_header_deserialize directed checksum="
  "PASS deep_outer_invalid_length_serialize directed checksum="
)
foreach(marker IN LISTS required_markers)
  string(FIND "${parity_output}" "${marker}" marker_pos)
  if(marker_pos EQUAL -1)
    message(FATAL_ERROR "required C/TS parity marker missing: ${marker}")
  endif()
endforeach()

string(REGEX MATCH
  "PASS c/ts directed coverage union_tag_error=([0-9]+) delimiter_error=([0-9]+) length_prefix_error=([0-9]+) truncation=([0-9]+)"
  directed_coverage_line
  "${parity_output}")
if(NOT directed_coverage_line)
  message(FATAL_ERROR "missing C/TS directed coverage summary marker")
endif()
if(NOT CMAKE_MATCH_1 EQUAL 1 OR NOT CMAKE_MATCH_2 EQUAL 1 OR NOT CMAKE_MATCH_3 EQUAL 3 OR NOT CMAKE_MATCH_4 EQUAL 2)
  message(FATAL_ERROR
    "C/TS directed coverage mismatch: union_tag_error=${CMAKE_MATCH_1}, delimiter_error=${CMAKE_MATCH_2}, "
    "length_prefix_error=${CMAKE_MATCH_3}, truncation=${CMAKE_MATCH_4}")
endif()

set(summary_file "${OUT_DIR}/c-ts-parity-summary.txt")
string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef summary_nonce)
set(summary_tmp "${summary_file}.tmp-${summary_nonce}")
file(WRITE "${summary_tmp}" "${parity_output}\n")
file(RENAME "${summary_tmp}" "${summary_file}")
message(STATUS "C/TS parity summary:\n${parity_output}")
