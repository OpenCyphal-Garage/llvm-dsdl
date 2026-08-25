cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC OUT_DIR C_COMPILER TSC_EXECUTABLE NODE_EXECUTABLE SOURCE_ROOT)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

if(NOT EXISTS "${DSDLC}")
  message(FATAL_ERROR "dsdlc executable not found: ${DSDLC}")
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

if(NOT DEFINED FIXTURE_ROOT OR "${FIXTURE_ROOT}" STREQUAL "")
  set(FIXTURE_ROOT "${SOURCE_ROOT}/test/integration/fixtures/signed_narrow/vendor")
endif()
if(NOT EXISTS "${FIXTURE_ROOT}")
  message(FATAL_ERROR "signed_narrow fixture root not found: ${FIXTURE_ROOT}")
endif()

if(NOT DEFINED ITERATIONS OR "${ITERATIONS}" STREQUAL "")
  set(ITERATIONS "256")
endif()

set(dsdlc_extra_args "")
if(DEFINED DSDLC_EXTRA_ARGS AND NOT "${DSDLC_EXTRA_ARGS}" STREQUAL "")
  separate_arguments(dsdlc_extra_args NATIVE_COMMAND "${DSDLC_EXTRA_ARGS}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(c_out "${OUT_DIR}/c")
set(ts_out "${OUT_DIR}/ts")
set(work_dir "${OUT_DIR}/work")
file(MAKE_DIRECTORY "${c_out}")
file(MAKE_DIRECTORY "${ts_out}")
file(MAKE_DIRECTORY "${work_dir}")

execute_process(
  COMMAND
    "${DSDLC}" --target-language c
      "${FIXTURE_ROOT}"
      ${dsdlc_extra_args}
      --outdir "${c_out}"
  RESULT_VARIABLE c_gen_result
  OUTPUT_VARIABLE c_gen_stdout
  ERROR_VARIABLE c_gen_stderr
)
if(NOT c_gen_result EQUAL 0)
  message(STATUS "dsdlc c stdout:\n${c_gen_stdout}")
  message(STATUS "dsdlc c stderr:\n${c_gen_stderr}")
  message(FATAL_ERROR "failed to generate signed_narrow C output for C/TS parity")
endif()

execute_process(
  COMMAND
    "${DSDLC}" --target-language ts --versioned-type-names
      "${FIXTURE_ROOT}"
      ${dsdlc_extra_args}
      --outdir "${ts_out}"
      --ts-module "signed_narrow_c_ts_parity"
  RESULT_VARIABLE ts_gen_result
  OUTPUT_VARIABLE ts_gen_stdout
  ERROR_VARIABLE ts_gen_stderr
)
if(NOT ts_gen_result EQUAL 0)
  message(STATUS "dsdlc ts stdout:\n${ts_gen_stdout}")
  message(STATUS "dsdlc ts stderr:\n${ts_gen_stderr}")
  message(FATAL_ERROR "failed to generate signed_narrow TypeScript output for C/TS parity")
endif()

set(ts_index "${ts_out}/index.ts")
if(NOT EXISTS "${ts_index}")
  message(FATAL_ERROR "generated TypeScript index missing: ${ts_index}")
endif()
file(READ "${ts_index}" ts_index_content)

string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*int3sat_1_0\";" sat_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate int3_sat_1_0 export alias in ${ts_index}")
endif()
set(ts_sat_module "${CMAKE_MATCH_1}")

string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*int3trunc_1_0\";" trunc_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate int3_trunc_1_0 export alias in ${ts_index}")
endif()
set(ts_trunc_module "${CMAKE_MATCH_1}")

set(c_harness_src "${work_dir}/signed_narrow_c_ts_parity_harness.c")
file(WRITE
  "${c_harness_src}"
  [=[
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "vendor/Int3Sat_1_0.h"
#include "vendor/Int3Trunc_1_0.h"

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

static uint32_t hash_i8(uint32_t h, int8_t v) {
  return hash_u8(h, (uint8_t) v);
}

static uint32_t hash_bytes(uint32_t h, const uint8_t* data, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    h = hash_u8(h, data[i]);
  }
  return h;
}

static int run_int3sat_random(size_t iterations, uint32_t* out_checksum) {
  uint32_t state = 0xA50A50A5U;
  uint32_t hash = 2166136261U;
  for (size_t i = 0; i < iterations; ++i) {
    vendor__Int3Sat in_obj = {0};
    in_obj.value = (int8_t) (prng_next(&state) & 0xFFU);
    uint8_t out_bytes[2] = {0};
    size_t out_size = sizeof(out_bytes);
    if (vendor__Int3Sat__serialize_(&in_obj, out_bytes, &out_size) != 0) {
      return 2;
    }
    vendor__Int3Sat out_obj = {0};
    size_t consumed = out_size;
    if (vendor__Int3Sat__deserialize_(&out_obj, out_bytes, &consumed) != 0) {
      return 3;
    }
    if (consumed != out_size) {
      return 4;
    }
    hash = hash_u8(hash, (uint8_t) out_size);
    hash = hash_bytes(hash, out_bytes, out_size);
    hash = hash_i8(hash, out_obj.value);
    hash = hash_u8(hash, (uint8_t) consumed);
  }
  *out_checksum = hash;
  return 0;
}

static int run_int3trunc_random(size_t iterations, uint32_t* out_checksum) {
  uint32_t state = 0x5A05A05AU;
  uint32_t hash = 2166136261U;
  for (size_t i = 0; i < iterations; ++i) {
    vendor__Int3Trunc in_obj = {0};
    in_obj.value = (int8_t) (prng_next(&state) & 0xFFU);
    uint8_t out_bytes[2] = {0};
    size_t out_size = sizeof(out_bytes);
    if (vendor__Int3Trunc__serialize_(&in_obj, out_bytes, &out_size) != 0) {
      return 5;
    }
    vendor__Int3Trunc out_obj = {0};
    size_t consumed = out_size;
    if (vendor__Int3Trunc__deserialize_(&out_obj, out_bytes, &consumed) != 0) {
      return 6;
    }
    if (consumed != out_size) {
      return 7;
    }
    hash = hash_u8(hash, (uint8_t) out_size);
    hash = hash_bytes(hash, out_bytes, out_size);
    hash = hash_i8(hash, out_obj.value);
    hash = hash_u8(hash, (uint8_t) consumed);
  }
  *out_checksum = hash;
  return 0;
}

static int serialize_sat_expect(int8_t value, uint8_t expected, uint32_t* out_checksum) {
  vendor__Int3Sat obj = {0};
  obj.value = value;
  uint8_t out_bytes[2] = {0};
  size_t out_size = sizeof(out_bytes);
  if (vendor__Int3Sat__serialize_(&obj, out_bytes, &out_size) != 0 || out_size != 1U) {
    return 8;
  }
  if (out_bytes[0] != expected) {
    return 9;
  }
  uint32_t hash = 2166136261U;
  hash = hash_u8(hash, out_bytes[0]);
  *out_checksum = hash;
  return 0;
}

static int serialize_trunc_expect(int8_t value, uint8_t expected, uint32_t* out_checksum) {
  vendor__Int3Trunc obj = {0};
  obj.value = value;
  uint8_t out_bytes[2] = {0};
  size_t out_size = sizeof(out_bytes);
  if (vendor__Int3Trunc__serialize_(&obj, out_bytes, &out_size) != 0 || out_size != 1U) {
    return 10;
  }
  if (out_bytes[0] != expected) {
    return 11;
  }
  uint32_t hash = 2166136261U;
  hash = hash_u8(hash, out_bytes[0]);
  *out_checksum = hash;
  return 0;
}

static int deserialize_sat_expect(uint8_t sample, int8_t expected, uint32_t* out_checksum) {
  vendor__Int3Sat obj = {0};
  size_t consumed = 1U;
  if (vendor__Int3Sat__deserialize_(&obj, &sample, &consumed) != 0 || consumed != 1U) {
    return 12;
  }
  if (obj.value != expected) {
    return 13;
  }
  uint32_t hash = 2166136261U;
  hash = hash_i8(hash, obj.value);
  hash = hash_u8(hash, (uint8_t) consumed);
  *out_checksum = hash;
  return 0;
}

static int deserialize_trunc_expect(uint8_t sample, int8_t expected, uint32_t* out_checksum) {
  vendor__Int3Trunc obj = {0};
  size_t consumed = 1U;
  if (vendor__Int3Trunc__deserialize_(&obj, &sample, &consumed) != 0 || consumed != 1U) {
    return 14;
  }
  if (obj.value != expected) {
    return 15;
  }
  uint32_t hash = 2166136261U;
  hash = hash_i8(hash, obj.value);
  hash = hash_u8(hash, (uint8_t) consumed);
  *out_checksum = hash;
  return 0;
}

static int truncated_sat_expect(uint32_t* out_checksum) {
  vendor__Int3Sat obj = {0};
  uint8_t bytes[1] = {0U};
  size_t consumed = 0U;
  if (vendor__Int3Sat__deserialize_(&obj, bytes, &consumed) != 0) {
    return 16;
  }
  if (obj.value != 0 || consumed != 0U) {
    return 17;
  }
  uint32_t hash = 2166136261U;
  hash = hash_i8(hash, obj.value);
  hash = hash_u8(hash, (uint8_t) consumed);
  *out_checksum = hash;
  return 0;
}

static int truncated_trunc_expect(uint32_t* out_checksum) {
  vendor__Int3Trunc obj = {0};
  uint8_t bytes[1] = {0U};
  size_t consumed = 0U;
  if (vendor__Int3Trunc__deserialize_(&obj, bytes, &consumed) != 0) {
    return 18;
  }
  if (obj.value != 0 || consumed != 0U) {
    return 19;
  }
  uint32_t hash = 2166136261U;
  hash = hash_i8(hash, obj.value);
  hash = hash_u8(hash, (uint8_t) consumed);
  *out_checksum = hash;
  return 0;
}

int main(int argc, char** argv) {
  size_t iterations = 256U;
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

  if (run_int3sat_random(iterations, &checksum) != 0) {
    return 101;
  }
  printf("PASS vendor.Int3Sat.1.0 random (%zu iterations) checksum=%08x\n", iterations, checksum);
  ++random_cases;

  if (run_int3trunc_random(iterations, &checksum) != 0) {
    return 102;
  }
  printf("PASS vendor.Int3Trunc.1.0 random (%zu iterations) checksum=%08x\n", iterations, checksum);
  ++random_cases;

  if (serialize_sat_expect(7, 0x03U, &checksum) != 0) {
    return 103;
  }
  printf("PASS int3sat_serialize_plus7_saturated directed checksum=%08x\n", checksum);
  ++directed_cases;

  if (serialize_sat_expect(-9, 0x04U, &checksum) != 0) {
    return 104;
  }
  printf("PASS int3sat_serialize_minus9_saturated directed checksum=%08x\n", checksum);
  ++directed_cases;

  if (serialize_trunc_expect(5, 0x05U, &checksum) != 0) {
    return 105;
  }
  printf("PASS int3trunc_serialize_plus5_truncated directed checksum=%08x\n", checksum);
  ++directed_cases;

  if (serialize_trunc_expect(-5, 0x03U, &checksum) != 0) {
    return 106;
  }
  printf("PASS int3trunc_serialize_minus5_truncated directed checksum=%08x\n", checksum);
  ++directed_cases;

  if (deserialize_sat_expect(0x07U, -1, &checksum) != 0) {
    return 107;
  }
  printf("PASS int3sat_sign_extend_0x07 directed checksum=%08x\n", checksum);
  ++directed_cases;

  if (deserialize_sat_expect(0x04U, -4, &checksum) != 0) {
    return 108;
  }
  printf("PASS int3sat_sign_extend_0x04 directed checksum=%08x\n", checksum);
  ++directed_cases;

  if (deserialize_trunc_expect(0x05U, -3, &checksum) != 0) {
    return 109;
  }
  printf("PASS int3trunc_sign_extend_0x05 directed checksum=%08x\n", checksum);
  ++directed_cases;

  if (deserialize_trunc_expect(0x03U, 3, &checksum) != 0) {
    return 110;
  }
  printf("PASS int3trunc_sign_extend_0x03 directed checksum=%08x\n", checksum);
  ++directed_cases;

  if (truncated_sat_expect(&checksum) != 0) {
    return 111;
  }
  printf("PASS int3sat_truncated_input directed checksum=%08x\n", checksum);
  ++directed_cases;

  if (truncated_trunc_expect(&checksum) != 0) {
    return 112;
  }
  printf("PASS int3trunc_truncated_input directed checksum=%08x\n", checksum);
  ++directed_cases;

  printf("PASS signed-narrow-c-ts-parity directed categories saturation_sign_extension=8 truncation=2\n");
  printf("PASS signed-narrow-c-ts-parity inventory random_cases=%zu directed_cases=%zu\n", random_cases, directed_cases);
  printf("PASS signed-narrow-c-ts-parity random_iterations=%zu random_cases=%zu directed_cases=%zu\n",
         iterations,
         random_cases,
         directed_cases);
  return 0;
}
]=]
)

