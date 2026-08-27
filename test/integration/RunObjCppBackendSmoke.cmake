cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC FIXTURES_ROOT OUT_DIR C_COMPILER CXX_COMPILER)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

if(NOT EXISTS "${DSDLC}")
  message(FATAL_ERROR "dsdlc executable not found: ${DSDLC}")
endif()
if(NOT EXISTS "${FIXTURES_ROOT}")
  message(FATAL_ERROR "fixtures root not found: ${FIXTURES_ROOT}")
endif()
if(NOT EXISTS "${C_COMPILER}")
  message(FATAL_ERROR "C compiler not found: ${C_COMPILER}")
endif()
if(NOT EXISTS "${CXX_COMPILER}")
  message(FATAL_ERROR "C++ compiler not found: ${CXX_COMPILER}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(archive_name "fixtures_obj_cpp")
set(little_out "${OUT_DIR}/little")
set(big_out "${OUT_DIR}/big")
set(no_archive_out "${OUT_DIR}/no-archive")
file(MAKE_DIRECTORY "${little_out}")
file(MAKE_DIRECTORY "${big_out}")
file(MAKE_DIRECTORY "${no_archive_out}")

# The obj backend publishes the C and C++ headers a consumer writes against, so its naming scheme is
# a user-visible choice like any other backend's. Registered under both.
if(NOT DEFINED TYPE_NAME_SCHEME OR "${TYPE_NAME_SCHEME}" STREQUAL "")
  set(TYPE_NAME_SCHEME "unversioned")
endif()
set(V1_0 "")
set(scheme_args "")
if(TYPE_NAME_SCHEME STREQUAL "versioned")
  set(V1_0 "_1_0")
  set(scheme_args --versioned-type-names)
elseif(NOT TYPE_NAME_SCHEME STREQUAL "unversioned")
  message(FATAL_ERROR "TYPE_NAME_SCHEME must be \"versioned\" or \"unversioned\", got \"${TYPE_NAME_SCHEME}\"")
endif()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env "CC=${C_COMPILER}" "CXX=${CXX_COMPILER}"
      "${DSDLC}" --target-language obj ${scheme_args} --obj-abi-language cpp --target-endianness little
      --jobs 1 --obj-archive-name "${archive_name}" --outdir "${little_out}" "${FIXTURES_ROOT}"
  RESULT_VARIABLE little_result
  OUTPUT_VARIABLE little_stdout
  ERROR_VARIABLE little_stderr
)
if(NOT little_result EQUAL 0)
  message(STATUS "little obj-cpp stdout:\n${little_stdout}")
  message(STATUS "little obj-cpp stderr:\n${little_stderr}")
  message(FATAL_ERROR "obj-cpp generation failed for little-endian")
endif()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env "CC=${C_COMPILER}" "CXX=${CXX_COMPILER}"
      "${DSDLC}" --target-language obj ${scheme_args} --obj-abi-language cpp --target-endianness big
      --jobs 2 --obj-archive-name "${archive_name}" --outdir "${big_out}" "${FIXTURES_ROOT}"
  RESULT_VARIABLE big_result
  OUTPUT_VARIABLE big_stdout
  ERROR_VARIABLE big_stderr
)
if(NOT big_result EQUAL 0)
  message(STATUS "big obj-cpp stdout:\n${big_stdout}")
  message(STATUS "big obj-cpp stderr:\n${big_stderr}")
  message(FATAL_ERROR "obj-cpp generation failed for big-endian")
endif()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env "CC=${C_COMPILER}" "CXX=${CXX_COMPILER}"
      "${DSDLC}" --target-language obj ${scheme_args} --obj-abi-language cpp --target-endianness little
      --jobs 4 --obj-no-archive --outdir "${no_archive_out}" "${FIXTURES_ROOT}"
  RESULT_VARIABLE no_archive_result
  OUTPUT_VARIABLE no_archive_stdout
  ERROR_VARIABLE no_archive_stderr
)
if(NOT no_archive_result EQUAL 0)
  message(STATUS "obj-cpp --obj-no-archive stdout:\n${no_archive_stdout}")
  message(STATUS "obj-cpp --obj-no-archive stderr:\n${no_archive_stderr}")
  message(FATAL_ERROR "obj-cpp generation failed for --obj-no-archive")
