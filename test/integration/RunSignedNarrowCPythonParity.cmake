cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC OUT_DIR C_COMPILER PYTHON_EXECUTABLE SOURCE_ROOT)
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
if(NOT EXISTS "${PYTHON_EXECUTABLE}")
  message(FATAL_ERROR "python executable not found: ${PYTHON_EXECUTABLE}")
endif()
if(NOT EXISTS "${SOURCE_ROOT}")
  message(FATAL_ERROR "source root not found: ${SOURCE_ROOT}")
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

set(py_runtime_specialization_arg "")
if(DEFINED PY_RUNTIME_SPECIALIZATION AND NOT "${PY_RUNTIME_SPECIALIZATION}" STREQUAL "")
  if(NOT "${PY_RUNTIME_SPECIALIZATION}" STREQUAL "portable" AND
     NOT "${PY_RUNTIME_SPECIALIZATION}" STREQUAL "fast")
    message(FATAL_ERROR "Invalid PY_RUNTIME_SPECIALIZATION value: ${PY_RUNTIME_SPECIALIZATION}")
  endif()
  set(py_runtime_specialization_arg --py-runtime-specialization "${PY_RUNTIME_SPECIALIZATION}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(c_out "${OUT_DIR}/c")
set(py_out "${OUT_DIR}/py")
set(work_dir "${OUT_DIR}/work")
file(MAKE_DIRECTORY "${c_out}")
file(MAKE_DIRECTORY "${py_out}")
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
  message(FATAL_ERROR "failed to generate signed_narrow C output for C/Python parity")
endif()

set(py_package "signed_narrow_c_python_parity")
execute_process(
  COMMAND
    "${DSDLC}" --target-language python --versioned-type-names
      "${FIXTURE_ROOT}"
      ${dsdlc_extra_args}
      --outdir "${py_out}"
      --py-package "${py_package}"
      ${py_runtime_specialization_arg}
  RESULT_VARIABLE py_gen_result
  OUTPUT_VARIABLE py_gen_stdout
  ERROR_VARIABLE py_gen_stderr
)
if(NOT py_gen_result EQUAL 0)
  message(STATUS "dsdlc python stdout:\n${py_gen_stdout}")
  message(STATUS "dsdlc python stderr:\n${py_gen_stderr}")
  message(FATAL_ERROR "failed to generate signed_narrow Python output for C/Python parity")
endif()

set(c_harness_src "${work_dir}/signed_narrow_c_python_parity_harness.c")
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
  printf("PASS int3sat_truncated_zero_fill directed checksum=%08x\n", checksum);
  ++directed_cases;

  if (truncated_trunc_expect(&checksum) != 0) {
    return 112;
  }
  printf("PASS int3trunc_truncated_zero_fill directed checksum=%08x\n", checksum);
  ++directed_cases;

  printf("PASS signed-narrow-c-python-parity directed categories saturation_sign_extension=8 truncation=2\n");
  printf("PASS signed-narrow-c-python-parity inventory random_cases=%zu directed_cases=%zu\n",
         random_cases,
         directed_cases);
  printf("PASS signed-narrow-c-python-parity random_iterations=%zu random_cases=%zu directed_cases=%zu\n",
         iterations,
         random_cases,
         directed_cases);
  return 0;
}
]=]
)

set(c_harness_bin "${work_dir}/signed_narrow_c_python_parity_harness")
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
  message(FATAL_ERROR "failed to compile signed_narrow C/Python parity C harness")
endif()

