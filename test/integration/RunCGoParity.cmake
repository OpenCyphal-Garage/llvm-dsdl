cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC UAVCAN_ROOT OUT_DIR C_COMPILER AR_EXECUTABLE GO_EXECUTABLE SOURCE_ROOT)
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

if(NOT EXISTS "${AR_EXECUTABLE}")
  message(FATAL_ERROR "archive tool not found: ${AR_EXECUTABLE}")
endif()

if(NOT EXISTS "${GO_EXECUTABLE}")
  message(FATAL_ERROR "go executable not found: ${GO_EXECUTABLE}")
endif()

set(go_mod_template "${SOURCE_ROOT}/test/integration/CGoParityGo.mod.in")
set(main_go_template "${SOURCE_ROOT}/test/integration/CGoParityMain.go")
set(c_harness_template "${SOURCE_ROOT}/test/integration/CGoParityCHarness.c")
foreach(path "${go_mod_template}" "${main_go_template}" "${c_harness_template}")
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "C/Go parity harness input missing: ${path}")
  endif()
endforeach()

file(STRINGS "${c_harness_template}" c_wrapper_lines
     REGEX "^DEFINE_ROUNDTRIP\\(c_[A-Za-z0-9_]+_roundtrip,")
list(LENGTH c_wrapper_lines expected_case_count)
if(expected_case_count EQUAL 0)
  message(FATAL_ERROR "failed to discover DEFINE_ROUNDTRIP wrappers in ${c_harness_template}")
endif()

set(dsdlc_extra_args "")
if(DEFINED DSDLC_EXTRA_ARGS AND NOT "${DSDLC_EXTRA_ARGS}" STREQUAL "")
  separate_arguments(dsdlc_extra_args NATIVE_COMMAND "${DSDLC_EXTRA_ARGS}")
endif()

# Optional sanitizer instrumentation of the C side of the CGo parity harness.
# SANITIZE is a comma-separated -fsanitize list (e.g. "address,undefined"); the
# generated C decoders + C harness are compiled with it, and the sanitizer
# runtime is threaded into the CGo external link via -extldflags so ASan/UBSan
# observe the C decode path over the harness's randomized + directed malformed
# inputs. NOTE: the generated *Go* decoders are memory-safe by construction (a
# malformed payload panics rather than corrupting memory, and the harness asserts
# C<->Go parity, so any panic already fails the test); ASan/UBSan therefore only
# instrument the C bindings here. Go-native fault safety is covered by the parity
# assertions, and could be deepened later with `go test -fuzz`/`-race`.
set(san_compile_flags "")
set(san_ext_ldflags "")
if(DEFINED SANITIZE AND NOT "${SANITIZE}" STREQUAL "")
  set(san_compile_flags
      "-fsanitize=${SANITIZE}"
      -fno-sanitize-recover=all
      -fno-omit-frame-pointer
      -g)
  set(san_ext_ldflags "-fsanitize=${SANITIZE}")
  message(STATUS "C/Go parity harness (C side) sanitized with: ${SANITIZE}")
endif()

# Keeping the scratch tree is opt-in, because the tree is large and the lane is usually run for its
# verdict rather than its output. It is the only way to see what the harness was actually compiled
# against: on success the run directory is deleted, and on failure the surviving one is easy to
# mistake for a current one, since nothing here used to clear the old ones.
#
# Settable either way. The environment variable is the useful one, because it reaches the script
# through ctest without editing a registration:
#
#   LLVMDSDL_KEEP_RUN_OUTPUT=1 ctest --test-dir <build> -R llvmdsdl-uavcan-c-go-parity
#
if(NOT DEFINED KEEP_RUN_OUTPUT)
  set(KEEP_RUN_OUTPUT "$ENV{LLVMDSDL_KEEP_RUN_OUTPUT}")
endif()

