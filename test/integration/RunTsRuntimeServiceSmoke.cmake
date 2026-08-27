cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC OUT_DIR SOURCE_ROOT TSC_EXECUTABLE NODE_EXECUTABLE)
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
if(NOT EXISTS "${TSC_EXECUTABLE}")
  message(FATAL_ERROR "tsc executable not found: ${TSC_EXECUTABLE}")
endif()
if(NOT EXISTS "${NODE_EXECUTABLE}")
  message(FATAL_ERROR "node executable not found: ${NODE_EXECUTABLE}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(fixture_root "${OUT_DIR}/fixture_root")
set(ts_out "${OUT_DIR}/ts")
file(MAKE_DIRECTORY "${fixture_root}/demo")
file(MAKE_DIRECTORY "${ts_out}")

file(WRITE
  "${fixture_root}/demo/Inner.1.0.dsdl"
  "uint8 x\n"
  "@sealed\n"
)
file(WRITE
  "${fixture_root}/demo/DemoService.1.0.dsdl"
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
    "${DSDLC}" --target-language ts --versioned-type-names
      "${fixture_root}"
      --outdir "${ts_out}"
      --ts-module "ts_runtime_service_smoke"
  RESULT_VARIABLE ts_gen_result
  OUTPUT_VARIABLE ts_gen_stdout
  ERROR_VARIABLE ts_gen_stderr
)
if(NOT ts_gen_result EQUAL 0)
  message(STATUS "dsdlc ts stdout:\n${ts_gen_stdout}")
  message(STATUS "dsdlc ts stderr:\n${ts_gen_stderr}")
  message(FATAL_ERROR "TypeScript generation failed for service smoke")
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

file(WRITE
  "${ts_out}/runtime_service_smoke.ts"
  "import { ${ts_type_module} } from \"./index\";\n"
  "\n"
  "const reqObj: ${ts_type_module}.DemoService_1_0_Request = { req_id: 7, req_vals: [0x1234, 0xabcd] };\n"
  "const reqBytes = ${ts_type_module}.serializeDemoService_1_0_Request(reqObj);\n"
  "const reqDecoded = ${ts_type_module}.deserializeDemoService_1_0_Request(reqBytes);\n"
  "if (reqDecoded.value.req_vals.length !== 2) {\n"
  "  throw new Error(\"unexpected request array length\");\n"
  "}\n"
  "console.log(\"req \" + reqBytes.length + \" \" + reqDecoded.value.req_id + \" \" + reqDecoded.value.req_vals.length + \" \" + reqDecoded.value.req_vals[0] + \" \" + reqDecoded.value.req_vals[1] + \" \" + reqDecoded.consumed);\n"
  "\n"
  "const respObj: ${ts_type_module}.DemoService_1_0_Response = { status: 99, result: { x: 42 } };\n"
  "const respBytes = ${ts_type_module}.serializeDemoService_1_0_Response(respObj);\n"
  "const respDecoded = ${ts_type_module}.deserializeDemoService_1_0_Response(respBytes);\n"
  "console.log(\"resp \" + respBytes.length + \" \" + respDecoded.value.status + \" \" + respDecoded.value.result.x + \" \" + respDecoded.consumed);\n"
)

file(WRITE
  "${ts_out}/tsconfig-runtime-service-smoke.json"
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
  COMMAND "${TSC_EXECUTABLE}" -p "${ts_out}/tsconfig-runtime-service-smoke.json" --pretty false
  WORKING_DIRECTORY "${ts_out}"
  RESULT_VARIABLE tsc_result
  OUTPUT_VARIABLE tsc_stdout
  ERROR_VARIABLE tsc_stderr
)
if(NOT tsc_result EQUAL 0)
  message(STATUS "tsc stdout:\n${tsc_stdout}")
  message(STATUS "tsc stderr:\n${tsc_stderr}")
  message(FATAL_ERROR "failed to compile service TypeScript runtime smoke")
endif()

file(WRITE "${ts_out}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")

execute_process(
  COMMAND "${NODE_EXECUTABLE}" "${ts_out}/js/runtime_service_smoke.js"
  RESULT_VARIABLE node_result
  OUTPUT_VARIABLE node_stdout
  ERROR_VARIABLE node_stderr
)
if(NOT node_result EQUAL 0)
  message(STATUS "node stdout:\n${node_stdout}")
  message(STATUS "node stderr:\n${node_stderr}")
  message(FATAL_ERROR "TypeScript service runtime smoke execution failed")
endif()

string(STRIP "${node_stdout}" actual_output)
set(expected_output "req 6 7 2 4660 43981 6\nresp 2 99 42 2")
if(NOT actual_output STREQUAL expected_output)
  file(WRITE "${OUT_DIR}/expected-output.txt" "${expected_output}\n")
  file(WRITE "${OUT_DIR}/actual-output.txt" "${actual_output}\n")
  message(FATAL_ERROR
    "unexpected service runtime smoke output. See ${OUT_DIR}/expected-output.txt and ${OUT_DIR}/actual-output.txt.")
endif()

message(STATUS "TypeScript service runtime smoke passed")