endif()

set(little_archive "${little_out}/${archive_name}.a")
set(big_archive "${big_out}/${archive_name}.a")
if(NOT EXISTS "${little_archive}")
  message(FATAL_ERROR "missing little-endian obj-cpp archive: ${little_archive}")
endif()
if(NOT EXISTS "${big_archive}")
  message(FATAL_ERROR "missing big-endian obj-cpp archive: ${big_archive}")
endif()
if(EXISTS "${no_archive_out}/llvmdsdl_generated.a")
  message(FATAL_ERROR "obj-cpp --obj-no-archive unexpectedly emitted archive")
endif()

if(NOT EXISTS "${little_out}/abi/fixtures/vendor/Widget_1_0_abi.o")
  message(FATAL_ERROR "missing canonical C++ ABI object for Widget.1.0")
endif()
if(NOT EXISTS "${little_out}/c_shim/fixtures/vendor/Widget_1_0_c_shim.o")
  message(FATAL_ERROR "missing C shim object for Widget.1.0")
endif()

# The published layout, asserted rather than only compiled. In this lane the C output is staged under
# `c/` and the generated ABI headers include it from there, so it has to be published under `c/` too.
# The compile below would not catch a missing root forwarder on its own: a quoted include resolves
# against the including file's own directory first, so `#include "dsdl_runtime.h"` from
# `c/<ns>/X.h` finds `c/dsdl_runtime.h` whether or not the root one exists.
if(NOT EXISTS "${little_out}/c/fixtures/vendor/Widget_1_0.h")
  message(FATAL_ERROR
    "the C header the published ABI headers include is missing from the published tree: "
    "expected ${little_out}/c/fixtures/vendor/Widget_1_0.h")
endif()
if(NOT EXISTS "${little_out}/c/dsdl_runtime.h")
  message(FATAL_ERROR "missing published C runtime header at ${little_out}/c/dsdl_runtime.h")
endif()
file(READ "${little_out}/c/dsdl_runtime.h" _c_runtime_text)
if(_c_runtime_text MATCHES "LLVMDSDL_CPPABI_C_RUNTIME_FORWARD_H")
  message(FATAL_ERROR
    "${little_out}/c/dsdl_runtime.h is the forwarder, not the C runtime header it forwards to")
endif()
if(NOT EXISTS "${little_out}/dsdl_runtime.h")
  message(FATAL_ERROR "missing root C runtime forwarder at ${little_out}/dsdl_runtime.h")
endif()
file(READ "${little_out}/dsdl_runtime.h" _root_runtime_text)
if(NOT _root_runtime_text MATCHES "LLVMDSDL_CPPABI_C_RUNTIME_FORWARD_H")
  message(FATAL_ERROR
    "${little_out}/dsdl_runtime.h is not the forwarder; the real C runtime header overwrote it")
endif()

