cmake_minimum_required(VERSION 3.24)

# Coverage-guided libFuzzer lane over the generated native (C) deserializers on
# the real UAVCAN corpus, compiled under ASan + UBSan. See NativeDecoderFuzz.c
# for the harness and the rationale. On a toolchain whose Clang lacks compiler-rt
# libFuzzer (-fsanitize=fuzzer), this degrades — loudly — to an ASan/UBSan
# *replay* over the seed + committed regression corpus, so the lane still runs.

foreach(var DSDLC UAVCAN_ROOT OUT_DIR C_COMPILER SOURCE_ROOT)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

if(NOT EXISTS "${DSDLC}")
  message(FATAL_ERROR "dsdlc executable not found: ${DSDLC}")
endif()
if(NOT EXISTS "${UAVCAN_ROOT}")
  message(FATAL_ERROR "uavcan root not found: ${UAVCAN_ROOT}")
endif()
if(NOT EXISTS "${C_COMPILER}")
  message(FATAL_ERROR "C compiler not found: ${C_COMPILER}")
endif()

if(NOT DEFINED SANITIZE OR "${SANITIZE}" STREQUAL "")
  set(SANITIZE "address,undefined")
endif()
if(NOT DEFINED FUZZ_RUNS OR "${FUZZ_RUNS}" STREQUAL "")
  set(FUZZ_RUNS "20000")
endif()
if(NOT DEFINED MAX_LEN OR "${MAX_LEN}" STREQUAL "")
  set(MAX_LEN "4096")
endif()

set(harness_src "${SOURCE_ROOT}/test/integration/NativeDecoderFuzz.c")
if(NOT EXISTS "${harness_src}")
  message(FATAL_ERROR "fuzz harness source missing: ${harness_src}")
endif()

set(c_out "${OUT_DIR}/c")
set(build_out "${OUT_DIR}/build")
set(obj_dir "${build_out}/obj")
set(work_corpus "${OUT_DIR}/corpus")
set(artifacts "${OUT_DIR}/artifacts")
file(REMOVE_RECURSE "${OUT_DIR}")
foreach(d "${c_out}" "${build_out}" "${obj_dir}" "${work_corpus}" "${artifacts}")
  file(MAKE_DIRECTORY "${d}")
endforeach()

set(san_base
    "-fsanitize=${SANITIZE}"
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
    -g
    -O1)

# --- Generate C from the corpus --------------------------------------------
execute_process(
  COMMAND "${DSDLC}" --target-language c "${UAVCAN_ROOT}" --outdir "${c_out}"
  RESULT_VARIABLE gen_result
  OUTPUT_VARIABLE gen_stdout
  ERROR_VARIABLE gen_stderr
)
if(NOT gen_result EQUAL 0)
  message(STATUS "dsdlc c stdout:\n${gen_stdout}")
  message(STATUS "dsdlc c stderr:\n${gen_stderr}")
  message(FATAL_ERROR "failed to generate C output for native decoder fuzzing")
endif()

# --- Probe for libFuzzer (compiler-rt fuzzer runtime) ----------------------
set(probe_src "${build_out}/fuzzer_probe.c")
file(WRITE "${probe_src}"
     "#include <stdint.h>\n#include <stddef.h>\n"
     "int LLVMFuzzerTestOneInput(const uint8_t* d, size_t n){(void)d;(void)n;return 0;}\n")
execute_process(
  COMMAND "${C_COMPILER}" -fsanitize=fuzzer "${probe_src}" -o "${build_out}/fuzzer_probe"
  RESULT_VARIABLE probe_result
  OUTPUT_VARIABLE probe_stdout
  ERROR_VARIABLE probe_stderr
)
if(probe_result EQUAL 0)
  set(have_fuzzer TRUE)
  set(obj_flags ${san_base} -fsanitize=fuzzer-no-link)
  set(fuzz_link_flags "-fsanitize=fuzzer,${SANITIZE}")
  # Non-libFuzzer executables (seed emitter) still link the coverage-instrumented
  # objects, so they need the no-main fuzzer runtime to resolve those symbols.
  set(aux_link_flags "-fsanitize=fuzzer-no-link,${SANITIZE}")
  message(STATUS "native-decoder-fuzz: libFuzzer available; coverage-guided mode")
