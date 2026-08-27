cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC FIXTURES_ROOT OUT_DIR C_COMPILER)
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

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(little_out "${OUT_DIR}/little")
set(big_out "${OUT_DIR}/big")
set(no_archive_out "${OUT_DIR}/no-archive")
set(archive_name "fixtures_obj")
set(type_object_rel "fixtures/vendor/Widget_1_0.o")

foreach(out_dir IN ITEMS "${little_out}" "${big_out}" "${no_archive_out}")
  file(MAKE_DIRECTORY "${out_dir}")
endforeach()

# The C headers this backend publishes are the archive's interface, so their naming scheme is the
# user's choice and both spellings are compiled and run.
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
    "${CMAKE_COMMAND}" -E env "CC=${C_COMPILER}"
      "${DSDLC}" --target-language obj ${scheme_args} --target-endianness little
      --jobs 1 --obj-archive-name "${archive_name}" --outdir "${little_out}" "${FIXTURES_ROOT}"
  RESULT_VARIABLE little_result
  OUTPUT_VARIABLE little_stdout
  ERROR_VARIABLE little_stderr
)
if(NOT little_result EQUAL 0)
  message(STATUS "little obj stdout:\n${little_stdout}")
  message(STATUS "little obj stderr:\n${little_stderr}")
  message(FATAL_ERROR "obj backend generation failed for little-endian")
endif()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env "CC=${C_COMPILER}"
      "${DSDLC}" --target-language obj ${scheme_args} --target-endianness big
      --jobs 2 --obj-archive-name "${archive_name}" --outdir "${big_out}" "${FIXTURES_ROOT}"
  RESULT_VARIABLE big_result
  OUTPUT_VARIABLE big_stdout
  ERROR_VARIABLE big_stderr
)
if(NOT big_result EQUAL 0)
  message(STATUS "big obj stdout:\n${big_stdout}")
  message(STATUS "big obj stderr:\n${big_stderr}")
  message(FATAL_ERROR "obj backend generation failed for big-endian")
endif()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env "CC=${C_COMPILER}"
      "${DSDLC}" --target-language obj ${scheme_args} --target-endianness little
      --jobs 4 --obj-no-archive --outdir "${no_archive_out}" "${FIXTURES_ROOT}"
  RESULT_VARIABLE no_archive_result
  OUTPUT_VARIABLE no_archive_stdout
  ERROR_VARIABLE no_archive_stderr
)
if(NOT no_archive_result EQUAL 0)
  message(STATUS "obj-no-archive stdout:\n${no_archive_stdout}")
  message(STATUS "obj-no-archive stderr:\n${no_archive_stderr}")
  message(FATAL_ERROR "obj backend generation failed for --obj-no-archive")
endif()

set(little_archive "${little_out}/${archive_name}.a")
set(big_archive "${big_out}/${archive_name}.a")
set(no_archive_archive "${no_archive_out}/llvmdsdl_generated.a")

if(NOT EXISTS "${little_archive}")
  message(FATAL_ERROR "expected little-endian archive was not generated: ${little_archive}")
endif()
if(NOT EXISTS "${big_archive}")
  message(FATAL_ERROR "expected big-endian archive was not generated: ${big_archive}")
endif()
if(EXISTS "${no_archive_archive}")
  message(FATAL_ERROR "--obj-no-archive unexpectedly emitted archive: ${no_archive_archive}")
endif()

foreach(out_dir IN ITEMS "${little_out}" "${big_out}" "${no_archive_out}")
  if(NOT EXISTS "${out_dir}/${type_object_rel}")
    message(FATAL_ERROR "expected object file missing: ${out_dir}/${type_object_rel}")
  endif()
endforeach()

find_program(LLVMDSDL_AR_TOOL ar)
if(LLVMDSDL_AR_TOOL)
  execute_process(
    COMMAND "${LLVMDSDL_AR_TOOL}" t "${little_archive}"
    RESULT_VARIABLE little_ar_result
    OUTPUT_VARIABLE little_ar_stdout
    ERROR_VARIABLE little_ar_stderr
  )
  if(NOT little_ar_result EQUAL 0)
    message(STATUS "ar stderr (little):\n${little_ar_stderr}")
    message(FATAL_ERROR "failed to inspect little-endian archive members")
  endif()
  string(FIND "${little_ar_stdout}" "Widget_1_0.o" little_type_hit)
  if(little_type_hit EQUAL -1)
    message(FATAL_ERROR "little-endian archive missing Widget_1_0.o member")
  endif()

  execute_process(
    COMMAND "${LLVMDSDL_AR_TOOL}" t "${big_archive}"
    RESULT_VARIABLE big_ar_result
    OUTPUT_VARIABLE big_ar_stdout
    ERROR_VARIABLE big_ar_stderr
  )
  if(NOT big_ar_result EQUAL 0)
    message(STATUS "ar stderr (big):\n${big_ar_stderr}")
    message(FATAL_ERROR "failed to inspect big-endian archive members")
  endif()
  string(FIND "${big_ar_stdout}" "Widget_1_0.o" big_type_hit)
  if(big_type_hit EQUAL -1)
    message(FATAL_ERROR "big-endian archive missing Widget_1_0.o member")
  endif()