file(MAKE_DIRECTORY "${OUT_DIR}")
foreach(stale_dir c go build harness .gocache .gomodcache)
  if(EXISTS "${OUT_DIR}/${stale_dir}")
    file(REMOVE_RECURSE "${OUT_DIR}/${stale_dir}")
  endif()
endforeach()
# Previous run directories go too. A failed run leaves its tree behind by design, and without this
# they accumulate silently -- which is how a July directory came to sit beside an August one, both
# looking equally current to anyone reading the output.
file(GLOB stale_runs "${OUT_DIR}/run-*")
foreach(stale_run IN LISTS stale_runs)
  if(IS_DIRECTORY "${stale_run}")
    file(REMOVE_RECURSE "${stale_run}")
  endif()
endforeach()
string(TIMESTAMP parity_run_timestamp "%Y%m%d%H%M%S")
string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef parity_run_nonce)
set(run_out "${OUT_DIR}/run-${parity_run_timestamp}-${parity_run_nonce}")
file(MAKE_DIRECTORY "${run_out}")

set(c_out "${run_out}/c")
set(go_out "${run_out}/go")
set(build_out "${run_out}/build")
set(harness_out "${run_out}/harness")
file(MAKE_DIRECTORY "${c_out}")
file(MAKE_DIRECTORY "${go_out}")
file(MAKE_DIRECTORY "${build_out}")
file(MAKE_DIRECTORY "${harness_out}")

# Go used to have no choice here -- uavcan carries 20 types at more than one version and a DSDL
# namespace is one Go package, so unversioned names could not be generated at all. The newest
# version of each type is now the default, so this harness runs with no naming flags. The
# harness names no version that the default drops, which is what makes that safe.
include("${SOURCE_ROOT}/cmake/HarnessTypeNameTokens.cmake")
llvmdsdl_harness_naming_scheme(C_DEFAULT "unversioned" OTHER_DEFAULT "unversioned")

execute_process(
  COMMAND
    "${DSDLC}" --target-language c ${c_scheme_args}
      "${UAVCAN_ROOT}"
      ${dsdlc_extra_args}
      --outdir "${c_out}"
  RESULT_VARIABLE c_result
  OUTPUT_VARIABLE c_stdout
  ERROR_VARIABLE c_stderr
)
if(NOT c_result EQUAL 0)
  message(STATUS "dsdlc c stdout:\n${c_stdout}")
  message(STATUS "dsdlc c stderr:\n${c_stderr}")
  message(FATAL_ERROR "failed to generate C output for C/Go parity harness")
endif()

execute_process(
  COMMAND
    "${DSDLC}" --target-language go ${other_scheme_args}
      "${UAVCAN_ROOT}"
      ${dsdlc_extra_args}
      --outdir "${go_out}"
      --go-module "uavcan_dsdl_generated"
  RESULT_VARIABLE go_result
  OUTPUT_VARIABLE go_stdout
  ERROR_VARIABLE go_stderr
)
if(NOT go_result EQUAL 0)
  message(STATUS "dsdlc go stdout:\n${go_stdout}")
  message(STATUS "dsdlc go stderr:\n${go_stderr}")
  message(FATAL_ERROR "failed to generate Go output for C/Go parity harness")
endif()

set(GO_OUT "${go_out}")
configure_file("${go_mod_template}" "${harness_out}/go.mod" @ONLY)
configure_file("${main_go_template}" "${harness_out}/main.go" @ONLY)
set(c_harness_src "${build_out}/c_harness.c")
configure_file("${c_harness_template}" "${c_harness_src}" @ONLY)

