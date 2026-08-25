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
  "uint8 value\n"
  "@sealed\n"
)
file(WRITE
  "${fixture_root}/Outer.1.0.dsdl"
  "uint1 head\n"
  "demo.Inner.1.0 inner\n"
  "void7\n"
  "uint1 tail\n"
  "@sealed\n"
)
file(WRITE
  "${fixture_root}/Choice.1.0.dsdl"
  "@union\n"
  "bool flag\n"
  "demo.Inner.1.0 inner\n"
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
      --ts-module "fixture_ts_padding_alignment_parity"
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
string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*outer_1_0\";" outer_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate outer_1_0 export alias in ${ts_index}")
endif()
set(ts_outer_module "${CMAKE_MATCH_1}")
string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*choice_1_0\";" choice_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate choice_1_0 export alias in ${ts_index}")
endif()
set(ts_choice_module "${CMAKE_MATCH_1}")

set(c_harness_src "${work_dir}/c_padding_alignment_parity_harness.c")
file(WRITE
  "${c_harness_src}"
  "#include <stdbool.h>\n"
  "#include <stdint.h>\n"
  "#include <stdio.h>\n"
  "#include \"demo/Inner_1_0.h\"\n"
  "#include \"demo/Outer_1_0.h\"\n"
  "#include \"demo/Choice_1_0.h\"\n"
  "\n"
  "int main(void) {\n"
  "  demo__Outer outer_in;\n"
  "  outer_in.head = 1U;\n"
  "  outer_in.inner.value = 170U;\n"
  "  outer_in.tail = 1U;\n"
  "\n"
  "  uint8_t outer_bytes[8] = {0};\n"
  "  size_t outer_size = sizeof(outer_bytes);\n"
  "  const int8_t outer_ser_rc = demo__Outer__serialize_(&outer_in, outer_bytes, &outer_size);\n"
  "  if ((outer_ser_rc != 0) || (outer_size != 3U)) {\n"
  "    return 2;\n"
  "  }\n"
  "  printf(\"%u %u %u\\n\", (unsigned) outer_bytes[0], (unsigned) outer_bytes[1], (unsigned) outer_bytes[2]);\n"
  "\n"
  "  demo__Outer outer_out;\n"
  "  outer_out.head = 0U;\n"
  "  outer_out.inner.value = 0U;\n"
  "  outer_out.tail = 0U;\n"
  "  size_t outer_consumed = outer_size;\n"
  "  const int8_t outer_de_rc = demo__Outer__deserialize_(&outer_out, outer_bytes, &outer_consumed);\n"
  "  if ((outer_de_rc != 0) || (outer_consumed != 3U)) {\n"
  "    return 3;\n"
  "  }\n"
  "  printf(\"%u %u %u %u\\n\", (unsigned) outer_out.head, (unsigned) outer_out.inner.value, (unsigned) outer_out.tail, (unsigned) outer_consumed);\n"
  "\n"
  "  demo__Choice choice_inner_in;\n"
  "  choice_inner_in._tag_ = 1U;\n"
  "  choice_inner_in.inner.value = 170U;\n"
  "  uint8_t choice_inner_bytes[8] = {0};\n"
  "  size_t choice_inner_size = sizeof(choice_inner_bytes);\n"
  "  const int8_t choice_inner_ser_rc = demo__Choice__serialize_(&choice_inner_in, choice_inner_bytes, &choice_inner_size);\n"
  "  if ((choice_inner_ser_rc != 0) || (choice_inner_size != 2U)) {\n"
  "    return 4;\n"
  "  }\n"
  "  printf(\"%u %u\\n\", (unsigned) choice_inner_bytes[0], (unsigned) choice_inner_bytes[1]);\n"
  "\n"
  "  demo__Choice choice_inner_out;\n"
  "  choice_inner_out._tag_ = 0U;\n"
  "  choice_inner_out.flag = false;\n"
  "  size_t choice_inner_consumed = choice_inner_size;\n"
  "  const int8_t choice_inner_de_rc = demo__Choice__deserialize_(&choice_inner_out, choice_inner_bytes, &choice_inner_consumed);\n"
  "  if ((choice_inner_de_rc != 0) || (choice_inner_consumed != 2U) || (choice_inner_out._tag_ != 1U)) {\n"
  "    return 5;\n"
  "  }\n"
  "  printf(\"%u %u %u\\n\", (unsigned) choice_inner_out._tag_, (unsigned) choice_inner_out.inner.value, (unsigned) choice_inner_consumed);\n"
  "\n"
  "  demo__Choice choice_flag_in;\n"
  "  choice_flag_in._tag_ = 0U;\n"
  "  choice_flag_in.flag = true;\n"
  "  uint8_t choice_flag_bytes[8] = {0};\n"
  "  size_t choice_flag_size = sizeof(choice_flag_bytes);\n"
  "  const int8_t choice_flag_ser_rc = demo__Choice__serialize_(&choice_flag_in, choice_flag_bytes, &choice_flag_size);\n"
  "  if ((choice_flag_ser_rc != 0) || (choice_flag_size != 2U)) {\n"
  "    return 6;\n"
  "  }\n"
  "  printf(\"%u %u\\n\", (unsigned) choice_flag_bytes[0], (unsigned) choice_flag_bytes[1]);\n"
  "\n"
  "  demo__Choice choice_flag_out;\n"
  "  choice_flag_out._tag_ = 1U;\n"
  "  choice_flag_out.inner.value = 0U;\n"
  "  size_t choice_flag_consumed = choice_flag_size;\n"
  "  const int8_t choice_flag_de_rc = demo__Choice__deserialize_(&choice_flag_out, choice_flag_bytes, &choice_flag_consumed);\n"
  "  if ((choice_flag_de_rc != 0) || (choice_flag_consumed != 2U) || (choice_flag_out._tag_ != 0U)) {\n"
  "    return 7;\n"
  "  }\n"
  "  printf(\"%u %u %u\\n\", (unsigned) choice_flag_out._tag_, (unsigned) choice_flag_out.flag, (unsigned) choice_flag_consumed);\n"
  "\n"
  "  return 0;\n"
  "}\n"
)

set(c_harness_bin "${work_dir}/c_padding_alignment_parity_harness")
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
      "${c_out}/demo/Outer_1_0.c"
      "${c_out}/demo/Choice_1_0.c"
      -o "${c_harness_bin}"
  RESULT_VARIABLE c_cc_result
  OUTPUT_VARIABLE c_cc_stdout
  ERROR_VARIABLE c_cc_stderr
)
if(NOT c_cc_result EQUAL 0)
  message(STATUS "C compile stdout:\n${c_cc_stdout}")
  message(STATUS "C compile stderr:\n${c_cc_stderr}")
  message(FATAL_ERROR "failed to compile fixture C padding/alignment parity harness")
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
  message(FATAL_ERROR "fixture C padding/alignment parity harness failed")