else()
  message(STATUS "Skipping archive member inspection: 'ar' not found")
endif()

set(harness_src "${OUT_DIR}/obj_backend_harness.c")
file(
  WRITE
  "${harness_src}"
  "#include <stdint.h>\n"
  "#include <stddef.h>\n"
  "#include <stdio.h>\n"
  "#include <string.h>\n"
  "#include \"fixtures/vendor/Widget_1_0.h\"\n"
  "#include \"fixtures/vendor/Helpers_1_0.h\"\n"
  "#include \"fixtures/vendor/UnionTag_1_0.h\"\n"
  "\n"
  "static int check_type(void) {\n"
  "  fixtures__vendor__Widget${V1_0} obj = {0};\n"
  "  obj.foo = 0x12U;\n"
  "  obj.bar = 0x3456U;\n"
  "  uint8_t buf[fixtures__vendor__Widget${V1_0}_SERIALIZATION_BUFFER_SIZE_BYTES_] = {0};\n"
  "  size_t size = sizeof(buf);\n"
  "  int8_t rc = fixtures__vendor__Widget${V1_0}__serialize_(&obj, buf, &size);\n"
  "  if ((rc != 0) || (size != 3U) || (buf[0] != 0x12U) || (buf[1] != 0x56U) || (buf[2] != 0x34U)) {\n"
  "    return 1;\n"
  "  }\n"
  "  fixtures__vendor__Widget${V1_0} out = {0};\n"
  "  size_t in_size = size;\n"
  "  rc = fixtures__vendor__Widget${V1_0}__deserialize_(&out, buf, &in_size);\n"
  "  if ((rc != 0) || (in_size != size) || (out.foo != obj.foo) || (out.bar != obj.bar)) {\n"
  "    return 2;\n"
  "  }\n"
  "  const uint8_t* view = NULL;\n"
  "  size_t view_size = size;\n"
  "  rc = fixtures__vendor__Widget${V1_0}__try_deserialize_view_(buf, &view_size, &view);\n"
  "#if defined(LLVMDSDL_TARGET_ENDIANNESS_BIG)\n"
  "  if ((rc != -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT) || (view_size != 0U) || (view != NULL)) {\n"
  "    return 3;\n"
  "  }\n"
  "#else\n"
  "  if ((rc != DSDL_RUNTIME_SUCCESS) || (view_size != size) || (view != buf)) {\n"
  "    return 4;\n"
  "  }\n"
  "  uint8_t copied[fixtures__vendor__Widget${V1_0}_SERIALIZATION_BUFFER_SIZE_BYTES_] = {0};\n"
  "  size_t copied_size = sizeof(copied);\n"
  "  rc = fixtures__vendor__Widget${V1_0}__try_serialize_view_(view, view_size, copied, &copied_size);\n"
  "  if ((rc != DSDL_RUNTIME_SUCCESS) || (copied_size != size) || (memcmp(copied, buf, size) != 0)) {\n"
  "    return 5;\n"
  "  }\n"
  "#endif\n"
  "  printf(\"Type %02x %02x %02x\\n\", (unsigned int)buf[0], (unsigned int)buf[1], (unsigned int)buf[2]);\n"
  "  return 0;\n"
  "}\n"
  "\n"
  "static int check_helpers(void) {\n"
  "  if (fixtures__vendor__Helpers${V1_0}_ZOH_ALIAS_ELIGIBLE_) {\n"
  "    return 10;\n"
  "  }\n"
  "  if (strcmp(fixtures__vendor__Helpers${V1_0}_ZOH_ALIAS_REASON_, \"sub-byte-field\") != 0) {\n"
  "    return 11;\n"
  "  }\n"
  "  fixtures__vendor__Helpers${V1_0} obj = {0};\n"
  "  obj.a = -1234;\n"
  "  obj.b = 1.5F;\n"
  "  obj.c.count = 3U;\n"
  "  obj.c.elements[0] = 0xAAU;\n"
  "  obj.c.elements[1] = 0xBBU;\n"
  "  obj.c.elements[2] = 0xCCU;\n"
  "  uint8_t buf[fixtures__vendor__Helpers${V1_0}_SERIALIZATION_BUFFER_SIZE_BYTES_] = {0};\n"
  "  size_t size = sizeof(buf);\n"
  "  int8_t rc = fixtures__vendor__Helpers${V1_0}__serialize_(&obj, buf, &size);\n"
  "  if (rc != 0) {\n"
  "    return 12;\n"
  "  }\n"
  "  fixtures__vendor__Helpers${V1_0} out = {0};\n"
  "  size_t in_size = size;\n"
  "  rc = fixtures__vendor__Helpers${V1_0}__deserialize_(&out, buf, &in_size);\n"
  "  if ((rc != 0) || (in_size != size) || (out.a != obj.a) || (out.c.count != obj.c.count)) {\n"
  "    return 13;\n"
  "  }\n"
  "  const uint8_t* view = NULL;\n"
  "  size_t view_size = fixtures__vendor__Helpers${V1_0}_SERIALIZATION_BUFFER_SIZE_BYTES_;\n"
  "  rc = fixtures__vendor__Helpers${V1_0}__try_deserialize_view_(buf, &view_size, &view);\n"
  "  if ((rc != -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT) || (view_size != 0U) || (view != NULL)) {\n"
  "    return 14;\n"
  "  }\n"
  "  printf(\"Helpers %zu\", size);\n"
  "  for (size_t i = 0U; i < size; ++i) {\n"
  "    printf(\" %02x\", (unsigned int)buf[i]);\n"
  "  }\n"
  "  printf(\"\\n\");\n"
  "  return 0;\n"
  "}\n"
  "\n"
  "static int check_union(void) {\n"
  "  if (fixtures__vendor__UnionTag${V1_0}_ZOH_ALIAS_ELIGIBLE_) {\n"
  "    return 20;\n"
  "  }\n"
  "  fixtures__vendor__UnionTag${V1_0} obj = {0};\n"
  "  obj._tag_ = 1U;\n"
  "  obj.second = 0x1122U;\n"
  "  uint8_t buf[fixtures__vendor__UnionTag${V1_0}_SERIALIZATION_BUFFER_SIZE_BYTES_] = {0};\n"
  "  size_t size = sizeof(buf);\n"
  "  int8_t rc = fixtures__vendor__UnionTag${V1_0}__serialize_(&obj, buf, &size);\n"
  "  if (rc != 0) {\n"
  "    return 21;\n"
  "  }\n"
  "  fixtures__vendor__UnionTag${V1_0} out = {0};\n"
  "  size_t in_size = size;\n"
  "  rc = fixtures__vendor__UnionTag${V1_0}__deserialize_(&out, buf, &in_size);\n"
  "  if ((rc != 0) || (in_size != size) || (out._tag_ != obj._tag_) || (out.second != obj.second)) {\n"
  "    return 22;\n"
  "  }\n"
  "  printf(\"Union %zu\", size);\n"
  "  for (size_t i = 0U; i < size; ++i) {\n"
  "    printf(\" %02x\", (unsigned int)buf[i]);\n"
  "  }\n"
  "  printf(\"\\n\");\n"
  "  return 0;\n"
  "}\n"
  "\n"
  "int main(void) {\n"
  "  const int type_status = check_type();\n"
  "  if (type_status != 0) {\n"
  "    fprintf(stderr, \"type-check failed: %d\\n\", type_status);\n"
  "    return 1;\n"
  "  }\n"
  "  const int helpers_status = check_helpers();\n"
  "  if (helpers_status != 0) {\n"
  "    fprintf(stderr, \"helpers-check failed: %d\\n\", helpers_status);\n"
  "    return 2;\n"
  "  }\n"
  "  const int union_status = check_union();\n"
  "  if (union_status != 0) {\n"
  "    fprintf(stderr, \"union-check failed: %d\\n\", union_status);\n"
  "    return 3;\n"
  "  }\n"
  "  return 0;\n"
  "}\n")

