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

set(fixture_root "${OUT_DIR}/demo")
set(ts_out "${OUT_DIR}/ts")
file(MAKE_DIRECTORY "${fixture_root}")
file(MAKE_DIRECTORY "${ts_out}")

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
    "${DSDLC}" --target-language ts
      "${fixture_root}"
      --outdir "${ts_out}"
      --ts-module "ts_runtime_truncated_decode_smoke"
  RESULT_VARIABLE ts_gen_result
  OUTPUT_VARIABLE ts_gen_stdout
  ERROR_VARIABLE ts_gen_stderr
)
if(NOT ts_gen_result EQUAL 0)
  message(STATUS "dsdlc ts stdout:\n${ts_gen_stdout}")
  message(STATUS "dsdlc ts stderr:\n${ts_gen_stderr}")
  message(FATAL_ERROR "TypeScript generation failed for truncated decode smoke")
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

file(WRITE
  "${ts_out}/runtime_truncated_decode_smoke.ts"
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
  "${ts_out}/tsconfig-runtime-truncated-decode-smoke.json"
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
  COMMAND "${TSC_EXECUTABLE}" -p "${ts_out}/tsconfig-runtime-truncated-decode-smoke.json" --pretty false
  WORKING_DIRECTORY "${ts_out}"
  RESULT_VARIABLE tsc_result
  OUTPUT_VARIABLE tsc_stdout
  ERROR_VARIABLE tsc_stderr
)
if(NOT tsc_result EQUAL 0)
  message(STATUS "tsc stdout:\n${tsc_stdout}")
  message(STATUS "tsc stderr:\n${tsc_stderr}")
  message(FATAL_ERROR "failed to compile TypeScript runtime truncated decode smoke")
endif()

file(WRITE "${ts_out}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")

execute_process(
  COMMAND "${NODE_EXECUTABLE}" "${ts_out}/js/runtime_truncated_decode_smoke.js"
  RESULT_VARIABLE node_result
  OUTPUT_VARIABLE node_stdout
  ERROR_VARIABLE node_stderr
)
if(NOT node_result EQUAL 0)
  message(STATUS "node stdout:\n${node_stdout}")
  message(STATUS "node stderr:\n${node_stderr}")
  message(FATAL_ERROR "TypeScript runtime truncated decode smoke execution failed")
endif()

string(STRIP "${node_stdout}" actual_output)
set(expected_output [=[scalar_truncated_zero_extended
vector_truncated_zero_extended
vector_invalid_length_rejected
composite_truncated_zero_extended
service_request_truncated_zero_extended
service_response_truncated_zero_extended]=])
if(NOT actual_output STREQUAL expected_output)
  file(WRITE "${OUT_DIR}/expected-output.txt" "${expected_output}\n")
  file(WRITE "${OUT_DIR}/actual-output.txt" "${actual_output}\n")
  message(FATAL_ERROR
    "unexpected runtime truncated decode smoke output. See ${OUT_DIR}/expected-output.txt and ${OUT_DIR}/actual-output.txt.")
endif()

message(STATUS "TypeScript runtime truncated decode smoke passed")