set(c_harness_bin "${work_dir}/signed_narrow_c_ts_parity_harness")
execute_process(
  COMMAND
    "${C_COMPILER}"
      -std=c11
      -Wall
      -Wextra
      -Werror
      -I "${c_out}"
      "${c_harness_src}"
      "${c_out}/vendor/Int3Sat_1_0.c"
      "${c_out}/vendor/Int3Trunc_1_0.c"
      -o "${c_harness_bin}"
  RESULT_VARIABLE c_cc_result
  OUTPUT_VARIABLE c_cc_stdout
  ERROR_VARIABLE c_cc_stderr
)
if(NOT c_cc_result EQUAL 0)
  message(STATUS "C compile stdout:\n${c_cc_stdout}")
  message(STATUS "C compile stderr:\n${c_cc_stderr}")
  message(FATAL_ERROR "failed to compile signed_narrow C/TS parity C harness")
endif()

execute_process(
  COMMAND "${c_harness_bin}" "${ITERATIONS}"
  RESULT_VARIABLE c_run_result
  OUTPUT_VARIABLE c_run_stdout
  ERROR_VARIABLE c_run_stderr
)
if(NOT c_run_result EQUAL 0)
  message(STATUS "C harness stdout:\n${c_run_stdout}")
  message(STATUS "C harness stderr:\n${c_run_stderr}")
  message(FATAL_ERROR "signed_narrow C/TS parity C harness failed")