else()
  set(have_fuzzer FALSE)
  set(obj_flags ${san_base})
  set(aux_link_flags "-fsanitize=${SANITIZE}")
  message(STATUS "native-decoder-fuzz: libFuzzer NOT available "
                 "(-fsanitize=fuzzer probe failed); ASan/UBSan replay mode")
  message(STATUS "fuzzer probe stderr:\n${probe_stderr}")
endif()

# --- Compile the generated C decoders (shared object set) ------------------
file(GLOB_RECURSE generated_c_sources "${c_out}/*.c")
list(LENGTH generated_c_sources generated_c_count)
if(generated_c_count EQUAL 0)
  message(FATAL_ERROR "no generated C sources under ${c_out}")
endif()
message(STATUS "native-decoder-fuzz: compiling ${generated_c_count} generated C sources under ${SANITIZE}")

set(generated_objs "")
set(idx 0)
foreach(src IN LISTS generated_c_sources)
  math(EXPR idx "${idx} + 1")
  set(obj "${obj_dir}/generated_${idx}.o")
  execute_process(
    COMMAND "${C_COMPILER}" -std=c11 ${obj_flags} -I "${c_out}" -c "${src}" -o "${obj}"
    RESULT_VARIABLE cc_result
    OUTPUT_VARIABLE cc_stdout
    ERROR_VARIABLE cc_stderr
  )
  if(NOT cc_result EQUAL 0)
    message(STATUS "failed source: ${src}")
    message(STATUS "compile stdout:\n${cc_stdout}")
    message(STATUS "compile stderr:\n${cc_stderr}")
    message(FATAL_ERROR "failed to compile generated C decoder for fuzzing")
  endif()
  list(APPEND generated_objs "${obj}")
endforeach()

# Helper: compile the harness translation unit with an optional mode macro.
function(_ndf_compile_harness out_obj mode_define)
  set(mode_arg "")
  if(NOT "${mode_define}" STREQUAL "")
    set(mode_arg "-D${mode_define}")
  endif()
  execute_process(
    COMMAND "${C_COMPILER}" -std=c11 ${obj_flags} ${mode_arg}
            -I "${c_out}" -c "${harness_src}" -o "${out_obj}"
    RESULT_VARIABLE _r OUTPUT_VARIABLE _o ERROR_VARIABLE _e)
  if(NOT _r EQUAL 0)
    message(STATUS "harness compile (${mode_define}) stdout:\n${_o}")
    message(STATUS "harness compile (${mode_define}) stderr:\n${_e}")
    message(FATAL_ERROR "failed to compile fuzz harness (${mode_define})")
  endif()
endfunction()

# --- Seed corpus: emit one valid payload per curated type ------------------
set(seed_obj "${obj_dir}/harness_seeds.o")
_ndf_compile_harness("${seed_obj}" "NDF_MODE_SEEDS")
set(seed_exe "${build_out}/ndf_seed_emitter")
execute_process(
  COMMAND "${C_COMPILER}" "${seed_obj}" ${generated_objs} ${aux_link_flags} -o "${seed_exe}"
  RESULT_VARIABLE seed_link_result
  OUTPUT_VARIABLE seed_link_stdout
  ERROR_VARIABLE seed_link_stderr
)
if(NOT seed_link_result EQUAL 0)
  message(STATUS "seed emitter link stdout:\n${seed_link_stdout}")
  message(STATUS "seed emitter link stderr:\n${seed_link_stderr}")
  message(FATAL_ERROR "failed to link seed emitter")
endif()
execute_process(
  COMMAND "${seed_exe}" "${work_corpus}"
  RESULT_VARIABLE seed_run_result
  OUTPUT_VARIABLE seed_run_stdout
  ERROR_VARIABLE seed_run_stderr
)
if(NOT seed_run_result EQUAL 0)
  message(STATUS "seed emitter stdout:\n${seed_run_stdout}")
  message(STATUS "seed emitter stderr:\n${seed_run_stderr}")
  message(FATAL_ERROR "seed emitter failed (ASan/UBSan may have fired on a default object)")
endif()