set(cpp_harness "${OUT_DIR}/obj_cpp_harness.cpp")
set(_body [=[
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "std/fixtures/vendor/Widget_1_0.hpp"
#include "std/fixtures/vendor/Helpers_1_0.hpp"

int main() {
  fixtures::vendor::Widget${V1_0} obj{};
  obj.foo = 0x12U;
  obj.bar = 0x3456U;

  std::uint8_t buffer[fixtures::vendor::Widget${V1_0}::SERIALIZATION_BUFFER_SIZE_BYTES]{};
  std::size_t size = sizeof(buffer);
  const std::int8_t ser_rc = obj.serialize(buffer, &size);
  if (ser_rc != 0 || size != 3U || buffer[0] != 0x12U || buffer[1] != 0x56U || buffer[2] != 0x34U) {
    std::fprintf(stderr, "serialize mismatch rc=%d size=%zu\n", static_cast<int>(ser_rc), size);
    return 1;
  }

  fixtures::vendor::Widget${V1_0} out{};
  std::size_t in_size = size;
  const std::int8_t des_rc = out.deserialize(buffer, &in_size);
  if (des_rc != 0 || in_size != size || out.foo != obj.foo || out.bar != obj.bar) {
    std::fprintf(stderr, "deserialize mismatch rc=%d in_size=%zu\n", static_cast<int>(des_rc), in_size);
    return 2;
  }

  const std::uint8_t* view = nullptr;
  std::size_t view_size = size;
  const std::int8_t view_rc = fixtures::vendor::Widget${V1_0}::try_deserialize_view(buffer, &view_size, &view);
#if defined(LLVMDSDL_TARGET_ENDIANNESS_BIG)
  if (view_rc != -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT || view_size != 0U || view != nullptr) {
    std::fprintf(stderr, "big-endian view behaviour mismatch rc=%d size=%zu\n", static_cast<int>(view_rc), view_size);
    return 3;
  }
#else
  if (view_rc != DSDL_RUNTIME_SUCCESS || view_size != size || view != buffer) {
    std::fprintf(stderr, "little-endian view behaviour mismatch rc=%d size=%zu\n", static_cast<int>(view_rc), view_size);
    return 4;
  }
#endif

  if (fixtures::vendor::Helpers${V1_0}::ZOH_ALIAS_ELIGIBLE) {
    std::fprintf(stderr, "helpers should be ineligible\n");
    return 5;
  }
  if (std::strcmp(fixtures::vendor::Helpers${V1_0}::ZOH_ALIAS_REASON, "sub-byte-field") != 0) {
    std::fprintf(stderr, "unexpected Helpers ZOH reason: %s\n", fixtures::vendor::Helpers${V1_0}::ZOH_ALIAS_REASON);
    return 6;
  }

  std::printf("%zu %02x %02x %02x\n", size, static_cast<unsigned>(buffer[0]), static_cast<unsigned>(buffer[1]), static_cast<unsigned>(buffer[2]));
  return 0;
}
]=])
string(CONFIGURE "${_body}" _body)
file(WRITE "${cpp_harness}" "${_body}")

set(c_harness "${OUT_DIR}/obj_cpp_cshim_harness.c")
set(_body [=[
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "c_shim/fixtures/vendor/Widget_1_0_c_shim.h"

int main(void) {
  llvmdsdl_cppabi__fixtures__vendor__Widget${V1_0} obj = {0};
  obj.foo = 0x12U;
  obj.bar = 0x3456U;
  uint8_t buf[3] = {0};
  size_t size = sizeof(buf);
  int8_t rc = llvmdsdl_cppabi_fixtures_vendor_Widget_1_0__serialize_(&obj, buf, &size);
  if ((rc != 0) || (size != 3U) || (buf[0] != 0x12U) || (buf[1] != 0x56U) || (buf[2] != 0x34U)) {
    fprintf(stderr, "shim serialize mismatch rc=%d size=%zu\n", (int)rc, size);
    return 1;
  }
  llvmdsdl_cppabi__fixtures__vendor__Widget${V1_0} out = {0};
  size_t in_size = size;
  rc = llvmdsdl_cppabi_fixtures_vendor_Widget_1_0__deserialize_(&out, buf, &in_size);
  if ((rc != 0) || (in_size != size) || (out.foo != obj.foo) || (out.bar != obj.bar)) {
    fprintf(stderr, "shim deserialize mismatch rc=%d in_size=%zu\n", (int)rc, in_size);
    return 2;
  }
  printf("%zu %02x %02x %02x\n", size, (unsigned)buf[0], (unsigned)buf[1], (unsigned)buf[2]);
  return 0;
}
]=])
string(CONFIGURE "${_body}" _body)
file(WRITE "${c_harness}" "${_body}")

set(adapter_smoke "${OUT_DIR}/adapter_smoke.cpp")
set(_body [=[
#include "std/fixtures/vendor/Widget_1_0.hpp"
#include "pmr/fixtures/vendor/Widget_1_0.hpp"
#include "autosar/fixtures/vendor/Widget_1_0.hpp"
int main() {
  fixtures::vendor::Widget${V1_0} a{};
  (void)a;
  return 0;
}
]=])
string(CONFIGURE "${_body}" _body)
file(WRITE "${adapter_smoke}" "${_body}")

