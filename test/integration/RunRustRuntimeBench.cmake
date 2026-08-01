cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC CARGO_EXECUTABLE RUST_BENCH_ROOT OUT_DIR)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

if(NOT EXISTS "${DSDLC}")
  message(FATAL_ERROR "dsdlc executable not found: ${DSDLC}")
endif()
if(NOT EXISTS "${CARGO_EXECUTABLE}")
  message(FATAL_ERROR "cargo executable not found: ${CARGO_EXECUTABLE}")
endif()
if(NOT EXISTS "${RUST_BENCH_ROOT}")
  message(FATAL_ERROR "Rust benchmark root not found: ${RUST_BENCH_ROOT}")
endif()

if(NOT DEFINED RUST_INLINE_THRESHOLD_BYTES OR
   "${RUST_INLINE_THRESHOLD_BYTES}" STREQUAL "")
  set(RUST_INLINE_THRESHOLD_BYTES 256)
endif()
if(NOT DEFINED BENCH_ITERATIONS_SMALL OR
   "${BENCH_ITERATIONS_SMALL}" STREQUAL "")
  set(BENCH_ITERATIONS_SMALL 120000)
endif()
if(NOT DEFINED BENCH_ITERATIONS_MEDIUM OR
   "${BENCH_ITERATIONS_MEDIUM}" STREQUAL "")
  set(BENCH_ITERATIONS_MEDIUM 45000)
endif()
if(NOT DEFINED BENCH_ITERATIONS_LARGE OR
   "${BENCH_ITERATIONS_LARGE}" STREQUAL "")
  set(BENCH_ITERATIONS_LARGE 10000)
endif()
if(NOT DEFINED BENCH_ENABLE_THRESHOLDS OR
   "${BENCH_ENABLE_THRESHOLDS}" STREQUAL "")
  set(BENCH_ENABLE_THRESHOLDS ON)
endif()
if(NOT DEFINED BENCH_THRESHOLDS_JSON OR
   "${BENCH_THRESHOLDS_JSON}" STREQUAL "")
  set(BENCH_THRESHOLDS_JSON "")
endif()
if(NOT DEFINED BENCH_REPORT_JSON OR "${BENCH_REPORT_JSON}" STREQUAL "")
  set(BENCH_REPORT_JSON "${OUT_DIR}/rust-runtime-bench.json")
endif()

foreach(numeric
    RUST_INLINE_THRESHOLD_BYTES
    BENCH_ITERATIONS_SMALL
    BENCH_ITERATIONS_MEDIUM
    BENCH_ITERATIONS_LARGE)
  if(NOT ${numeric} MATCHES "^[0-9]+$")
    message(FATAL_ERROR "Invalid numeric value for ${numeric}: ${${numeric}}")
  endif()
  if(${numeric} LESS 1)
    message(FATAL_ERROR "${numeric} must be >= 1")
  endif()
endforeach()

set(mode_list "max-inline;inline-then-pool")
set(family_list "small;medium;large")

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(summary_text "")
string(APPEND summary_text
  "Rust runtime benchmark (memory-mode comparison)\n"
  "root=${RUST_BENCH_ROOT}\n"
  "inline_threshold_bytes=${RUST_INLINE_THRESHOLD_BYTES}\n"
  "iterations_small=${BENCH_ITERATIONS_SMALL} iterations_medium=${BENCH_ITERATIONS_MEDIUM} iterations_large=${BENCH_ITERATIONS_LARGE}\n\n")