endif()

file(WRITE
  "${ts_out}/runtime_padding_alignment_parity.ts"
  "import { ${ts_outer_module}, ${ts_choice_module} } from \"./index\";\n"
  "\n"
  "const outerIn: ${ts_outer_module}.Outer_1_0 = { head: 1, inner: { value: 170 }, tail: 1 };\n"
  "const outerBytes = ${ts_outer_module}.serializeOuter_1_0(outerIn);\n"
  "if (outerBytes.length !== 3) {\n"
  "  throw new Error(\"unexpected outer serialized size \" + outerBytes.length);\n"
  "}\n"
  "console.log(outerBytes[0] + \" \" + outerBytes[1] + \" \" + outerBytes[2]);\n"
  "const outerDecoded = ${ts_outer_module}.deserializeOuter_1_0(outerBytes);\n"
  "console.log(outerDecoded.value.head + \" \" + outerDecoded.value.inner.value + \" \" + outerDecoded.value.tail + \" \" + outerDecoded.consumed);\n"
  "\n"
  "const choiceInnerIn: ${ts_choice_module}.Choice_1_0 = { _tag: 1, inner: { value: 170 } };\n"
  "const choiceInnerBytes = ${ts_choice_module}.serializeChoice_1_0(choiceInnerIn);\n"
  "if (choiceInnerBytes.length !== 2) {\n"
  "  throw new Error(\"unexpected choice-inner serialized size \" + choiceInnerBytes.length);\n"
  "}\n"
  "console.log(choiceInnerBytes[0] + \" \" + choiceInnerBytes[1]);\n"
  "const choiceInnerDecoded = ${ts_choice_module}.deserializeChoice_1_0(choiceInnerBytes);\n"
  "if (choiceInnerDecoded.value._tag !== 1 || !(\"inner\" in choiceInnerDecoded.value)) {\n"
  "  throw new Error(\"unexpected decoded choice-inner variant\");\n"
  "}\n"
  "console.log(choiceInnerDecoded.value._tag + \" \" + choiceInnerDecoded.value.inner.value + \" \" + choiceInnerDecoded.consumed);\n"
  "\n"
  "const choiceFlagIn: ${ts_choice_module}.Choice_1_0 = { _tag: 0, flag: true };\n"
  "const choiceFlagBytes = ${ts_choice_module}.serializeChoice_1_0(choiceFlagIn);\n"
  "if (choiceFlagBytes.length !== 2) {\n"
  "  throw new Error(\"unexpected choice-flag serialized size \" + choiceFlagBytes.length);\n"
  "}\n"
  "console.log(choiceFlagBytes[0] + \" \" + choiceFlagBytes[1]);\n"
  "const choiceFlagDecoded = ${ts_choice_module}.deserializeChoice_1_0(choiceFlagBytes);\n"
  "if (choiceFlagDecoded.value._tag !== 0 || !(\"flag\" in choiceFlagDecoded.value)) {\n"
  "  throw new Error(\"unexpected decoded choice-flag variant\");\n"
  "}\n"
  "console.log(choiceFlagDecoded.value._tag + \" \" + (choiceFlagDecoded.value.flag ? 1 : 0) + \" \" + choiceFlagDecoded.consumed);\n"
)

