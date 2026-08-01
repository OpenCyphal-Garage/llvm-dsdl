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
  "${fixture_root}/Inner.1.0.dsdl"
  "uint16 a\n"
  "@sealed\n"
)
file(WRITE
  "${fixture_root}/Composite.1.0.dsdl"
  "demo.Inner.1.0 inner\n"
  "uint8 tail\n"
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
    "${DSDLC}" --target-language ts
      "${fixture_root}"
      --outdir "${ts_out}"
      --ts-module "fixture_ts_truncated_decode_parity"
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

string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*composite_1_0\";" composite_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate composite_1_0 export alias in ${ts_index}")
endif()
set(ts_composite_module "${CMAKE_MATCH_1}")

string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*svc_1_0\";" svc_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate svc_1_0 export alias in ${ts_index}")
endif()
set(ts_svc_module "${CMAKE_MATCH_1}")

set(c_harness_src "${work_dir}/c_truncated_decode_parity_harness.c")
file(WRITE
  "${c_harness_src}"
  "#include <stdint.h>\n"
  "#include <stdio.h>\n"
  "#include \"demo/Scalar_1_0.h\"\n"
  "#include \"demo/Vector_1_0.h\"\n"
  "#include \"demo/Inner_1_0.h\"\n"
  "#include \"demo/Composite_1_0.h\"\n"
  "#include \"demo/Svc_1_0.h\"\n"
  "\n"
  "int main(void) {\n"
  "  demo__Scalar scalar_in;\n"
  "  scalar_in.value = 0x3456U;\n"
  "  uint8_t scalar_full[4] = {0};\n"
  "  size_t scalar_full_size = sizeof(scalar_full);\n"
  "  if (demo__Scalar__serialize_(&scalar_in, scalar_full, &scalar_full_size) != 0 || scalar_full_size != 2U) {\n"
  "    return 2;\n"
  "  }\n"
  "  demo__Scalar scalar_out;\n"
  "  scalar_out.value = 0U;\n"
  "  size_t scalar_short_size = scalar_full_size - 1U;\n"
  "  if (demo__Scalar__deserialize_(&scalar_out, scalar_full, &scalar_short_size) != 0) {\n"
  "    return 3;\n"
  "  }\n"
  "  if (scalar_out.value != 0x56U || scalar_short_size != 1U) {\n"
  "    return 4;\n"
  "  }\n"
  "  printf(\"scalar_truncated_zero_extended\\n\");\n"
  "\n"
  "  demo__Vector vector_in;\n"
  "  vector_in.values.count = 5U;\n"
  "  vector_in.values.elements[0] = 1U;\n"
  "  vector_in.values.elements[1] = 2U;\n"
  "  vector_in.values.elements[2] = 3U;\n"
  "  vector_in.values.elements[3] = 4U;\n"
  "  vector_in.values.elements[4] = 229U;\n"
  "  uint8_t vector_full[16] = {0};\n"
  "  size_t vector_full_size = sizeof(vector_full);\n"
  "  if (demo__Vector__serialize_(&vector_in, vector_full, &vector_full_size) != 0) {\n"
  "    return 5;\n"
  "  }\n"
  "  demo__Vector vector_out;\n"
  "  vector_out.values.count = 0U;\n"
  "  size_t vector_short_size = vector_full_size - 1U;\n"
  "  if (demo__Vector__deserialize_(&vector_out, vector_full, &vector_short_size) != 0) {\n"
  "    return 6;\n"
  "  }\n"
  "  if (vector_out.values.count != 5U || vector_out.values.elements[0] != 1U || vector_out.values.elements[1] != 2U || vector_out.values.elements[2] != 3U || vector_out.values.elements[3] != 4U || vector_out.values.elements[4] != 0U || vector_short_size != vector_full_size - 1U) {\n"
  "    return 7;\n"
  "  }\n"
  "  printf(\"vector_truncated_zero_extended\\n\");\n"
  "\n"
  "  demo__Vector vector_invalid;\n"
  "  vector_invalid.values.count = 0U;\n"
  "  uint8_t vector_invalid_bytes[1] = {0x07U};\n"
  "  size_t vector_invalid_size = 1U;\n"
  "  if (demo__Vector__deserialize_(&vector_invalid, vector_invalid_bytes, &vector_invalid_size) == 0) {\n"
  "    return 8;\n"
  "  }\n"
  "  printf(\"vector_invalid_length_rejected\\n\");\n"
  "\n"
  "  demo__Composite composite_in;\n"
  "  composite_in.inner.a = 0x1234U;\n"
  "  composite_in.tail = 0xABU;\n"
  "  uint8_t composite_full[8] = {0};\n"
  "  size_t composite_full_size = sizeof(composite_full);\n"
  "  if (demo__Composite__serialize_(&composite_in, composite_full, &composite_full_size) != 0 || composite_full_size != 3U) {\n"
  "    return 9;\n"
  "  }\n"
  "  demo__Composite composite_out;\n"
  "  composite_out.inner.a = 0U;\n"
  "  composite_out.tail = 0U;\n"
  "  size_t composite_short_size = composite_full_size - 1U;\n"
  "  if (demo__Composite__deserialize_(&composite_out, composite_full, &composite_short_size) != 0) {\n"
  "    return 10;\n"
  "  }\n"
  "  if (composite_out.inner.a != 0x1234U || composite_out.tail != 0U || composite_short_size != 2U) {\n"
  "    return 11;\n"
  "  }\n"
  "  printf(\"composite_truncated_zero_extended\\n\");\n"
  "\n"
  "  demo__Svc__Request svc_req_in;\n"
  "  svc_req_in.x = 0x4567U;\n"
  "  uint8_t svc_req_full[8] = {0};\n"
  "  size_t svc_req_full_size = sizeof(svc_req_full);\n"
  "  if (demo__Svc__Request__serialize_(&svc_req_in, svc_req_full, &svc_req_full_size) != 0 || svc_req_full_size != 2U) {\n"
  "    return 12;\n"
  "  }\n"
  "  demo__Svc__Request svc_req_out;\n"
  "  svc_req_out.x = 0U;\n"
  "  size_t svc_req_short_size = svc_req_full_size - 1U;\n"
  "  if (demo__Svc__Request__deserialize_(&svc_req_out, svc_req_full, &svc_req_short_size) != 0) {\n"
  "    return 13;\n"
  "  }\n"
  "  if (svc_req_out.x != 0x67U || svc_req_short_size != 1U) {\n"
  "    return 14;\n"
  "  }\n"
  "  printf(\"service_request_truncated_zero_extended\\n\");\n"
  "\n"
  "  demo__Svc__Response svc_resp_in;\n"
  "  svc_resp_in.y = 0xABU;\n"
  "  uint8_t svc_resp_full[8] = {0};\n"
  "  size_t svc_resp_full_size = sizeof(svc_resp_full);\n"
  "  if (demo__Svc__Response__serialize_(&svc_resp_in, svc_resp_full, &svc_resp_full_size) != 0 || svc_resp_full_size != 1U) {\n"
  "    return 15;\n"
  "  }\n"
  "  demo__Svc__Response svc_resp_out;\n"
  "  svc_resp_out.y = 0U;\n"
  "  size_t svc_resp_short_size = 0U;\n"
  "  if (demo__Svc__Response__deserialize_(&svc_resp_out, svc_resp_full, &svc_resp_short_size) != 0) {\n"
  "    return 16;\n"
  "  }\n"
  "  if (svc_resp_out.y != 0U || svc_resp_short_size != 0U) {\n"
  "    return 17;\n"
  "  }\n"
  "  printf(\"service_response_truncated_zero_extended\\n\");\n"
  "\n"
  "  return 0;\n"
  "}\n"
)

set(c_harness_bin "${work_dir}/c_truncated_decode_parity_harness")
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
      "${c_out}/demo/Inner_1_0.c"
      "${c_out}/demo/Composite_1_0.c"
      "${c_out}/demo/Svc_1_0.c"
      -o "${c_harness_bin}"
  RESULT_VARIABLE c_cc_result
  OUTPUT_VARIABLE c_cc_stdout
  ERROR_VARIABLE c_cc_stderr
)
if(NOT c_cc_result EQUAL 0)
  message(STATUS "C compile stdout:\n${c_cc_stdout}")
  message(STATUS "C compile stderr:\n${c_cc_stderr}")
  message(FATAL_ERROR "failed to compile fixture C truncated decode parity harness")
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
  message(FATAL_ERROR "fixture C truncated decode parity harness failed")