endif()

set(ts_harness_template
  [=[
import { @ts_sat_module@, @ts_trunc_module@ } from "./index";

const iterations = @ITERATIONS@;

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

function hashI8(h: number, v: number): number {
  return hashU8(h, v & 0xff);
}

function hashBytes(h: number, data: Uint8Array): number {
  let out = h >>> 0;
  for (const b of data) {
    out = hashU8(out, b);
  }
  return out >>> 0;
}

function hex32(v: number): string {
  return (v >>> 0).toString(16).padStart(8, "0");
}

function toI8(raw: number): number {
  const v = raw & 0xff;
  return v >= 0x80 ? v - 0x100 : v;
}

function runInt3SatRandom(iterCount: number): number {
  const state = { value: 0xa50a50a5 >>> 0 };
  let hash = 0x811c9dc5 >>> 0;
  for (let i = 0; i < iterCount; ++i) {
    const value = toI8(prngNext(state));
    const bytes = @ts_sat_module@.serializeInt3Sat_1_0({ value });
    const decoded = @ts_sat_module@.deserializeInt3Sat_1_0(bytes);
    if (decoded.consumed !== bytes.length) {
      throw new Error("int3sat random consumed mismatch");
    }
    hash = hashU8(hash, bytes.length);
    hash = hashBytes(hash, bytes);
    hash = hashI8(hash, decoded.value.value);
    hash = hashU8(hash, decoded.consumed);
  }
  return hash >>> 0;
}

function runInt3TruncRandom(iterCount: number): number {
  const state = { value: 0x5a05a05a >>> 0 };
  let hash = 0x811c9dc5 >>> 0;
  for (let i = 0; i < iterCount; ++i) {
    const value = toI8(prngNext(state));
    const bytes = @ts_trunc_module@.serializeInt3Trunc_1_0({ value });
    const decoded = @ts_trunc_module@.deserializeInt3Trunc_1_0(bytes);
    if (decoded.consumed !== bytes.length) {
      throw new Error("int3trunc random consumed mismatch");
    }
    hash = hashU8(hash, bytes.length);
    hash = hashBytes(hash, bytes);
    hash = hashI8(hash, decoded.value.value);
    hash = hashU8(hash, decoded.consumed);
  }
  return hash >>> 0;
}

function serializeSatExpect(value: number, expected: number): number {
  const out = @ts_sat_module@.serializeInt3Sat_1_0({ value });
  if (out.length !== 1 || out[0] !== expected) {
    throw new Error("int3sat directed serialize mismatch");
  }
  let hash = 0x811c9dc5 >>> 0;
  hash = hashU8(hash, out[0]);
  return hash >>> 0;
}

function serializeTruncExpect(value: number, expected: number): number {
  const out = @ts_trunc_module@.serializeInt3Trunc_1_0({ value });
  if (out.length !== 1 || out[0] !== expected) {
    throw new Error("int3trunc directed serialize mismatch");
  }
  let hash = 0x811c9dc5 >>> 0;
  hash = hashU8(hash, out[0]);
  return hash >>> 0;
}

function deserializeSatExpect(sample: number, expected: number): number {
  const decoded = @ts_sat_module@.deserializeInt3Sat_1_0(new Uint8Array([sample]));
  if (decoded.consumed !== 1 || decoded.value.value !== expected) {
    throw new Error("int3sat sign extension mismatch");
  }
  let hash = 0x811c9dc5 >>> 0;
  hash = hashI8(hash, decoded.value.value);
  hash = hashU8(hash, decoded.consumed);
  return hash >>> 0;
}

function deserializeTruncExpect(sample: number, expected: number): number {
  const decoded = @ts_trunc_module@.deserializeInt3Trunc_1_0(new Uint8Array([sample]));
  if (decoded.consumed !== 1 || decoded.value.value !== expected) {
    throw new Error("int3trunc sign extension mismatch");
  }
  let hash = 0x811c9dc5 >>> 0;
  hash = hashI8(hash, decoded.value.value);
  hash = hashU8(hash, decoded.consumed);
  return hash >>> 0;
}

function truncatedSatExpect(): number {
  const decoded = @ts_sat_module@.deserializeInt3Sat_1_0(new Uint8Array());
  if (decoded.consumed !== 0 || decoded.value.value !== 0) {
    throw new Error("int3sat truncated input mismatch");
  }
  let hash = 0x811c9dc5 >>> 0;
  hash = hashI8(hash, decoded.value.value);
  hash = hashU8(hash, decoded.consumed);
  return hash >>> 0;
}

function truncatedTruncExpect(): number {
  const decoded = @ts_trunc_module@.deserializeInt3Trunc_1_0(new Uint8Array());
  if (decoded.consumed !== 0 || decoded.value.value !== 0) {
    throw new Error("int3trunc truncated input mismatch");
  }
  let hash = 0x811c9dc5 >>> 0;
  hash = hashI8(hash, decoded.value.value);
  hash = hashU8(hash, decoded.consumed);
  return hash >>> 0;
}

let randomCases = 0;
let directedCases = 0;

let checksum = runInt3SatRandom(iterations);
console.log(`PASS vendor.Int3Sat.1.0 random (${iterations} iterations) checksum=${hex32(checksum)}`);
randomCases += 1;

checksum = runInt3TruncRandom(iterations);
console.log(`PASS vendor.Int3Trunc.1.0 random (${iterations} iterations) checksum=${hex32(checksum)}`);
randomCases += 1;

checksum = serializeSatExpect(7, 0x03);
console.log(`PASS int3sat_serialize_plus7_saturated directed checksum=${hex32(checksum)}`);
directedCases += 1;

checksum = serializeSatExpect(-9, 0x04);
console.log(`PASS int3sat_serialize_minus9_saturated directed checksum=${hex32(checksum)}`);
directedCases += 1;

checksum = serializeTruncExpect(5, 0x05);
console.log(`PASS int3trunc_serialize_plus5_truncated directed checksum=${hex32(checksum)}`);
directedCases += 1;

checksum = serializeTruncExpect(-5, 0x03);
console.log(`PASS int3trunc_serialize_minus5_truncated directed checksum=${hex32(checksum)}`);
directedCases += 1;

checksum = deserializeSatExpect(0x07, -1);
console.log(`PASS int3sat_sign_extend_0x07 directed checksum=${hex32(checksum)}`);
directedCases += 1;

checksum = deserializeSatExpect(0x04, -4);
console.log(`PASS int3sat_sign_extend_0x04 directed checksum=${hex32(checksum)}`);
directedCases += 1;

checksum = deserializeTruncExpect(0x05, -3);
console.log(`PASS int3trunc_sign_extend_0x05 directed checksum=${hex32(checksum)}`);
directedCases += 1;

checksum = deserializeTruncExpect(0x03, 3);
console.log(`PASS int3trunc_sign_extend_0x03 directed checksum=${hex32(checksum)}`);
directedCases += 1;

checksum = truncatedSatExpect();
console.log(`PASS int3sat_truncated_input directed checksum=${hex32(checksum)}`);
directedCases += 1;

checksum = truncatedTruncExpect();
console.log(`PASS int3trunc_truncated_input directed checksum=${hex32(checksum)}`);
directedCases += 1;

console.log("PASS signed-narrow-c-ts-parity directed categories saturation_sign_extension=8 truncation=2");
console.log(`PASS signed-narrow-c-ts-parity inventory random_cases=${randomCases} directed_cases=${directedCases}`);
console.log(`PASS signed-narrow-c-ts-parity random_iterations=${iterations} random_cases=${randomCases} directed_cases=${directedCases}`);
]=]
)
string(CONFIGURE "${ts_harness_template}" ts_harness_content @ONLY)
file(WRITE "${ts_out}/signed_narrow_c_ts_parity.ts" "${ts_harness_content}")