# Fold in the committed regression corpus (previously-found crashers + hand seeds).
if(DEFINED CORPUS_DIR AND NOT "${CORPUS_DIR}" STREQUAL "" AND EXISTS "${CORPUS_DIR}")
  file(GLOB committed_seeds "${CORPUS_DIR}/*")
  foreach(seed IN LISTS committed_seeds)
    if(NOT IS_DIRECTORY "${seed}")
      file(COPY "${seed}" DESTINATION "${work_corpus}")
    endif()
  endforeach()
endif()

# --- Run: coverage-guided fuzzing, or ASan replay fallback -----------------
if(have_fuzzer)
  set(harness_obj "${obj_dir}/harness_fuzz.o")
  _ndf_compile_harness("${harness_obj}" "")
  set(fuzz_exe "${build_out}/ndf_fuzzer")
  execute_process(
    COMMAND "${C_COMPILER}" "${harness_obj}" ${generated_objs} ${fuzz_link_flags} -o "${fuzz_exe}"
    RESULT_VARIABLE link_result
    OUTPUT_VARIABLE link_stdout
    ERROR_VARIABLE link_stderr
  )
  if(NOT link_result EQUAL 0)
    message(STATUS "fuzzer link stdout:\n${link_stdout}")
    message(STATUS "fuzzer link stderr:\n${link_stderr}")
    message(FATAL_ERROR "failed to link libFuzzer executable")
  endif()

  execute_process(
    COMMAND "${fuzz_exe}"
            "-runs=${FUZZ_RUNS}"
            "-max_len=${MAX_LEN}"
            "-timeout=10"
            "-rss_limit_mb=4096"
            "-seed=1"
            "-artifact_prefix=${artifacts}/"
            "${work_corpus}"
    WORKING_DIRECTORY "${build_out}"
    RESULT_VARIABLE fuzz_result
    OUTPUT_VARIABLE fuzz_stdout
    ERROR_VARIABLE fuzz_stderr
  )
  message(STATUS "libFuzzer stdout:\n${fuzz_stdout}")
  if(NOT fuzz_result EQUAL 0)
    message(STATUS "libFuzzer stderr:\n${fuzz_stderr}")
    file(GLOB found_artifacts "${artifacts}/*")
    list(LENGTH found_artifacts artifact_count)
    message(FATAL_ERROR
      "native decoder fuzzing found a defect (exit=${fuzz_result}); "
      "${artifact_count} reproducer(s) saved under ${artifacts}. "
      "Add the reproducer to test/fuzz/corpus/native_decoder/ after fixing.")
  endif()
  message(STATUS "native-decoder-fuzz PASS (coverage-guided) runs=${FUZZ_RUNS} sanitize=${SANITIZE}")
else()
  set(replay_obj "${obj_dir}/harness_replay.o")
  _ndf_compile_harness("${replay_obj}" "NDF_MODE_REPLAY")
  set(replay_exe "${build_out}/ndf_replay")
  execute_process(
    COMMAND "${C_COMPILER}" "${replay_obj}" ${generated_objs} ${aux_link_flags} -o "${replay_exe}"
    RESULT_VARIABLE replay_link_result
    OUTPUT_VARIABLE replay_link_stdout
    ERROR_VARIABLE replay_link_stderr
  )
  if(NOT replay_link_result EQUAL 0)
    message(STATUS "replay link stdout:\n${replay_link_stdout}")
    message(STATUS "replay link stderr:\n${replay_link_stderr}")
    message(FATAL_ERROR "failed to link replay executable")
  endif()

  file(GLOB replay_inputs "${work_corpus}/*")
  list(LENGTH replay_inputs replay_count)
  if(replay_count EQUAL 0)
    message(FATAL_ERROR "no corpus inputs to replay")
  endif()
  execute_process(
    COMMAND "${replay_exe}" ${replay_inputs}
    RESULT_VARIABLE replay_result
    OUTPUT_VARIABLE replay_stdout
    ERROR_VARIABLE replay_stderr
  )
  message(STATUS "replay stdout:\n${replay_stdout}")
  if(NOT replay_result EQUAL 0)
    message(STATUS "replay stderr:\n${replay_stderr}")
    message(FATAL_ERROR
      "native decoder ASan/UBSan replay found a defect (exit=${replay_result})")
  endif()
  message(STATUS
    "native-decoder-fuzz PASS (ASan/UBSan replay, no libFuzzer) inputs=${replay_count} sanitize=${SANITIZE}")
endif()