foreach(endian IN ITEMS little big)
  if(endian STREQUAL "little")
    set(root "${little_out}")
    set(archive "${little_archive}")
  else()
    set(root "${big_out}")
    set(archive "${big_archive}")
  endif()

  set(cpp_exe "${OUT_DIR}/${endian}-cpp-harness")
  set(cpp_compile_cmd
    "${CXX_COMPILER}"
    -std=c++17
    -Wall
    -Wextra
    -Werror
  )
  if(endian STREQUAL "big")
    list(APPEND cpp_compile_cmd -DLLVMDSDL_TARGET_ENDIANNESS_BIG=1)
  endif()
  list(APPEND cpp_compile_cmd
    -I
    "${root}"
    "${cpp_harness}"
    "${archive}"
    -o
    "${cpp_exe}"
  )

  execute_process(
    COMMAND ${cpp_compile_cmd}
    RESULT_VARIABLE cpp_build_result
    OUTPUT_VARIABLE cpp_build_stdout
    ERROR_VARIABLE cpp_build_stderr
  )
  if(NOT cpp_build_result EQUAL 0)
    message(STATUS "${endian} cpp harness build stdout:\n${cpp_build_stdout}")
    message(STATUS "${endian} cpp harness build stderr:\n${cpp_build_stderr}")
    message(FATAL_ERROR "failed to build ${endian} C++ harness")
  endif()

  set(c_exe "${OUT_DIR}/${endian}-c-harness")
  execute_process(
    COMMAND
      "${C_COMPILER}" -std=c11 -Wall -Wextra -Werror
      -I
      "${root}"
      -c "${c_harness}" -o "${OUT_DIR}/${endian}-c-harness.o"
    RESULT_VARIABLE c_compile_result
    OUTPUT_VARIABLE c_compile_stdout
    ERROR_VARIABLE c_compile_stderr
  )
  if(NOT c_compile_result EQUAL 0)
    message(STATUS "${endian} c harness compile stdout:\n${c_compile_stdout}")
    message(STATUS "${endian} c harness compile stderr:\n${c_compile_stderr}")
    message(FATAL_ERROR "failed to compile ${endian} C shim harness")
  endif()

  execute_process(
    COMMAND
      "${CXX_COMPILER}" "${OUT_DIR}/${endian}-c-harness.o" "${archive}" -o "${c_exe}"
    RESULT_VARIABLE c_link_result
    OUTPUT_VARIABLE c_link_stdout
    ERROR_VARIABLE c_link_stderr
  )
  if(NOT c_link_result EQUAL 0)
    message(STATUS "${endian} c harness link stdout:\n${c_link_stdout}")
    message(STATUS "${endian} c harness link stderr:\n${c_link_stderr}")
    message(FATAL_ERROR "failed to link ${endian} C shim harness")
  endif()

  execute_process(
    COMMAND "${cpp_exe}"
    RESULT_VARIABLE cpp_run_result
    OUTPUT_FILE "${OUT_DIR}/${endian}-cpp.out"
    ERROR_VARIABLE cpp_run_stderr
  )
  if(NOT cpp_run_result EQUAL 0)
    message(STATUS "${endian} cpp harness stderr:\n${cpp_run_stderr}")
    message(FATAL_ERROR "${endian} C++ harness failed")
  endif()

  execute_process(
    COMMAND "${c_exe}"
    RESULT_VARIABLE c_run_result
    OUTPUT_FILE "${OUT_DIR}/${endian}-c.out"
    ERROR_VARIABLE c_run_stderr
  )
  if(NOT c_run_result EQUAL 0)
    message(STATUS "${endian} c harness stderr:\n${c_run_stderr}")
    message(FATAL_ERROR "${endian} C shim harness failed")
  endif()
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT_DIR}/little-cpp.out" "${OUT_DIR}/big-cpp.out"
  RESULT_VARIABLE endian_cmp
)
if(NOT endian_cmp EQUAL 0)
  file(READ "${OUT_DIR}/little-cpp.out" little_cpp)
  file(READ "${OUT_DIR}/big-cpp.out" big_cpp)
  message(STATUS "little cpp output:\n${little_cpp}")
  message(STATUS "big cpp output:\n${big_cpp}")
  message(FATAL_ERROR "little vs big obj-cpp wire parity mismatch")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT_DIR}/little-cpp.out" "${OUT_DIR}/little-c.out"
  RESULT_VARIABLE shim_cmp
)
if(NOT shim_cmp EQUAL 0)
  file(READ "${OUT_DIR}/little-cpp.out" little_cpp)
  file(READ "${OUT_DIR}/little-c.out" little_c)
  message(STATUS "little cpp output:\n${little_cpp}")
  message(STATUS "little c shim output:\n${little_c}")
  message(FATAL_ERROR "C++ ABI and C shim outputs differ")
