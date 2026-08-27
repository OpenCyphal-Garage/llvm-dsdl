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

file(WRITE
  "${fixture_root}/EmptyDelimited.1.0.dsdl"
  "@extent 8\n"
)
file(WRITE
  "${fixture_root}/UsesEmptyDelimited.1.0.dsdl"
  "demo.EmptyDelimited.1.0 nested\n"
  "@sealed\n"
)

file(WRITE
  "${fixture_root}/MaxDelimited.1.0.dsdl"
  "uint8[64] bytes\n"
  "@extent 512\n"
)
file(WRITE
  "${fixture_root}/UsesMaxDelimited.1.0.dsdl"
  "demo.MaxDelimited.1.0 nested\n"
  "@sealed\n"
)

file(WRITE
  "${fixture_root}/DelimitedInner.1.0.dsdl"
  "uint8 value\n"
  "@extent 64\n"
)
file(WRITE
  "${fixture_root}/DelimitedOuter.1.0.dsdl"
  "demo.DelimitedInner.1.0 inner\n"
  "@extent 128\n"
)
file(WRITE
  "${fixture_root}/UsesNestedDelimited.1.0.dsdl"
  "demo.DelimitedOuter.1.0 nested\n"
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
      --ts-module "fixture_ts_delimited_parity"
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

string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*uses_empty_delimited_1_0\";" ts_empty_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate uses_empty_delimited_1_0 export alias in ${ts_index}")
endif()
set(ts_empty_module "${CMAKE_MATCH_1}")

string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*uses_max_delimited_1_0\";" ts_max_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate uses_max_delimited_1_0 export alias in ${ts_index}")
endif()
set(ts_max_module "${CMAKE_MATCH_1}")

string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*uses_nested_delimited_1_0\";" ts_nested_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate uses_nested_delimited_1_0 export alias in ${ts_index}")
endif()
set(ts_nested_module "${CMAKE_MATCH_1}")

set(c_harness_src "${work_dir}/c_delimited_parity_harness.c")
file(WRITE
  "${c_harness_src}"
  "#include <stddef.h>\n"
  "#include <stdint.h>\n"
  "#include <stdio.h>\n"
  "#include \"demo/Delimited_1_0.h\"\n"
  "#include \"demo/UsesDelimited_1_0.h\"\n"
  "#include \"demo/EmptyDelimited_1_0.h\"\n"
  "#include \"demo/UsesEmptyDelimited_1_0.h\"\n"
  "#include \"demo/MaxDelimited_1_0.h\"\n"
  "#include \"demo/UsesMaxDelimited_1_0.h\"\n"
  "#include \"demo/DelimitedInner_1_0.h\"\n"
  "#include \"demo/DelimitedOuter_1_0.h\"\n"
  "#include \"demo/UsesNestedDelimited_1_0.h\"\n"
  "\n"
  "int main(void) {\n"
  "  demo__UsesDelimited basic_in = {0};\n"
  "  basic_in.nested.value = 171U;\n"
  "  uint8_t basic_bytes[16] = {0};\n"
  "  size_t basic_size = sizeof(basic_bytes);\n"
  "  if (demo__UsesDelimited__serialize_(&basic_in, basic_bytes, &basic_size) != 0 || basic_size != 5U) {\n"
  "    return 2;\n"
  "  }\n"
  "  if (basic_bytes[0] != 1U || basic_bytes[1] != 0U || basic_bytes[2] != 0U || basic_bytes[3] != 0U || basic_bytes[4] != 171U) {\n"
  "    return 3;\n"
  "  }\n"
  "  demo__UsesDelimited basic_out = {0};\n"
  "  size_t basic_consumed = basic_size;\n"
  "  if (demo__UsesDelimited__deserialize_(&basic_out, basic_bytes, &basic_consumed) != 0 || basic_consumed != 5U || basic_out.nested.value != 171U) {\n"
  "    return 4;\n"
  "  }\n"
  "  printf(\"delimited_basic_ok\\n\");\n"
  "\n"
  "  demo__UsesEmptyDelimited zero_in = {0};\n"
  "  uint8_t zero_bytes[8] = {0};\n"
  "  size_t zero_size = sizeof(zero_bytes);\n"
  "  if (demo__UsesEmptyDelimited__serialize_(&zero_in, zero_bytes, &zero_size) != 0 || zero_size != 4U) {\n"
  "    return 5;\n"
  "  }\n"
  "  if (zero_bytes[0] != 0U || zero_bytes[1] != 0U || zero_bytes[2] != 0U || zero_bytes[3] != 0U) {\n"
  "    return 6;\n"
  "  }\n"
  "  demo__UsesEmptyDelimited zero_out = {0};\n"
  "  size_t zero_consumed = zero_size;\n"
  "  if (demo__UsesEmptyDelimited__deserialize_(&zero_out, zero_bytes, &zero_consumed) != 0 || zero_consumed != 4U) {\n"
  "    return 7;\n"
  "  }\n"
  "  printf(\"delimited_zero_payload_ok\\n\");\n"
  "\n"
  "  demo__UsesMaxDelimited max_in = {0};\n"
  "  for (size_t i = 0U; i < 64U; ++i) {\n"
  "    max_in.nested.bytes[i] = (uint8_t) i;\n"
  "  }\n"
  "  uint8_t max_bytes[128] = {0};\n"
  "  size_t max_size = sizeof(max_bytes);\n"
  "  if (demo__UsesMaxDelimited__serialize_(&max_in, max_bytes, &max_size) != 0 || max_size != 68U) {\n"
  "    return 8;\n"
  "  }\n"
  "  if (max_bytes[0] != 64U || max_bytes[1] != 0U || max_bytes[2] != 0U || max_bytes[3] != 0U || max_bytes[4] != 0U || max_bytes[67] != 63U) {\n"
  "    return 9;\n"
  "  }\n"
  "  demo__UsesMaxDelimited max_out = {0};\n"
  "  size_t max_consumed = max_size;\n"
  "  if (demo__UsesMaxDelimited__deserialize_(&max_out, max_bytes, &max_consumed) != 0 || max_consumed != 68U) {\n"
  "    return 10;\n"
  "  }\n"
  "  if (max_out.nested.bytes[0] != 0U || max_out.nested.bytes[63] != 63U) {\n"
  "    return 11;\n"
  "  }\n"
  "  printf(\"delimited_max_payload_ok\\n\");\n"
  "\n"
  "  demo__UsesNestedDelimited nested_in = {0};\n"
  "  nested_in.nested.inner.value = 171U;\n"
  "  uint8_t nested_bytes[32] = {0};\n"
  "  size_t nested_size = sizeof(nested_bytes);\n"
  "  if (demo__UsesNestedDelimited__serialize_(&nested_in, nested_bytes, &nested_size) != 0 || nested_size != 9U) {\n"
  "    return 12;\n"
  "  }\n"
  "  if (nested_bytes[0] != 5U || nested_bytes[1] != 0U || nested_bytes[2] != 0U || nested_bytes[3] != 0U || nested_bytes[4] != 1U || nested_bytes[8] != 171U) {\n"
  "    return 13;\n"
  "  }\n"
  "  demo__UsesNestedDelimited nested_out = {0};\n"
  "  size_t nested_consumed = nested_size;\n"
  "  if (demo__UsesNestedDelimited__deserialize_(&nested_out, nested_bytes, &nested_consumed) != 0 || nested_consumed != 9U || nested_out.nested.inner.value != 171U) {\n"
  "    return 14;\n"
  "  }\n"
  "  printf(\"delimited_nested_chain_ok\\n\");\n"
  "\n"
  "  return 0;\n"
  "}\n"
)

set(c_harness_bin "${work_dir}/c_delimited_parity_harness")
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
      "${c_out}/demo/EmptyDelimited_1_0.c"
      "${c_out}/demo/UsesEmptyDelimited_1_0.c"
      "${c_out}/demo/MaxDelimited_1_0.c"
      "${c_out}/demo/UsesMaxDelimited_1_0.c"
      "${c_out}/demo/DelimitedInner_1_0.c"
      "${c_out}/demo/DelimitedOuter_1_0.c"
      "${c_out}/demo/UsesNestedDelimited_1_0.c"
      -o "${c_harness_bin}"
  RESULT_VARIABLE c_cc_result
  OUTPUT_VARIABLE c_cc_stdout
  ERROR_VARIABLE c_cc_stderr
)
if(NOT c_cc_result EQUAL 0)
  message(STATUS "C compile stdout:\n${c_cc_stdout}")
  message(STATUS "C compile stderr:\n${c_cc_stderr}")
  message(FATAL_ERROR "failed to compile fixture C delimited parity harness")
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
  message(FATAL_ERROR "fixture C delimited parity harness failed")