file(WRITE
  "${ts_out}/tsconfig-signed-narrow-c-ts-parity.json"
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
  COMMAND "${TSC_EXECUTABLE}" -p "${ts_out}/tsconfig-signed-narrow-c-ts-parity.json" --pretty false
  WORKING_DIRECTORY "${ts_out}"
  RESULT_VARIABLE tsc_result
  OUTPUT_VARIABLE tsc_stdout
  ERROR_VARIABLE tsc_stderr
)
if(NOT tsc_result EQUAL 0)
  message(STATUS "tsc stdout:\n${tsc_stdout}")
  message(STATUS "tsc stderr:\n${tsc_stderr}")
  message(FATAL_ERROR "failed to compile signed_narrow C/TS parity TypeScript harness")
endif()

file(WRITE "${ts_out}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")

execute_process(
  COMMAND "${NODE_EXECUTABLE}" "${ts_out}/js/signed_narrow_c_ts_parity.js"
  RESULT_VARIABLE ts_run_result
  OUTPUT_VARIABLE ts_run_stdout
  ERROR_VARIABLE ts_run_stderr
)
if(NOT ts_run_result EQUAL 0)
  message(STATUS "TypeScript harness stdout:\n${ts_run_stdout}")
  message(STATUS "TypeScript harness stderr:\n${ts_run_stderr}")
  message(FATAL_ERROR "signed_narrow C/TS parity TypeScript harness failed")