endif()

file(GLOB_RECURSE no_archive_objects "${no_archive_out}/*.o")
list(SORT no_archive_objects)
list(LENGTH no_archive_objects no_archive_count)
if(no_archive_count EQUAL 0)
  message(FATAL_ERROR "obj-cpp no-archive run emitted zero objects")
endif()

set(no_archive_cpp_exe "${OUT_DIR}/no-archive-cpp-harness")
execute_process(
  COMMAND
    "${CXX_COMPILER}" -std=c++17 -Wall -Wextra -Werror
    -I
    "${no_archive_out}"
    "${cpp_harness}"
    ${no_archive_objects}
    -o
    "${no_archive_cpp_exe}"
  RESULT_VARIABLE no_archive_cpp_build_result
  OUTPUT_VARIABLE no_archive_cpp_build_stdout
  ERROR_VARIABLE no_archive_cpp_build_stderr
)
if(NOT no_archive_cpp_build_result EQUAL 0)
  message(STATUS "no-archive cpp harness build stdout:\n${no_archive_cpp_build_stdout}")
  message(STATUS "no-archive cpp harness build stderr:\n${no_archive_cpp_build_stderr}")
  message(FATAL_ERROR "failed to build no-archive C++ harness")
endif()

execute_process(
  COMMAND "${no_archive_cpp_exe}"
  RESULT_VARIABLE no_archive_cpp_run_result
  OUTPUT_FILE "${OUT_DIR}/no-archive-cpp.out"
  ERROR_VARIABLE no_archive_cpp_run_stderr
)
if(NOT no_archive_cpp_run_result EQUAL 0)
  message(STATUS "no-archive cpp harness stderr:\n${no_archive_cpp_run_stderr}")
  message(FATAL_ERROR "no-archive C++ harness failed")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT_DIR}/little-cpp.out" "${OUT_DIR}/no-archive-cpp.out"
  RESULT_VARIABLE no_archive_cmp
)
if(NOT no_archive_cmp EQUAL 0)
  file(READ "${OUT_DIR}/little-cpp.out" little_cpp)
  file(READ "${OUT_DIR}/no-archive-cpp.out" no_archive_cpp)
  message(STATUS "little cpp output:\n${little_cpp}")
  message(STATUS "no-archive cpp output:\n${no_archive_cpp}")
  message(FATAL_ERROR "archived and no-archive obj-cpp outputs differ")
endif()

set(adapter_exe "${OUT_DIR}/adapter-smoke")
execute_process(
  COMMAND
    "${CXX_COMPILER}" -std=c++17 -Wall -Wextra -Werror
    -I
    "${little_out}"
    "${adapter_smoke}" -o "${adapter_exe}"
  RESULT_VARIABLE adapter_result
  OUTPUT_VARIABLE adapter_stdout
  ERROR_VARIABLE adapter_stderr
)
if(NOT adapter_result EQUAL 0)
  message(STATUS "adapter smoke build stdout:\n${adapter_stdout}")
  message(STATUS "adapter smoke build stderr:\n${adapter_stderr}")
  message(FATAL_ERROR "adapter include smoke failed")
endif()

message(STATUS "obj-cpp backend smoke/parity passed (little+big+no-archive, shim, adapters)")
