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

set(fixture_root "${OUT_DIR}/fixture_root")
set(c_out "${OUT_DIR}/c")
set(ts_out "${OUT_DIR}/ts")
set(work_dir "${OUT_DIR}/work")
file(MAKE_DIRECTORY "${fixture_root}/demo")
file(MAKE_DIRECTORY "${c_out}")
file(MAKE_DIRECTORY "${ts_out}")
file(MAKE_DIRECTORY "${work_dir}")

file(WRITE
  "${fixture_root}/demo/Empty.1.0.dsdl"
  "@sealed\n"
)
file(WRITE
  "${fixture_root}/demo/UnionEmpty.1.0.dsdl"
  "@union\n"
  "demo.Empty.1.0 none\n"
  "uint8 flag\n"
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
      --ts-module "fixture_ts_union_empty_composite_parity"
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
string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*union_empty_1_0\";" ts_type_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate union_empty_1_0 export alias in ${ts_index}")
endif()
set(ts_type_module "${CMAKE_MATCH_1}")

set(c_harness_src "${work_dir}/c_union_empty_composite_parity_harness.c")
file(WRITE
  "${c_harness_src}"
  "#include <stdint.h>\n"
  "#include <stdio.h>\n"
  "#include \"fixture_root/demo/Empty_1_0.h\"\n"
  "#include \"fixture_root/demo/UnionEmpty_1_0.h\"\n"
  "\n"
  "int main(void) {\n"
  "  fixture_root__demo__UnionEmpty in_empty;\n"
  "  in_empty._tag_ = 0U;\n"
  "  in_empty.none._dummy_ = 0U;\n"
  "\n"
  "  uint8_t empty_bytes[2] = {0};\n"
  "  size_t empty_size = sizeof(empty_bytes);\n"
  "  const int8_t ser_empty_rc = fixture_root__demo__UnionEmpty__serialize_(&in_empty, empty_bytes, &empty_size);\n"
  "  if ((ser_empty_rc != 0) || (empty_size != 1U)) {\n"
  "    return 2;\n"
  "  }\n"
  "  printf(\"%u\\n\", (unsigned) empty_bytes[0]);\n"
  "\n"
  "  fixture_root__demo__UnionEmpty out_empty;\n"
  "  out_empty._tag_ = 1U;\n"
  "  out_empty.flag = 0U;\n"
  "  size_t empty_consumed = empty_size;\n"
  "  const int8_t de_empty_rc = fixture_root__demo__UnionEmpty__deserialize_(&out_empty, empty_bytes, &empty_consumed);\n"
  "  if ((de_empty_rc != 0) || (empty_consumed != 1U) || (out_empty._tag_ != 0U)) {\n"
  "    return 3;\n"
  "  }\n"
  "  printf(\"%u %u\\n\", (unsigned) out_empty._tag_, (unsigned) empty_consumed);\n"
  "\n"
  "  fixture_root__demo__UnionEmpty in_flag;\n"
  "  in_flag._tag_ = 1U;\n"
  "  in_flag.flag = 42U;\n"
  "\n"
  "  uint8_t flag_bytes[2] = {0};\n"
  "  size_t flag_size = sizeof(flag_bytes);\n"
  "  const int8_t ser_flag_rc = fixture_root__demo__UnionEmpty__serialize_(&in_flag, flag_bytes, &flag_size);\n"
  "  if ((ser_flag_rc != 0) || (flag_size != 2U)) {\n"
  "    return 4;\n"
  "  }\n"
  "  printf(\"%u %u\\n\", (unsigned) flag_bytes[0], (unsigned) flag_bytes[1]);\n"
  "\n"
  "  fixture_root__demo__UnionEmpty out_flag;\n"
  "  out_flag._tag_ = 0U;\n"
  "  out_flag.none._dummy_ = 0U;\n"
  "  size_t flag_consumed = flag_size;\n"
  "  const int8_t de_flag_rc = fixture_root__demo__UnionEmpty__deserialize_(&out_flag, flag_bytes, &flag_consumed);\n"
  "  if ((de_flag_rc != 0) || (flag_consumed != 2U) || (out_flag._tag_ != 1U)) {\n"
  "    return 5;\n"
  "  }\n"
  "  printf(\"%u %u %u\\n\", (unsigned) out_flag._tag_, (unsigned) out_flag.flag, (unsigned) flag_consumed);\n"
  "  return 0;\n"
  "}\n"
)

set(c_harness_bin "${work_dir}/c_union_empty_composite_parity_harness")
execute_process(
  COMMAND
    "${C_COMPILER}"
      -std=c11
      -Wall
      -Wextra
      -Werror
      -I "${c_out}"
      "${c_harness_src}"
      "${c_out}/fixture_root/demo/Empty_1_0.c"
      "${c_out}/fixture_root/demo/UnionEmpty_1_0.c"
      -o "${c_harness_bin}"
  RESULT_VARIABLE c_cc_result
  OUTPUT_VARIABLE c_cc_stdout
  ERROR_VARIABLE c_cc_stderr
)
if(NOT c_cc_result EQUAL 0)
  message(STATUS "C compile stdout:\n${c_cc_stdout}")
  message(STATUS "C compile stderr:\n${c_cc_stderr}")
  message(FATAL_ERROR "failed to compile fixture C union-empty-composite parity harness")
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
  message(FATAL_ERROR "fixture C union-empty-composite parity harness failed")
endif()

file(WRITE
  "${ts_out}/runtime_union_empty_composite_parity_smoke.ts"
  "import { ${ts_type_module} } from \"./index\";\n"
  "\n"
  "const emptyObj: ${ts_type_module}.UnionEmpty_1_0 = { _tag: 0, none: {} as any };\n"
  "const emptyBytes = ${ts_type_module}.serializeUnionEmpty_1_0(emptyObj);\n"
  "console.log(Array.from(emptyBytes).join(\" \"));\n"
  "const emptyDecoded = ${ts_type_module}.deserializeUnionEmpty_1_0(emptyBytes);\n"
  "console.log(emptyDecoded.value._tag + \" \" + emptyDecoded.consumed);\n"
  "\n"
  "const flagObj: ${ts_type_module}.UnionEmpty_1_0 = { _tag: 1, flag: 42 };\n"
  "const flagBytes = ${ts_type_module}.serializeUnionEmpty_1_0(flagObj);\n"
  "console.log(Array.from(flagBytes).join(\" \"));\n"
  "const flagDecoded = ${ts_type_module}.deserializeUnionEmpty_1_0(flagBytes);\n"
  "if (flagDecoded.value._tag !== 1 || !(\"flag\" in flagDecoded.value)) {\n"
  "  throw new Error(\"unexpected decoded union flag variant\");\n"
  "}\n"
  "console.log(flagDecoded.value._tag + \" \" + flagDecoded.value.flag + \" \" + flagDecoded.consumed);\n"
)

file(WRITE
  "${ts_out}/tsconfig-runtime-union-empty-composite-parity.json"
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
  COMMAND "${TSC_EXECUTABLE}" -p "${ts_out}/tsconfig-runtime-union-empty-composite-parity.json" --pretty false
  WORKING_DIRECTORY "${ts_out}"
  RESULT_VARIABLE tsc_result
  OUTPUT_VARIABLE tsc_stdout
  ERROR_VARIABLE tsc_stderr
)
if(NOT tsc_result EQUAL 0)
  message(STATUS "tsc stdout:\n${tsc_stdout}")
  message(STATUS "tsc stderr:\n${tsc_stderr}")
  message(FATAL_ERROR "failed to compile fixture TypeScript union-empty-composite parity smoke")
endif()

file(WRITE "${ts_out}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")

execute_process(
  COMMAND "${NODE_EXECUTABLE}" "${ts_out}/js/runtime_union_empty_composite_parity_smoke.js"
  RESULT_VARIABLE node_result
  OUTPUT_VARIABLE node_stdout
  ERROR_VARIABLE node_stderr
)
if(NOT node_result EQUAL 0)
  message(STATUS "node stdout:\n${node_stdout}")
  message(STATUS "node stderr:\n${node_stderr}")
  message(FATAL_ERROR "TypeScript union-empty-composite parity smoke execution failed")
endif()

string(STRIP "${c_run_stdout}" c_output)
string(STRIP "${node_stdout}" ts_output)
if(NOT c_output STREQUAL ts_output)
  file(WRITE "${OUT_DIR}/c-output.txt" "${c_output}\n")
  file(WRITE "${OUT_DIR}/ts-output.txt" "${ts_output}\n")
  message(FATAL_ERROR
    "C vs TypeScript union-empty-composite parity mismatch. See ${OUT_DIR}/c-output.txt and ${OUT_DIR}/ts-output.txt.")
endif()

message(STATUS "fixture C<->TypeScript union-empty-composite parity smoke passed")