endif()

string(STRIP "${c_run_stdout}" c_output)
string(STRIP "${ts_run_stdout}" ts_output)
if(NOT c_output STREQUAL ts_output)
  file(WRITE "${OUT_DIR}/c-output.txt" "${c_output}\n")
  file(WRITE "${OUT_DIR}/ts-output.txt" "${ts_output}\n")
  message(FATAL_ERROR
    "signed_narrow C/TS parity mismatch. See ${OUT_DIR}/c-output.txt and ${OUT_DIR}/ts-output.txt.")
endif()

set(parity_output "${c_output}")

set(min_iterations 256)
set(expected_random_cases 2)
set(expected_directed_cases 10)
string(REGEX MATCH
  "PASS signed-narrow-c-ts-parity random_iterations=([0-9]+) random_cases=([0-9]+) directed_cases=([0-9]+)"
  summary_line
  "${parity_output}")
if(NOT summary_line)
  message(FATAL_ERROR "failed to parse signed_narrow C/TS parity summary line from harness output")
endif()
set(observed_iterations "${CMAKE_MATCH_1}")
set(observed_random_cases "${CMAKE_MATCH_2}")
set(observed_directed_cases "${CMAKE_MATCH_3}")
if(observed_iterations LESS min_iterations)
  message(FATAL_ERROR
    "signed_narrow C/TS parity iteration regression: observed=${observed_iterations}, required>=${min_iterations}")