set(py_harness_script "${work_dir}/signed_narrow_c_python_parity_harness.py")
file(WRITE
  "${py_harness_script}"
  [=[
from __future__ import annotations

import importlib
import sys

PKG = "@PY_PACKAGE@"
Int3Sat_1_0 = importlib.import_module(f"{PKG}.vendor.int3sat_1_0").Int3Sat_1_0
Int3Trunc_1_0 = importlib.import_module(f"{PKG}.vendor.int3trunc_1_0").Int3Trunc_1_0


def prng_next(state: int) -> tuple[int, int]:
  x = state & 0xFFFFFFFF
  x ^= (x << 13) & 0xFFFFFFFF
  x ^= (x >> 17) & 0xFFFFFFFF
  x ^= (x << 5) & 0xFFFFFFFF
  return x & 0xFFFFFFFF, x & 0xFFFFFFFF


def hash_u8(h: int, v: int) -> int:
  return ((h ^ (v & 0xFF)) * 16777619) & 0xFFFFFFFF


def hash_i8(h: int, v: int) -> int:
  return hash_u8(h, v & 0xFF)


def hash_bytes(h: int, data: bytes) -> int:
  out = h
  for b in data:
    out = hash_u8(out, b)
  return out


def to_i8(v: int) -> int:
  x = v & 0xFF
  return x - 256 if x >= 128 else x


def run_int3sat_random(iterations: int) -> int:
  state = 0xA50A50A5
  h = 2166136261
  for _ in range(iterations):
    state, rnd = prng_next(state)
    in_obj = Int3Sat_1_0(value=to_i8(rnd))
    out_bytes = in_obj.serialize()
    out_obj = Int3Sat_1_0.deserialize(out_bytes)
    consumed = len(out_bytes)
    h = hash_u8(h, len(out_bytes))
    h = hash_bytes(h, out_bytes)
    h = hash_i8(h, int(out_obj.value))
    h = hash_u8(h, consumed)
  return h


def run_int3trunc_random(iterations: int) -> int:
  state = 0x5A05A05A
  h = 2166136261
  for _ in range(iterations):
    state, rnd = prng_next(state)
    in_obj = Int3Trunc_1_0(value=to_i8(rnd))
    out_bytes = in_obj.serialize()
    out_obj = Int3Trunc_1_0.deserialize(out_bytes)
    consumed = len(out_bytes)
    h = hash_u8(h, len(out_bytes))
    h = hash_bytes(h, out_bytes)
    h = hash_i8(h, int(out_obj.value))
    h = hash_u8(h, consumed)
  return h


def serialize_sat_expect(value: int, expected: int) -> int:
  out_bytes = Int3Sat_1_0(value=value).serialize()
  assert len(out_bytes) == 1
  assert out_bytes[0] == expected
  h = 2166136261
  return hash_u8(h, out_bytes[0])


def serialize_trunc_expect(value: int, expected: int) -> int:
  out_bytes = Int3Trunc_1_0(value=value).serialize()
  assert len(out_bytes) == 1
  assert out_bytes[0] == expected
  h = 2166136261
  return hash_u8(h, out_bytes[0])


def deserialize_sat_expect(sample: int, expected: int) -> int:
  out_obj = Int3Sat_1_0.deserialize(bytes([sample]))
  h = 2166136261
  h = hash_i8(h, int(out_obj.value))
  h = hash_u8(h, 1)
  assert int(out_obj.value) == expected
  return h


def deserialize_trunc_expect(sample: int, expected: int) -> int:
  out_obj = Int3Trunc_1_0.deserialize(bytes([sample]))
  h = 2166136261
  h = hash_i8(h, int(out_obj.value))
  h = hash_u8(h, 1)
  assert int(out_obj.value) == expected
  return h


def truncated_sat_expect() -> int:
  out_obj = Int3Sat_1_0.deserialize(bytes())
  assert int(out_obj.value) == 0
  h = 2166136261
  h = hash_i8(h, int(out_obj.value))
  h = hash_u8(h, 0)
  return h


def truncated_trunc_expect() -> int:
  out_obj = Int3Trunc_1_0.deserialize(bytes())
  assert int(out_obj.value) == 0
  h = 2166136261
  h = hash_i8(h, int(out_obj.value))
  h = hash_u8(h, 0)
  return h


def main() -> int:
  iterations = int(sys.argv[1]) if len(sys.argv) > 1 else 256
  random_cases = 0
  directed_cases = 0

  checksum = run_int3sat_random(iterations)
  print(f"PASS vendor.Int3Sat.1.0 random ({iterations} iterations) checksum={checksum:08x}")
  random_cases += 1

  checksum = run_int3trunc_random(iterations)
  print(f"PASS vendor.Int3Trunc.1.0 random ({iterations} iterations) checksum={checksum:08x}")
  random_cases += 1

  checksum = serialize_sat_expect(7, 0x03)
  print(f"PASS int3sat_serialize_plus7_saturated directed checksum={checksum:08x}")
  directed_cases += 1

  checksum = serialize_sat_expect(-9, 0x04)
  print(f"PASS int3sat_serialize_minus9_saturated directed checksum={checksum:08x}")
  directed_cases += 1

  checksum = serialize_trunc_expect(5, 0x05)
  print(f"PASS int3trunc_serialize_plus5_truncated directed checksum={checksum:08x}")
  directed_cases += 1

  checksum = serialize_trunc_expect(-5, 0x03)
  print(f"PASS int3trunc_serialize_minus5_truncated directed checksum={checksum:08x}")
  directed_cases += 1

  checksum = deserialize_sat_expect(0x07, -1)
  print(f"PASS int3sat_sign_extend_0x07 directed checksum={checksum:08x}")
  directed_cases += 1

  checksum = deserialize_sat_expect(0x04, -4)
  print(f"PASS int3sat_sign_extend_0x04 directed checksum={checksum:08x}")
  directed_cases += 1

  checksum = deserialize_trunc_expect(0x05, -3)
  print(f"PASS int3trunc_sign_extend_0x05 directed checksum={checksum:08x}")
  directed_cases += 1

  checksum = deserialize_trunc_expect(0x03, 3)
  print(f"PASS int3trunc_sign_extend_0x03 directed checksum={checksum:08x}")
  directed_cases += 1

  checksum = truncated_sat_expect()
  print(f"PASS int3sat_truncated_zero_fill directed checksum={checksum:08x}")
  directed_cases += 1

  checksum = truncated_trunc_expect()
  print(f"PASS int3trunc_truncated_zero_fill directed checksum={checksum:08x}")
  directed_cases += 1

  print("PASS signed-narrow-c-python-parity directed categories saturation_sign_extension=8 truncation=2")
  print(f"PASS signed-narrow-c-python-parity inventory random_cases={random_cases} directed_cases={directed_cases}")
  print(f"PASS signed-narrow-c-python-parity random_iterations={iterations} random_cases={random_cases} directed_cases={directed_cases}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
]=]
)

file(READ "${py_harness_script}" py_harness_content)
string(REPLACE "@PY_PACKAGE@" "${py_package}" py_harness_content "${py_harness_content}")
file(WRITE "${py_harness_script}" "${py_harness_content}")

execute_process(
  COMMAND "${c_harness_bin}" "${ITERATIONS}"
  RESULT_VARIABLE c_run_result
  OUTPUT_VARIABLE c_run_stdout
  ERROR_VARIABLE c_run_stderr
)
if(NOT c_run_result EQUAL 0)
  message(STATUS "C harness stdout:\n${c_run_stdout}")
  message(STATUS "C harness stderr:\n${c_run_stderr}")
  message(FATAL_ERROR "signed_narrow C harness failed")
endif()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env
      "PYTHONPATH=${py_out}"
      "LLVMDSDL_PY_RUNTIME_MODE=pure"
      "${PYTHON_EXECUTABLE}" "${py_harness_script}" "${ITERATIONS}"
  RESULT_VARIABLE py_run_result
  OUTPUT_VARIABLE py_run_stdout
  ERROR_VARIABLE py_run_stderr
)
if(NOT py_run_result EQUAL 0)
  message(STATUS "Python harness stdout:\n${py_run_stdout}")
  message(STATUS "Python harness stderr:\n${py_run_stderr}")
  message(FATAL_ERROR "signed_narrow Python harness failed")
endif()

string(STRIP "${c_run_stdout}" c_output)
string(STRIP "${py_run_stdout}" py_output)
if(NOT c_output STREQUAL py_output)
  file(WRITE "${OUT_DIR}/c-output.txt" "${c_output}\n")
  file(WRITE "${OUT_DIR}/python-output.txt" "${py_output}\n")
  message(FATAL_ERROR
    "signed_narrow C vs Python parity mismatch. See ${OUT_DIR}/c-output.txt and ${OUT_DIR}/python-output.txt.")
endif()

message(STATUS "signed_narrow C<->Python parity passed")
