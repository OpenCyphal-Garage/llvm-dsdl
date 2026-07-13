set(LLVMDSDL_UAVCAN_ROOT
    "${LLVMDSDL_SOURCE_DIR}/submodules/public_regulated_data_types/uavcan")

set(LLVMDSDL_FIXTURES_ROOT
    "${LLVMDSDL_SOURCE_DIR}/test/lit/fixtures")
find_program(PYTHON_EXECUTABLE python3 QUIET)

set(LLVMDSDL_PY_RUNTIME_BENCH_SPECIALIZATIONS
    "portable;fast"
    CACHE STRING
    "Python runtime benchmark specializations to generate (semicolon-separated).")
set(LLVMDSDL_PY_RUNTIME_BENCH_ITERATIONS_SMALL
    "2000"
    CACHE STRING
    "Python runtime benchmark iterations for the small payload family.")
set(LLVMDSDL_PY_RUNTIME_BENCH_ITERATIONS_MEDIUM
    "750"
    CACHE STRING
    "Python runtime benchmark iterations for the medium payload family.")
set(LLVMDSDL_PY_RUNTIME_BENCH_ITERATIONS_LARGE
    "250"
    CACHE STRING
    "Python runtime benchmark iterations for the large payload family.")
set(LLVMDSDL_PY_RUNTIME_BENCH_ENABLE_THRESHOLDS
    ON
    CACHE BOOL
    "Enable pass/fail threshold checks for the Python runtime benchmark integration test.")
set(LLVMDSDL_PY_RUNTIME_BENCH_THRESHOLDS_JSON
    "${LLVMDSDL_SOURCE_DIR}/test/integration/python_runtime_bench_thresholds.json"
    CACHE FILEPATH
    "Threshold config used when LLVMDSDL_PY_RUNTIME_BENCH_ENABLE_THRESHOLDS is ON.")
set(LLVMDSDL_RUST_RUNTIME_BENCH_INLINE_THRESHOLD_BYTES
    "256"
    CACHE STRING
    "Inline threshold in bytes used by Rust runtime memory-mode benchmark.")
set(LLVMDSDL_RUST_RUNTIME_BENCH_ITERATIONS_SMALL
    "120000"
    CACHE STRING
    "Rust runtime benchmark iterations for the small payload family.")
set(LLVMDSDL_RUST_RUNTIME_BENCH_ITERATIONS_MEDIUM
    "45000"
    CACHE STRING
    "Rust runtime benchmark iterations for the medium payload family.")
set(LLVMDSDL_RUST_RUNTIME_BENCH_ITERATIONS_LARGE
    "10000"
    CACHE STRING
    "Rust runtime benchmark iterations for the large payload family.")
set(LLVMDSDL_RUST_RUNTIME_BENCH_ENABLE_THRESHOLDS
    ON
    CACHE BOOL
    "Enable pass/fail threshold checks for the Rust runtime benchmark integration test.")
set(LLVMDSDL_RUST_RUNTIME_BENCH_THRESHOLDS_JSON
    "${LLVMDSDL_SOURCE_DIR}/test/integration/rust_runtime_bench_thresholds.json"
    CACHE FILEPATH
    "Threshold config used when LLVMDSDL_RUST_RUNTIME_BENCH_ENABLE_THRESHOLDS is ON.")
string(REPLACE ";"
               "\\;"
               LLVMDSDL_PY_RUNTIME_BENCH_SPECIALIZATIONS_ARG
               "${LLVMDSDL_PY_RUNTIME_BENCH_SPECIALIZATIONS}")

if(PYTHON_EXECUTABLE)
  add_test(
    NAME llvmdsdl-convergence-scorecard
    COMMAND
      ${CMAKE_COMMAND}
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -DREPO_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DOUTPUT_JSON=${CMAKE_CURRENT_BINARY_DIR}/convergence-scorecard-out/report.json
        -DOUTPUT_MD=${CMAKE_CURRENT_BINARY_DIR}/convergence-scorecard-out/CONVERGENCE_SCORECARD.md
        -DBASELINE_JSON=${LLVMDSDL_SOURCE_DIR}/tools/convergence/convergence_baseline.json
        -DCONVERGENCE_SCRIPT=${LLVMDSDL_SOURCE_DIR}/tools/convergence/convergence_report.py
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/RunConvergenceReport.cmake
  )

  set_tests_properties(
    llvmdsdl-convergence-scorecard
    PROPERTIES
      LABELS "integration;convergence;release-blocking"
  )

  add_test(
    NAME llvmdsdl-convergence-scorecard-selftest
    COMMAND
      ${CMAKE_COMMAND}
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -DREPO_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/RunConvergenceReportSelftest.cmake
  )

  set_tests_properties(
    llvmdsdl-convergence-scorecard-selftest
    PROPERTIES
      LABELS "integration;convergence"
  )

  add_test(
    NAME llvmdsdl-convergence-matrix-reports-selftest
    COMMAND
      ${CMAKE_COMMAND}
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -DREPO_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DCTEST_TEST_DIR=${CMAKE_BINARY_DIR}
        -DCTEST_CONFIG=$<CONFIG>
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/RunConvergenceMatrixReportsSelftest.cmake
  )

  set_tests_properties(
    llvmdsdl-convergence-matrix-reports-selftest
    PROPERTIES
      LABELS "integration;convergence"
  )

  add_test(
    NAME llvmdsdl-runtime-semantic-wrapper-allowlist
    COMMAND
      ${CMAKE_COMMAND}
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -DREPO_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/RunRuntimeSemanticWrapperAllowlist.cmake
  )

  set_tests_properties(
    llvmdsdl-runtime-semantic-wrapper-allowlist
    PROPERTIES
      LABELS "integration;runtime;release-blocking"
  )

  add_test(
    NAME llvmdsdl-runtime-semantic-wrapper-allowlist-selftest
    COMMAND
      ${CMAKE_COMMAND}
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -DREPO_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/RunRuntimeSemanticWrapperAllowlistSelftest.cmake
  )

  set_tests_properties(
    llvmdsdl-runtime-semantic-wrapper-allowlist-selftest
    PROPERTIES
      LABELS "integration;runtime"
  )

  add_test(
    NAME llvmdsdl-execution-engine-boundary-guard
    COMMAND
      ${CMAKE_COMMAND}
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -DREPO_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/RunExecutionEngineBoundaryGuard.cmake
  )

  set_tests_properties(
    llvmdsdl-execution-engine-boundary-guard
    PROPERTIES
      LABELS "integration;convergence;release-blocking"
  )

  add_test(
    NAME llvmdsdl-execution-engine-boundary-guard-selftest
    COMMAND
      ${CMAKE_COMMAND}
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -DREPO_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/RunExecutionEngineBoundaryGuardSelftest.cmake
  )

  set_tests_properties(
    llvmdsdl-execution-engine-boundary-guard-selftest
    PROPERTIES
      LABELS "integration;convergence"
  )

  add_test(
    NAME llvmdsdl-hard-cut-integrity-guard
    COMMAND
      ${CMAKE_COMMAND}
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -DREPO_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/RunHardCutIntegrityGuard.cmake
  )

  set_tests_properties(
    llvmdsdl-hard-cut-integrity-guard
    PROPERTIES
      LABELS "integration;convergence;release-blocking"
  )

  add_test(
    NAME llvmdsdl-hard-cut-integrity-guard-selftest
    COMMAND
      ${CMAKE_COMMAND}
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -DREPO_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/RunHardCutIntegrityGuardSelftest.cmake
  )

  set_tests_properties(
    llvmdsdl-hard-cut-integrity-guard-selftest
    PROPERTIES
      LABELS "integration;convergence"
  )

  # Emit-order verifier (golden step-trace): runs dsdlc over fixtures with the LLVMDSDL_EMIT_TRACE side
  # channel and checks every backend's emit-order trace is a member of the proven-safe
  # equivalence class (the ordering the Dafny oracle proves for the canonical), plus
  # cross-backend wire-skeleton agreement. Only needs dsdlc + Python (no target toolchains).
  add_test(
    NAME llvmdsdl-emit-order-verifier
    COMMAND
      ${PYTHON_EXECUTABLE}
        ${LLVMDSDL_SOURCE_DIR}/tools/convergence/emit_order_verifier.py
        --dsdlc $<TARGET_FILE:dsdlc>
  )

  set_tests_properties(
    llvmdsdl-emit-order-verifier
    PROPERTIES
      LABELS "integration;convergence;emit-order"
  )

  # Negative control: the membership check must reject a mask-before-validate reorder.
  add_test(
    NAME llvmdsdl-emit-order-verifier-selftest
    COMMAND
      ${PYTHON_EXECUTABLE}
        ${LLVMDSDL_SOURCE_DIR}/tools/convergence/emit_order_verifier.py
        --selftest
  )

  set_tests_properties(
    llvmdsdl-emit-order-verifier-selftest
    PROPERTIES
      LABELS "integration;convergence;emit-order"
  )

  if(EXISTS "${LLVMDSDL_UAVCAN_ROOT}")
    add_test(
      NAME llvmdsdl-embedded-uavcan-catalog-guard
      COMMAND
        ${CMAKE_COMMAND}
          -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
          -DREPO_ROOT=${LLVMDSDL_SOURCE_DIR}
          -DDSDLC=$<TARGET_FILE:dsdlc>
          -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
          -P ${LLVMDSDL_SOURCE_DIR}/cmake/RunEmbeddedUavcanCatalogGuard.cmake
    )

    set_tests_properties(
      llvmdsdl-embedded-uavcan-catalog-guard
      PROPERTIES
        LABELS "integration;uavcan;release-blocking"
    )

    add_test(
      NAME llvmdsdl-embedded-uavcan-catalog-guard-selftest
      COMMAND
        ${CMAKE_COMMAND}
          -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
          -DREPO_ROOT=${LLVMDSDL_SOURCE_DIR}
          -P ${LLVMDSDL_SOURCE_DIR}/cmake/RunEmbeddedUavcanCatalogGuardSelftest.cmake
    )

    set_tests_properties(
      llvmdsdl-embedded-uavcan-catalog-guard-selftest
      PROPERTIES
        LABELS "integration;uavcan"
    )
  else()
    message(STATUS
      "Skipping embedded UAVCAN catalog guard tests: ${LLVMDSDL_UAVCAN_ROOT} missing")
  endif()

  add_test(
    NAME llvmdsdl-parity-matrix-coverage
    COMMAND
      ${CMAKE_COMMAND}
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -DREPO_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DCTEST_TEST_DIR=${CMAKE_BINARY_DIR}
        -DOUTPUT_JSON=${CMAKE_CURRENT_BINARY_DIR}/parity-matrix-out/report.json
        -DOUTPUT_MD=${CMAKE_CURRENT_BINARY_DIR}/parity-matrix-out/PARITY_MATRIX.md
        -DPARITY_BASELINE_JSON=${LLVMDSDL_SOURCE_DIR}/tools/convergence/parity_matrix_baseline.json
        -DPARITY_MATRIX_SCRIPT=${LLVMDSDL_SOURCE_DIR}/tools/convergence/parity_matrix_report.py
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/RunParityMatrixReport.cmake
  )

  set_tests_properties(
    llvmdsdl-parity-matrix-coverage
    PROPERTIES
      LABELS "integration;convergence;parity;release-blocking"
  )

  add_test(
    NAME llvmdsdl-malformed-contract-matrix
    COMMAND
      ${CMAKE_COMMAND}
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -DREPO_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DCTEST_TEST_DIR=${CMAKE_BINARY_DIR}
        -DOUTPUT_JSON=${CMAKE_CURRENT_BINARY_DIR}/malformed-contract-matrix-out/report.json
        -DOUTPUT_MD=${CMAKE_CURRENT_BINARY_DIR}/malformed-contract-matrix-out/MALFORMED_INPUT_CONTRACT_MATRIX.md
        -DMALFORMED_CONTRACT_BASELINE_JSON=${LLVMDSDL_SOURCE_DIR}/tools/convergence/malformed_contract_matrix_baseline.json
        -DMALFORMED_CONTRACT_SCRIPT=${LLVMDSDL_SOURCE_DIR}/tools/convergence/malformed_contract_matrix_report.py
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/RunMalformedContractMatrixReport.cmake
  )

  set_tests_properties(
    llvmdsdl-malformed-contract-matrix
    PROPERTIES
      LABELS "integration;convergence;contract;release-blocking"
  )

  add_test(
    NAME llvmdsdl-determinism-matrix-coverage
    COMMAND
      ${CMAKE_COMMAND}
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -DREPO_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DCTEST_TEST_DIR=${CMAKE_BINARY_DIR}
        -DOUTPUT_JSON=${CMAKE_CURRENT_BINARY_DIR}/determinism-matrix-out/report.json
        -DOUTPUT_MD=${CMAKE_CURRENT_BINARY_DIR}/determinism-matrix-out/DETERMINISM_MATRIX.md
        -DDETERMINISM_BASELINE_JSON=${LLVMDSDL_SOURCE_DIR}/tools/convergence/determinism_matrix_baseline.json
        -DDETERMINISM_MATRIX_SCRIPT=${LLVMDSDL_SOURCE_DIR}/tools/convergence/determinism_matrix_report.py
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/RunDeterminismMatrixReport.cmake
  )

  set_tests_properties(
    llvmdsdl-determinism-matrix-coverage
    PROPERTIES
      LABELS "integration;convergence;determinism;release-blocking"
  )