file(WRITE
  "${ts_out}/tsconfig-runtime-padding-alignment-parity.json"
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
  COMMAND "${TSC_EXECUTABLE}" -p "${ts_out}/tsconfig-runtime-padding-alignment-parity.json" --pretty false
  WORKING_DIRECTORY "${ts_out}"
  RESULT_VARIABLE tsc_result
  OUTPUT_VARIABLE tsc_stdout
  ERROR_VARIABLE tsc_stderr
)
if(NOT tsc_result EQUAL 0)
  message(STATUS "tsc stdout:\n${tsc_stdout}")
  message(STATUS "tsc stderr:\n${tsc_stderr}")
  message(FATAL_ERROR "failed to compile fixture TypeScript padding/alignment parity")
endif()

file(WRITE "${ts_out}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")

execute_process(
  COMMAND "${NODE_EXECUTABLE}" "${ts_out}/js/runtime_padding_alignment_parity.js"
  RESULT_VARIABLE node_result
  OUTPUT_VARIABLE node_stdout
  ERROR_VARIABLE node_stderr
)
if(NOT node_result EQUAL 0)
  message(STATUS "node stdout:\n${node_stdout}")
  message(STATUS "node stderr:\n${node_stderr}")
  message(FATAL_ERROR "TypeScript padding/alignment parity execution failed")
endif()

string(STRIP "${c_run_stdout}" c_output)
string(STRIP "${node_stdout}" ts_output)
if(NOT c_output STREQUAL ts_output)
  file(WRITE "${OUT_DIR}/c-output.txt" "${c_output}\n")
  file(WRITE "${OUT_DIR}/ts-output.txt" "${ts_output}\n")
  message(FATAL_ERROR
    "C vs TypeScript padding/alignment parity mismatch. See ${OUT_DIR}/c-output.txt and ${OUT_DIR}/ts-output.txt.")
endif()

message(STATUS "fixture C<->TypeScript padding/alignment parity passed")