endif()

file(WRITE
  "${ts_out}/runtime_delimited_parity_smoke.ts"
  "import { ${ts_type_module}, ${ts_empty_module}, ${ts_max_module}, ${ts_nested_module} } from \"./index\";\n"
  "\n"
  "const basicIn: ${ts_type_module}.UsesDelimited_1_0 = { nested: { value: 171 } };\n"
  "const basicBytes = ${ts_type_module}.serializeUsesDelimited_1_0(basicIn);\n"
  "if (basicBytes.length !== 5 || basicBytes[0] !== 1 || basicBytes[1] !== 0 || basicBytes[2] !== 0 || basicBytes[3] !== 0 || basicBytes[4] !== 171) {\n"
  "  throw new Error(\"unexpected basic delimited serialization\");\n"
  "}\n"
  "const basicDecoded = ${ts_type_module}.deserializeUsesDelimited_1_0(basicBytes);\n"
  "if (basicDecoded.value.nested.value !== 171 || basicDecoded.consumed !== 5) {\n"
  "  throw new Error(\"unexpected basic delimited deserialize\");\n"
  "}\n"
  "console.log(\"delimited_basic_ok\");\n"
  "\n"
  "const zeroIn: ${ts_empty_module}.UsesEmptyDelimited_1_0 = { nested: {} };\n"
  "const zeroBytes = ${ts_empty_module}.serializeUsesEmptyDelimited_1_0(zeroIn);\n"
  "if (zeroBytes.length !== 4 || zeroBytes[0] !== 0 || zeroBytes[1] !== 0 || zeroBytes[2] !== 0 || zeroBytes[3] !== 0) {\n"
  "  throw new Error(\"unexpected zero-payload delimiter serialization\");\n"
  "}\n"
  "const zeroDecoded = ${ts_empty_module}.deserializeUsesEmptyDelimited_1_0(zeroBytes);\n"
  "if (zeroDecoded.consumed !== 4) {\n"
  "  throw new Error(\"unexpected zero-payload delimiter deserialize\");\n"
  "}\n"
  "console.log(\"delimited_zero_payload_ok\");\n"
  "\n"
  "const maxPayload = Array.from({ length: 64 }, (_v, i) => i);\n"
  "const maxIn: ${ts_max_module}.UsesMaxDelimited_1_0 = { nested: { bytes: maxPayload } };\n"
  "const maxBytes = ${ts_max_module}.serializeUsesMaxDelimited_1_0(maxIn);\n"
  "if (maxBytes.length !== 68 || maxBytes[0] !== 64 || maxBytes[1] !== 0 || maxBytes[2] !== 0 || maxBytes[3] !== 0 || maxBytes[4] !== 0 || maxBytes[67] !== 63) {\n"
  "  throw new Error(\"unexpected max-payload delimiter serialization\");\n"
  "}\n"
  "const maxDecoded = ${ts_max_module}.deserializeUsesMaxDelimited_1_0(maxBytes);\n"
  "if (maxDecoded.consumed !== 68 || maxDecoded.value.nested.bytes.length !== 64 || maxDecoded.value.nested.bytes[0] !== 0 || maxDecoded.value.nested.bytes[63] !== 63) {\n"
  "  throw new Error(\"unexpected max-payload delimiter deserialize\");\n"
  "}\n"
  "console.log(\"delimited_max_payload_ok\");\n"
  "\n"
  "const nestedIn: ${ts_nested_module}.UsesNestedDelimited_1_0 = { nested: { inner: { value: 171 } } };\n"
  "const nestedBytes = ${ts_nested_module}.serializeUsesNestedDelimited_1_0(nestedIn);\n"
  "if (nestedBytes.length !== 9 || nestedBytes[0] !== 5 || nestedBytes[1] !== 0 || nestedBytes[2] !== 0 || nestedBytes[3] !== 0 || nestedBytes[4] !== 1 || nestedBytes[8] !== 171) {\n"
  "  throw new Error(\"unexpected nested delimiter-chain serialization\");\n"
  "}\n"
  "const nestedDecoded = ${ts_nested_module}.deserializeUsesNestedDelimited_1_0(nestedBytes);\n"
  "if (nestedDecoded.consumed !== 9 || nestedDecoded.value.nested.inner.value !== 171) {\n"
  "  throw new Error(\"unexpected nested delimiter-chain deserialize\");\n"
  "}\n"
  "console.log(\"delimited_nested_chain_ok\");\n"
)