else()
  message(STATUS "Skipping convergence scorecard test: python3 unavailable")
endif()

if(EXISTS "${LLVMDSDL_FIXTURES_ROOT}")
  add_test(
    NAME llvmdsdl-dsdl-opt-sanity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DDSDLOPT=$<TARGET_FILE:dsdl-opt>
        -DFIXTURES_ROOT=${LLVMDSDL_FIXTURES_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/dsdl-opt-sanity-out
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunDsdlOptSanity.cmake
  )

  set_tests_properties(
    llvmdsdl-dsdl-opt-sanity
    PROPERTIES
      LABELS "integration;tools;mlir"
  )

  if(CMAKE_C_COMPILER)
    add_test(
      NAME llvmdsdl-obj-backend-smoke
      COMMAND
        ${CMAKE_COMMAND}
          -DDSDLC=$<TARGET_FILE:dsdlc>
          -DFIXTURES_ROOT=${LLVMDSDL_FIXTURES_ROOT}
          -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/obj-backend-smoke-out
          -DC_COMPILER=${CMAKE_C_COMPILER}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/RunObjBackendSmoke.cmake
    )

    set_tests_properties(
      llvmdsdl-obj-backend-smoke
      PROPERTIES
        LABELS "integration;fixtures;obj"
    )

    if(CMAKE_CXX_COMPILER)
      add_test(
        NAME llvmdsdl-obj-cpp-backend-smoke
        COMMAND
          ${CMAKE_COMMAND}
            -DDSDLC=$<TARGET_FILE:dsdlc>
            -DFIXTURES_ROOT=${LLVMDSDL_FIXTURES_ROOT}
            -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/obj-cpp-backend-smoke-out
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/RunObjCppBackendSmoke.cmake
      )

      set_tests_properties(
        llvmdsdl-obj-cpp-backend-smoke
        PROPERTIES
          LABELS "integration;fixtures;obj;obj-cpp;release-blocking"
      )
    else()
      message(STATUS "Skipping obj-cpp backend smoke test: C++ compiler unavailable")
    endif()
  else()
    message(STATUS "Skipping obj backend smoke test: C compiler unavailable")
  endif()

  add_test(
    NAME llvmdsdl-fixtures-ts-generation-hardening
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DFIXTURES_ROOT=${LLVMDSDL_FIXTURES_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-ts-generation-hardening-out
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunFixturesTsGenerationHardening.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-ts-generation-hardening
    PROPERTIES
      LABELS "integration;fixtures;ts"
  )

  if(PYTHON_EXECUTABLE)
    add_test(
      NAME llvmdsdl-fixtures-python-generation-hardening
      COMMAND
        ${CMAKE_COMMAND}
          -DDSDLC=$<TARGET_FILE:dsdlc>
          -DFIXTURES_ROOT=${LLVMDSDL_FIXTURES_ROOT}
          -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-python-generation-hardening-out
          -P ${CMAKE_CURRENT_SOURCE_DIR}/RunFixturesPythonGenerationHardening.cmake
    )

    set_tests_properties(
      llvmdsdl-fixtures-python-generation-hardening
      PROPERTIES
        LABELS "integration;fixtures;python"
    )

    add_test(
      NAME llvmdsdl-fixtures-python-runtime-smoke
      COMMAND
        ${CMAKE_COMMAND}
          -DDSDLC=$<TARGET_FILE:dsdlc>
          -DFIXTURES_ROOT=${LLVMDSDL_FIXTURES_ROOT}
          -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-python-runtime-smoke-out
          -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/RunPythonRuntimeSmoke.cmake
    )

    set_tests_properties(
      llvmdsdl-fixtures-python-runtime-smoke
      PROPERTIES
        LABELS "integration;fixtures;python;runtime"
    )

    add_test(
      NAME llvmdsdl-fixtures-python-runtime-smoke-optimized
      COMMAND
        ${CMAKE_COMMAND}
          -DDSDLC=$<TARGET_FILE:dsdlc>
          -DDSDLC_EXTRA_ARGS=--optimize-lowered-serdes
          -DFIXTURES_ROOT=${LLVMDSDL_FIXTURES_ROOT}
          -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-python-runtime-smoke-optimized-out
          -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/RunPythonRuntimeSmoke.cmake
    )

    set_tests_properties(
      llvmdsdl-fixtures-python-runtime-smoke-optimized
      PROPERTIES
        LABELS "integration;fixtures;python;runtime;optimized"
    )

    add_test(
      NAME llvmdsdl-fixtures-python-runtime-smoke-fast
      COMMAND
        ${CMAKE_COMMAND}
          -DDSDLC=$<TARGET_FILE:dsdlc>
          -DFIXTURES_ROOT=${LLVMDSDL_FIXTURES_ROOT}
          -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-python-runtime-smoke-fast-out
          -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
          -DPY_RUNTIME_SPECIALIZATION=fast
          -P ${CMAKE_CURRENT_SOURCE_DIR}/RunPythonRuntimeSmoke.cmake
    )

    set_tests_properties(
      llvmdsdl-fixtures-python-runtime-smoke-fast
      PROPERTIES
        LABELS "integration;fixtures;python;runtime;python-runtime-specialization"
    )

    add_test(
      NAME llvmdsdl-fixtures-python-runtime-backend-selection
      COMMAND
        ${CMAKE_COMMAND}
          -DDSDLC=$<TARGET_FILE:dsdlc>
          -DFIXTURES_ROOT=${LLVMDSDL_FIXTURES_ROOT}
          -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-python-runtime-backend-selection-out
          -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
          -DACCEL_MODULE=$<$<TARGET_EXISTS:llvmdsdl-python-runtime-accelerator>:$<TARGET_FILE:llvmdsdl-python-runtime-accelerator>>
          -P ${CMAKE_CURRENT_SOURCE_DIR}/RunPythonRuntimeBackendSelection.cmake
    )

    set_tests_properties(
      llvmdsdl-fixtures-python-runtime-backend-selection
      PROPERTIES
        LABELS "integration;fixtures;python;runtime"
    )

    if(LLVMDSDL_ENABLE_PYTHON_ACCELERATOR)
      add_test(
        NAME llvmdsdl-fixtures-python-runtime-backend-selection-accel-required
        COMMAND
          ${CMAKE_COMMAND}
            -DDSDLC=$<TARGET_FILE:dsdlc>
            -DFIXTURES_ROOT=${LLVMDSDL_FIXTURES_ROOT}
            -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-python-runtime-backend-selection-accel-required-out
            -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
            -DACCEL_MODULE=$<$<TARGET_EXISTS:llvmdsdl-python-runtime-accelerator>:$<TARGET_FILE:llvmdsdl-python-runtime-accelerator>>
            -DREQUIRE_ACCEL=ON
            -P ${CMAKE_CURRENT_SOURCE_DIR}/RunPythonRuntimeBackendSelection.cmake
      )

      set_tests_properties(
        llvmdsdl-fixtures-python-runtime-backend-selection-accel-required
        PROPERTIES
          LABELS "integration;fixtures;python;runtime;python-accel-required"
      )
    endif()

    add_test(
      NAME llvmdsdl-fixtures-python-malformed-input-contract
      COMMAND
        ${CMAKE_COMMAND}
          -DDSDLC=$<TARGET_FILE:dsdlc>
          -DFIXTURES_ROOT=${LLVMDSDL_FIXTURES_ROOT}
          -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-python-malformed-input-contract-out
          -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
          -DACCEL_MODULE=$<$<TARGET_EXISTS:llvmdsdl-python-runtime-accelerator>:$<TARGET_FILE:llvmdsdl-python-runtime-accelerator>>
          -P ${CMAKE_CURRENT_SOURCE_DIR}/RunPythonMalformedInputContract.cmake
    )

    set_tests_properties(
      llvmdsdl-fixtures-python-malformed-input-contract
      PROPERTIES
        LABELS "integration;fixtures;python;runtime;contract"
    )

    add_test(
      NAME llvmdsdl-fixtures-python-malformed-decode-fuzz-parity
      COMMAND
        ${CMAKE_COMMAND}
          -DDSDLC=$<TARGET_FILE:dsdlc>
          -DFIXTURES_ROOT=${LLVMDSDL_FIXTURES_ROOT}
          -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-python-malformed-decode-fuzz-parity-out
          -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
          -DACCEL_MODULE=$<$<TARGET_EXISTS:llvmdsdl-python-runtime-accelerator>:$<TARGET_FILE:llvmdsdl-python-runtime-accelerator>>
          -P ${CMAKE_CURRENT_SOURCE_DIR}/RunPythonMalformedDecodeFuzzParity.cmake
    )

    set_tests_properties(
      llvmdsdl-fixtures-python-malformed-decode-fuzz-parity
      PROPERTIES
        LABELS "integration;fixtures;python;runtime;contract;differential;fuzz"
    )

    if(LLVMDSDL_ENABLE_PYTHON_ACCELERATOR)
      add_test(
        NAME llvmdsdl-fixtures-python-malformed-decode-fuzz-parity-accel-required
        COMMAND
          ${CMAKE_COMMAND}
            -DDSDLC=$<TARGET_FILE:dsdlc>
            -DFIXTURES_ROOT=${LLVMDSDL_FIXTURES_ROOT}
            -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-python-malformed-decode-fuzz-parity-accel-required-out
            -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
            -DACCEL_MODULE=$<$<TARGET_EXISTS:llvmdsdl-python-runtime-accelerator>:$<TARGET_FILE:llvmdsdl-python-runtime-accelerator>>
            -DREQUIRE_ACCEL=ON
            -P ${CMAKE_CURRENT_SOURCE_DIR}/RunPythonMalformedDecodeFuzzParity.cmake
      )

      set_tests_properties(
        llvmdsdl-fixtures-python-malformed-decode-fuzz-parity-accel-required
        PROPERTIES
          LABELS "integration;fixtures;python;runtime;contract;differential;fuzz;python-accel-required"
      )
    endif()

    add_test(
      NAME llvmdsdl-fixtures-python-runtime-parity
      COMMAND
        ${CMAKE_COMMAND}
          -DDSDLC=$<TARGET_FILE:dsdlc>
          -DFIXTURES_ROOT=${LLVMDSDL_FIXTURES_ROOT}
          -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-python-runtime-parity-out
          -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
          -DACCEL_MODULE=$<$<TARGET_EXISTS:llvmdsdl-python-runtime-accelerator>:$<TARGET_FILE:llvmdsdl-python-runtime-accelerator>>
          -P ${CMAKE_CURRENT_SOURCE_DIR}/RunPythonRuntimeParity.cmake
    )

    set_tests_properties(
      llvmdsdl-fixtures-python-runtime-parity
      PROPERTIES
        LABELS "integration;fixtures;python;runtime;differential"
    )

    if(LLVMDSDL_ENABLE_PYTHON_ACCELERATOR)
      add_test(
        NAME llvmdsdl-fixtures-python-runtime-parity-accel-required
        COMMAND
          ${CMAKE_COMMAND}
            -DDSDLC=$<TARGET_FILE:dsdlc>
            -DFIXTURES_ROOT=${LLVMDSDL_FIXTURES_ROOT}
            -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-python-runtime-parity-accel-required-out
            -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
            -DACCEL_MODULE=$<$<TARGET_EXISTS:llvmdsdl-python-runtime-accelerator>:$<TARGET_FILE:llvmdsdl-python-runtime-accelerator>>
            -DREQUIRE_ACCEL=ON
            -P ${CMAKE_CURRENT_SOURCE_DIR}/RunPythonRuntimeParity.cmake
      )

      set_tests_properties(
        llvmdsdl-fixtures-python-runtime-parity-accel-required
        PROPERTIES
          LABELS "integration;fixtures;python;runtime;differential;python-accel-required"
      )
    endif()
  else()
    message(STATUS "Skipping Python fixture integration tests: python3 unavailable")
  endif()
endif()

if(CMAKE_C_COMPILER AND CMAKE_CXX_COMPILER)
  add_test(
    NAME llvmdsdl-signed-narrow-cpp-c-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/signed-narrow-cpp-c-parity-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCPP_PROFILE=std
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunSignedNarrowCppCParity.cmake
  )

  set_tests_properties(
    llvmdsdl-signed-narrow-cpp-c-parity
    PROPERTIES
      LABELS "integration;fixtures;cpp;differential"
  )

  add_test(
    NAME llvmdsdl-signed-narrow-cpp-c-parity-optimized
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DDSDLC_EXTRA_ARGS=--optimize-lowered-serdes
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/signed-narrow-cpp-c-parity-optimized-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCPP_PROFILE=std
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunSignedNarrowCppCParity.cmake
  )

  set_tests_properties(
    llvmdsdl-signed-narrow-cpp-c-parity-optimized
    PROPERTIES
      LABELS "integration;fixtures;cpp;differential;optimized"
  )

  add_test(
    NAME llvmdsdl-signed-narrow-cpp-pmr-c-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/signed-narrow-cpp-pmr-c-parity-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCPP_PROFILE=pmr
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunSignedNarrowCppCParity.cmake
  )

  set_tests_properties(
    llvmdsdl-signed-narrow-cpp-pmr-c-parity
    PROPERTIES
      LABELS "integration;fixtures;cpp;differential"
  )

  add_test(
    NAME llvmdsdl-signed-narrow-cpp-autosar-c-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/signed-narrow-cpp-autosar-c-parity-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCPP_PROFILE=autosar
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunSignedNarrowCppCParity.cmake
  )

  set_tests_properties(
    llvmdsdl-signed-narrow-cpp-autosar-c-parity
    PROPERTIES
      LABELS "integration;fixtures;cpp;differential;autosar"
  )

  add_test(
    NAME llvmdsdl-signed-narrow-cpp-autosar-c-parity-optimized
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DDSDLC_EXTRA_ARGS=--optimize-lowered-serdes
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/signed-narrow-cpp-autosar-c-parity-optimized-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCPP_PROFILE=autosar
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunSignedNarrowCppCParity.cmake
  )

  set_tests_properties(
    llvmdsdl-signed-narrow-cpp-autosar-c-parity-optimized
    PROPERTIES
      LABELS "integration;fixtures;cpp;differential;optimized;autosar"
  )
else()
  message(STATUS "Skipping signed_narrow C/C++ parity tests: C/C++ compiler unavailable")
endif()

find_program(CARGO_EXECUTABLE cargo QUIET)
find_program(RUSTC_EXECUTABLE rustc QUIET)
find_program(GO_EXECUTABLE go QUIET)
find_program(TSC_EXECUTABLE tsc QUIET)
find_program(NODE_EXECUTABLE node QUIET)

if(GO_EXECUTABLE)
  add_test(
    NAME llvmdsdl-go-runtime-unit-tests
    COMMAND
      ${CMAKE_COMMAND}
        -DGO_EXECUTABLE=${GO_EXECUTABLE}
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/go-runtime-unit-tests-out
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunGoRuntimeUnitTests.cmake
  )

  set_tests_properties(
    llvmdsdl-go-runtime-unit-tests
    PROPERTIES
      LABELS "integration;go;runtime;go-differential"
  )
else()
  message(STATUS "Skipping Go runtime unit tests: go unavailable")
endif()

# Isolated cross-language runtime-primitive equivalence: every native runtime
# (C, Rust, Go) is checked against one shared golden-vector file, per primitive
# direction, so paired bugs cannot mask each other.
if(CMAKE_C_COMPILER AND CARGO_EXECUTABLE AND GO_EXECUTABLE AND PYTHON_EXECUTABLE)
  add_test(
    NAME llvmdsdl-primitive-equivalence
    COMMAND
      ${CMAKE_COMMAND}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DGO_EXECUTABLE=${GO_EXECUTABLE}
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/primitive-equivalence-out
        -DVECTORS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/primitive_vectors.txt
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunPrimitiveEquivalence.cmake
  )

  set_tests_properties(
    llvmdsdl-primitive-equivalence
    PROPERTIES
      LABELS "integration;runtime;differential;go;rust;python;ts;go-differential"
  )
else()
  message(STATUS
    "Skipping cross-language primitive equivalence: need C compiler, cargo, go, and python3")
endif()

if(CMAKE_C_COMPILER AND TSC_EXECUTABLE AND NODE_EXECUTABLE)
  add_test(
    NAME llvmdsdl-c-ts-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/c-ts-parity-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCTsParity.cmake
  )

  set_tests_properties(
    llvmdsdl-c-ts-parity
    PROPERTIES
      LABELS "integration;fixtures;ts;differential"
  )

  add_test(
    NAME llvmdsdl-c-ts-parity-runtime-fast
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DTS_RUNTIME_SPECIALIZATION=fast
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/c-ts-parity-runtime-fast-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCTsParity.cmake
  )

  set_tests_properties(
    llvmdsdl-c-ts-parity-runtime-fast
    PROPERTIES
      LABELS "integration;fixtures;ts;differential;ts-runtime-specialization"
  )

  add_test(
    NAME llvmdsdl-c-ts-parity-optimized
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DDSDLC_EXTRA_ARGS=--optimize-lowered-serdes
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/c-ts-parity-optimized-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCTsParity.cmake
  )

  set_tests_properties(
    llvmdsdl-c-ts-parity-optimized
    PROPERTIES
      LABELS "integration;fixtures;ts;differential;optimized"
  )

  add_test(
    NAME llvmdsdl-signed-narrow-c-ts-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/signed-narrow-c-ts-parity-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunSignedNarrowCTsParity.cmake
  )

  set_tests_properties(
    llvmdsdl-signed-narrow-c-ts-parity
    PROPERTIES
      LABELS "integration;fixtures;ts;differential"
  )

  add_test(
    NAME llvmdsdl-signed-narrow-c-ts-parity-optimized
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DDSDLC_EXTRA_ARGS=--optimize-lowered-serdes
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/signed-narrow-c-ts-parity-optimized-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunSignedNarrowCTsParity.cmake
  )

  set_tests_properties(
    llvmdsdl-signed-narrow-c-ts-parity-optimized
    PROPERTIES
      LABELS "integration;fixtures;ts;differential;optimized"
  )

  add_test(
    NAME llvmdsdl-fixtures-c-ts-runtime-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-c-ts-runtime-parity-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunFixturesCTsRuntimeParity.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-c-ts-runtime-parity
    PROPERTIES
      LABELS "integration;fixtures;ts;differential"
  )

  add_test(
    NAME llvmdsdl-fixtures-c-ts-service-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-c-ts-service-parity-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunFixturesCTsServiceParity.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-c-ts-service-parity
    PROPERTIES
      LABELS "integration;fixtures;ts;differential"
  )

  add_test(
    NAME llvmdsdl-fixtures-c-ts-variable-array-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-c-ts-variable-array-parity-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunFixturesCTsVariableArrayParity.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-c-ts-variable-array-parity
    PROPERTIES
      LABELS "integration;fixtures;ts;differential"
  )

  add_test(
    NAME llvmdsdl-fixtures-c-ts-bigint-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-c-ts-bigint-parity-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunFixturesCTsBigIntParity.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-c-ts-bigint-parity
    PROPERTIES
      LABELS "integration;fixtures;ts;differential"
  )

  add_test(
    NAME llvmdsdl-fixtures-c-ts-float-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-c-ts-float-parity-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunFixturesCTsFloatParity.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-c-ts-float-parity
    PROPERTIES
      LABELS "integration;fixtures;ts;differential"
  )

  add_test(
    NAME llvmdsdl-fixtures-c-ts-utf8-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-c-ts-utf8-parity-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunFixturesCTsUtf8Parity.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-c-ts-utf8-parity
    PROPERTIES
      LABELS "integration;fixtures;ts;differential"
  )

  add_test(
    NAME llvmdsdl-fixtures-c-ts-union-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-c-ts-union-parity-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunFixturesCTsUnionParity.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-c-ts-union-parity
    PROPERTIES
      LABELS "integration;fixtures;ts;differential"
  )

  add_test(
    NAME llvmdsdl-fixtures-c-ts-union-composite-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-c-ts-union-composite-parity-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunFixturesCTsUnionCompositeParity.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-c-ts-union-composite-parity
    PROPERTIES
      LABELS "integration;fixtures;ts;differential"
  )

  add_test(
    NAME llvmdsdl-fixtures-c-ts-union-empty-composite-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-c-ts-union-empty-composite-parity-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunFixturesCTsUnionEmptyCompositeParity.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-c-ts-union-empty-composite-parity
    PROPERTIES
      LABELS "integration;fixtures;ts;differential"
  )

  add_test(
    NAME llvmdsdl-fixtures-c-ts-union-array-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-c-ts-union-array-parity-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunFixturesCTsUnionArrayParity.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-c-ts-union-array-parity
    PROPERTIES
      LABELS "integration;fixtures;ts;differential"
  )

  add_test(
    NAME llvmdsdl-fixtures-c-ts-truncated-decode-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-c-ts-truncated-decode-parity-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunFixturesCTsTruncatedDecodeParity.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-c-ts-truncated-decode-parity
    PROPERTIES
      LABELS "integration;fixtures;ts;differential"
  )

  add_test(
    NAME llvmdsdl-fixtures-c-ts-padding-alignment-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-c-ts-padding-alignment-parity-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunFixturesCTsPaddingAlignmentParity.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-c-ts-padding-alignment-parity
    PROPERTIES
      LABELS "integration;fixtures;ts;differential"
  )

  add_test(
    NAME llvmdsdl-fixtures-c-ts-composite-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-c-ts-composite-parity-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunFixturesCTsCompositeParity.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-c-ts-composite-parity
    PROPERTIES
      LABELS "integration;fixtures;ts;differential"
  )

  add_test(
    NAME llvmdsdl-fixtures-c-ts-delimited-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-c-ts-delimited-parity-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunFixturesCTsDelimitedParity.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-c-ts-delimited-parity
    PROPERTIES
      LABELS "integration;fixtures;ts;differential"
  )

  add_test(
    NAME llvmdsdl-fixtures-c-ts-delimited-invalid-header-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-c-ts-delimited-invalid-header-parity-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunFixturesCTsDelimitedInvalidHeaderParity.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-c-ts-delimited-invalid-header-parity
    PROPERTIES
      LABELS "integration;fixtures;ts;differential"
  )

  add_test(
    NAME llvmdsdl-fixtures-c-ts-composite-fixed-array-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-c-ts-composite-fixed-array-parity-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunFixturesCTsCompositeFixedArrayParity.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-c-ts-composite-fixed-array-parity
    PROPERTIES
      LABELS "integration;fixtures;ts;differential"
  )

  add_test(
    NAME llvmdsdl-fixtures-c-ts-composite-variable-array-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-c-ts-composite-variable-array-parity-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunFixturesCTsCompositeVariableArrayParity.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-c-ts-composite-variable-array-parity
    PROPERTIES
      LABELS "integration;fixtures;ts;differential"
  )
else()
  message(STATUS "Skipping fixtures C/TypeScript runtime parity test: C compiler, tsc, or node unavailable")
endif()

if(CMAKE_C_COMPILER AND PYTHON_EXECUTABLE)
  set(c_python_parity_families
    runtime
    service
    variable_array
    fixed_array
    bigint
    float
    utf8
    delimited
    union
    composite
    truncated_decode
    padding_alignment
  )

  foreach(c_python_family IN LISTS c_python_parity_families)
    string(REPLACE "_" "-" c_python_family_slug "${c_python_family}")

    add_test(
      NAME llvmdsdl-fixtures-c-python-${c_python_family_slug}-parity
      COMMAND
        ${CMAKE_COMMAND}
          -DDSDLC=$<TARGET_FILE:dsdlc>
          -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-c-python-${c_python_family_slug}-parity-out
          -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
          -DC_COMPILER=${CMAKE_C_COMPILER}
          -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
          -DFAMILY=${c_python_family}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCPythonFixtureParity.cmake
    )

    set_tests_properties(
      llvmdsdl-fixtures-c-python-${c_python_family_slug}-parity
      PROPERTIES
        LABELS "integration;fixtures;python;differential"
    )

    add_test(
      NAME llvmdsdl-fixtures-c-python-${c_python_family_slug}-parity-optimized
      COMMAND
        ${CMAKE_COMMAND}
          -DDSDLC=$<TARGET_FILE:dsdlc>
          -DDSDLC_EXTRA_ARGS=--optimize-lowered-serdes
          -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-c-python-${c_python_family_slug}-parity-optimized-out
          -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
          -DC_COMPILER=${CMAKE_C_COMPILER}
          -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
          -DFAMILY=${c_python_family}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCPythonFixtureParity.cmake
    )

    set_tests_properties(
      llvmdsdl-fixtures-c-python-${c_python_family_slug}-parity-optimized
      PROPERTIES
        LABELS "integration;fixtures;python;differential;optimized"
    )

    if(c_python_family STREQUAL "runtime")
      add_test(
        NAME llvmdsdl-fixtures-c-python-${c_python_family_slug}-parity-runtime-fast
        COMMAND
          ${CMAKE_COMMAND}
            -DDSDLC=$<TARGET_FILE:dsdlc>
            -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-c-python-${c_python_family_slug}-parity-runtime-fast-out
            -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
            -DPY_RUNTIME_SPECIALIZATION=fast
            -DFAMILY=${c_python_family}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCPythonFixtureParity.cmake
      )

      set_tests_properties(
        llvmdsdl-fixtures-c-python-${c_python_family_slug}-parity-runtime-fast
        PROPERTIES
          LABELS "integration;fixtures;python;differential;python-runtime-specialization"
      )
    endif()
  endforeach()

  add_test(
    NAME llvmdsdl-signed-narrow-c-python-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/signed-narrow-c-python-parity-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunSignedNarrowCPythonParity.cmake
  )

  set_tests_properties(
    llvmdsdl-signed-narrow-c-python-parity
    PROPERTIES
      LABELS "integration;fixtures;python;differential"
  )

  add_test(
    NAME llvmdsdl-signed-narrow-c-python-parity-optimized
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DDSDLC_EXTRA_ARGS=--optimize-lowered-serdes
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/signed-narrow-c-python-parity-optimized-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunSignedNarrowCPythonParity.cmake
  )

  set_tests_properties(
    llvmdsdl-signed-narrow-c-python-parity-optimized
    PROPERTIES
      LABELS "integration;fixtures;python;differential;optimized"
  )

  add_test(
    NAME llvmdsdl-signed-narrow-c-python-parity-runtime-fast
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/signed-narrow-c-python-parity-runtime-fast-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -DPY_RUNTIME_SPECIALIZATION=fast
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunSignedNarrowCPythonParity.cmake
  )

  set_tests_properties(
    llvmdsdl-signed-narrow-c-python-parity-runtime-fast
    PROPERTIES
      LABELS "integration;fixtures;python;differential;python-runtime-specialization"
  )
else()
  message(STATUS "Skipping fixtures C/Python parity tests: C compiler or python unavailable")
endif()

if(TSC_EXECUTABLE AND NODE_EXECUTABLE)
  add_test(
    NAME llvmdsdl-ts-runtime-fixed-array-smoke
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ts-runtime-fixed-array-smoke-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunTsRuntimeFixedArraySmoke.cmake
  )

  set_tests_properties(
    llvmdsdl-ts-runtime-fixed-array-smoke
    PROPERTIES
      LABELS "integration;fixtures;ts"
  )

  add_test(
    NAME llvmdsdl-ts-runtime-variable-array-smoke
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ts-runtime-variable-array-smoke-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunTsRuntimeVariableArraySmoke.cmake
  )

  set_tests_properties(
    llvmdsdl-ts-runtime-variable-array-smoke
    PROPERTIES
      LABELS "integration;fixtures;ts"
  )

  add_test(
    NAME llvmdsdl-ts-runtime-bigint-smoke
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ts-runtime-bigint-smoke-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunTsRuntimeBigIntSmoke.cmake
  )

  set_tests_properties(
    llvmdsdl-ts-runtime-bigint-smoke
    PROPERTIES
      LABELS "integration;fixtures;ts"
  )

  add_test(
    NAME llvmdsdl-ts-runtime-float-smoke
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ts-runtime-float-smoke-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunTsRuntimeFloatSmoke.cmake
  )

  set_tests_properties(
    llvmdsdl-ts-runtime-float-smoke
    PROPERTIES
      LABELS "integration;fixtures;ts"
  )

  add_test(
    NAME llvmdsdl-ts-runtime-utf8-smoke
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ts-runtime-utf8-smoke-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunTsRuntimeUtf8Smoke.cmake
  )

  set_tests_properties(
    llvmdsdl-ts-runtime-utf8-smoke
    PROPERTIES
      LABELS "integration;fixtures;ts"
  )

  add_test(
    NAME llvmdsdl-ts-runtime-service-smoke
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ts-runtime-service-smoke-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunTsRuntimeServiceSmoke.cmake
  )

  set_tests_properties(
    llvmdsdl-ts-runtime-service-smoke
    PROPERTIES
      LABELS "integration;fixtures;ts"
  )

  add_test(
    NAME llvmdsdl-ts-runtime-union-smoke
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ts-runtime-union-smoke-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunTsRuntimeUnionSmoke.cmake
  )

  set_tests_properties(
    llvmdsdl-ts-runtime-union-smoke
    PROPERTIES
      LABELS "integration;fixtures;ts"
  )

  add_test(
    NAME llvmdsdl-ts-runtime-union-composite-smoke
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ts-runtime-union-composite-smoke-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunTsRuntimeUnionCompositeSmoke.cmake
  )

  set_tests_properties(
    llvmdsdl-ts-runtime-union-composite-smoke
    PROPERTIES
      LABELS "integration;fixtures;ts"
  )

  add_test(
    NAME llvmdsdl-ts-runtime-union-empty-composite-smoke
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ts-runtime-union-empty-composite-smoke-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunTsRuntimeUnionEmptyCompositeSmoke.cmake
  )

  set_tests_properties(
    llvmdsdl-ts-runtime-union-empty-composite-smoke
    PROPERTIES
      LABELS "integration;fixtures;ts"
  )

  add_test(
    NAME llvmdsdl-ts-runtime-union-array-smoke
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ts-runtime-union-array-smoke-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunTsRuntimeUnionArraySmoke.cmake
  )

  set_tests_properties(
    llvmdsdl-ts-runtime-union-array-smoke
    PROPERTIES
      LABELS "integration;fixtures;ts"
  )

  add_test(
    NAME llvmdsdl-ts-runtime-truncated-decode-smoke
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ts-runtime-truncated-decode-smoke-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunTsRuntimeTruncatedDecodeSmoke.cmake
  )

  set_tests_properties(
    llvmdsdl-ts-runtime-truncated-decode-smoke
    PROPERTIES
      LABELS "integration;fixtures;ts"
  )

  add_test(
    NAME llvmdsdl-ts-runtime-padding-alignment-smoke
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ts-runtime-padding-alignment-smoke-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunTsRuntimePaddingAlignmentSmoke.cmake
  )

  set_tests_properties(
    llvmdsdl-ts-runtime-padding-alignment-smoke
    PROPERTIES
      LABELS "integration;fixtures;ts"
  )

  add_test(
    NAME llvmdsdl-ts-runtime-composite-smoke
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ts-runtime-composite-smoke-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunTsRuntimeCompositeSmoke.cmake
  )

  set_tests_properties(
    llvmdsdl-ts-runtime-composite-smoke
    PROPERTIES
      LABELS "integration;fixtures;ts"
  )

  add_test(
    NAME llvmdsdl-ts-runtime-delimited-smoke
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ts-runtime-delimited-smoke-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunTsRuntimeDelimitedSmoke.cmake
  )

  set_tests_properties(
    llvmdsdl-ts-runtime-delimited-smoke
    PROPERTIES
      LABELS "integration;fixtures;ts"
  )

  add_test(
    NAME llvmdsdl-ts-runtime-delimited-invalid-header-smoke
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ts-runtime-delimited-invalid-header-smoke-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunTsRuntimeDelimitedInvalidHeaderSmoke.cmake
  )

  set_tests_properties(
    llvmdsdl-ts-runtime-delimited-invalid-header-smoke
    PROPERTIES
      LABELS "integration;fixtures;ts"
  )

  add_test(
    NAME llvmdsdl-ts-runtime-composite-fixed-array-smoke
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ts-runtime-composite-fixed-array-smoke-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunTsRuntimeCompositeFixedArraySmoke.cmake
  )

  set_tests_properties(
    llvmdsdl-ts-runtime-composite-fixed-array-smoke
    PROPERTIES
      LABELS "integration;fixtures;ts"
  )

  add_test(
    NAME llvmdsdl-ts-runtime-composite-variable-array-smoke
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ts-runtime-composite-variable-array-smoke-out
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunTsRuntimeCompositeVariableArraySmoke.cmake
  )

  set_tests_properties(
    llvmdsdl-ts-runtime-composite-variable-array-smoke
    PROPERTIES
      LABELS "integration;fixtures;ts"
  )
else()
  message(STATUS "Skipping TypeScript runtime smoke tests: tsc or node unavailable")
endif()

if(CMAKE_C_COMPILER AND CARGO_EXECUTABLE AND RUSTC_EXECUTABLE)
  add_test(
    NAME llvmdsdl-signed-narrow-c-rust-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/signed-narrow-c-rust-parity-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DAR_EXECUTABLE=${CMAKE_AR}
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunSignedNarrowCRustParity.cmake
  )

  set_tests_properties(
    llvmdsdl-signed-narrow-c-rust-parity
    PROPERTIES
      LABELS "integration;fixtures;rust;differential"
  )

  add_test(
    NAME llvmdsdl-signed-narrow-c-rust-parity-optimized
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DDSDLC_EXTRA_ARGS=--optimize-lowered-serdes
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/signed-narrow-c-rust-parity-optimized-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DAR_EXECUTABLE=${CMAKE_AR}
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunSignedNarrowCRustParity.cmake
  )

  set_tests_properties(
    llvmdsdl-signed-narrow-c-rust-parity-optimized
    PROPERTIES
      LABELS "integration;fixtures;rust;differential;optimized"
  )

  add_test(
    NAME llvmdsdl-signed-narrow-c-rust-parity-no-std-alloc
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DRUST_PROFILE=no-std-alloc
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/signed-narrow-c-rust-parity-no-std-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DAR_EXECUTABLE=${CMAKE_AR}
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunSignedNarrowCRustParity.cmake
  )

  set_tests_properties(
    llvmdsdl-signed-narrow-c-rust-parity-no-std-alloc
    PROPERTIES
      LABELS "integration;fixtures;rust;differential;rust-no-std"
  )

  add_test(
    NAME llvmdsdl-signed-narrow-c-rust-parity-runtime-fast
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DRUST_RUNTIME_SPECIALIZATION=fast
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/signed-narrow-c-rust-parity-runtime-fast-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DAR_EXECUTABLE=${CMAKE_AR}
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunSignedNarrowCRustParity.cmake
  )

  set_tests_properties(
    llvmdsdl-signed-narrow-c-rust-parity-runtime-fast
    PROPERTIES
      LABELS "integration;fixtures;rust;differential;rust-runtime-specialization"
  )

  add_test(
    NAME llvmdsdl-signed-narrow-c-rust-parity-inline-then-pool
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DRUST_MEMORY_MODE=inline-then-pool
        -DRUST_INLINE_THRESHOLD_BYTES=64
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/signed-narrow-c-rust-parity-inline-then-pool-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DAR_EXECUTABLE=${CMAKE_AR}
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunSignedNarrowCRustParity.cmake
  )

  set_tests_properties(
    llvmdsdl-signed-narrow-c-rust-parity-inline-then-pool
    PROPERTIES
      LABELS "integration;fixtures;rust;differential;rust-memory-mode"
  )

  add_test(
    NAME llvmdsdl-signed-narrow-c-rust-parity-no-std-runtime-fast-inline-then-pool
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DRUST_PROFILE=no-std-alloc
        -DRUST_RUNTIME_SPECIALIZATION=fast
        -DRUST_MEMORY_MODE=inline-then-pool
        -DRUST_INLINE_THRESHOLD_BYTES=64
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/signed-narrow-c-rust-parity-no-std-fast-inline-then-pool-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DAR_EXECUTABLE=${CMAKE_AR}
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunSignedNarrowCRustParity.cmake
  )

  set_tests_properties(
    llvmdsdl-signed-narrow-c-rust-parity-no-std-runtime-fast-inline-then-pool
    PROPERTIES
      LABELS "integration;fixtures;rust;differential;rust-no-std;rust-runtime-specialization;rust-memory-mode"
  )
else()
  message(STATUS "Skipping signed_narrow C/Rust parity test: C compiler or cargo/rustc unavailable")
endif()

if(CMAKE_C_COMPILER AND GO_EXECUTABLE AND CMAKE_AR)
  add_test(
    NAME llvmdsdl-signed-narrow-c-go-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/signed-narrow-c-go-parity-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DAR_EXECUTABLE=${CMAKE_AR}
        -DGO_EXECUTABLE=${GO_EXECUTABLE}
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunSignedNarrowCGoParity.cmake
  )

  set_tests_properties(
    llvmdsdl-signed-narrow-c-go-parity
    PROPERTIES
      LABELS "integration;fixtures;go;differential;go-differential"
  )

  add_test(
    NAME llvmdsdl-signed-narrow-c-go-parity-optimized
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DDSDLC_EXTRA_ARGS=--optimize-lowered-serdes
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/signed-narrow-c-go-parity-optimized-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DAR_EXECUTABLE=${CMAKE_AR}
        -DGO_EXECUTABLE=${GO_EXECUTABLE}
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunSignedNarrowCGoParity.cmake
  )

  set_tests_properties(
    llvmdsdl-signed-narrow-c-go-parity-optimized
    PROPERTIES
      LABELS "integration;fixtures;go;differential;go-differential;optimized"
  )
else()
  message(STATUS "Skipping signed_narrow C/Go parity test: C compiler, ar, or go unavailable")
endif()

if(NOT EXISTS "${LLVMDSDL_UAVCAN_ROOT}")
  message(WARNING "Skipping uavcan integration test: ${LLVMDSDL_UAVCAN_ROOT} missing")
  return()
endif()

if(NOT CMAKE_C_COMPILER)
  message(WARNING "Skipping uavcan integration test: C compiler unavailable")
  return()
endif()

if(NOT CMAKE_CXX_COMPILER)
  message(WARNING "Skipping uavcan C++ integration test: C++ compiler unavailable")
  return()
endif()

add_test(
  NAME llvmdsdl-uavcan-generation
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-out
      -DC_COMPILER=${CMAKE_C_COMPILER}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanGeneration.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-generation
  PROPERTIES
    LABELS "integration;uavcan;slow"
)

add_test(
  NAME llvmdsdl-uavcan-mlir-lowering
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DDSDLOPT=$<TARGET_FILE:dsdl-opt>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-mlir-lowering-out
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanMlirLowering.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-mlir-lowering
  PROPERTIES
    LABELS "integration;uavcan;mlir;slow"
)

add_test(
  NAME llvmdsdl-uavcan-rust-generation
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-rust-out
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanRustGeneration.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-rust-generation
  PROPERTIES
    LABELS "integration;uavcan;rust"
)

add_test(
  NAME llvmdsdl-uavcan-rust-generation-no-std-alloc
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-rust-no-std-out
      -DRUST_PROFILE=no-std-alloc
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanRustGeneration.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-rust-generation-no-std-alloc
  PROPERTIES
    LABELS "integration;uavcan;rust;slow;rust-no-std"
)

add_test(
  NAME llvmdsdl-uavcan-rust-generation-runtime-fast
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-rust-runtime-fast-out
      -DRUST_RUNTIME_SPECIALIZATION=fast
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanRustGeneration.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-rust-generation-runtime-fast
  PROPERTIES
    LABELS "integration;uavcan;rust;slow;rust-runtime-specialization"
)

add_test(
  NAME llvmdsdl-uavcan-rust-generation-no-std-runtime-fast
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-rust-no-std-fast-out
      -DRUST_PROFILE=no-std-alloc
      -DRUST_RUNTIME_SPECIALIZATION=fast
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanRustGeneration.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-rust-generation-no-std-runtime-fast
  PROPERTIES
    LABELS "integration;uavcan;rust;slow;rust-no-std;rust-runtime-specialization"
)

add_test(
  NAME llvmdsdl-uavcan-rust-generation-inline-then-pool
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-rust-inline-then-pool-out
      -DRUST_MEMORY_MODE=inline-then-pool
      -DRUST_INLINE_THRESHOLD_BYTES=512
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanRustGeneration.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-rust-generation-inline-then-pool
  PROPERTIES
    LABELS "integration;uavcan;rust;slow;rust-memory-mode"
)

add_test(
  NAME llvmdsdl-uavcan-rust-generation-no-std-runtime-fast-inline-then-pool
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-rust-no-std-fast-inline-then-pool-out
      -DRUST_PROFILE=no-std-alloc
      -DRUST_RUNTIME_SPECIALIZATION=fast
      -DRUST_MEMORY_MODE=inline-then-pool
      -DRUST_INLINE_THRESHOLD_BYTES=512
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanRustGeneration.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-rust-generation-no-std-runtime-fast-inline-then-pool
  PROPERTIES
    LABELS "integration;uavcan;rust;slow;rust-no-std;rust-runtime-specialization;rust-memory-mode"
)

add_test(
  NAME llvmdsdl-uavcan-rust-profile-semantic-diff
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-rust-profile-diff-out
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunRustProfileSemanticDiff.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-rust-profile-semantic-diff
  PROPERTIES
    LABELS "integration;uavcan;rust;slow;rust-no-std"
)

add_test(
  NAME llvmdsdl-uavcan-rust-runtime-specialization-diff
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-rust-runtime-diff-out
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunRustRuntimeSpecializationDiff.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-rust-runtime-specialization-diff
  PROPERTIES
    LABELS "integration;uavcan;rust;slow;rust-runtime-specialization"
)

add_test(
  NAME llvmdsdl-uavcan-rust-runtime-specialization-diff-no-std-alloc
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-rust-runtime-diff-no-std-out
      -DRUST_PROFILE=no-std-alloc
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunRustRuntimeSpecializationDiff.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-rust-runtime-specialization-diff-no-std-alloc
  PROPERTIES
    LABELS "integration;uavcan;rust;slow;rust-no-std;rust-runtime-specialization"
)

add_test(
  NAME llvmdsdl-uavcan-rust-memory-mode-semantic-diff
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-rust-memory-mode-diff-out
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunRustMemoryModeSemanticDiff.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-rust-memory-mode-semantic-diff
  PROPERTIES
    LABELS "integration;uavcan;rust;slow;rust-memory-mode"
)

add_test(
  NAME llvmdsdl-uavcan-rust-memory-mode-semantic-diff-no-std-alloc
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-rust-memory-mode-diff-no-std-out
      -DRUST_PROFILE=no-std-alloc
      -DRUST_RUNTIME_SPECIALIZATION=fast
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunRustMemoryModeSemanticDiff.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-rust-memory-mode-semantic-diff-no-std-alloc
  PROPERTIES
    LABELS "integration;uavcan;rust;slow;rust-no-std;rust-runtime-specialization;rust-memory-mode"
)

add_test(
  NAME llvmdsdl-uavcan-rust-determinism-inline-then-pool
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-rust-determinism-inline-then-pool-out
      -DRUST_MEMORY_MODE=inline-then-pool
      -DRUST_INLINE_THRESHOLD_BYTES=512
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanRustDeterminism.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-rust-determinism-inline-then-pool
  PROPERTIES
    LABELS "integration;uavcan;rust;slow;rust-memory-mode;determinism"
)

add_test(
  NAME llvmdsdl-uavcan-ts-generation
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-ts-out
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanTsGeneration.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-ts-generation
  PROPERTIES
    LABELS "integration;uavcan;ts;slow"
)

add_test(
  NAME llvmdsdl-uavcan-ts-generation-runtime-fast
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-ts-runtime-fast-out
      -DTS_RUNTIME_SPECIALIZATION=fast
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanTsGeneration.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-ts-generation-runtime-fast
  PROPERTIES
    LABELS "integration;uavcan;ts;slow;ts-runtime-specialization"
)

add_test(
  NAME llvmdsdl-uavcan-ts-runtime-specialization-diff
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-ts-runtime-specialization-diff-out
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunTsRuntimeSpecializationDiff.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-ts-runtime-specialization-diff
  PROPERTIES
    LABELS "integration;uavcan;ts;slow;ts-runtime-specialization"
)

add_test(
  NAME llvmdsdl-uavcan-ts-determinism
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-ts-determinism-out
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanTsDeterminism.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-ts-determinism
  PROPERTIES
    LABELS "integration;uavcan;ts;slow"
)

if(PYTHON_EXECUTABLE)
  add_test(
    NAME llvmdsdl-uavcan-python-generation
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-python-out
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanPythonGeneration.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-python-generation
    PROPERTIES
      LABELS "integration;uavcan;python;slow"
  )

  add_test(
    NAME llvmdsdl-uavcan-python-generation-runtime-fast
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-python-runtime-fast-out
        -DPY_RUNTIME_SPECIALIZATION=fast
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanPythonGeneration.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-python-generation-runtime-fast
    PROPERTIES
      LABELS "integration;uavcan;python;slow;python-runtime-specialization"
  )

  add_test(
    NAME llvmdsdl-uavcan-python-runtime-specialization-diff
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-python-runtime-specialization-diff-out
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunPythonRuntimeSpecializationDiff.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-python-runtime-specialization-diff
    PROPERTIES
      LABELS "integration;uavcan;python;slow;python-runtime-specialization"
  )

  add_test(
    NAME llvmdsdl-uavcan-python-determinism
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-python-determinism-out
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanPythonDeterminism.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-python-determinism
    PROPERTIES
      LABELS "integration;uavcan;python;slow"
  )

  add_test(
    NAME llvmdsdl-uavcan-python-runtime-bench
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-python-runtime-bench-out
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -DBENCH_SPECIALIZATIONS=${LLVMDSDL_PY_RUNTIME_BENCH_SPECIALIZATIONS_ARG}
        -DBENCH_ITERATIONS_SMALL=${LLVMDSDL_PY_RUNTIME_BENCH_ITERATIONS_SMALL}
        -DBENCH_ITERATIONS_MEDIUM=${LLVMDSDL_PY_RUNTIME_BENCH_ITERATIONS_MEDIUM}
        -DBENCH_ITERATIONS_LARGE=${LLVMDSDL_PY_RUNTIME_BENCH_ITERATIONS_LARGE}
        -DBENCH_ENABLE_THRESHOLDS=${LLVMDSDL_PY_RUNTIME_BENCH_ENABLE_THRESHOLDS}
        -DBENCH_THRESHOLDS_JSON=${LLVMDSDL_PY_RUNTIME_BENCH_THRESHOLDS_JSON}
        -DBENCH_REPORT_JSON=${CMAKE_CURRENT_BINARY_DIR}/uavcan-python-runtime-bench-out/python-runtime-bench.json
        -DACCEL_MODULE=$<$<TARGET_EXISTS:llvmdsdl-python-runtime-accelerator>:$<TARGET_FILE:llvmdsdl-python-runtime-accelerator>>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunPythonRuntimeBench.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-python-runtime-bench
    PROPERTIES
      LABELS "integration;uavcan;python;bench;slow;release-blocking"
  )

  add_test(
    NAME llvmdsdl-uavcan-python-runtime-execution
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-python-runtime-execution-out
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanPythonRuntimeExecution.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-python-runtime-execution
    PROPERTIES
      LABELS "integration;uavcan;python;runtime;slow"
  )

  add_test(
    NAME llvmdsdl-uavcan-python-runtime-execution-runtime-fast
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-python-runtime-execution-runtime-fast-out
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -DPY_RUNTIME_SPECIALIZATION=fast
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanPythonRuntimeExecution.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-python-runtime-execution-runtime-fast
    PROPERTIES
      LABELS "integration;uavcan;python;runtime;slow;python-runtime-specialization"
  )

  add_test(
    NAME llvmdsdl-uavcan-python-runtime-execution-optimized
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DDSDLC_EXTRA_ARGS=--optimize-lowered-serdes
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-python-runtime-execution-optimized-out
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanPythonRuntimeExecution.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-python-runtime-execution-optimized
    PROPERTIES
      LABELS "integration;uavcan;python;runtime;slow;optimized"
  )
else()
  message(STATUS "Skipping uavcan Python integration tests: python3 unavailable")
endif()

add_test(
  NAME llvmdsdl-uavcan-ts-index-contract
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-ts-index-contract-out
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanTsIndexContract.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-ts-index-contract
  PROPERTIES
    LABELS "integration;uavcan;ts;slow"
)

if(TSC_EXECUTABLE)
  add_test(
    NAME llvmdsdl-uavcan-ts-typecheck
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-ts-typecheck-out
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanTsTypecheck.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-ts-typecheck
    PROPERTIES
      LABELS "integration;uavcan;ts;slow"
  )

  add_test(
    NAME llvmdsdl-uavcan-ts-typecheck-runtime-fast
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-ts-typecheck-runtime-fast-out
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -DTS_RUNTIME_SPECIALIZATION=fast
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanTsTypecheck.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-ts-typecheck-runtime-fast
    PROPERTIES
      LABELS "integration;uavcan;ts;slow;ts-runtime-specialization"
  )

  add_test(
    NAME llvmdsdl-uavcan-ts-consumer-smoke
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-ts-consumer-smoke-out
        -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanTsConsumerSmoke.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-ts-consumer-smoke
    PROPERTIES
      LABELS "integration;uavcan;ts;slow"
  )

  if(NODE_EXECUTABLE)
    add_test(
      NAME llvmdsdl-uavcan-ts-runtime-execution-smoke
      COMMAND
        ${CMAKE_COMMAND}
          -DDSDLC=$<TARGET_FILE:dsdlc>
          -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
          -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-ts-runtime-execution-smoke-out
          -DTSC_EXECUTABLE=${TSC_EXECUTABLE}
          -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanTsRuntimeExecution.cmake
    )

    set_tests_properties(
      llvmdsdl-uavcan-ts-runtime-execution-smoke
      PROPERTIES
        LABELS "integration;uavcan;ts;slow"
    )
  endif()
endif()

add_test(
  NAME llvmdsdl-uavcan-go-generation
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-go-out
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanGoGeneration.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-go-generation
  PROPERTIES
    LABELS "integration;uavcan;go;go-differential"
)

add_test(
  NAME llvmdsdl-uavcan-go-determinism
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-go-determinism-out
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanGoDeterminism.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-go-determinism
  PROPERTIES
    LABELS "integration;uavcan;go;go-differential;slow"
)

find_program(CARGO_EXECUTABLE cargo QUIET)
find_program(RUSTC_EXECUTABLE rustc QUIET)
if(CARGO_EXECUTABLE AND RUSTC_EXECUTABLE)
  add_test(
    NAME llvmdsdl-fixtures-rust-runtime-unit-tests
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DFIXTURES_ROOT=${LLVMDSDL_FIXTURES_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-rust-runtime-unit-tests-out
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DRUST_PROFILE=std
        -DRUST_RUNTIME_SPECIALIZATION=portable
        -DRUST_MEMORY_MODE=max-inline
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunRustRuntimeUnitTests.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-rust-runtime-unit-tests
    PROPERTIES
      LABELS "integration;fixtures;rust;runtime;contract"
  )

  add_test(
    NAME llvmdsdl-fixtures-rust-runtime-unit-tests-no-std-pool
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DFIXTURES_ROOT=${LLVMDSDL_FIXTURES_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-rust-runtime-unit-tests-no-std-pool-out
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DRUST_PROFILE=no-std-alloc
        -DRUST_RUNTIME_SPECIALIZATION=fast
        -DRUST_MEMORY_MODE=inline-then-pool
        -DRUST_INLINE_THRESHOLD_BYTES=64
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunRustRuntimeUnitTests.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-rust-runtime-unit-tests-no-std-pool
    PROPERTIES
      LABELS "integration;fixtures;rust;runtime;contract;rust-no-std;rust-runtime-specialization;rust-memory-mode"
  )

  add_test(
    NAME llvmdsdl-fixtures-rust-runtime-bench
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures-rust-runtime-bench-out
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DRUST_BENCH_ROOT=${LLVMDSDL_SOURCE_DIR}/test/benchmark/rust_runtime/benchrust
        -DRUST_INLINE_THRESHOLD_BYTES=${LLVMDSDL_RUST_RUNTIME_BENCH_INLINE_THRESHOLD_BYTES}
        -DBENCH_ITERATIONS_SMALL=${LLVMDSDL_RUST_RUNTIME_BENCH_ITERATIONS_SMALL}
        -DBENCH_ITERATIONS_MEDIUM=${LLVMDSDL_RUST_RUNTIME_BENCH_ITERATIONS_MEDIUM}
        -DBENCH_ITERATIONS_LARGE=${LLVMDSDL_RUST_RUNTIME_BENCH_ITERATIONS_LARGE}
        -DBENCH_ENABLE_THRESHOLDS=${LLVMDSDL_RUST_RUNTIME_BENCH_ENABLE_THRESHOLDS}
        -DBENCH_THRESHOLDS_JSON=${LLVMDSDL_RUST_RUNTIME_BENCH_THRESHOLDS_JSON}
        -DBENCH_REPORT_JSON=${CMAKE_CURRENT_BINARY_DIR}/fixtures-rust-runtime-bench-out/rust-runtime-bench.json
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunRustRuntimeBench.cmake
  )

  set_tests_properties(
    llvmdsdl-fixtures-rust-runtime-bench
    PROPERTIES
      LABELS "integration;fixtures;rust;runtime;bench;slow;rust-memory-mode;release-blocking"
  )

  add_test(
    NAME llvmdsdl-uavcan-rust-cargo-check
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-rust-cargo-check-out
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanRustCargoCheck.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-rust-cargo-check
    PROPERTIES
      LABELS "integration;uavcan;rust;slow"
  )

  add_test(
    NAME llvmdsdl-uavcan-rust-cargo-check-no-std-alloc
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-rust-cargo-check-no-std-out
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DRUST_PROFILE=no-std-alloc
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanRustCargoCheck.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-rust-cargo-check-no-std-alloc
    PROPERTIES
      LABELS "integration;uavcan;rust;slow;rust-no-std"
  )

  add_test(
    NAME llvmdsdl-uavcan-rust-cargo-check-runtime-fast
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-rust-cargo-check-fast-out
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DRUST_RUNTIME_SPECIALIZATION=fast
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanRustCargoCheck.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-rust-cargo-check-runtime-fast
    PROPERTIES
      LABELS "integration;uavcan;rust;slow;rust-runtime-specialization"
  )

  add_test(
    NAME llvmdsdl-uavcan-rust-cargo-check-no-std-runtime-fast
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-rust-cargo-check-no-std-fast-out
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DRUST_PROFILE=no-std-alloc
        -DRUST_RUNTIME_SPECIALIZATION=fast
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanRustCargoCheck.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-rust-cargo-check-no-std-runtime-fast
    PROPERTIES
      LABELS "integration;uavcan;rust;slow;rust-no-std;rust-runtime-specialization"
  )

  add_test(
    NAME llvmdsdl-uavcan-rust-cargo-check-inline-then-pool
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-rust-cargo-check-inline-then-pool-out
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DRUST_MEMORY_MODE=inline-then-pool
        -DRUST_INLINE_THRESHOLD_BYTES=512
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanRustCargoCheck.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-rust-cargo-check-inline-then-pool
    PROPERTIES
      LABELS "integration;uavcan;rust;slow;rust-memory-mode"
  )

  add_test(
    NAME llvmdsdl-uavcan-rust-cargo-check-no-std-runtime-fast-inline-then-pool
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-rust-cargo-check-no-std-fast-inline-then-pool-out
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DRUST_PROFILE=no-std-alloc
        -DRUST_RUNTIME_SPECIALIZATION=fast
        -DRUST_MEMORY_MODE=inline-then-pool
        -DRUST_INLINE_THRESHOLD_BYTES=512
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanRustCargoCheck.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-rust-cargo-check-no-std-runtime-fast-inline-then-pool
    PROPERTIES
      LABELS "integration;uavcan;rust;slow;rust-no-std;rust-runtime-specialization;rust-memory-mode"
  )
endif()

if(GO_EXECUTABLE)
  add_test(
    NAME llvmdsdl-uavcan-go-build
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-go-build-out
        -DGO_EXECUTABLE=${GO_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanGoBuild.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-go-build
    PROPERTIES
      LABELS "integration;uavcan;go;go-differential;slow"
  )

  if(CMAKE_AR)
    add_test(
      NAME llvmdsdl-uavcan-c-go-parity
      COMMAND
        ${CMAKE_COMMAND}
          -DDSDLC=$<TARGET_FILE:dsdlc>
          -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
          -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-c-go-parity-out
          -DC_COMPILER=${CMAKE_C_COMPILER}
          -DAR_EXECUTABLE=${CMAKE_AR}
          -DGO_EXECUTABLE=${GO_EXECUTABLE}
          -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCGoParity.cmake
    )

    set_tests_properties(
      llvmdsdl-uavcan-c-go-parity
      PROPERTIES
        LABELS "integration;uavcan;go;differential;go-differential;slow"
    )

    add_test(
      NAME llvmdsdl-uavcan-c-go-parity-optimized
      COMMAND
        ${CMAKE_COMMAND}
          -DDSDLC=$<TARGET_FILE:dsdlc>
          -DDSDLC_EXTRA_ARGS=--optimize-lowered-serdes
          -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
          -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-c-go-parity-optimized-out
          -DC_COMPILER=${CMAKE_C_COMPILER}
          -DAR_EXECUTABLE=${CMAKE_AR}
          -DGO_EXECUTABLE=${GO_EXECUTABLE}
          -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCGoParity.cmake
    )

    set_tests_properties(
      llvmdsdl-uavcan-c-go-parity-optimized
      PROPERTIES
        LABELS "integration;uavcan;go;differential;go-differential;slow;optimized"
    )

    # ASan/UBSan variant of the C/Go parity harness: the generated C decoders
    # linked into the CGo binary are sanitized. Restricted to Linux + Clang
    # because threading the ASan runtime into a Go external link is only reliably
    # supported there; on other hosts the C/C++ sanitizer variant above provides
    # the equivalent generated-C decoder coverage.
    if(LLVMDSDL_NATIVE_HARNESS_SANITIZE_EFFECTIVE
       AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
       AND CMAKE_C_COMPILER_ID MATCHES "Clang")
      add_test(
        NAME llvmdsdl-uavcan-c-go-parity-sanitized
        COMMAND
          ${CMAKE_COMMAND}
            -DDSDLC=$<TARGET_FILE:dsdlc>
            -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
            -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-c-go-parity-sanitized-out
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DAR_EXECUTABLE=${CMAKE_AR}
            -DGO_EXECUTABLE=${GO_EXECUTABLE}
            -DSANITIZE=${LLVMDSDL_NATIVE_HARNESS_SANITIZE_EFFECTIVE}
            -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCGoParity.cmake
      )

      set_tests_properties(
        llvmdsdl-uavcan-c-go-parity-sanitized
        PROPERTIES
          LABELS "integration;uavcan;go;differential;go-differential;slow;sanitizer"
      )
    endif()
  endif()
endif()

add_test(
  NAME llvmdsdl-uavcan-c-determinism
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-c-determinism-out
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanCDeterminism.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-c-determinism
  PROPERTIES
    LABELS "integration;uavcan;c;slow;determinism"
)

add_test(
  NAME llvmdsdl-uavcan-cpp-generation
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-cpp-out
      -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanCppGeneration.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-cpp-generation
  PROPERTIES
    LABELS "integration;uavcan;cpp;slow"
)

add_test(
  NAME llvmdsdl-uavcan-cpp-determinism
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-cpp-determinism-out
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunUavcanCppDeterminism.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-cpp-determinism
  PROPERTIES
    LABELS "integration;uavcan;cpp;slow;determinism"
)

add_test(
  NAME llvmdsdl-uavcan-cpp-c-parity
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-cpp-c-parity-out
      -DC_COMPILER=${CMAKE_C_COMPILER}
      -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DCPP_PROFILE=std
      -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCppCParity.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-cpp-c-parity
  PROPERTIES
    LABELS "integration;uavcan;cpp;differential;slow"
)

# ASan/UBSan variant of the C/C++ decoder parity harness. Reuses the same
# randomized + directed malformed-input harness but rebuilds the generated C and
# C++ decoders (and the harness main) under sanitizers, so memory/UB defects in
# the generated decode path surface as a hard failure. Registered only when a
# sanitizer flavor is active (ci-asan preset) and the toolchain is Clang, which
# is what -fsanitize=address,undefined requires.
if(LLVMDSDL_NATIVE_HARNESS_SANITIZE_EFFECTIVE
   AND CMAKE_C_COMPILER_ID MATCHES "Clang|AppleClang"
   AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
  add_test(
    NAME llvmdsdl-uavcan-cpp-c-parity-sanitized
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-cpp-c-parity-sanitized-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCPP_PROFILE=std
        -DSANITIZE=${LLVMDSDL_NATIVE_HARNESS_SANITIZE_EFFECTIVE}
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCppCParity.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-cpp-c-parity-sanitized
    PROPERTIES
      LABELS "integration;uavcan;cpp;differential;slow;sanitizer"
  )
endif()

add_test(
  NAME llvmdsdl-uavcan-cpp-c-parity-optimized
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DDSDLC_EXTRA_ARGS=--optimize-lowered-serdes
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-cpp-c-parity-optimized-out
      -DC_COMPILER=${CMAKE_C_COMPILER}
      -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DCPP_PROFILE=std
      -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCppCParity.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-cpp-c-parity-optimized
  PROPERTIES
    LABELS "integration;uavcan;cpp;differential;slow;optimized"
)

add_test(
  NAME llvmdsdl-uavcan-cpp-pmr-c-parity
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-cpp-pmr-c-parity-out
      -DC_COMPILER=${CMAKE_C_COMPILER}
      -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DCPP_PROFILE=pmr
      -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCppCParity.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-cpp-pmr-c-parity
  PROPERTIES
    LABELS "integration;uavcan;cpp;differential;slow"
)

add_test(
  NAME llvmdsdl-uavcan-cpp-pmr-c-parity-optimized
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DDSDLC_EXTRA_ARGS=--optimize-lowered-serdes
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-cpp-pmr-c-parity-optimized-out
      -DC_COMPILER=${CMAKE_C_COMPILER}
      -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DCPP_PROFILE=pmr
      -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCppCParity.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-cpp-pmr-c-parity-optimized
  PROPERTIES
    LABELS "integration;uavcan;cpp;differential;slow;optimized"
)

add_test(
  NAME llvmdsdl-uavcan-cpp-autosar-c-parity
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-cpp-autosar-c-parity-out
      -DC_COMPILER=${CMAKE_C_COMPILER}
      -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DCPP_PROFILE=autosar
      -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCppCParity.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-cpp-autosar-c-parity
  PROPERTIES
    LABELS "integration;uavcan;cpp;differential;slow;autosar"
)

add_test(
  NAME llvmdsdl-uavcan-cpp-autosar-c-parity-optimized
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DDSDLC_EXTRA_ARGS=--optimize-lowered-serdes
      -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
      -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-cpp-autosar-c-parity-optimized-out
      -DC_COMPILER=${CMAKE_C_COMPILER}
      -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DCPP_PROFILE=autosar
      -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCppCParity.cmake
)

set_tests_properties(
  llvmdsdl-uavcan-cpp-autosar-c-parity-optimized
  PROPERTIES
    LABELS "integration;uavcan;cpp;differential;slow;optimized;autosar"
)

if(CARGO_EXECUTABLE AND RUSTC_EXECUTABLE)
  add_test(
    NAME llvmdsdl-uavcan-c-rust-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-c-rust-parity-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DAR_EXECUTABLE=${CMAKE_AR}
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCRustParity.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-c-rust-parity
    PROPERTIES
      LABELS "integration;uavcan;rust;differential;slow"
  )

  add_test(
    NAME llvmdsdl-uavcan-c-rust-parity-optimized
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DDSDLC_EXTRA_ARGS=--optimize-lowered-serdes
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-c-rust-parity-optimized-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DAR_EXECUTABLE=${CMAKE_AR}
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCRustParity.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-c-rust-parity-optimized
    PROPERTIES
      LABELS "integration;uavcan;rust;differential;slow;optimized"
  )

  add_test(
    NAME llvmdsdl-uavcan-c-rust-parity-no-std-alloc
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DRUST_PROFILE=no-std-alloc
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-c-rust-parity-no-std-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DAR_EXECUTABLE=${CMAKE_AR}
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCRustParity.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-c-rust-parity-no-std-alloc
    PROPERTIES
      LABELS "integration;uavcan;rust;differential;slow;rust-no-std"
  )

  add_test(
    NAME llvmdsdl-uavcan-c-rust-parity-runtime-fast
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DRUST_RUNTIME_SPECIALIZATION=fast
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-c-rust-parity-runtime-fast-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DAR_EXECUTABLE=${CMAKE_AR}
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCRustParity.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-c-rust-parity-runtime-fast
    PROPERTIES
      LABELS "integration;uavcan;rust;differential;slow;rust-runtime-specialization"
  )

  add_test(
    NAME llvmdsdl-uavcan-c-rust-parity-no-std-runtime-fast
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DRUST_PROFILE=no-std-alloc
        -DRUST_RUNTIME_SPECIALIZATION=fast
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-c-rust-parity-no-std-fast-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DAR_EXECUTABLE=${CMAKE_AR}
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCRustParity.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-c-rust-parity-no-std-runtime-fast
    PROPERTIES
      LABELS "integration;uavcan;rust;differential;slow;rust-no-std;rust-runtime-specialization"
  )

  add_test(
    NAME llvmdsdl-uavcan-c-rust-parity-inline-then-pool
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DRUST_MEMORY_MODE=inline-then-pool
        -DRUST_INLINE_THRESHOLD_BYTES=512
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-c-rust-parity-inline-then-pool-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DAR_EXECUTABLE=${CMAKE_AR}
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCRustParity.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-c-rust-parity-inline-then-pool
    PROPERTIES
      LABELS "integration;uavcan;rust;differential;slow;rust-memory-mode"
  )

  add_test(
    NAME llvmdsdl-uavcan-c-rust-parity-no-std-runtime-fast-inline-then-pool
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DRUST_PROFILE=no-std-alloc
        -DRUST_RUNTIME_SPECIALIZATION=fast
        -DRUST_MEMORY_MODE=inline-then-pool
        -DRUST_INLINE_THRESHOLD_BYTES=512
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/uavcan-c-rust-parity-no-std-fast-inline-then-pool-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DAR_EXECUTABLE=${CMAKE_AR}
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCRustParity.cmake
  )

  set_tests_properties(
    llvmdsdl-uavcan-c-rust-parity-no-std-runtime-fast-inline-then-pool
    PROPERTIES
      LABELS "integration;uavcan;rust;differential;slow;rust-no-std;rust-runtime-specialization;rust-memory-mode"
  )
else()
  message(STATUS "Skipping C/Rust parity test: cargo/rustc executable not found")
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
# Overridable so CI (or any environment) can point the Nunavut differential
# parity at explicit checkouts instead of the default sibling repos; the harness
# consumes these as source trees via PYTHONPATH (no pip install required).
set(LLVMDSDL_NUNAVUT_REPO "${LLVMDSDL_SOURCE_DIR}/../nunavut"
    CACHE PATH "Path to a nunavut source checkout for the differential parity test")
set(LLVMDSDL_PYDSDL_REPO "${LLVMDSDL_SOURCE_DIR}/../pydsdl"
    CACHE PATH "Path to a pydsdl source checkout for the differential parity test")
# When ON, a missing prerequisite is a hard configure error instead of a silent
# skip, so CI cannot report a green run while the reference-parity lane was never
# built (local green != false confidence).
option(LLVMDSDL_REQUIRE_DIFFERENTIAL_PARITY
    "Fail configuration if the Nunavut differential parity prerequisites are missing" OFF)

set(_diff_parity_missing "")
if(NOT Python3_Interpreter_FOUND)
  list(APPEND _diff_parity_missing "Python3 interpreter")
endif()
if(NOT EXISTS "${LLVMDSDL_NUNAVUT_REPO}/src/nunavut")
  list(APPEND _diff_parity_missing "nunavut source tree (${LLVMDSDL_NUNAVUT_REPO}/src/nunavut)")
endif()
if(NOT EXISTS "${LLVMDSDL_PYDSDL_REPO}/pydsdl")
  list(APPEND _diff_parity_missing "pydsdl source tree (${LLVMDSDL_PYDSDL_REPO}/pydsdl)")
endif()

if(_diff_parity_missing STREQUAL "")
  add_test(
    NAME llvmdsdl-differential-parity
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/differential-parity-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DNUNAVUT_REPO=${LLVMDSDL_NUNAVUT_REPO}
        -DPYDSDL_REPO=${LLVMDSDL_PYDSDL_REPO}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunDifferentialParity.cmake
  )

  set_tests_properties(
    llvmdsdl-differential-parity
    PROPERTIES
      LABELS "integration;uavcan;differential;slow"
  )

  add_test(
    NAME llvmdsdl-differential-parity-optimized
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DDSDLC_EXTRA_ARGS=--optimize-lowered-serdes
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/differential-parity-optimized-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DNUNAVUT_REPO=${LLVMDSDL_NUNAVUT_REPO}
        -DPYDSDL_REPO=${LLVMDSDL_PYDSDL_REPO}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunDifferentialParity.cmake
  )

  set_tests_properties(
    llvmdsdl-differential-parity-optimized
    PROPERTIES
      LABELS "integration;uavcan;differential;slow;optimized"
  )
else()
  string(REPLACE ";" ", " _diff_parity_missing_str "${_diff_parity_missing}")
  if(LLVMDSDL_REQUIRE_DIFFERENTIAL_PARITY)
    message(FATAL_ERROR
      "Differential parity is required (LLVMDSDL_REQUIRE_DIFFERENTIAL_PARITY=ON) but "
      "prerequisites are missing: ${_diff_parity_missing_str}. Provide a nunavut/pydsdl "
      "checkout via -DLLVMDSDL_NUNAVUT_REPO=/path -DLLVMDSDL_PYDSDL_REPO=/path.")
  else()
    message(STATUS
      "Skipping differential parity test; missing: ${_diff_parity_missing_str}. "
      "Set -DLLVMDSDL_REQUIRE_DIFFERENTIAL_PARITY=ON to make this a hard error.")
  endif()
endif()

# --- Native-decoder libFuzzer lane (ASan/UBSan over generated C decoders) ----
# Registered only when explicitly enabled (the ci-asan preset) and the compiler
# is Clang, since the harness requires -fsanitize=address,undefined and, for
# coverage-guided mode, compiler-rt's libFuzzer. The harness itself degrades to
# an ASan/UBSan corpus replay when libFuzzer is unavailable, so the lane is
# meaningful on any Clang; it is never a silent no-op.
if(LLVMDSDL_ENABLE_NATIVE_DECODER_FUZZ
   AND CMAKE_C_COMPILER_ID MATCHES "Clang|AppleClang")
  add_test(
    NAME llvmdsdl-native-decoder-fuzz
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DUAVCAN_ROOT=${LLVMDSDL_UAVCAN_ROOT}
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/native-decoder-fuzz-out
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DSANITIZE=${LLVMDSDL_NATIVE_HARNESS_SANITIZE_EFFECTIVE}
        -DFUZZ_RUNS=${LLVMDSDL_NATIVE_DECODER_FUZZ_RUNS}
        -DCORPUS_DIR=${LLVMDSDL_SOURCE_DIR}/test/fuzz/corpus/native_decoder
        -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunNativeDecoderFuzz.cmake
  )

  set_tests_properties(
    llvmdsdl-native-decoder-fuzz
    PROPERTIES
      LABELS "integration;uavcan;fuzz;sanitizer;slow"
      TIMEOUT 2400
  )
endif()