foreach(mode IN LISTS mode_list)
  string(REPLACE "-" "_" mode_var "${mode}")
  set(mode_out "${OUT_DIR}/${mode}")
  set(mode_report_json "${mode_out}/rust-runtime-bench-${mode}.json")
  set(cargo_target_dir "${mode_out}/cargo-target")

  file(MAKE_DIRECTORY "${mode_out}")

  execute_process(
    COMMAND
      "${DSDLC}" --target-language rust
        "${RUST_BENCH_ROOT}"
        --outdir "${mode_out}"
        --rust-crate-name "llvmdsdl_runtime_bench"
        --rust-profile "std"
        --rust-runtime-specialization "portable"
        --rust-memory-mode "${mode}"
        --rust-inline-threshold-bytes "${RUST_INLINE_THRESHOLD_BYTES}"
    RESULT_VARIABLE gen_result
    OUTPUT_VARIABLE gen_stdout
    ERROR_VARIABLE gen_stderr
  )
  if(NOT gen_result EQUAL 0)
    message(STATUS "dsdlc rust (${mode}) stdout:\n${gen_stdout}")
    message(STATUS "dsdlc rust (${mode}) stderr:\n${gen_stderr}")
    message(FATAL_ERROR "Rust runtime benchmark generation failed for mode=${mode}")
  endif()

  set(RUST_BENCH_MODE "${mode}")
  file(MAKE_DIRECTORY "${mode_out}/src/bin")
  configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/RustRuntimeBenchMain.rs.in"
    "${mode_out}/src/bin/runtime_bench.rs"
    @ONLY
  )

  execute_process(
    COMMAND
      "${CMAKE_COMMAND}" -E env
        "CARGO_TARGET_DIR=${cargo_target_dir}"
        "LLVMDSDL_RUST_BENCH_REPORT_JSON=${mode_report_json}"
        "${CARGO_EXECUTABLE}" run --quiet --release
          --manifest-path "${mode_out}/Cargo.toml"
          --bin runtime_bench
    RESULT_VARIABLE bench_result
    OUTPUT_VARIABLE bench_stdout
    ERROR_VARIABLE bench_stderr
  )
  file(WRITE "${mode_out}/rust-runtime-bench.txt" "${bench_stdout}\n${bench_stderr}\n")
  if(NOT bench_result EQUAL 0)
    message(STATUS "cargo run (${mode}) stdout:\n${bench_stdout}")
    message(STATUS "cargo run (${mode}) stderr:\n${bench_stderr}")
    message(FATAL_ERROR "Rust runtime benchmark execution failed for mode=${mode}")
  endif()
  if(NOT EXISTS "${mode_report_json}")
    message(FATAL_ERROR "Rust runtime benchmark report missing for mode=${mode}: ${mode_report_json}")
  endif()

  file(READ "${mode_report_json}" mode_report)
  string(JSON parsed_mode GET "${mode_report}" mode)
  if(NOT "${parsed_mode}" STREQUAL "${mode}")
    message(FATAL_ERROR "Rust benchmark report mode mismatch: expected=${mode}, got=${parsed_mode}")
  endif()
  string(JSON mode_total_elapsed GET "${mode_report}" totals elapsed_sec)
  set(metric_${mode_var}_total_elapsed "${mode_total_elapsed}")
  set(mode_json_${mode_var} "${mode_report}")

  string(APPEND summary_text "mode=${mode}\n")
  foreach(family IN LISTS family_list)
    string(JSON payload_len GET "${mode_report}" families "${family}" payload_len)
    string(JSON iterations GET "${mode_report}" families "${family}" iterations)
    string(JSON estimated_pool_alloc_calls GET "${mode_report}" families "${family}" estimated_pool_alloc_calls)
    string(JSON encode_elapsed GET "${mode_report}" families "${family}" encode elapsed_sec)
    string(JSON encode_mibps GET "${mode_report}" families "${family}" encode throughput_mib_per_sec)
    string(JSON decode_elapsed GET "${mode_report}" families "${family}" decode elapsed_sec)
    string(JSON decode_mibps GET "${mode_report}" families "${family}" decode throughput_mib_per_sec)

    set(metric_${mode_var}_${family}_encode_elapsed "${encode_elapsed}")
    set(metric_${mode_var}_${family}_decode_elapsed "${decode_elapsed}")

    string(APPEND summary_text
      "  family=${family} payload_len=${payload_len} iter=${iterations} "
      "encode_sec=${encode_elapsed} decode_sec=${decode_elapsed} "
      "encode_mibps=${encode_mibps} decode_mibps=${decode_mibps} "
      "estimated_pool_alloc_calls=${estimated_pool_alloc_calls}\n")
  endforeach()
  string(APPEND summary_text "  total_elapsed_sec=${mode_total_elapsed}\n\n")
endforeach()

set(
  recommendation
  "Keep max-inline as the default baseline for deterministic latency/memory behaviour. Use inline-then-pool when RAM pressure is high and pool provisioning is available.")