file(WRITE
  "${ts_out}/tsconfig-runtime-delimited-parity.json"
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
  COMMAND "${TSC_EXECUTABLE}" -p "${ts_out}/tsconfig-runtime-delimited-parity.json" --pretty false
  WORKING_DIRECTORY "${ts_out}"
  RESULT_VARIABLE tsc_result
  OUTPUT_VARIABLE tsc_stdout
  ERROR_VARIABLE tsc_stderr
)
if(NOT tsc_result EQUAL 0)
  message(STATUS "tsc stdout:\n${tsc_stdout}")
  message(STATUS "tsc stderr:\n${tsc_stderr}")
  message(FATAL_ERROR "failed to compile fixture TypeScript delimited parity smoke")
endif()

file(WRITE "${ts_out}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")

execute_process(
  COMMAND "${NODE_EXECUTABLE}" "${ts_out}/js/runtime_delimited_parity_smoke.js"
  RESULT_VARIABLE node_result
  OUTPUT_VARIABLE node_stdout
  ERROR_VARIABLE node_stderr
)
if(NOT node_result EQUAL 0)
  message(STATUS "node stdout:\n${node_stdout}")
  message(STATUS "node stderr:\n${node_stderr}")
  message(FATAL_ERROR "TypeScript delimited parity smoke execution failed")
endif()

string(STRIP "${c_run_stdout}" c_output)
string(STRIP "${node_stdout}" ts_output)
if(NOT c_output STREQUAL ts_output)
  file(WRITE "${OUT_DIR}/c-output.txt" "${c_output}\n")
  file(WRITE "${OUT_DIR}/ts-output.txt" "${ts_output}\n")
  message(FATAL_ERROR
    "C vs TypeScript delimited parity mismatch. See ${OUT_DIR}/c-output.txt and ${OUT_DIR}/ts-output.txt.")
endif()

set(expected_output [=[delimited_basic_ok
delimited_zero_payload_ok
delimited_max_payload_ok
delimited_nested_chain_ok]=])
if(NOT c_output STREQUAL expected_output)
  file(WRITE "${OUT_DIR}/expected-output.txt" "${expected_output}\n")
  file(WRITE "${OUT_DIR}/actual-output.txt" "${c_output}\n")
  message(FATAL_ERROR
    "unexpected delimiter parity marker output. See ${OUT_DIR}/expected-output.txt and ${OUT_DIR}/actual-output.txt.")
endif()

message(STATUS "fixture C<->TypeScript delimited parity smoke passed")