# The harness round-trips every type in the corpus, deprecated ones included, so it trips the
# deprecation attributes the C backend emits by default. That diagnostic is working as intended --
# this is user code naming a deprecated type -- and it is not what this test is measuring, so it is
# silenced here only. The generated sources below stay strict: nothing there may warn.
set(harness_obj "${build_out}/c_harness.o")
execute_process(
  COMMAND
    "${C_COMPILER}"
      -std=c11
      -Wall
      -Wextra
      -Werror
      -Wno-deprecated-declarations
      ${san_compile_flags}
      -I "${c_out}"
      -c "${c_harness_src}"
      -o "${harness_obj}"
  RESULT_VARIABLE harness_cc_result
  OUTPUT_VARIABLE harness_cc_stdout
  ERROR_VARIABLE harness_cc_stderr
)
if(NOT harness_cc_result EQUAL 0)
  message(STATUS "C harness compile stdout:\n${harness_cc_stdout}")
  message(STATUS "C harness compile stderr:\n${harness_cc_stderr}")
  message(FATAL_ERROR "failed to compile C/Go parity C harness")
endif()

file(GLOB_RECURSE generated_c_sources "${c_out}/*.c")
list(LENGTH generated_c_sources generated_c_count)
if(generated_c_count EQUAL 0)
  message(FATAL_ERROR "no generated C implementation sources under ${c_out}")
endif()

set(generated_obj_dir "${build_out}/generated-obj")
file(MAKE_DIRECTORY "${generated_obj_dir}")
set(generated_objs "")
set(c_index 0)
foreach(src IN LISTS generated_c_sources)
  math(EXPR c_index "${c_index} + 1")
  set(obj "${generated_obj_dir}/generated_${c_index}.o")
  execute_process(
    COMMAND
      "${C_COMPILER}"
        -std=c11
        -Wall
        -Wextra
        -Werror
        ${san_compile_flags}
        -I "${c_out}"
        -c "${src}"
        -o "${obj}"
    RESULT_VARIABLE c_cc_result
    OUTPUT_VARIABLE c_cc_stdout
    ERROR_VARIABLE c_cc_stderr
  )
  if(NOT c_cc_result EQUAL 0)
    message(STATUS "failed source: ${src}")
    message(STATUS "generated C compile stdout:\n${c_cc_stdout}")
    message(STATUS "generated C compile stderr:\n${c_cc_stderr}")
    message(FATAL_ERROR "failed to compile generated C implementation for C/Go parity")
  endif()
  list(APPEND generated_objs "${obj}")
endforeach()

set(static_lib "${build_out}/libllvmdsdl_c_go_parity.a")
execute_process(
  COMMAND "${AR_EXECUTABLE}" rcs "${static_lib}" "${harness_obj}" ${generated_objs}
  RESULT_VARIABLE ar_result
  OUTPUT_VARIABLE ar_stdout
  ERROR_VARIABLE ar_stderr
)
if(NOT ar_result EQUAL 0)
  message(STATUS "archive stdout:\n${ar_stdout}")
  message(STATUS "archive stderr:\n${ar_stderr}")
  message(FATAL_ERROR "failed to archive C/Go parity support library")
endif()
if(NOT EXISTS "${static_lib}")
  message(FATAL_ERROR "C/Go parity archive missing after creation: ${static_lib}")
endif()

set(go_cache "${run_out}/.gocache")
set(go_mod_cache "${run_out}/.gomodcache")
set(ext_ldflags "${static_lib}")
if(NOT "${san_ext_ldflags}" STREQUAL "")
  # Pull the sanitizer runtime/interceptors into the CGo external link so the
  # ASan/UBSan instrumentation compiled into the C archive resolves at link time.
  set(ext_ldflags "${ext_ldflags} ${san_ext_ldflags}")
endif()
set(go_ldflags "-extldflags '${ext_ldflags}'")
execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env
      "CC=${C_COMPILER}"
      "CGO_ENABLED=1"
      "GOCACHE=${go_cache}"
      "GOMODCACHE=${go_mod_cache}"
      "${GO_EXECUTABLE}" run -ldflags "${go_ldflags}" . 128
  WORKING_DIRECTORY "${harness_out}"
  RESULT_VARIABLE run_result
  OUTPUT_VARIABLE run_stdout
  ERROR_VARIABLE run_stderr
)
if(NOT run_result EQUAL 0)
  message(STATUS "go run stdout:\n${run_stdout}")
  message(STATUS "go run stderr:\n${run_stderr}")
  message(FATAL_ERROR "C/Go parity harness reported mismatches")
