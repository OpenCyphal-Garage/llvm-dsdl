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
set(work_dir "${OUT_DIR}/work")
file(MAKE_DIRECTORY "${fixture_root}/demo")
file(MAKE_DIRECTORY "${ts_out}")
file(MAKE_DIRECTORY "${work_dir}")

file(WRITE
  "${fixture_root}/demo/BigIntType.1.0.dsdl"
  "uint64 wide_u\n"
  "int64 wide_s\n"
  "@sealed\n"
)

execute_process(
  COMMAND
    "${DSDLC}" --target-language ts --versioned-type-names
      "${fixture_root}"
      --outdir "${ts_out}"
      --ts-module "ts_runtime_bigint_smoke"
  RESULT_VARIABLE ts_gen_result
  OUTPUT_VARIABLE ts_gen_stdout
  ERROR_VARIABLE ts_gen_stderr
)
if(NOT ts_gen_result EQUAL 0)
  message(STATUS "dsdlc ts stdout:\n${ts_gen_stdout}")
  message(STATUS "dsdlc ts stderr:\n${ts_gen_stderr}")
  message(FATAL_ERROR "TypeScript generation failed for bigint smoke")
endif()

set(ts_index "${ts_out}/index.ts")
if(NOT EXISTS "${ts_index}")
  message(FATAL_ERROR "generated TypeScript index missing: ${ts_index}")
endif()
file(READ "${ts_index}" ts_index_content)
string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*big_int_type_1_0\";" ts_type_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate big_int_type_1_0 export alias in ${ts_index}")
endif()
set(ts_type_module "${CMAKE_MATCH_1}")

file(WRITE
  "${ts_out}/runtime_bigint_smoke.ts"
  "import { ${ts_type_module} } from \"./index\";\n"
  "\n"
  "const inObj: ${ts_type_module}.BigIntType_1_0 = {\n"
  "  wide_u: 1152921504606846977n,\n"
  "  wide_s: -1152921504606846976n,\n"
  "};\n"
  "const outBytes = ${ts_type_module}.serializeBigIntType_1_0(inObj);\n"
  "if (outBytes.length !== 16) {\n"
  "  throw new Error(\"unexpected serialized size \" + outBytes.length);\n"
  "}\n"
  "console.log(Array.from(outBytes).join(\" \"));\n"
  "\n"
  "const decoded = ${ts_type_module}.deserializeBigIntType_1_0(outBytes);\n"
  "if (typeof decoded.value.wide_u !== \"bigint\" || typeof decoded.value.wide_s !== \"bigint\") {\n"
  "  throw new Error(\"decoded fields are not bigint\");\n"
  "}\n"
  "console.log(decoded.value.wide_u.toString() + \" \" + decoded.value.wide_s.toString() + \" \" + decoded.consumed);\n"
)

file(WRITE
  "${ts_out}/tsconfig-runtime-bigint-smoke.json"
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
  COMMAND "${TSC_EXECUTABLE}" -p "${ts_out}/tsconfig-runtime-bigint-smoke.json" --pretty false
  WORKING_DIRECTORY "${ts_out}"
  RESULT_VARIABLE tsc_result
  OUTPUT_VARIABLE tsc_stdout
  ERROR_VARIABLE tsc_stderr
)
if(NOT tsc_result EQUAL 0)
  message(STATUS "tsc stdout:\n${tsc_stdout}")
  message(STATUS "tsc stderr:\n${tsc_stderr}")
  message(FATAL_ERROR "failed to compile bigint TypeScript runtime smoke")
endif()

file(WRITE "${ts_out}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")

execute_process(
  COMMAND "${NODE_EXECUTABLE}" "${ts_out}/js/runtime_bigint_smoke.js"
  RESULT_VARIABLE node_result
  OUTPUT_VARIABLE node_stdout
  ERROR_VARIABLE node_stderr
)
if(NOT node_result EQUAL 0)
  message(STATUS "node stdout:\n${node_stdout}")
  message(STATUS "node stderr:\n${node_stderr}")
  message(FATAL_ERROR "TypeScript bigint runtime smoke execution failed")
endif()

string(STRIP "${node_stdout}" actual_output)
set(expected_output "1 0 0 0 0 0 0 16 0 0 0 0 0 0 0 240\n1152921504606846977 -1152921504606846976 16")
if(NOT actual_output STREQUAL expected_output)
  file(WRITE "${OUT_DIR}/expected-output.txt" "${expected_output}\n")
  file(WRITE "${OUT_DIR}/actual-output.txt" "${actual_output}\n")
  message(FATAL_ERROR
    "unexpected bigint runtime smoke output. See ${OUT_DIR}/expected-output.txt and ${OUT_DIR}/actual-output.txt.")
endif()

message(STATUS "TypeScript bigint runtime smoke passed")