set(little_exe "${OUT_DIR}/obj_little_harness")
set(big_exe "${OUT_DIR}/obj_big_harness")
set(no_archive_exe "${OUT_DIR}/obj_no_archive_harness")

execute_process(
  COMMAND
    "${C_COMPILER}" "${harness_src}"
      "-I${little_out}/.obj_stage_c"
      "${little_archive}"
      -o "${little_exe}"
  RESULT_VARIABLE little_build_result
  OUTPUT_VARIABLE little_build_stdout
  ERROR_VARIABLE little_build_stderr
)
if(NOT little_build_result EQUAL 0)
  message(STATUS "little harness build stdout:\n${little_build_stdout}")
  message(STATUS "little harness build stderr:\n${little_build_stderr}")
  message(FATAL_ERROR "failed to link little-endian obj harness")
endif()

execute_process(
  COMMAND
    "${C_COMPILER}" "${harness_src}"
      "-DLLVMDSDL_TARGET_ENDIANNESS_BIG=1"
      "-I${big_out}/.obj_stage_c"
      "${big_archive}"
      -o "${big_exe}"
  RESULT_VARIABLE big_build_result
  OUTPUT_VARIABLE big_build_stdout
  ERROR_VARIABLE big_build_stderr
)
if(NOT big_build_result EQUAL 0)
  message(STATUS "big harness build stdout:\n${big_build_stdout}")
  message(STATUS "big harness build stderr:\n${big_build_stderr}")
  message(FATAL_ERROR "failed to link big-endian obj harness")