endif()

set(min_random 128)
set(min_cases 109)
set(min_directed 265)
string(REGEX MATCH
  "PASS c/go parity random_iterations=([0-9]+) random_cases=([0-9]+) directed_cases=([0-9]+)"
  parity_summary_line
  "${run_stdout}")
if(NOT parity_summary_line)
  message(FATAL_ERROR
    "failed to parse C/Go parity summary line from harness output")
endif()
set(observed_random "${CMAKE_MATCH_1}")
set(observed_cases "${CMAKE_MATCH_2}")
set(observed_directed "${CMAKE_MATCH_3}")
if(observed_random LESS min_random)
  message(FATAL_ERROR
    "C/Go parity random-iteration regression: observed=${observed_random}, required>=${min_random}")
endif()
if(observed_cases LESS min_cases)
  message(FATAL_ERROR
    "C/Go parity case count regression: observed=${observed_cases}, required>=${min_cases}")
endif()
if(NOT observed_cases EQUAL expected_case_count)
  message(FATAL_ERROR
    "C/Go parity case/wrapper mismatch: observed cases=${observed_cases}, "
    "DEFINE_ROUNDTRIP wrappers=${expected_case_count}")
endif()
if(observed_directed LESS min_directed)
  message(FATAL_ERROR
    "C/Go parity directed count regression: observed=${observed_directed}, required>=${min_directed}")
endif()

string(REGEX MATCHALL
  "PASS [A-Za-z0-9_]+ random \\([0-9]+ iterations\\)"
  random_pass_lines
  "${run_stdout}")
list(LENGTH random_pass_lines observed_random_pass_lines)
if(NOT observed_random_pass_lines EQUAL observed_cases)
  message(FATAL_ERROR
    "random case execution count mismatch: pass-lines=${observed_random_pass_lines}, "
    "observed cases=${observed_cases}")
endif()

string(REGEX MATCHALL
  "PASS [A-Za-z0-9_]+ directed"
  directed_pass_lines
  "${run_stdout}")
set(directed_vector_pass_lines "${directed_pass_lines}")
list(FILTER directed_vector_pass_lines EXCLUDE REGEX "^PASS (real16_nan_vector|scalar_real32_signaling_nan_payload) directed$")
list(LENGTH directed_vector_pass_lines observed_directed_vector_pass_lines)
if(NOT observed_directed_vector_pass_lines EQUAL observed_directed)
  message(FATAL_ERROR
    "directed vector execution count mismatch: pass-lines=${observed_directed_vector_pass_lines}, "
    "observed directed=${observed_directed}")
endif()

string(REGEX MATCH
  "PASS c/go inventory random_cases=([0-9]+) directed_cases=([0-9]+)"
  inventory_summary_match
  "${run_stdout}")
if(NOT inventory_summary_match)
  message(FATAL_ERROR "missing parity inventory summary marker")
endif()
set(inventory_cases "${CMAKE_MATCH_1}")
set(inventory_directed "${CMAKE_MATCH_2}")
if(NOT inventory_cases EQUAL observed_cases OR
   NOT inventory_directed EQUAL observed_directed)
  message(FATAL_ERROR
    "parity inventory summary mismatch: inventory cases=${inventory_cases}, "
    "inventory directed=${inventory_directed}, observed cases=${observed_cases}, "
    "observed directed=${observed_directed}")
endif()

string(REGEX MATCH
  "PASS directed baseline auto_added=([0-9]+)"
  directed_baseline_match
  "${run_stdout}")
if(NOT directed_baseline_match)
  message(FATAL_ERROR "missing directed baseline auto-coverage marker")
endif()
set(observed_auto_added "${CMAKE_MATCH_1}")
if(observed_auto_added LESS 1)
  message(FATAL_ERROR
    "directed baseline auto-coverage unexpectedly disabled: auto_added=${observed_auto_added}")
