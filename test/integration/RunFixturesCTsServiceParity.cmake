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
  "${fixture_root}/Inner.1.0.dsdl"
  "uint8 x\n"
  "@sealed\n"
)
file(WRITE
  "${fixture_root}/DemoService.1.0.dsdl"
  "uint8 req_id\n"
  "uint16[<=2] req_vals\n"
  "@sealed\n"
  "---\n"
  "uint8 status\n"
  "demo.Inner.1.0 result\n"
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
      --ts-module "fixture_ts_service_parity"
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
string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*demo_service_1_0\";" ts_type_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate demo_service_1_0 export alias in ${ts_index}")
endif()
set(ts_type_module "${CMAKE_MATCH_1}")

set(c_harness_src "${work_dir}/c_service_parity_harness.c")
file(WRITE
  "${c_harness_src}"
  "#include <stdint.h>\n"
  "#include <stdio.h>\n"
  "#include \"demo/Inner_1_0.h\"\n"
  "#include \"demo/DemoService_1_0.h\"\n"
  "\n"
  "static void print_bytes(const char* label, const uint8_t* bytes, size_t size) {\n"
  "  printf(\"%s %u\", label, (unsigned)size);\n"
  "  for (size_t i = 0; i < size; ++i) {\n"
  "    printf(\" %u\", (unsigned)bytes[i]);\n"
  "  }\n"
  "  printf(\"\\n\");\n"
  "}\n"
  "\n"
  "int main(void) {\n"
  "  demo__DemoService__Request req_obj;\n"
  "  req_obj.req_id = 7U;\n"
  "  req_obj.req_vals.count = 2U;\n"
  "  req_obj.req_vals.elements[0] = 0x1234U;\n"
  "  req_obj.req_vals.elements[1] = 0xABCDU;\n"
  "  uint8_t req_bytes[16] = {0};\n"
  "  size_t req_size = sizeof(req_bytes);\n"
  "  const int8_t req_ser_rc = demo__DemoService__Request__serialize_(&req_obj, req_bytes, &req_size);\n"
  "  if (req_ser_rc != 0) {\n"
  "    return 2;\n"
  "  }\n"
  "  print_bytes(\"req_bytes\", req_bytes, req_size);\n"
  "\n"
  "  demo__DemoService__Request req_out;\n"
  "  req_out.req_id = 0U;\n"
  "  req_out.req_vals.count = 0U;\n"
  "  size_t req_consumed = req_size;\n"
  "  const int8_t req_de_rc = demo__DemoService__Request__deserialize_(&req_out, req_bytes, &req_consumed);\n"
  "  if ((req_de_rc != 0) || (req_out.req_vals.count != 2U)) {\n"
  "    return 3;\n"
  "  }\n"
  "  printf(\"req_decoded %u %u %u %u %u\\n\", (unsigned)req_out.req_id, (unsigned)req_out.req_vals.count, (unsigned)req_out.req_vals.elements[0], (unsigned)req_out.req_vals.elements[1], (unsigned)req_consumed);\n"
  "\n"
  "  demo__DemoService__Response resp_obj;\n"
  "  resp_obj.status = 99U;\n"
  "  resp_obj.result.x = 42U;\n"
  "  uint8_t resp_bytes[16] = {0};\n"
  "  size_t resp_size = sizeof(resp_bytes);\n"
  "  const int8_t resp_ser_rc = demo__DemoService__Response__serialize_(&resp_obj, resp_bytes, &resp_size);\n"
  "  if (resp_ser_rc != 0) {\n"
  "    return 4;\n"
  "  }\n"
  "  print_bytes(\"resp_bytes\", resp_bytes, resp_size);\n"
  "\n"
  "  demo__DemoService__Response resp_out;\n"
  "  resp_out.status = 0U;\n"
  "  resp_out.result.x = 0U;\n"
  "  size_t resp_consumed = resp_size;\n"
  "  const int8_t resp_de_rc = demo__DemoService__Response__deserialize_(&resp_out, resp_bytes, &resp_consumed);\n"
  "  if (resp_de_rc != 0) {\n"
  "    return 5;\n"
  "  }\n"
  "  printf(\"resp_decoded %u %u %u\\n\", (unsigned)resp_out.status, (unsigned)resp_out.result.x, (unsigned)resp_consumed);\n"
  "  return 0;\n"
  "}\n"
)

set(c_harness_bin "${work_dir}/c_service_parity_harness")
execute_process(
  COMMAND
    "${C_COMPILER}"
      -std=c11
      -Wall
      -Wextra
      -Werror
      -I "${c_out}"
      "${c_harness_src}"
      "${c_out}/demo/Inner_1_0.c"
      "${c_out}/demo/DemoService_1_0.c"
      -o "${c_harness_bin}"
  RESULT_VARIABLE c_cc_result
  OUTPUT_VARIABLE c_cc_stdout
  ERROR_VARIABLE c_cc_stderr
)
if(NOT c_cc_result EQUAL 0)
  message(STATUS "C compile stdout:\n${c_cc_stdout}")
  message(STATUS "C compile stderr:\n${c_cc_stderr}")
  message(FATAL_ERROR "failed to compile fixture C service parity harness")
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
  message(FATAL_ERROR "fixture C service parity harness failed")
endif()

file(WRITE
  "${ts_out}/runtime_service_parity_smoke.ts"
  "import { ${ts_type_module} } from \"./index\";\n"
  "\n"
  "const reqObj: ${ts_type_module}.DemoService_1_0_Request = { req_id: 7, req_vals: [0x1234, 0xabcd] };\n"
  "const reqBytes = ${ts_type_module}.serializeDemoService_1_0_Request(reqObj);\n"
  "const reqByteValues = Array.from(reqBytes, (value) => value.toString()).join(\" \");\n"
  "console.log(\"req_bytes \" + reqBytes.length + (reqByteValues.length > 0 ? \" \" + reqByteValues : \"\"));\n"
  "const reqDecoded = ${ts_type_module}.deserializeDemoService_1_0_Request(reqBytes);\n"
  "console.log(\"req_decoded \" + reqDecoded.value.req_id + \" \" + reqDecoded.value.req_vals.length + \" \" + reqDecoded.value.req_vals[0] + \" \" + reqDecoded.value.req_vals[1] + \" \" + reqDecoded.consumed);\n"
  "\n"
  "const respObj: ${ts_type_module}.DemoService_1_0_Response = { status: 99, result: { x: 42 } };\n"
  "const respBytes = ${ts_type_module}.serializeDemoService_1_0_Response(respObj);\n"
  "const respByteValues = Array.from(respBytes, (value) => value.toString()).join(\" \");\n"
  "console.log(\"resp_bytes \" + respBytes.length + (respByteValues.length > 0 ? \" \" + respByteValues : \"\"));\n"
  "const respDecoded = ${ts_type_module}.deserializeDemoService_1_0_Response(respBytes);\n"
  "console.log(\"resp_decoded \" + respDecoded.value.status + \" \" + respDecoded.value.result.x + \" \" + respDecoded.consumed);\n"
)

file(WRITE
  "${ts_out}/tsconfig-runtime-service-parity.json"
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
  COMMAND "${TSC_EXECUTABLE}" -p "${ts_out}/tsconfig-runtime-service-parity.json" --pretty false
  WORKING_DIRECTORY "${ts_out}"
  RESULT_VARIABLE tsc_result
  OUTPUT_VARIABLE tsc_stdout
  ERROR_VARIABLE tsc_stderr
)
if(NOT tsc_result EQUAL 0)
  message(STATUS "tsc stdout:\n${tsc_stdout}")
  message(STATUS "tsc stderr:\n${tsc_stderr}")
  message(FATAL_ERROR "failed to compile fixture TypeScript service parity smoke")
endif()

file(WRITE "${ts_out}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")

execute_process(
  COMMAND "${NODE_EXECUTABLE}" "${ts_out}/js/runtime_service_parity_smoke.js"
  RESULT_VARIABLE node_result
  OUTPUT_VARIABLE node_stdout
  ERROR_VARIABLE node_stderr
)
if(NOT node_result EQUAL 0)
  message(STATUS "node stdout:\n${node_stdout}")
  message(STATUS "node stderr:\n${node_stderr}")
  message(FATAL_ERROR "TypeScript service parity smoke execution failed")
endif()

string(STRIP "${c_run_stdout}" c_output)
string(STRIP "${node_stdout}" ts_output)
if(NOT c_output STREQUAL ts_output)
  file(WRITE "${OUT_DIR}/c-output.txt" "${c_output}\n")
  file(WRITE "${OUT_DIR}/ts-output.txt" "${ts_output}\n")
  message(FATAL_ERROR
    "C vs TypeScript service parity mismatch. See ${OUT_DIR}/c-output.txt and ${OUT_DIR}/ts-output.txt.")
endif()

message(STATUS "fixture C<->TypeScript service parity smoke passed")
