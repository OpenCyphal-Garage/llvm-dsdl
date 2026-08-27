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
  "${fixture_root}/Delimited.1.0.dsdl"
  "uint8 value\n"
  "@extent 64\n"
)
file(WRITE
  "${fixture_root}/UsesDelimited.1.0.dsdl"
  "demo.Delimited.1.0 nested\n"
  "@sealed\n"
)

execute_process(
  COMMAND
    "${DSDLC}" --target-language c
      "${fixture_root}"
      --outdir "${c_out}"
  RESULT_VARIABLE c_gen_result
  OUTPUT_VARIABLE c_gen_stdout
  ERROR_VARIABLE c_gen_stderr
)
if(NOT c_gen_result EQUAL 0)
  message(STATUS "dsdlc c stdout:\n${c_gen_stdout}")
  message(STATUS "dsdlc c stderr:\n${c_gen_stderr}")
  message(FATAL_ERROR "fixture C generation failed")
endif()

execute_process(
  COMMAND
    "${DSDLC}" --target-language ts --versioned-type-names
      "${fixture_root}"
      --outdir "${ts_out}"
      --ts-module "fixture_ts_delimited_invalid_header_parity"
  RESULT_VARIABLE ts_gen_result
  OUTPUT_VARIABLE ts_gen_stdout
  ERROR_VARIABLE ts_gen_stderr
)
if(NOT ts_gen_result EQUAL 0)
  message(STATUS "dsdlc ts stdout:\n${ts_gen_stdout}")
  message(STATUS "dsdlc ts stderr:\n${ts_gen_stderr}")
  message(FATAL_ERROR "fixture TypeScript generation failed")
endif()

set(ts_index "${ts_out}/index.ts")
if(NOT EXISTS "${ts_index}")
  message(FATAL_ERROR "generated TypeScript index missing: ${ts_index}")
endif()
file(READ "${ts_index}" ts_index_content)
string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*uses_delimited_1_0\";" ts_type_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate uses_delimited_1_0 export alias in ${ts_index}")
endif()
set(ts_type_module "${CMAKE_MATCH_1}")

set(c_harness_src "${work_dir}/c_delimited_invalid_header_parity_harness.c")
file(WRITE
  "${c_harness_src}"
  "#include <stdint.h>\n"
  "#include <stdio.h>\n"
  "#include <string.h>\n"
  "#include \"demo/Delimited_1_0.h\"\n"
  "#include \"demo/UsesDelimited_1_0.h\"\n"
  "\n"
  "int main(void) {\n"
  "  demo__UsesDelimited in_obj;\n"
  "  in_obj.nested.value = 171U;\n"
  "\n"
  "  uint8_t out_bytes[16] = {0};\n"
  "  size_t out_size = sizeof(out_bytes);\n"
  "  const int8_t ser_rc = demo__UsesDelimited__serialize_(&in_obj, out_bytes, &out_size);\n"
  "  if (ser_rc != 0) {\n"
  "    return 2;\n"
  "  }\n"
  "  printf(\"%u\", (unsigned) out_size);\n"
  "  for (size_t i = 0; i < out_size; ++i) {\n"
  "    printf(\" %u\", (unsigned) out_bytes[i]);\n"
  "  }\n"
  "  printf(\"\\n\");\n"
  "\n"
  "  demo__UsesDelimited out_obj = {0};\n"
  "  size_t consumed = out_size;\n"
  "  const int8_t de_rc = demo__UsesDelimited__deserialize_(&out_obj, out_bytes, &consumed);\n"
  "  if (de_rc != 0) {\n"
  "    return 3;\n"
  "  }\n"
  "  printf(\"%u %u\\n\", (unsigned) out_obj.nested.value, (unsigned) consumed);\n"
  "\n"
  "  uint8_t invalid_bytes[16] = {0};\n"
  "  memcpy(invalid_bytes, out_bytes, out_size);\n"
  "  invalid_bytes[0] = 6U;\n"
  "  invalid_bytes[1] = 0U;\n"
  "  invalid_bytes[2] = 0U;\n"
  "  invalid_bytes[3] = 0U;\n"
  "\n"
  "  demo__UsesDelimited invalid_obj = {0};\n"
  "  size_t invalid_consumed = out_size;\n"
  "  const int8_t invalid_rc = demo__UsesDelimited__deserialize_(&invalid_obj, invalid_bytes, &invalid_consumed);\n"
  "  if (invalid_rc >= 0) {\n"
  "    return 4;\n"
  "  }\n"
  "  printf(\"invalid_rejected\\n\");\n"
  "  return 0;\n"
  "}\n"
)