endif()

file(GLOB_RECURSE no_archive_objects "${no_archive_out}/*.o")
list(SORT no_archive_objects)
list(LENGTH no_archive_objects no_archive_object_count)
if(no_archive_object_count EQUAL 0)
  message(FATAL_ERROR "--obj-no-archive output did not produce any .o files")
endif()

execute_process(
  COMMAND
    "${C_COMPILER}" "${harness_src}"
      "-I${no_archive_out}/.obj_stage_c"
      ${no_archive_objects}
      -o "${no_archive_exe}"
  RESULT_VARIABLE no_archive_build_result
  OUTPUT_VARIABLE no_archive_build_stdout
  ERROR_VARIABLE no_archive_build_stderr
)
if(NOT no_archive_build_result EQUAL 0)
  message(STATUS "no-archive harness build stdout:\n${no_archive_build_stdout}")
  message(STATUS "no-archive harness build stderr:\n${no_archive_build_stderr}")
  message(FATAL_ERROR "failed to link --obj-no-archive harness")
endif()

set(little_out_txt "${OUT_DIR}/little.out.txt")
set(big_out_txt "${OUT_DIR}/big.out.txt")
set(no_archive_out_txt "${OUT_DIR}/no-archive.out.txt")

execute_process(
  COMMAND "${little_exe}"
  RESULT_VARIABLE little_run_result
  OUTPUT_FILE "${little_out_txt}"
  ERROR_VARIABLE little_run_stderr
)
if(NOT little_run_result EQUAL 0)
  message(STATUS "little harness stderr:\n${little_run_stderr}")
  message(FATAL_ERROR "little-endian obj harness execution failed")
endif()

execute_process(
  COMMAND "${big_exe}"
  RESULT_VARIABLE big_run_result
  OUTPUT_FILE "${big_out_txt}"
  ERROR_VARIABLE big_run_stderr
)
if(NOT big_run_result EQUAL 0)
  message(STATUS "big harness stderr:\n${big_run_stderr}")
  message(FATAL_ERROR "big-endian obj harness execution failed")
endif()

execute_process(
  COMMAND "${no_archive_exe}"
  RESULT_VARIABLE no_archive_run_result
  OUTPUT_FILE "${no_archive_out_txt}"
  ERROR_VARIABLE no_archive_run_stderr
)
if(NOT no_archive_run_result EQUAL 0)
  message(STATUS "no-archive harness stderr:\n${no_archive_run_stderr}")
  message(FATAL_ERROR "--obj-no-archive harness execution failed")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${little_out_txt}" "${big_out_txt}"
  RESULT_VARIABLE endian_compare_result
)
if(NOT endian_compare_result EQUAL 0)
  file(READ "${little_out_txt}" little_run_text)
  file(READ "${big_out_txt}" big_run_text)
  message(STATUS "little run output:\n${little_run_text}")
  message(STATUS "big run output:\n${big_run_text}")
  message(FATAL_ERROR "little-endian and big-endian obj outputs diverged")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${little_out_txt}" "${no_archive_out_txt}"
  RESULT_VARIABLE no_archive_compare_result
)
if(NOT no_archive_compare_result EQUAL 0)
  file(READ "${little_out_txt}" little_run_text)
  file(READ "${no_archive_out_txt}" no_archive_run_text)
  message(STATUS "little run output:\n${little_run_text}")
  message(STATUS "no-archive run output:\n${no_archive_run_text}")
  message(FATAL_ERROR "archive and no-archive obj outputs diverged")
endif()

message(STATUS
  "obj backend smoke/parity check passed (little + big + no-archive, serialization parity verified)")