endif()

string(REGEX MATCH
  "PASS directed coverage any=([0-9]+) truncation=([0-9]+) serialize_buffer=([0-9]+)"
  directed_coverage_match
  "${run_stdout}")
if(NOT directed_coverage_match)
  message(FATAL_ERROR "missing directed coverage summary marker")
endif()
set(coverage_any "${CMAKE_MATCH_1}")
set(coverage_truncation "${CMAKE_MATCH_2}")
set(coverage_serialize_buffer "${CMAKE_MATCH_3}")
if(NOT coverage_any EQUAL observed_cases OR
   NOT coverage_truncation EQUAL observed_cases OR
   NOT coverage_serialize_buffer EQUAL observed_cases)
  message(FATAL_ERROR
    "directed coverage mismatch: any=${coverage_any}, truncation=${coverage_truncation}, "
    "serialize_buffer=${coverage_serialize_buffer}, cases=${observed_cases}")
endif()

set(required_random_category_mins
  "message_section:44"
  "service_section:26"
  "union_delimited:7"
  "variable_composite:5"
  "primitive_scalar:10"
  "primitive_array:10"
  "primitive_composite:3"
)
foreach(spec IN LISTS required_random_category_mins)
  string(REPLACE ":" ";" parts "${spec}")
  list(GET parts 0 key)
  list(GET parts 1 min_value)
  string(REGEX MATCH "${key}=([0-9]+)" key_match "${run_stdout}")
  if(NOT key_match)
    message(FATAL_ERROR
      "missing random parity category count: ${key}")
  endif()
  set(observed_value "${CMAKE_MATCH_1}")
  if(observed_value LESS min_value)
    message(FATAL_ERROR
      "random parity category regression for ${key}: observed=${observed_value}, required>=${min_value}")
  endif()
endforeach()

set(required_random_markers
  "PASS heartbeat random"
  "PASS execute_command_request random"
  "PASS register_value random"
  "PASS node_id random"
  "PASS node_mode random"
  "PASS node_version random"
  "PASS node_io_statistics random"
  "PASS time_system random"
  "PASS can_data_classic random"
  "PASS can_data_fd random"
  "PASS can_manifestation random"
  "PASS can_arbitration_id random"
  "PASS node_port_subject_id_list random"
  "PASS node_port_id random"
  "PASS node_port_service_id random"
  "PASS file_error random"
  "PASS si_unit_acceleration_vector3 random"
  "PASS si_unit_force_vector3 random"
  "PASS si_unit_torque_vector3 random"
  "PASS si_unit_velocity_vector3 random"
  "PASS si_sample_acceleration_vector3 random"
  "PASS si_sample_force_vector3 random"
  "PASS si_sample_torque_vector3 random"
  "PASS si_sample_temperature_scalar random"
  "PASS si_sample_voltage_scalar random"
  "PASS metatransport_ethernet_ethertype random"
  "PASS register_name random"
  "PASS register_list_request random"
  "PASS file_write_request random"
  "PASS file_modify_request random"
  "PASS file_get_info_request random"
  "PASS get_transport_statistics_request random"
  "PASS get_sync_master_info_request random"
  "PASS pnp_cluster_append_entries_request random"
  "PASS pnp_cluster_request_vote_response random"
  "PASS pnp_cluster_discovery random"
  "PASS metatransport_udp_endpoint random"
  "PASS metatransport_serial_fragment random"
  "PASS metatransport_ethernet_frame random"
  "PASS metatransport_udp_frame random"
  "PASS node_port_list random"
)
foreach(marker IN LISTS required_random_markers)
  string(FIND "${run_stdout}" "${marker}" marker_pos)
  if(marker_pos EQUAL -1)
    message(FATAL_ERROR
      "required random parity marker missing: ${marker}")
  endif()
endforeach()