endif()

file(WRITE
  "${ts_out}/runtime_truncated_decode_parity.ts"
  "import { ${ts_scalar_module}, ${ts_vector_module}, ${ts_composite_module}, ${ts_svc_module} } from \"./index\";\n"
  "\n"
  "const scalarFull = ${ts_scalar_module}.serializeScalar_1_0({ value: 0x3456 });\n"
  "const scalarShort = scalarFull.subarray(0, scalarFull.length - 1);\n"
  "const scalarDecoded = ${ts_scalar_module}.deserializeScalar_1_0(scalarShort);\n"
  "if (scalarDecoded.value.value !== 0x56 || scalarDecoded.consumed !== scalarShort.length) {\n"
  "  throw new Error(\"unexpected scalar truncation decode behaviour\");\n"
  "}\n"
  "console.log(\"scalar_truncated_zero_extended\");\n"
  "\n"
  "const vectorFull = ${ts_vector_module}.serializeVector_1_0({ values: [1, 2, 3, 4, 229] });\n"
  "const vectorShort = vectorFull.subarray(0, vectorFull.length - 1);\n"
  "const vectorDecoded = ${ts_vector_module}.deserializeVector_1_0(vectorShort);\n"
  "if (vectorDecoded.value.values.length !== 5 || vectorDecoded.value.values[0] !== 1 || vectorDecoded.value.values[1] !== 2 || vectorDecoded.value.values[2] !== 3 || vectorDecoded.value.values[3] !== 4 || vectorDecoded.value.values[4] !== 0 || vectorDecoded.consumed !== vectorShort.length) {\n"
  "  throw new Error(\"unexpected vector truncation decode behaviour\");\n"
  "}\n"
  "console.log(\"vector_truncated_zero_extended\");\n"
  "\n"
  "let vectorInvalidRejected = false;\n"
  "try {\n"
  "  ${ts_vector_module}.deserializeVector_1_0(new Uint8Array([0x07]));\n"
  "} catch (_err) {\n"
  "  vectorInvalidRejected = true;\n"
  "}\n"
  "if (!vectorInvalidRejected) {\n"
  "  throw new Error(\"invalid vector length header was accepted\");\n"
  "}\n"
  "console.log(\"vector_invalid_length_rejected\");\n"
  "\n"
  "const compositeFull = ${ts_composite_module}.serializeComposite_1_0({ inner: { a: 0x1234 }, tail: 0xab });\n"
  "const compositeShort = compositeFull.subarray(0, compositeFull.length - 1);\n"
  "const compositeDecoded = ${ts_composite_module}.deserializeComposite_1_0(compositeShort);\n"
  "if (compositeDecoded.value.inner.a !== 0x1234 || compositeDecoded.value.tail !== 0 || compositeDecoded.consumed !== compositeShort.length) {\n"
  "  throw new Error(\"unexpected composite truncation decode behaviour\");\n"
  "}\n"
  "console.log(\"composite_truncated_zero_extended\");\n"
  "\n"
  "const svcReqFull = ${ts_svc_module}.serializeSvc_1_0_Request({ x: 0x4567 });\n"
  "const svcReqShort = svcReqFull.subarray(0, svcReqFull.length - 1);\n"
  "const svcReqDecoded = ${ts_svc_module}.deserializeSvc_1_0_Request(svcReqShort);\n"
  "if (svcReqDecoded.value.x !== 0x67 || svcReqDecoded.consumed !== svcReqShort.length) {\n"
  "  throw new Error(\"unexpected service request truncation decode behaviour\");\n"
  "}\n"
  "console.log(\"service_request_truncated_zero_extended\");\n"
  "\n"
  "const svcRespFull = ${ts_svc_module}.serializeSvc_1_0_Response({ y: 0xab });\n"
  "const svcRespShort = svcRespFull.subarray(0, 0);\n"
  "const svcRespDecoded = ${ts_svc_module}.deserializeSvc_1_0_Response(svcRespShort);\n"
  "if (svcRespDecoded.value.y !== 0 || svcRespDecoded.consumed !== 0) {\n"
  "  throw new Error(\"unexpected service response truncation decode behaviour\");\n"
  "}\n"
  "console.log(\"service_response_truncated_zero_extended\");\n"
)