endif()
if(NOT observed_random_cases EQUAL expected_random_cases)
  message(FATAL_ERROR
    "signed_narrow C/TS parity random-case drift: observed=${observed_random_cases}, expected=${expected_random_cases}")
endif()
if(NOT observed_directed_cases EQUAL expected_directed_cases)
  message(FATAL_ERROR
    "signed_narrow C/TS parity directed-case drift: observed=${observed_directed_cases}, expected=${expected_directed_cases}")
endif()

string(REGEX MATCH
  "PASS signed-narrow-c-ts-parity inventory random_cases=([0-9]+) directed_cases=([0-9]+)"
  inventory_line
  "${parity_output}")
if(NOT inventory_line)
  message(FATAL_ERROR "missing signed_narrow C/TS parity inventory marker")
endif()
set(inventory_random_cases "${CMAKE_MATCH_1}")
set(inventory_directed_cases "${CMAKE_MATCH_2}")
if(NOT inventory_random_cases EQUAL observed_random_cases OR
   NOT inventory_directed_cases EQUAL observed_directed_cases)
  message(FATAL_ERROR
    "signed_narrow C/TS inventory mismatch: inventory random=${inventory_random_cases}, "
    "inventory directed=${inventory_directed_cases}, summary random=${observed_random_cases}, "
    "summary directed=${observed_directed_cases}")
