#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#

find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
  message(STATUS
    "Skipping benchmark target setup: Python3 interpreter not available.")
  return()
endif()

# The corpus is generated into the build tree rather than committed. It is a few
# thousand definitions that follow from three counts, and the generator beside it
# is the readable form of the workload.
set(LLVMDSDL_COMPLEX_BENCHMARK_GENERATOR
    "${CMAKE_CURRENT_SOURCE_DIR}/complex/generate_complex_dsdl.py")
set(LLVMDSDL_COMPLEX_BENCHMARK_CORPUS_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/complex")
set(LLVMDSDL_COMPLEX_BENCHMARK_ROOT
    "${LLVMDSDL_COMPLEX_BENCHMARK_CORPUS_DIR}/civildrone")
set(LLVMDSDL_COMPLEX_BENCHMARK_CORPUS_STAMP
    "${LLVMDSDL_COMPLEX_BENCHMARK_CORPUS_DIR}/corpus.stamp")
set(LLVMDSDL_COMPLEX_BENCHMARK_PRDT_UAVCAN_LOOKUP_ROOT
    "${LLVMDSDL_SOURCE_DIR}/submodules/public_regulated_data_types/uavcan")
set(LLVMDSDL_COMPLEX_BENCHMARK_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmark_codegen.py")
set(LLVMDSDL_LSP_BENCHMARK_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmark_lsp.py")
set(LLVMDSDL_COMPLEX_BENCHMARK_THRESHOLDS
    "${CMAKE_CURRENT_SOURCE_DIR}/complex_codegen_thresholds.json")

set(LLVMDSDL_COMPLEX_BENCHMARK_OUT_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/complex-codegen")
set(LLVMDSDL_COMPLEX_BENCHMARK_GENERATED_DIR
    "${LLVMDSDL_COMPLEX_BENCHMARK_OUT_DIR}/generated")
set(LLVMDSDL_COMPLEX_BENCHMARK_REPORT_RECORD
    "${LLVMDSDL_COMPLEX_BENCHMARK_OUT_DIR}/record-report.json")
set(LLVMDSDL_COMPLEX_BENCHMARK_REPORT_CHECK_CI
    "${LLVMDSDL_COMPLEX_BENCHMARK_OUT_DIR}/check-ci-oom-report.json")
set(LLVMDSDL_COMPLEX_BENCHMARK_REPORT_CHECK_DEV
    "${LLVMDSDL_COMPLEX_BENCHMARK_OUT_DIR}/check-dev-ab-report.json")
set(LLVMDSDL_COMPLEX_BENCHMARK_REPORT_EMBEDC_COMPARE
    "${LLVMDSDL_COMPLEX_BENCHMARK_OUT_DIR}/embedc-compare-report.json")
set(LLVMDSDL_COMPLEX_BENCHMARK_THRESHOLDS_GENERATED
    "${LLVMDSDL_COMPLEX_BENCHMARK_OUT_DIR}/complex_codegen_thresholds.generated.json")
set(LLVMDSDL_LSP_BENCHMARK_OUT_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/lsp-benchmark")
set(LLVMDSDL_LSP_BENCHMARK_INDEX_CACHE_DIR
    "${LLVMDSDL_LSP_BENCHMARK_OUT_DIR}/index-cache")
set(LLVMDSDL_LSP_BENCHMARK_REPLAY_REPORT
    "${LLVMDSDL_LSP_BENCHMARK_OUT_DIR}/replay-report.json")
set(LLVMDSDL_LSP_BENCHMARK_INDEX_REPORT
    "${LLVMDSDL_LSP_BENCHMARK_OUT_DIR}/index-cold-warm-report.json")

set(LLVMDSDL_COMPLEX_BENCHMARK_LOOKUP_ARGS)
if(EXISTS "${LLVMDSDL_COMPLEX_BENCHMARK_PRDT_UAVCAN_LOOKUP_ROOT}")
  list(APPEND LLVMDSDL_COMPLEX_BENCHMARK_LOOKUP_ARGS
       --lookup-dir "${LLVMDSDL_COMPLEX_BENCHMARK_PRDT_UAVCAN_LOOKUP_ROOT}")
else()
  message(WARNING
    "Benchmark uavcan lookup root missing at "
    "${LLVMDSDL_COMPLEX_BENCHMARK_PRDT_UAVCAN_LOOKUP_ROOT}; "
    "uavcan namespace resolution may fail if not present under benchmark root.")
endif()

set(LLVMDSDL_CODEGEN_BENCHMARK_LANGUAGES
    "all"
    CACHE STRING
    "Comma-separated benchmark language keys (default: all).")
set(LLVMDSDL_CODEGEN_BENCHMARK_ITERATIONS
    1
    CACHE STRING
    "Iterations per language for benchmark runs.")
set(LLVMDSDL_CODEGEN_BENCHMARK_PER_LANGUAGE_TIMEOUT_SEC
    0
    CACHE STRING
    "Per-language benchmark timeout in seconds (0 disables timeout).")
set(LLVMDSDL_CODEGEN_BENCHMARK_STATUS_INTERVAL_SEC
    15
    CACHE STRING
    "Heartbeat status print interval while each language benchmark runs (0 disables).")
set(LLVMDSDL_CODEGEN_BENCHMARK_MAX_RSS_MIB
    0
    CACHE STRING
    "Maximum allowed per-language peak RSS in MiB for codegen benchmark (0 disables).")
set(LLVMDSDL_LSP_BENCHMARK_OPEN_FILE_COUNT
    64
    CACHE STRING
    "Maximum number of DSDL files opened by LSP benchmark sessions.")
set(LLVMDSDL_LSP_BENCHMARK_REPLAY_ITERATIONS
    40
    CACHE STRING
    "Replay iterations for mixed-method LSP benchmark.")
set(LLVMDSDL_LSP_BENCHMARK_SYMBOL_ITERATIONS
    80
    CACHE STRING
    "workspace/symbol iterations per phase for cold/warm index benchmark.")
set(LLVMDSDL_LSP_BENCHMARK_REQUEST_TIMEOUT_SEC
    180
    CACHE STRING
    "Per-request timeout (seconds) for LSP benchmark requests.")
set(LLVMDSDL_LSP_BENCHMARK_WORKSPACE_QUERY
    "Vision"
    CACHE STRING
    "workspace/symbol query used by LSP benchmark harness.")

option(LLVMDSDL_ENABLE_BENCHMARK_TESTS
       "Enable benchmark throughput CTest checks (timing thresholds)"
       OFF)

# Every benchmark target reaches the corpus through this rule, so the definitions
# are written before anything measures how long they take to compile.
add_custom_command(
  OUTPUT "${LLVMDSDL_COMPLEX_BENCHMARK_CORPUS_STAMP}"
  COMMAND
    ${Python3_EXECUTABLE}
      "${LLVMDSDL_COMPLEX_BENCHMARK_GENERATOR}"
      --outdir "${LLVMDSDL_COMPLEX_BENCHMARK_CORPUS_DIR}"
      --stamp "${LLVMDSDL_COMPLEX_BENCHMARK_CORPUS_STAMP}"
  DEPENDS "${LLVMDSDL_COMPLEX_BENCHMARK_GENERATOR}"
  VERBATIM
  COMMENT "Writing the complex benchmark corpus"
)

# The benchmark tests take the corpus as a path rather than building it, so with
# those enabled it belongs in the default build. Left out of ALL otherwise: a
# build that is not benchmarking has no use for three thousand definitions, and
# `--target benchmark-corpus` is there for a run by hand.
if(LLVMDSDL_ENABLE_BENCHMARK_TESTS)
  add_custom_target(benchmark-corpus ALL
    DEPENDS "${LLVMDSDL_COMPLEX_BENCHMARK_CORPUS_STAMP}")
else()
  add_custom_target(benchmark-corpus
    DEPENDS "${LLVMDSDL_COMPLEX_BENCHMARK_CORPUS_STAMP}")
endif()

add_custom_target(benchmark-codegen-record
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${LLVMDSDL_COMPLEX_BENCHMARK_OUT_DIR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${LLVMDSDL_COMPLEX_BENCHMARK_GENERATED_DIR}"
  COMMAND
    ${Python3_EXECUTABLE}
      ${LLVMDSDL_COMPLEX_BENCHMARK_SCRIPT}
      record
      --dsdlc $<TARGET_FILE:dsdlc>
      --root-namespace-dir ${LLVMDSDL_COMPLEX_BENCHMARK_ROOT}
      ${LLVMDSDL_COMPLEX_BENCHMARK_LOOKUP_ARGS}
      --out-base-dir ${LLVMDSDL_COMPLEX_BENCHMARK_GENERATED_DIR}
      --report-json ${LLVMDSDL_COMPLEX_BENCHMARK_REPORT_RECORD}
      --iterations ${LLVMDSDL_CODEGEN_BENCHMARK_ITERATIONS}
      --languages ${LLVMDSDL_CODEGEN_BENCHMARK_LANGUAGES}
      --per-language-timeout-sec ${LLVMDSDL_CODEGEN_BENCHMARK_PER_LANGUAGE_TIMEOUT_SEC}
      --status-interval-sec ${LLVMDSDL_CODEGEN_BENCHMARK_STATUS_INTERVAL_SEC}
      --max-rss-mib ${LLVMDSDL_CODEGEN_BENCHMARK_MAX_RSS_MIB}
  DEPENDS dsdlc benchmark-corpus
  USES_TERMINAL
  VERBATIM
  COMMENT "Recording complex multi-language codegen benchmark timings"
)

if(LLVMDSDL_COMPLEX_BENCHMARK_LOOKUP_ARGS)
  add_custom_target(benchmark-codegen-embedc-compare
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${LLVMDSDL_COMPLEX_BENCHMARK_OUT_DIR}/embedc-compare-generated"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LLVMDSDL_COMPLEX_BENCHMARK_OUT_DIR}/embedc-compare-generated"
    COMMAND
      ${Python3_EXECUTABLE}
        ${LLVMDSDL_COMPLEX_BENCHMARK_SCRIPT}
        record
        --dsdlc $<TARGET_FILE:dsdlc>
        --root-namespace-dir ${LLVMDSDL_COMPLEX_BENCHMARK_ROOT}
        ${LLVMDSDL_COMPLEX_BENCHMARK_LOOKUP_ARGS}
        --out-base-dir ${LLVMDSDL_COMPLEX_BENCHMARK_OUT_DIR}/embedc-compare-generated
        --report-json ${LLVMDSDL_COMPLEX_BENCHMARK_REPORT_EMBEDC_COMPARE}
        --iterations ${LLVMDSDL_CODEGEN_BENCHMARK_ITERATIONS}
        --languages embedC,noEmbedC
        --per-language-timeout-sec ${LLVMDSDL_CODEGEN_BENCHMARK_PER_LANGUAGE_TIMEOUT_SEC}
        --status-interval-sec ${LLVMDSDL_CODEGEN_BENCHMARK_STATUS_INTERVAL_SEC}
        --max-rss-mib ${LLVMDSDL_CODEGEN_BENCHMARK_MAX_RSS_MIB}
    DEPENDS dsdlc benchmark-corpus
    USES_TERMINAL
    VERBATIM
    COMMENT "Recording C benchmark A/B with embedded UAVCAN catalog vs --no-embedded-uavcan"
  )
else()
  message(STATUS
    "Skipping benchmark-codegen-embedc-compare target: no uavcan lookup root "
    "available for noEmbedC lane.")
endif()

add_custom_target(benchmark-codegen-init-thresholds
  COMMAND
    ${Python3_EXECUTABLE}
      ${LLVMDSDL_COMPLEX_BENCHMARK_SCRIPT}
      init-thresholds
      --report-json ${LLVMDSDL_COMPLEX_BENCHMARK_REPORT_RECORD}
      --thresholds-json ${LLVMDSDL_COMPLEX_BENCHMARK_THRESHOLDS_GENERATED}
      --dev-relative-multiplier 1.35
      --dev-absolute-margin-sec 2.0
      --ci-relative-multiplier 10.0
      --ci-absolute-margin-sec 30.0
  DEPENDS benchmark-codegen-record
  USES_TERMINAL
  VERBATIM
  COMMENT "Generating benchmark threshold template from recorded timings"
)

if(EXISTS "${LLVMDSDL_COMPLEX_BENCHMARK_THRESHOLDS}")
  add_custom_target(benchmark-codegen-check-ci-oom
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${LLVMDSDL_COMPLEX_BENCHMARK_GENERATED_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LLVMDSDL_COMPLEX_BENCHMARK_GENERATED_DIR}"
    COMMAND
      ${Python3_EXECUTABLE}
        ${LLVMDSDL_COMPLEX_BENCHMARK_SCRIPT}
        check
        --dsdlc $<TARGET_FILE:dsdlc>
        --root-namespace-dir ${LLVMDSDL_COMPLEX_BENCHMARK_ROOT}
        ${LLVMDSDL_COMPLEX_BENCHMARK_LOOKUP_ARGS}
        --out-base-dir ${LLVMDSDL_COMPLEX_BENCHMARK_GENERATED_DIR}
        --report-json ${LLVMDSDL_COMPLEX_BENCHMARK_REPORT_CHECK_CI}
        --thresholds-json ${LLVMDSDL_COMPLEX_BENCHMARK_THRESHOLDS}
        --profile ci_oom
        --iterations ${LLVMDSDL_CODEGEN_BENCHMARK_ITERATIONS}
        --languages ${LLVMDSDL_CODEGEN_BENCHMARK_LANGUAGES}
        --per-language-timeout-sec ${LLVMDSDL_CODEGEN_BENCHMARK_PER_LANGUAGE_TIMEOUT_SEC}
        --status-interval-sec ${LLVMDSDL_CODEGEN_BENCHMARK_STATUS_INTERVAL_SEC}
        --max-rss-mib ${LLVMDSDL_CODEGEN_BENCHMARK_MAX_RSS_MIB}
    DEPENDS dsdlc benchmark-corpus
    USES_TERMINAL
    VERBATIM
    COMMENT "Checking complex benchmark timings against ci_oom threshold profile"
  )

  add_custom_target(benchmark-codegen-check-dev-ab
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${LLVMDSDL_COMPLEX_BENCHMARK_GENERATED_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LLVMDSDL_COMPLEX_BENCHMARK_GENERATED_DIR}"
    COMMAND
      ${Python3_EXECUTABLE}
        ${LLVMDSDL_COMPLEX_BENCHMARK_SCRIPT}
        check
        --dsdlc $<TARGET_FILE:dsdlc>
        --root-namespace-dir ${LLVMDSDL_COMPLEX_BENCHMARK_ROOT}
        ${LLVMDSDL_COMPLEX_BENCHMARK_LOOKUP_ARGS}
        --out-base-dir ${LLVMDSDL_COMPLEX_BENCHMARK_GENERATED_DIR}
        --report-json ${LLVMDSDL_COMPLEX_BENCHMARK_REPORT_CHECK_DEV}
        --thresholds-json ${LLVMDSDL_COMPLEX_BENCHMARK_THRESHOLDS}
        --profile dev_ab
        --iterations ${LLVMDSDL_CODEGEN_BENCHMARK_ITERATIONS}
        --languages ${LLVMDSDL_CODEGEN_BENCHMARK_LANGUAGES}
        --per-language-timeout-sec ${LLVMDSDL_CODEGEN_BENCHMARK_PER_LANGUAGE_TIMEOUT_SEC}
        --status-interval-sec ${LLVMDSDL_CODEGEN_BENCHMARK_STATUS_INTERVAL_SEC}
        --max-rss-mib ${LLVMDSDL_CODEGEN_BENCHMARK_MAX_RSS_MIB}
    DEPENDS dsdlc benchmark-corpus
    USES_TERMINAL
    VERBATIM
    COMMENT "Checking complex benchmark timings against dev_ab threshold profile"
  )

  if(LLVMDSDL_ENABLE_BENCHMARK_TESTS)
    add_test(
      NAME llvmdsdl-codegen-benchmark-ci-oom
      COMMAND
        ${Python3_EXECUTABLE}
          ${LLVMDSDL_COMPLEX_BENCHMARK_SCRIPT}
          check
          --dsdlc $<TARGET_FILE:dsdlc>
          --root-namespace-dir ${LLVMDSDL_COMPLEX_BENCHMARK_ROOT}
          ${LLVMDSDL_COMPLEX_BENCHMARK_LOOKUP_ARGS}
          --out-base-dir ${LLVMDSDL_COMPLEX_BENCHMARK_GENERATED_DIR}/ctest-ci-oom
          --report-json ${LLVMDSDL_COMPLEX_BENCHMARK_OUT_DIR}/ctest-check-ci-oom-report.json
          --thresholds-json ${LLVMDSDL_COMPLEX_BENCHMARK_THRESHOLDS}
          --profile ci_oom
          --iterations ${LLVMDSDL_CODEGEN_BENCHMARK_ITERATIONS}
          --languages ${LLVMDSDL_CODEGEN_BENCHMARK_LANGUAGES}
          --per-language-timeout-sec ${LLVMDSDL_CODEGEN_BENCHMARK_PER_LANGUAGE_TIMEOUT_SEC}
          --status-interval-sec ${LLVMDSDL_CODEGEN_BENCHMARK_STATUS_INTERVAL_SEC}
          --max-rss-mib ${LLVMDSDL_CODEGEN_BENCHMARK_MAX_RSS_MIB}
    )
    set_tests_properties(
      llvmdsdl-codegen-benchmark-ci-oom
      PROPERTIES
        LABELS "benchmark;performance;integration;slow"
    )

    foreach(lang IN ITEMS c cpp rust go ts python)
      add_test(
        NAME llvmdsdl-codegen-benchmark-ci-oom-${lang}
        COMMAND
          ${Python3_EXECUTABLE}
            ${LLVMDSDL_COMPLEX_BENCHMARK_SCRIPT}
            check
            --dsdlc $<TARGET_FILE:dsdlc>
            --root-namespace-dir ${LLVMDSDL_COMPLEX_BENCHMARK_ROOT}
            ${LLVMDSDL_COMPLEX_BENCHMARK_LOOKUP_ARGS}
            --out-base-dir ${LLVMDSDL_COMPLEX_BENCHMARK_GENERATED_DIR}/ctest-ci-oom-${lang}
            --report-json ${LLVMDSDL_COMPLEX_BENCHMARK_OUT_DIR}/ctest-check-ci-oom-${lang}-report.json
            --thresholds-json ${LLVMDSDL_COMPLEX_BENCHMARK_THRESHOLDS}
            --profile ci_oom
            --iterations ${LLVMDSDL_CODEGEN_BENCHMARK_ITERATIONS}
            --languages ${lang}
            --per-language-timeout-sec ${LLVMDSDL_CODEGEN_BENCHMARK_PER_LANGUAGE_TIMEOUT_SEC}
            --status-interval-sec ${LLVMDSDL_CODEGEN_BENCHMARK_STATUS_INTERVAL_SEC}
            --max-rss-mib ${LLVMDSDL_CODEGEN_BENCHMARK_MAX_RSS_MIB}
      )
      set_tests_properties(
        llvmdsdl-codegen-benchmark-ci-oom-${lang}
        PROPERTIES
          LABELS "benchmark;performance;integration;slow;codegen;codegen-${lang}"
      )
    endforeach()
  endif()
else()
  message(WARNING
    "Benchmark thresholds file is missing at "
    "${LLVMDSDL_COMPLEX_BENCHMARK_THRESHOLDS}. "
    "benchmark-codegen-check-* targets will not be created.")
endif()

add_custom_target(benchmark-lsp-replay
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${LLVMDSDL_LSP_BENCHMARK_OUT_DIR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${LLVMDSDL_LSP_BENCHMARK_OUT_DIR}"
  COMMAND
    ${Python3_EXECUTABLE}
      ${LLVMDSDL_LSP_BENCHMARK_SCRIPT}
      replay
      --dsdld $<TARGET_FILE:dsdld>
      --root-namespace-dir ${LLVMDSDL_COMPLEX_BENCHMARK_ROOT}
      ${LLVMDSDL_COMPLEX_BENCHMARK_LOOKUP_ARGS}
      --index-cache-dir ${LLVMDSDL_LSP_BENCHMARK_INDEX_CACHE_DIR}
      --report-json ${LLVMDSDL_LSP_BENCHMARK_REPLAY_REPORT}
      --open-file-count ${LLVMDSDL_LSP_BENCHMARK_OPEN_FILE_COUNT}
      --request-timeout-sec ${LLVMDSDL_LSP_BENCHMARK_REQUEST_TIMEOUT_SEC}
      --request-iterations ${LLVMDSDL_LSP_BENCHMARK_REPLAY_ITERATIONS}
      --workspace-symbol-query ${LLVMDSDL_LSP_BENCHMARK_WORKSPACE_QUERY}
  DEPENDS dsdld benchmark-corpus
  USES_TERMINAL
  VERBATIM
  COMMENT "Running LSP replay latency benchmark (p50/p95/p99 report)"
)

add_custom_target(benchmark-lsp-index-cold-warm
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${LLVMDSDL_LSP_BENCHMARK_OUT_DIR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${LLVMDSDL_LSP_BENCHMARK_OUT_DIR}"
  COMMAND
    ${Python3_EXECUTABLE}
      ${LLVMDSDL_LSP_BENCHMARK_SCRIPT}
      index-bench
      --dsdld $<TARGET_FILE:dsdld>
      --root-namespace-dir ${LLVMDSDL_COMPLEX_BENCHMARK_ROOT}
      ${LLVMDSDL_COMPLEX_BENCHMARK_LOOKUP_ARGS}
      --index-cache-dir ${LLVMDSDL_LSP_BENCHMARK_INDEX_CACHE_DIR}
      --report-json ${LLVMDSDL_LSP_BENCHMARK_INDEX_REPORT}
      --open-file-count ${LLVMDSDL_LSP_BENCHMARK_OPEN_FILE_COUNT}
      --request-timeout-sec ${LLVMDSDL_LSP_BENCHMARK_REQUEST_TIMEOUT_SEC}
      --symbol-iterations ${LLVMDSDL_LSP_BENCHMARK_SYMBOL_ITERATIONS}
      --workspace-symbol-query ${LLVMDSDL_LSP_BENCHMARK_WORKSPACE_QUERY}
  DEPENDS dsdld benchmark-corpus
  USES_TERMINAL
  VERBATIM
  COMMENT "Running LSP workspace index cold/warm benchmark"
)

if(LLVMDSDL_ENABLE_BENCHMARK_TESTS)
  add_test(
    NAME llvmdsdl-lsp-replay-benchmark
    COMMAND
      ${Python3_EXECUTABLE}
        ${LLVMDSDL_LSP_BENCHMARK_SCRIPT}
        replay
        --dsdld $<TARGET_FILE:dsdld>
        --root-namespace-dir ${LLVMDSDL_COMPLEX_BENCHMARK_ROOT}
        ${LLVMDSDL_COMPLEX_BENCHMARK_LOOKUP_ARGS}
        --index-cache-dir ${LLVMDSDL_LSP_BENCHMARK_OUT_DIR}/ctest-replay-index-cache
        --report-json ${LLVMDSDL_LSP_BENCHMARK_OUT_DIR}/ctest-replay-report.json
        --open-file-count ${LLVMDSDL_LSP_BENCHMARK_OPEN_FILE_COUNT}
        --request-timeout-sec ${LLVMDSDL_LSP_BENCHMARK_REQUEST_TIMEOUT_SEC}
        --request-iterations ${LLVMDSDL_LSP_BENCHMARK_REPLAY_ITERATIONS}
        --workspace-symbol-query ${LLVMDSDL_LSP_BENCHMARK_WORKSPACE_QUERY}
  )
  set_tests_properties(
    llvmdsdl-lsp-replay-benchmark
    PROPERTIES
      LABELS "benchmark;performance;lsp;integration;slow"
  )

  add_test(
    NAME llvmdsdl-lsp-index-cold-warm-benchmark
    COMMAND
      ${Python3_EXECUTABLE}
        ${LLVMDSDL_LSP_BENCHMARK_SCRIPT}
        index-bench
        --dsdld $<TARGET_FILE:dsdld>
        --root-namespace-dir ${LLVMDSDL_COMPLEX_BENCHMARK_ROOT}
        ${LLVMDSDL_COMPLEX_BENCHMARK_LOOKUP_ARGS}
        --index-cache-dir ${LLVMDSDL_LSP_BENCHMARK_OUT_DIR}/ctest-index-cache
        --report-json ${LLVMDSDL_LSP_BENCHMARK_OUT_DIR}/ctest-index-report.json
        --open-file-count ${LLVMDSDL_LSP_BENCHMARK_OPEN_FILE_COUNT}
        --request-timeout-sec ${LLVMDSDL_LSP_BENCHMARK_REQUEST_TIMEOUT_SEC}
        --symbol-iterations ${LLVMDSDL_LSP_BENCHMARK_SYMBOL_ITERATIONS}
        --workspace-symbol-query ${LLVMDSDL_LSP_BENCHMARK_WORKSPACE_QUERY}
  )
  set_tests_properties(
    llvmdsdl-lsp-index-cold-warm-benchmark
    PROPERTIES
      LABELS "benchmark;performance;lsp;integration;slow"
  )
endif()
