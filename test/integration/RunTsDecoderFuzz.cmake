# Robustness fuzz lane over the generated TypeScript deserializers.
#
# TS/JS is memory-safe + GC'd, so the caught class is a runtime fault (TypeError
# / RangeError escaping deserialize, e.g. a huge `new Array(...)` after a
# length-validation bypass) or a hang / unbounded allocation — not OOB. The
# driver (test/integration/TsDecoderFuzzDriver.ts) feeds a deterministic
# pseudo-random byte stream into each generated deserialize for a compact,
# self-contained adversarial fixture set (union, variable-length arrays, nested
# delimited composites, narrow non-byte-aligned scalars) and round-trips accepted
# objects through serialize.
#
# Iteration count is overridable via env LLVMDSDL_TS_FUZZ_ITERS for the deep run.
cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC OUT_DIR TSC_EXECUTABLE NODE_EXECUTABLE SOURCE_ROOT)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

foreach(tool "${DSDLC}" "${TSC_EXECUTABLE}" "${NODE_EXECUTABLE}")
  if(NOT EXISTS "${tool}")
    message(FATAL_ERROR "required tool not found: ${tool}")
  endif()
endforeach()

set(driver_src "${SOURCE_ROOT}/test/integration/TsDecoderFuzzDriver.ts")
if(NOT EXISTS "${driver_src}")
  message(FATAL_ERROR "TS decoder fuzz driver missing: ${driver_src}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
set(dsdl_root "${OUT_DIR}/dsdl")
set(ts_out "${OUT_DIR}/ts")
file(MAKE_DIRECTORY "${dsdl_root}/vt")

# Compact, self-contained adversarial fixtures (union + var-array + delimited
# composite nesting + narrow scalar). The driver imports vt/Uni and vt/Outer.
file(WRITE "${dsdl_root}/vt/Uni.1.0.dsdl" "@union\nuint8 a\nuint16 b\nuint8[<=64] c\n@sealed\n")
file(WRITE "${dsdl_root}/vt/Inner.1.0.dsdl" "uint16 x\n@extent 64 * 8\n")
file(WRITE "${dsdl_root}/vt/Outer.1.0.dsdl" "vt.Inner.1.0 inner\nvt.Inner.1.0[<=3] more\nint7 narrow\n@sealed\n")

execute_process(
  COMMAND
    "${DSDLC}" --target-language ts
      "${dsdl_root}"
      --outdir "${ts_out}"
      --ts-module dsdlgen
  RESULT_VARIABLE gen_result
  OUTPUT_VARIABLE gen_stdout
  ERROR_VARIABLE gen_stderr
)
if(NOT gen_result EQUAL 0)
  message(STATUS "dsdlc stdout:\n${gen_stdout}")
  message(STATUS "dsdlc stderr:\n${gen_stderr}")
  message(FATAL_ERROR "TypeScript generation for the decoder fuzz lane failed")
endif()

configure_file("${driver_src}" "${ts_out}/decoder_fuzz.ts" COPYONLY)
file(WRITE "${ts_out}/tsconfig.json"
  "{ \"compilerOptions\": { \"target\": \"ES2020\", \"module\": \"CommonJS\", \"moduleResolution\": \"node\", \"strict\": true, \"outDir\": \"./dist\", \"skipLibCheck\": true }, \"include\": [\"**/*.ts\"] }\n")

execute_process(
  COMMAND "${TSC_EXECUTABLE}" -p "${ts_out}/tsconfig.json" --pretty false
  RESULT_VARIABLE tsc_result
  OUTPUT_VARIABLE tsc_stdout
  ERROR_VARIABLE tsc_stderr
)
if(NOT tsc_result EQUAL 0)
  message(STATUS "tsc stdout:\n${tsc_stdout}")
  message(STATUS "tsc stderr:\n${tsc_stderr}")
  message(FATAL_ERROR "tsc failed to compile the TS decoder fuzz driver")
endif()

# The generated TS ships an npm package.json with "type":"module"; tsc emits
# CommonJS, so mark the compiled output as CommonJS to run it under node.
file(WRITE "${ts_out}/dist/package.json" "{\"type\":\"commonjs\"}\n")

# A hard timeout turns a hang / unbounded loop in a decoder into a lane failure.
execute_process(
  COMMAND "${NODE_EXECUTABLE}" "${ts_out}/dist/decoder_fuzz.js"
  RESULT_VARIABLE run_result
  OUTPUT_VARIABLE run_stdout
  ERROR_VARIABLE run_stderr
  TIMEOUT 300
)
message(STATUS "node stdout:\n${run_stdout}")
if(NOT run_stderr STREQUAL "")
  message(STATUS "node stderr:\n${run_stderr}")
endif()
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "generated TS decoder fuzz driver reported a failure (runtime fault or hang on fuzzed input): ${run_result}")
endif()

message(STATUS "TS decoder fuzz lane passed")