if(metric_inline_then_pool_total_elapsed LESS_EQUAL metric_max_inline_total_elapsed)
  set(
    recommendation
    "inline-then-pool matched or outperformed max-inline on this host for the benchmark corpus. Keep max-inline as the conservative default, and prefer inline-then-pool for RAM-constrained embedded targets with bounded pools.")
endif()

string(TIMESTAMP bench_timestamp "%Y-%m-%dT%H:%M:%SZ" UTC)
string(APPEND summary_text
  "recommendation=${recommendation}\n"
  "embedded_profiles:\n"
  "  - safety_critical: std/no-std with max-inline and deterministic pre-allocation.\n"
  "  - balanced: no-std inline-then-pool with threshold=${RUST_INLINE_THRESHOLD_BYTES} and measured pool budgets.\n"
  "  - throughput_host: std max-inline for minimum decode overhead unless inline-then-pool benchmark remains comparable.\n")

set(report_json "{\n")
string(APPEND report_json "  \"schema_version\": 1,\n")
string(APPEND report_json "  \"created_utc\": \"${bench_timestamp}\",\n")
string(APPEND report_json "  \"root_namespace_dir\": \"${RUST_BENCH_ROOT}\",\n")
string(APPEND report_json "  \"inline_threshold_bytes\": ${RUST_INLINE_THRESHOLD_BYTES},\n")
string(APPEND report_json "  \"iterations\": {\n")
string(APPEND report_json "    \"small\": ${BENCH_ITERATIONS_SMALL},\n")
string(APPEND report_json "    \"medium\": ${BENCH_ITERATIONS_MEDIUM},\n")
string(APPEND report_json "    \"large\": ${BENCH_ITERATIONS_LARGE}\n")
string(APPEND report_json "  },\n")
string(APPEND report_json "  \"modes\": {\n")
string(APPEND report_json "    \"max-inline\": ${mode_json_max_inline},\n")
string(APPEND report_json "    \"inline-then-pool\": ${mode_json_inline_then_pool}\n")
string(APPEND report_json "  }\n")
string(APPEND report_json "}\n")
file(WRITE "${BENCH_REPORT_JSON}" "${report_json}")

set(threshold_failures "")
if(BENCH_ENABLE_THRESHOLDS)
  if("${BENCH_THRESHOLDS_JSON}" STREQUAL "" OR
     NOT EXISTS "${BENCH_THRESHOLDS_JSON}")
    message(FATAL_ERROR
      "Rust runtime benchmark thresholds requested but BENCH_THRESHOLDS_JSON is missing: ${BENCH_THRESHOLDS_JSON}")
  endif()
  file(READ "${BENCH_THRESHOLDS_JSON}" thresholds_json)
  foreach(mode IN LISTS mode_list)
    string(REPLACE "-" "_" mode_var "${mode}")
    foreach(family IN LISTS family_list)
      foreach(operation IN ITEMS encode decode)
        set(limit "")
        string(
          JSON limit
          ERROR_VARIABLE limit_error
          GET "${thresholds_json}" max_elapsed_sec "${mode}" "${family}" "${operation}")
        if(limit_error)
          continue()
        endif()
        if(limit GREATER 0)
          set(actual "${metric_${mode_var}_${family}_${operation}_elapsed}")
          if(actual GREATER limit)
            string(APPEND threshold_failures
              "  ${mode}/${family}/${operation}: actual=${actual}s limit=${limit}s\n")
          endif()
        endif()
      endforeach()
    endforeach()
  endforeach()

  if(threshold_failures)
    file(WRITE "${OUT_DIR}/rust-runtime-bench-threshold-failures.txt" "${threshold_failures}")
    message(STATUS "Rust runtime benchmark threshold failures:\n${threshold_failures}")
    message(FATAL_ERROR
      "Rust runtime benchmark exceeded configured thresholds (see ${OUT_DIR}/rust-runtime-bench-threshold-failures.txt)")
  endif()
endif()

file(WRITE "${OUT_DIR}/rust-runtime-bench.txt" "${summary_text}")
message(STATUS "Rust runtime benchmark completed")
message(STATUS "${summary_text}")
message(STATUS "Rust runtime benchmark report: ${BENCH_REPORT_JSON}")