set(c_harness_bin "${work_dir}/c_delimited_invalid_header_parity_harness")
execute_process(
  COMMAND
    "${C_COMPILER}"
      -std=c11
      -Wall
      -Wextra
      -Werror
      -I "${c_out}"
      "${c_harness_src}"
      "${c_out}/demo/Delimited_1_0.c"
      "${c_out}/demo/UsesDelimited_1_0.c"
      -o "${c_harness_bin}"
  RESULT_VARIABLE c_cc_result
  OUTPUT_VARIABLE c_cc_stdout
  ERROR_VARIABLE c_cc_stderr
)
if(NOT c_cc_result EQUAL 0)
  message(STATUS "C compile stdout:\n${c_cc_stdout}")
  message(STATUS "C compile stderr:\n${c_cc_stderr}")
  message(FATAL_ERROR "failed to compile fixture C delimited invalid-header parity harness")
endif()

execute_process(
  COMMAND "${c_harness_bin}"
  RESULT_VARIABLE c_run_result
  OUTPUT_VARIABLE c_run_stdout
  ERROR_VARIABLE c_run_stderr
)
if(NOT c_run_result EQUAL 0)
  message(STATUS "C harness stdout:\n${c_run_stdout}")
  message(STATUS "C harness stderr:\n${c_run_stderr}")
  message(FATAL_ERROR "fixture C delimited invalid-header parity harness failed")
endif()

file(WRITE
  "${ts_out}/runtime_delimited_invalid_header_parity_smoke.ts"
  "import { ${ts_type_module} } from \"./index\";\n"
  "\n"
  "const inObj: ${ts_type_module}.UsesDelimited_1_0 = { nested: { value: 171 } };\n"
  "const validBytes = ${ts_type_module}.serializeUsesDelimited_1_0(inObj);\n"
  "console.log(validBytes.length + \" \" + Array.from(validBytes).join(\" \") );\n"
  "const validDecoded = ${ts_type_module}.deserializeUsesDelimited_1_0(validBytes);\n"
  "console.log(validDecoded.value.nested.value + \" \" + validDecoded.consumed);\n"
  "\n"
  "const invalidBytes = new Uint8Array(validBytes);\n"
  "invalidBytes[0] = 6;\n"
  "invalidBytes[1] = 0;\n"
  "invalidBytes[2] = 0;\n"
  "invalidBytes[3] = 0;\n"
  "let invalidRejected = false;\n"
  "try {\n"
  "  ${ts_type_module}.deserializeUsesDelimited_1_0(invalidBytes);\n"
  "} catch (_err) {\n"
  "  invalidRejected = true;\n"
  "}\n"
  "if (!invalidRejected) {\n"
  "  throw new Error(\"invalid delimiter header unexpectedly accepted\");\n"
  "}\n"
  "console.log(\"invalid_rejected\");\n"
)

file(WRITE
  "${ts_out}/tsconfig-runtime-delimited-invalid-header-parity.json"
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
  COMMAND "${TSC_EXECUTABLE}" -p "${ts_out}/tsconfig-runtime-delimited-invalid-header-parity.json" --pretty false
  WORKING_DIRECTORY "${ts_out}"
  RESULT_VARIABLE tsc_result
  OUTPUT_VARIABLE tsc_stdout
  ERROR_VARIABLE tsc_stderr
)
if(NOT tsc_result EQUAL 0)
  message(STATUS "tsc stdout:\n${tsc_stdout}")
  message(STATUS "tsc stderr:\n${tsc_stderr}")
  message(FATAL_ERROR "failed to compile fixture TypeScript delimited invalid-header parity smoke")
endif()

file(WRITE "${ts_out}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")

execute_process(
  COMMAND "${NODE_EXECUTABLE}" "${ts_out}/js/runtime_delimited_invalid_header_parity_smoke.js"
  RESULT_VARIABLE node_result
  OUTPUT_VARIABLE node_stdout
  ERROR_VARIABLE node_stderr
)
if(NOT node_result EQUAL 0)
  message(STATUS "node stdout:\n${node_stdout}")
  message(STATUS "node stderr:\n${node_stderr}")
  message(FATAL_ERROR "TypeScript delimited invalid-header parity smoke execution failed")
endif()

string(STRIP "${c_run_stdout}" c_output)
string(STRIP "${node_stdout}" ts_output)
if(NOT c_output STREQUAL ts_output)
  file(WRITE "${OUT_DIR}/c-output.txt" "${c_output}\n")
  file(WRITE "${OUT_DIR}/ts-output.txt" "${ts_output}\n")
  message(FATAL_ERROR
    "C vs TypeScript delimited invalid-header parity mismatch. See ${OUT_DIR}/c-output.txt and ${OUT_DIR}/ts-output.txt.")
endif()

message(STATUS "fixture C<->TypeScript delimited invalid-header parity passed")