set(required_directed_category_mins
  "union_tag_error:7"
  "delimiter_error:2"
  "length_prefix_error:25"
  "truncation:109"
  "float_nan:5"
  "serialize_buffer:109"
  "high_bits_normalization:10"
)
foreach(spec IN LISTS required_directed_category_mins)
  string(REPLACE ":" ";" parts "${spec}")
  list(GET parts 0 key)
  list(GET parts 1 min_value)
  string(REGEX MATCH "${key}=([0-9]+)" key_match "${run_stdout}")
  if(NOT key_match)
    message(FATAL_ERROR
      "missing directed parity category count: ${key}")
  endif()
  set(observed_value "${CMAKE_MATCH_1}")
  if(observed_value LESS min_value)
    message(FATAL_ERROR
      "directed parity category regression for ${key}: observed=${observed_value}, required>=${min_value}")
  endif()
endforeach()

string(REGEX MATCH "misc=([0-9]+)" misc_match "${run_stdout}")
if(misc_match)
  if(CMAKE_MATCH_1 GREATER 0)
    message(FATAL_ERROR
      "directed parity includes uncategorized vectors (misc=${CMAKE_MATCH_1}); update classifyDirectedVector()")
  endif()
endif()

set(required_directed_markers
  # Union-tag validation.
  "PASS register_value_invalid_union_tag directed"
  # Delimiter bounds validation.
  "PASS port_list_bad_delimiter_header directed"
  # Float/NaN path parity.
  "PASS scalar_real64_nan_payload directed"
  "PASS scalar_real32_signaling_nan_payload directed"
  # Truncation/zero-extension coverage.
  "PASS scalar_integer64_truncated_input directed"
  "PASS scalar_natural8_truncated_input directed"
  "PASS array_integer32_truncated_payload directed"
  "PASS array_natural32_truncated_payload directed"
  "PASS array_integer64_truncated_payload directed"
  "PASS node_id_truncated_input directed"
  "PASS register_name_truncated_input directed"
  "PASS can_error_truncated_input directed"
  "PASS file_write_request_truncated_input directed"
  "PASS file_modify_request_truncated_input directed"
  "PASS file_get_info_request_truncated_input directed"
  "PASS get_transport_statistics_request_truncated_input directed"
  "PASS node_version_truncated_input directed"
  "PASS node_io_statistics_truncated_input directed"
  "PASS time_tai_info_truncated_input directed"
  "PASS get_sync_master_info_request_truncated_input directed"
  # Serialize-capacity error parity.
  "PASS heartbeat_serialize_small_buffer directed"
  "PASS file_path_serialize_small_buffer directed"
  "PASS node_id_serialize_small_buffer directed"
  "PASS metatransport_udp_endpoint_serialize_small_buffer directed"
  "PASS file_write_response_serialize_small_buffer directed"
  "PASS file_modify_response_serialize_small_buffer directed"
  "PASS file_get_info_response_serialize_small_buffer directed"
  "PASS get_transport_statistics_response_serialize_small_buffer directed"
  "PASS get_transport_statistics_response_bad_length_prefix directed"
  "PASS node_io_statistics_serialize_small_buffer directed"
  "PASS can_data_classic_bad_length_prefix directed"
  "PASS can_data_classic_serialize_small_buffer directed"
  "PASS node_health_high_bits_input directed"
  "PASS time_system_high_bits_input directed"
  "PASS get_sync_master_info_response_serialize_small_buffer directed"
  "PASS register_list_response_serialize_small_buffer directed"
  "PASS can_manifestation_invalid_union_tag directed"
  "PASS can_arbitration_id_invalid_union_tag directed"
  "PASS node_port_subject_id_list_invalid_union_tag directed"
  "PASS node_port_id_invalid_union_tag directed"
  "PASS can_data_fd_bad_length_prefix directed"
  "PASS can_extended_arbitration_id_high_bits_input directed"
  "PASS node_port_subject_id_high_bits_input directed"
  "PASS pnp_cluster_discovery_bad_length_prefix directed"
  "PASS can_manifestation_truncated_input directed"
  "PASS can_arbitration_id_truncated_input directed"
  "PASS node_port_subject_id_list_truncated_input directed"
  "PASS node_port_id_truncated_input directed"
  "PASS si_sample_temperature_scalar_truncated_input directed"
  "PASS si_unit_acceleration_vector3_truncated_input directed"
  "PASS si_unit_force_vector3_truncated_input directed"
  "PASS si_unit_torque_vector3_truncated_input directed"
  "PASS si_sample_acceleration_vector3_truncated_input directed"
  "PASS si_sample_force_vector3_truncated_input directed"
  "PASS si_sample_torque_vector3_truncated_input directed"
  "PASS si_unit_voltage_scalar_truncated_input directed"
  "PASS si_sample_voltage_scalar_truncated_input directed"
  "PASS pnp_cluster_discovery_truncated_input directed"
  "PASS can_manifestation_serialize_small_buffer directed"
  "PASS can_arbitration_id_serialize_small_buffer directed"
  "PASS node_port_subject_id_list_serialize_small_buffer directed"
  "PASS node_port_id_serialize_small_buffer directed"
  "PASS si_unit_velocity_vector3_serialize_small_buffer directed"
  "PASS si_unit_acceleration_vector3_serialize_small_buffer directed"
  "PASS si_unit_force_vector3_serialize_small_buffer directed"
  "PASS si_unit_torque_vector3_serialize_small_buffer directed"
  "PASS si_sample_acceleration_vector3_serialize_small_buffer directed"
  "PASS si_sample_force_vector3_serialize_small_buffer directed"
  "PASS si_sample_torque_vector3_serialize_small_buffer directed"
  "PASS si_unit_voltage_scalar_serialize_small_buffer directed"
  "PASS si_sample_voltage_scalar_serialize_small_buffer directed"
  "PASS pnp_cluster_discovery_serialize_small_buffer directed"
  "PASS pnp_cluster_append_entries_request_bad_length_prefix directed"
  "PASS metatransport_serial_fragment_bad_length_prefix directed"
  "PASS metatransport_ethernet_frame_bad_length_prefix directed"
  "PASS pnp_cluster_request_vote_request_truncated_input directed"
  "PASS metatransport_udp_frame_truncated_input directed"
  "PASS metatransport_ethernet_frame_serialize_small_buffer directed"
  "PASS pnp_cluster_append_entries_response_serialize_small_buffer directed"
  "PASS register_list_request_truncated_input directed"
)
foreach(marker IN LISTS required_directed_markers)
  string(FIND "${run_stdout}" "${marker}" marker_pos)
  if(marker_pos EQUAL -1)
    message(FATAL_ERROR
      "required directed parity marker missing: ${marker}")
  endif()
endforeach()

set(summary_file "${OUT_DIR}/c-go-parity-summary.txt")
set(summary_tmp "${summary_file}.tmp-${parity_run_nonce}")
file(WRITE "${summary_tmp}" "${run_stdout}\n")
file(RENAME "${summary_tmp}" "${summary_file}")
if(KEEP_RUN_OUTPUT)
  # Said loudly, and with the parts named, because the directory is the whole point of asking.
  message(STATUS
    "KEEP_RUN_OUTPUT: parity scratch tree kept at ${run_out}\n"
    "  ${run_out}/c        generated C\n"
    "  ${run_out}/go       generated Go\n"
    "  ${run_out}/harness  the harness sources, after token substitution\n"
    "  ${run_out}/build    compiled artefacts")
else()
  file(REMOVE_RECURSE "${run_out}")
  if(EXISTS "${run_out}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E rm -rf "${run_out}")
  endif()
  if(EXISTS "${run_out}")
    message(WARNING "unable to remove parity scratch directory: ${run_out}")
  endif()
endif()
message(STATUS "C/Go parity summary:\n${run_stdout}")