file(WRITE
  "${ts_out}/tsconfig-runtime-truncated-decode-parity.json"
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
  COMMAND "${TSC_EXECUTABLE}" -p "${ts_out}/tsconfig-runtime-truncated-decode-parity.json" --pretty false
  WORKING_DIRECTORY "${ts_out}"
  RESULT_VARIABLE tsc_result
  OUTPUT_VARIABLE tsc_stdout
  ERROR_VARIABLE tsc_stderr
)
if(NOT tsc_result EQUAL 0)
  message(STATUS "tsc stdout:\n${tsc_stdout}")
  message(STATUS "tsc stderr:\n${tsc_stderr}")
  message(FATAL_ERROR "failed to compile fixture TypeScript truncated decode parity")
endif()

file(WRITE "${ts_out}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")

execute_process(
  COMMAND "${NODE_EXECUTABLE}" "${ts_out}/js/runtime_truncated_decode_parity.js"
  RESULT_VARIABLE node_result
  OUTPUT_VARIABLE node_stdout
  ERROR_VARIABLE node_stderr
)
if(NOT node_result EQUAL 0)
  message(STATUS "node stdout:\n${node_stdout}")
  message(STATUS "node stderr:\n${node_stderr}")
  message(FATAL_ERROR "TypeScript truncated decode parity execution failed")
endif()

string(STRIP "${c_run_stdout}" c_output)
string(STRIP "${node_stdout}" ts_output)
if(NOT c_output STREQUAL ts_output)
  file(WRITE "${OUT_DIR}/c-output.txt" "${c_output}\n")
  file(WRITE "${OUT_DIR}/ts-output.txt" "${ts_output}\n")
  message(FATAL_ERROR
    "C vs TypeScript truncated decode parity mismatch. See ${OUT_DIR}/c-output.txt and ${OUT_DIR}/ts-output.txt.")
endif()

message(STATUS "fixture C<->TypeScript truncated decode parity passed")