endif()

string(REGEX MATCHALL
  "PASS [A-Za-z0-9_.]+ random \\([0-9]+ iterations\\) checksum=[0-9a-f]+"
  random_pass_lines
  "${parity_output}")
list(LENGTH random_pass_lines observed_random_pass_lines)
if(NOT observed_random_pass_lines EQUAL observed_random_cases)
  message(FATAL_ERROR
    "signed_narrow C/TS random execution count mismatch: pass-lines=${observed_random_pass_lines}, "
    "summary random=${observed_random_cases}")
endif()

string(REGEX MATCHALL
  "PASS [A-Za-z0-9_]+ directed checksum=[0-9a-f]+"
  directed_pass_lines
  "${parity_output}")
list(LENGTH directed_pass_lines observed_directed_pass_lines)
if(NOT observed_directed_pass_lines EQUAL observed_directed_cases)
  message(FATAL_ERROR
    "signed_narrow C/TS directed execution count mismatch: pass-lines=${observed_directed_pass_lines}, "
    "summary directed=${observed_directed_cases}")
endif()

string(REGEX MATCH
  "PASS signed-narrow-c-ts-parity directed categories saturation_sign_extension=([0-9]+) truncation=([0-9]+)"
  category_line
  "${parity_output}")
if(NOT category_line)
  message(FATAL_ERROR "missing signed_narrow C/TS directed category summary")
endif()
if(CMAKE_MATCH_1 LESS 8 OR CMAKE_MATCH_2 LESS 2)
  message(FATAL_ERROR
    "signed_narrow C/TS directed category regression: saturation_sign_extension=${CMAKE_MATCH_1}, truncation=${CMAKE_MATCH_2}")
endif()

set(required_markers
  "PASS vendor.Int3Sat.1.0 random ("
  "PASS vendor.Int3Trunc.1.0 random ("
  "PASS int3sat_serialize_plus7_saturated directed checksum="
  "PASS int3sat_serialize_minus9_saturated directed checksum="
  "PASS int3trunc_serialize_plus5_truncated directed checksum="
  "PASS int3trunc_serialize_minus5_truncated directed checksum="
  "PASS int3sat_sign_extend_0x07 directed checksum="
  "PASS int3sat_sign_extend_0x04 directed checksum="
  "PASS int3trunc_sign_extend_0x05 directed checksum="
  "PASS int3trunc_sign_extend_0x03 directed checksum="
  "PASS int3sat_truncated_input directed checksum="
  "PASS int3trunc_truncated_input directed checksum="
)
foreach(marker IN LISTS required_markers)
  string(FIND "${parity_output}" "${marker}" marker_pos)
  if(marker_pos EQUAL -1)
    message(FATAL_ERROR "required signed_narrow C/TS marker missing: ${marker}")
  endif()
endforeach()

set(summary_file "${OUT_DIR}/signed-narrow-c-ts-parity-summary.txt")
string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef summary_nonce)
set(summary_tmp "${summary_file}.tmp-${summary_nonce}")
file(WRITE "${summary_tmp}" "${parity_output}\n")
file(RENAME "${summary_tmp}" "${summary_file}")
message(STATUS "Signed narrow C/TS parity summary:\n${parity_output}")
