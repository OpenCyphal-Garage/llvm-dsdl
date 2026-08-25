//===----------------------------------------------------------------------===//
///
/// @file
/// Go parity driver comparing generated Go and C serializers/deserializers.
///
/// The program executes randomized and directed parity checks through cgo
/// bindings and reports category-based pass/fail summaries.
///
//===----------------------------------------------------------------------===//

package main

/*
#include <stddef.h>
#include <stdint.h>

typedef struct CCaseResult {
	int8_t deserialize_rc;
	size_t deserialize_consumed;
	int8_t serialize_rc;
	size_t serialize_size;
} CCaseResult;

int c_heartbeat_roundtrip(const uint8_t* input,
                          size_t input_size,
                          uint8_t* output,
                          size_t output_capacity,
                          CCaseResult* result);
int c_execute_command_request_roundtrip(const uint8_t* input,
                                        size_t input_size,
                                        uint8_t* output,
                                        size_t output_capacity,
                                        CCaseResult* result);
int c_execute_command_response_roundtrip(const uint8_t* input,
                                         size_t input_size,
                                         uint8_t* output,
                                         size_t output_capacity,
                                         CCaseResult* result);
int c_node_id_roundtrip(const uint8_t* input,
                        size_t input_size,
                        uint8_t* output,
                        size_t output_capacity,
                        CCaseResult* result);
int c_node_mode_roundtrip(const uint8_t* input,
                          size_t input_size,
                          uint8_t* output,
                          size_t output_capacity,
                          CCaseResult* result);
int c_node_version_roundtrip(const uint8_t* input,
                             size_t input_size,
                             uint8_t* output,
                             size_t output_capacity,
                             CCaseResult* result);
int c_node_health_roundtrip(const uint8_t* input,
                            size_t input_size,
                            uint8_t* output,
                            size_t output_capacity,
                            CCaseResult* result);
int c_node_io_statistics_roundtrip(const uint8_t* input,
                                   size_t input_size,
                                   uint8_t* output,
                                   size_t output_capacity,
                                   CCaseResult* result);
int c_get_info_response_roundtrip(const uint8_t* input,
                                  size_t input_size,
                                  uint8_t* output,
                                  size_t output_capacity,
                                  CCaseResult* result);
int c_diagnostic_record_roundtrip(const uint8_t* input,
                                  size_t input_size,
                                  uint8_t* output,
                                  size_t output_capacity,
                                  CCaseResult* result);
int c_diagnostic_severity_roundtrip(const uint8_t* input,
                                    size_t input_size,
                                    uint8_t* output,
                                    size_t output_capacity,
                                    CCaseResult* result);
int c_register_value_roundtrip(const uint8_t* input,
                               size_t input_size,
                               uint8_t* output,
                               size_t output_capacity,
                               CCaseResult* result);
int c_register_access_request_roundtrip(const uint8_t* input,
                                        size_t input_size,
                                        uint8_t* output,
                                        size_t output_capacity,
                                        CCaseResult* result);
int c_register_access_response_roundtrip(const uint8_t* input,
                                         size_t input_size,
                                         uint8_t* output,
                                         size_t output_capacity,
                                         CCaseResult* result);
int c_register_name_roundtrip(const uint8_t* input,
                              size_t input_size,
                              uint8_t* output,
                              size_t output_capacity,
                              CCaseResult* result);
int c_register_list_request_roundtrip(const uint8_t* input,
                                      size_t input_size,
                                      uint8_t* output,
                                      size_t output_capacity,
                                      CCaseResult* result);
int c_register_list_response_roundtrip(const uint8_t* input,
                                       size_t input_size,
                                       uint8_t* output,
                                       size_t output_capacity,
                                       CCaseResult* result);
int c_file_list_request_roundtrip(const uint8_t* input,
                                  size_t input_size,
                                  uint8_t* output,
                                  size_t output_capacity,
                                  CCaseResult* result);
int c_file_list_response_roundtrip(const uint8_t* input,
                                   size_t input_size,
                                   uint8_t* output,
                                   size_t output_capacity,
                                   CCaseResult* result);
int c_file_read_request_roundtrip(const uint8_t* input,
                                  size_t input_size,
                                  uint8_t* output,
                                  size_t output_capacity,
                                  CCaseResult* result);
int c_file_read_response_roundtrip(const uint8_t* input,
                                   size_t input_size,
                                   uint8_t* output,
                                   size_t output_capacity,
                                   CCaseResult* result);
int c_file_write_request_roundtrip(const uint8_t* input,
                                   size_t input_size,
                                   uint8_t* output,
                                   size_t output_capacity,
                                   CCaseResult* result);
int c_file_write_response_roundtrip(const uint8_t* input,
                                    size_t input_size,
                                    uint8_t* output,
                                    size_t output_capacity,
                                    CCaseResult* result);
int c_file_modify_request_roundtrip(const uint8_t* input,
                                    size_t input_size,
                                    uint8_t* output,
                                    size_t output_capacity,
                                    CCaseResult* result);
int c_file_modify_response_roundtrip(const uint8_t* input,
                                     size_t input_size,
                                     uint8_t* output,
                                     size_t output_capacity,
                                     CCaseResult* result);
int c_file_get_info_request_roundtrip(const uint8_t* input,
                                      size_t input_size,
                                      uint8_t* output,
                                      size_t output_capacity,
                                      CCaseResult* result);
int c_file_get_info_response_roundtrip(const uint8_t* input,
                                       size_t input_size,
                                       uint8_t* output,
                                       size_t output_capacity,
                                       CCaseResult* result);
int c_file_error_roundtrip(const uint8_t* input,
                           size_t input_size,
                           uint8_t* output,
                           size_t output_capacity,
                           CCaseResult* result);
int c_get_transport_statistics_request_roundtrip(const uint8_t* input,
                                                 size_t input_size,
                                                 uint8_t* output,
                                                 size_t output_capacity,
                                                 CCaseResult* result);
int c_get_transport_statistics_response_roundtrip(const uint8_t* input,
                                                  size_t input_size,
                                                  uint8_t* output,
                                                  size_t output_capacity,
                                                  CCaseResult* result);
int c_can_frame_roundtrip(const uint8_t* input,
                          size_t input_size,
                          uint8_t* output,
                          size_t output_capacity,
                          CCaseResult* result);
int c_can_data_classic_roundtrip(const uint8_t* input,
                                 size_t input_size,
                                 uint8_t* output,
                                 size_t output_capacity,
                                 CCaseResult* result);
int c_can_data_fd_roundtrip(const uint8_t* input,
                            size_t input_size,
                            uint8_t* output,
                            size_t output_capacity,
                            CCaseResult* result);
int c_can_error_roundtrip(const uint8_t* input,
                          size_t input_size,
                          uint8_t* output,
                          size_t output_capacity,
                          CCaseResult* result);
int c_can_rtr_roundtrip(const uint8_t* input,
                        size_t input_size,
                        uint8_t* output,
                        size_t output_capacity,
                        CCaseResult* result);
int c_can_manifestation_roundtrip(const uint8_t* input,
                                  size_t input_size,
                                  uint8_t* output,
                                  size_t output_capacity,
                                  CCaseResult* result);
int c_can_arbitration_id_roundtrip(const uint8_t* input,
                                   size_t input_size,
                                   uint8_t* output,
                                   size_t output_capacity,
                                   CCaseResult* result);
int c_can_base_arbitration_id_roundtrip(const uint8_t* input,
                                        size_t input_size,
                                        uint8_t* output,
                                        size_t output_capacity,
                                        CCaseResult* result);
int c_can_extended_arbitration_id_roundtrip(const uint8_t* input,
                                            size_t input_size,
                                            uint8_t* output,
                                            size_t output_capacity,
                                            CCaseResult* result);
int c_metatransport_serial_fragment_roundtrip(const uint8_t* input,
                                              size_t input_size,
                                              uint8_t* output,
                                              size_t output_capacity,
                                              CCaseResult* result);
int c_metatransport_ethernet_frame_roundtrip(const uint8_t* input,
                                             size_t input_size,
                                             uint8_t* output,
                                             size_t output_capacity,
                                             CCaseResult* result);
int c_metatransport_udp_endpoint_roundtrip(const uint8_t* input,
                                           size_t input_size,
                                           uint8_t* output,
                                           size_t output_capacity,
                                           CCaseResult* result);
int c_metatransport_udp_frame_roundtrip(const uint8_t* input,
                                        size_t input_size,
                                        uint8_t* output,
                                        size_t output_capacity,
                                        CCaseResult* result);
int c_time_synchronization_roundtrip(const uint8_t* input,
                                     size_t input_size,
                                     uint8_t* output,
                                     size_t output_capacity,
                                     CCaseResult* result);
int c_time_synchronized_timestamp_roundtrip(const uint8_t* input,
                                            size_t input_size,
                                            uint8_t* output,
                                            size_t output_capacity,
                                            CCaseResult* result);
int c_time_system_roundtrip(const uint8_t* input,
                            size_t input_size,
                            uint8_t* output,
                            size_t output_capacity,
                            CCaseResult* result);
int c_time_tai_info_roundtrip(const uint8_t* input,
                              size_t input_size,
                              uint8_t* output,
                              size_t output_capacity,
                              CCaseResult* result);
int c_time_get_sync_master_info_request_roundtrip(const uint8_t* input,
                                                  size_t input_size,
                                                  uint8_t* output,
                                                  size_t output_capacity,
                                                  CCaseResult* result);
int c_time_get_sync_master_info_response_roundtrip(const uint8_t* input,
                                                   size_t input_size,
                                                   uint8_t* output,
                                                   size_t output_capacity,
                                                   CCaseResult* result);
int c_udp_outgoing_packet_roundtrip(const uint8_t* input,
                                    size_t input_size,
                                    uint8_t* output,
                                    size_t output_capacity,
                                    CCaseResult* result);
int c_udp_handle_incoming_request_roundtrip(const uint8_t* input,
                                            size_t input_size,
                                            uint8_t* output,
                                            size_t output_capacity,
                                            CCaseResult* result);
int c_udp_handle_incoming_response_roundtrip(const uint8_t* input,
                                             size_t input_size,
                                             uint8_t* output,
                                             size_t output_capacity,
                                             CCaseResult* result);
int c_si_unit_angle_quaternion_roundtrip(const uint8_t* input,
                                         size_t input_size,
                                         uint8_t* output,
                                         size_t output_capacity,
                                         CCaseResult* result);
int c_si_unit_length_wide_vector3_roundtrip(const uint8_t* input,
                                            size_t input_size,
                                            uint8_t* output,
                                            size_t output_capacity,
                                            CCaseResult* result);
int c_si_sample_angle_quaternion_roundtrip(const uint8_t* input,
                                           size_t input_size,
                                           uint8_t* output,
                                           size_t output_capacity,
                                           CCaseResult* result);
int c_si_unit_velocity_vector3_roundtrip(const uint8_t* input,
                                         size_t input_size,
                                         uint8_t* output,
                                         size_t output_capacity,
                                         CCaseResult* result);
int c_si_sample_velocity_vector3_roundtrip(const uint8_t* input,
                                           size_t input_size,
                                           uint8_t* output,
                                           size_t output_capacity,
                                           CCaseResult* result);
int c_si_unit_temperature_scalar_roundtrip(const uint8_t* input,
                                           size_t input_size,
                                           uint8_t* output,
                                           size_t output_capacity,
                                           CCaseResult* result);
int c_si_sample_temperature_scalar_roundtrip(const uint8_t* input,
                                             size_t input_size,
                                             uint8_t* output,
                                             size_t output_capacity,
                                             CCaseResult* result);
int c_si_unit_acceleration_vector3_roundtrip(const uint8_t* input,
                                             size_t input_size,
                                             uint8_t* output,
                                             size_t output_capacity,
                                             CCaseResult* result);
int c_si_unit_force_vector3_roundtrip(const uint8_t* input,
                                      size_t input_size,
                                      uint8_t* output,
                                      size_t output_capacity,
                                      CCaseResult* result);
int c_si_unit_torque_vector3_roundtrip(const uint8_t* input,
                                       size_t input_size,
                                       uint8_t* output,
                                       size_t output_capacity,
                                       CCaseResult* result);
int c_si_sample_acceleration_vector3_roundtrip(const uint8_t* input,
                                               size_t input_size,
                                               uint8_t* output,
                                               size_t output_capacity,
                                               CCaseResult* result);
int c_si_sample_force_vector3_roundtrip(const uint8_t* input,
                                        size_t input_size,
                                        uint8_t* output,
                                        size_t output_capacity,
                                        CCaseResult* result);
int c_si_sample_torque_vector3_roundtrip(const uint8_t* input,
                                         size_t input_size,
                                         uint8_t* output,
                                         size_t output_capacity,
                                         CCaseResult* result);
int c_si_unit_voltage_scalar_roundtrip(const uint8_t* input,
                                       size_t input_size,
                                       uint8_t* output,
                                       size_t output_capacity,
                                       CCaseResult* result);
int c_si_sample_voltage_scalar_roundtrip(const uint8_t* input,
                                         size_t input_size,
                                         uint8_t* output,
                                         size_t output_capacity,
                                         CCaseResult* result);
int c_natural8_roundtrip(const uint8_t* input,
                         size_t input_size,
                         uint8_t* output,
                         size_t output_capacity,
                         CCaseResult* result);
int c_real16_roundtrip(const uint8_t* input,
                       size_t input_size,
                       uint8_t* output,
                       size_t output_capacity,
                       CCaseResult* result);
int c_real32_roundtrip(const uint8_t* input,
                       size_t input_size,
                       uint8_t* output,
                       size_t output_capacity,
                       CCaseResult* result);
int c_bit_array_roundtrip(const uint8_t* input,
                          size_t input_size,
                          uint8_t* output,
                          size_t output_capacity,
                          CCaseResult* result);
int c_scalar_bit_roundtrip(const uint8_t* input,
                           size_t input_size,
                           uint8_t* output,
                           size_t output_capacity,
                           CCaseResult* result);
int c_scalar_integer8_roundtrip(const uint8_t* input,
                                size_t input_size,
                                uint8_t* output,
                                size_t output_capacity,
                                CCaseResult* result);
int c_scalar_integer16_roundtrip(const uint8_t* input,
                                 size_t input_size,
                                 uint8_t* output,
                                 size_t output_capacity,
                                 CCaseResult* result);
int c_scalar_integer32_roundtrip(const uint8_t* input,
                                 size_t input_size,
                                 uint8_t* output,
                                 size_t output_capacity,
                                 CCaseResult* result);
int c_scalar_integer64_roundtrip(const uint8_t* input,
                                 size_t input_size,
                                 uint8_t* output,
                                 size_t output_capacity,
                                 CCaseResult* result);
int c_scalar_natural8_roundtrip(const uint8_t* input,
                                size_t input_size,
                                uint8_t* output,
                                size_t output_capacity,
                                CCaseResult* result);
int c_scalar_natural16_roundtrip(const uint8_t* input,
                                 size_t input_size,
                                 uint8_t* output,
                                 size_t output_capacity,
                                 CCaseResult* result);
int c_scalar_natural32_roundtrip(const uint8_t* input,
                                 size_t input_size,
                                 uint8_t* output,
                                 size_t output_capacity,
                                 CCaseResult* result);
int c_scalar_natural64_roundtrip(const uint8_t* input,
                                 size_t input_size,
                                 uint8_t* output,
                                 size_t output_capacity,
                                 CCaseResult* result);
int c_scalar_real16_roundtrip(const uint8_t* input,
                              size_t input_size,
                              uint8_t* output,
                              size_t output_capacity,
                              CCaseResult* result);
int c_scalar_real32_roundtrip(const uint8_t* input,
                              size_t input_size,
                              uint8_t* output,
                              size_t output_capacity,
                              CCaseResult* result);
int c_scalar_real64_roundtrip(const uint8_t* input,
                              size_t input_size,
                              uint8_t* output,
                              size_t output_capacity,
                              CCaseResult* result);
int c_array_integer8_roundtrip(const uint8_t* input,
                               size_t input_size,
                               uint8_t* output,
                               size_t output_capacity,
                               CCaseResult* result);
int c_array_integer16_roundtrip(const uint8_t* input,
                                size_t input_size,
                                uint8_t* output,
                                size_t output_capacity,
                                CCaseResult* result);
int c_array_integer32_roundtrip(const uint8_t* input,
                                size_t input_size,
                                uint8_t* output,
                                size_t output_capacity,
                                CCaseResult* result);
int c_array_integer64_roundtrip(const uint8_t* input,
                                size_t input_size,
                                uint8_t* output,
                                size_t output_capacity,
                                CCaseResult* result);
int c_array_natural16_roundtrip(const uint8_t* input,
                                size_t input_size,
                                uint8_t* output,
                                size_t output_capacity,
                                CCaseResult* result);
int c_array_natural32_roundtrip(const uint8_t* input,
                                size_t input_size,
                                uint8_t* output,
                                size_t output_capacity,
                                CCaseResult* result);
int c_array_natural64_roundtrip(const uint8_t* input,
                                size_t input_size,
                                uint8_t* output,
                                size_t output_capacity,
                                CCaseResult* result);
int c_array_real64_roundtrip(const uint8_t* input,
                             size_t input_size,
                             uint8_t* output,
                             size_t output_capacity,
                             CCaseResult* result);
int c_primitive_empty_roundtrip(const uint8_t* input,
                                size_t input_size,
                                uint8_t* output,
                                size_t output_capacity,
                                CCaseResult* result);
int c_primitive_string_roundtrip(const uint8_t* input,
                                 size_t input_size,
                                 uint8_t* output,
                                 size_t output_capacity,
                                 CCaseResult* result);
int c_primitive_unstructured_roundtrip(const uint8_t* input,
                                       size_t input_size,
                                       uint8_t* output,
                                       size_t output_capacity,
                                       CCaseResult* result);
int c_file_path_roundtrip(const uint8_t* input,
                          size_t input_size,
                          uint8_t* output,
                          size_t output_capacity,
                          CCaseResult* result);
int c_node_id_allocation_data_roundtrip(const uint8_t* input,
                                        size_t input_size,
                                        uint8_t* output,
                                        size_t output_capacity,
                                        CCaseResult* result);
int c_pnp_cluster_entry_roundtrip(const uint8_t* input,
                                  size_t input_size,
                                  uint8_t* output,
                                  size_t output_capacity,
                                  CCaseResult* result);
int c_pnp_cluster_append_entries_request_roundtrip(const uint8_t* input,
                                                   size_t input_size,
                                                   uint8_t* output,
                                                   size_t output_capacity,
                                                   CCaseResult* result);
int c_pnp_cluster_append_entries_response_roundtrip(const uint8_t* input,
                                                    size_t input_size,
                                                    uint8_t* output,
                                                    size_t output_capacity,
                                                    CCaseResult* result);
int c_pnp_cluster_request_vote_request_roundtrip(const uint8_t* input,
                                                 size_t input_size,
                                                 uint8_t* output,
                                                 size_t output_capacity,
                                                 CCaseResult* result);
int c_pnp_cluster_request_vote_response_roundtrip(const uint8_t* input,
                                                  size_t input_size,
                                                  uint8_t* output,
                                                  size_t output_capacity,
                                                  CCaseResult* result);
int c_pnp_cluster_discovery_roundtrip(const uint8_t* input,
                                      size_t input_size,
                                      uint8_t* output,
                                      size_t output_capacity,
                                      CCaseResult* result);
int c_node_port_service_id_roundtrip(const uint8_t* input,
                                     size_t input_size,
                                     uint8_t* output,
                                     size_t output_capacity,
                                     CCaseResult* result);
int c_node_port_subject_id_roundtrip(const uint8_t* input,
                                     size_t input_size,
                                     uint8_t* output,
                                     size_t output_capacity,
                                     CCaseResult* result);
int c_node_port_service_id_list_roundtrip(const uint8_t* input,
                                          size_t input_size,
                                          uint8_t* output,
                                          size_t output_capacity,
                                          CCaseResult* result);
int c_node_port_subject_id_list_roundtrip(const uint8_t* input,
                                          size_t input_size,
                                          uint8_t* output,
                                          size_t output_capacity,
                                          CCaseResult* result);
int c_node_port_id_roundtrip(const uint8_t* input,
                             size_t input_size,
                             uint8_t* output,
                             size_t output_capacity,
                             CCaseResult* result);
int c_port_list_roundtrip(const uint8_t* input,
                          size_t input_size,
                          uint8_t* output,
                          size_t output_capacity,
                          CCaseResult* result);
int c_metatransport_ethernet_ethertype_roundtrip(const uint8_t* input,
                                                 size_t input_size,
                                                 uint8_t* output,
                                                 size_t output_capacity,
                                                 CCaseResult* result);
*/
import "C"

import (
	"bytes"
	"fmt"
	"math"
	"os"
	"sort"
	"strconv"
	"strings"
	"unsafe"

	diagnosticpkg "uavcan_dsdl_generated/uavcan/diagnostic"
	filepkg "uavcan_dsdl_generated/uavcan/file"
	internetudppkg "uavcan_dsdl_generated/uavcan/internet/udp"
	metacan "uavcan_dsdl_generated/uavcan/metatransport/can"
	metaethernetpkg "uavcan_dsdl_generated/uavcan/metatransport/ethernet"
	metaserialpkg "uavcan_dsdl_generated/uavcan/metatransport/serial"
	metaudppkg "uavcan_dsdl_generated/uavcan/metatransport/udp"
	node "uavcan_dsdl_generated/uavcan/node"
	nodeport "uavcan_dsdl_generated/uavcan/node/port"
	pnppkg "uavcan_dsdl_generated/uavcan/pnp"
	pnpclusterpkg "uavcan_dsdl_generated/uavcan/pnp/cluster"
	primitivepkg "uavcan_dsdl_generated/uavcan/primitive"
	primarray "uavcan_dsdl_generated/uavcan/primitive/array"
	primitivescalarpkg "uavcan_dsdl_generated/uavcan/primitive/scalar"
	registerpkg "uavcan_dsdl_generated/uavcan/register"
	sisampleaccelerationpkg "uavcan_dsdl_generated/uavcan/si/sample/acceleration"
	sisampleanglepkg "uavcan_dsdl_generated/uavcan/si/sample/angle"
	sisampleforcepkg "uavcan_dsdl_generated/uavcan/si/sample/force"
	sisampletemperaturepkg "uavcan_dsdl_generated/uavcan/si/sample/temperature"
	sisampletorquepkg "uavcan_dsdl_generated/uavcan/si/sample/torque"
	sisamplevelocitypkg "uavcan_dsdl_generated/uavcan/si/sample/velocity"
	sisamplevoltagepkg "uavcan_dsdl_generated/uavcan/si/sample/voltage"
	siunitaccelerationpkg "uavcan_dsdl_generated/uavcan/si/unit/acceleration"
	siunitanglepkg "uavcan_dsdl_generated/uavcan/si/unit/angle"
	siunitforcepkg "uavcan_dsdl_generated/uavcan/si/unit/force"
	siunitlengthpkg "uavcan_dsdl_generated/uavcan/si/unit/length"
	siunitemperaturepkg "uavcan_dsdl_generated/uavcan/si/unit/temperature"
	siunittorquepkg "uavcan_dsdl_generated/uavcan/si/unit/torque"
	siunitvelocitypkg "uavcan_dsdl_generated/uavcan/si/unit/velocity"
	siunitvoltagepkg "uavcan_dsdl_generated/uavcan/si/unit/voltage"
	timepkg "uavcan_dsdl_generated/uavcan/time"
)

const maxIOBuffer = 16384

type cRoundtripFn func(*C.uint8_t, C.size_t, *C.uint8_t, C.size_t, *C.CCaseResult) C.int

type parityCase struct {
	name              string
	maxSerialized     int
	iterations        int
	requireByteParity bool
	cRoundtrip        cRoundtripFn
	goRoundtrip       func(input []byte, output []byte) (int8, int, int8, int)
}

type parityOutcome struct {
	cDeserializeRC        int8
	cDeserializeConsumed  int
	cSerializeRC          int8
	cSerializeSize        int
	goDeserializeRC       int8
	goDeserializeConsumed int
	goSerializeRC         int8
	goSerializeSize       int
}

type directedVector struct {
	name                   string
	caseName               string
	input                  []byte
	useOutputCapacity      bool
	outputCapacity         int
	expectDeserializeError bool
	expectSerializeError   bool
}

func makeNodeGetInfoBadNameLengthVector() []byte {
	out := make([]byte, 31)
	// After protocol/hardware/software versions (6), vcs revision (8), and unique_id (16),
	// the next byte is the variable-length name prefix (capacity 50). Force 255.
	out[30] = 0xFF
	return out
}

func nextRandomU32(state *uint64) uint32 {
	*state ^= *state << 13
	*state ^= *state >> 7
	*state ^= *state << 17
	return uint32(*state & 0xFFFF_FFFF)
}

func fillRandomBytes(dst []byte, state *uint64) {
	for i := range dst {
		dst[i] = byte(nextRandomU32(state) & 0xFF)
	}
}

func formatBytes(data []byte) string {
	var out strings.Builder
	for i, b := range data {
		if i > 0 {
			out.WriteByte(' ')
		}
		fmt.Fprintf(&out, "%02X", b)
	}
	return out.String()
}

func scaledIterations(base int, divisor int, minimum int) int {
	if divisor <= 0 {
		divisor = 1
	}
	scaled := base / divisor
	if scaled < minimum {
		return minimum
	}
	return scaled
}

func classifyRandomCase(name string) string {
	switch {
	case strings.HasPrefix(name, "scalar_"):
		return "primitive_scalar"
	case strings.HasPrefix(name, "array_") || name == "natural8" || name == "real16" || name == "real32" || name == "bit_array":
		return "primitive_array"
	case strings.HasPrefix(name, "primitive_"):
		return "primitive_composite"
	case name == "register_value" || name == "can_frame" || name == "can_manifestation" || name == "can_arbitration_id" || name == "node_port_id" || name == "node_port_list" || name == "node_port_subject_id_list":
		return "union_delimited"
	case name == "file_path" || name == "register_name" || name == "diagnostic_record" || name == "get_info_response" || name == "pnp_cluster_discovery":
		return "variable_composite"
	case strings.Contains(name, "_request") || strings.Contains(name, "_response"):
		return "service_section"
	default:
		return "message_section"
	}
}

func classifyDirectedVector(name string) string {
	switch {
	case strings.Contains(name, "invalid_union_tag") || strings.Contains(name, "bad_nested_union_tag"):
		return "union_tag_error"
	case strings.Contains(name, "bad_delimiter_header") || strings.Contains(name, "large_delimiter_header"):
		return "delimiter_error"
	case strings.Contains(name, "bad_length_prefix") || strings.Contains(name, "bad_destination_address_length") || strings.Contains(name, "bad_payload_length") || strings.Contains(name, "bad_name_length_prefix"):
		return "length_prefix_error"
	case strings.Contains(name, "truncated"):
		return "truncation"
	case strings.Contains(name, "nan_payload") || strings.Contains(name, "nan_vector"):
		return "float_nan"
	case strings.Contains(name, "serialize_small_buffer") || strings.Contains(name, "serialize_zero_buffer"):
		return "serialize_buffer"
	case strings.Contains(name, "high_bits_input"):
		return "high_bits_normalization"
	default:
		return "misc"
	}
}

func emitCategorySummary(prefix string, counts map[string]int) {
	keys := make([]string, 0, len(counts))
	for key := range counts {
		keys = append(keys, key)
	}
	sort.Strings(keys)

	parts := make([]string, 0, len(keys))
	for _, key := range keys {
		parts = append(parts, fmt.Sprintf("%s=%d", key, counts[key]))
	}
	fmt.Printf("PASS c/go parity %s categories %s\n", prefix, strings.Join(parts, " "))
}

func ensureDirectedBaselineCoverage(cases []parityCase, directed []directedVector) []directedVector {
	hasTruncation := map[string]bool{}
	hasSerializeBuffer := map[string]bool{}
	seenNames := map[string]bool{}
	for _, v := range directed {
		seenNames[v.name] = true
		switch classifyDirectedVector(v.name) {
		case "truncation":
			hasTruncation[v.caseName] = true
		case "serialize_buffer":
			hasSerializeBuffer[v.caseName] = true
		}
	}

	makeUniqueName := func(base string) string {
		if !seenNames[base] {
			seenNames[base] = true
			return base
		}
		for i := 2; ; i++ {
			candidate := fmt.Sprintf("%s_%d", base, i)
			if !seenNames[candidate] {
				seenNames[candidate] = true
				return candidate
			}
		}
	}

	autoAdded := 0
	for _, tc := range cases {
		if !hasTruncation[tc.name] {
			directed = append(directed, directedVector{
				name:                   makeUniqueName(tc.name + "_auto_truncated_input"),
				caseName:               tc.name,
				input:                  []byte{},
				expectDeserializeError: false,
			})
			hasTruncation[tc.name] = true
			autoAdded++
		}

		if !hasSerializeBuffer[tc.name] {
			vector := directedVector{
				caseName:               tc.name,
				input:                  []byte{},
				useOutputCapacity:      true,
				expectDeserializeError: false,
			}
			if tc.maxSerialized > 0 {
				vector.name = makeUniqueName(tc.name + "_auto_serialize_small_buffer")
				vector.outputCapacity = tc.maxSerialized - 1
				vector.expectSerializeError = true
			} else {
				vector.name = makeUniqueName(tc.name + "_auto_serialize_zero_buffer")
				vector.outputCapacity = 0
				vector.expectSerializeError = false
			}
			directed = append(directed, vector)
			hasSerializeBuffer[tc.name] = true
			autoAdded++
		}
	}

	fmt.Printf("PASS directed baseline auto_added=%d\n", autoAdded)
	return directed
}

func validateDirectedCoverage(cases []parityCase, directed []directedVector) error {
	hasAny := map[string]bool{}
	hasTruncation := map[string]bool{}
	hasSerializeBuffer := map[string]bool{}
	for _, v := range directed {
		hasAny[v.caseName] = true
		switch classifyDirectedVector(v.name) {
		case "truncation":
			hasTruncation[v.caseName] = true
		case "serialize_buffer":
			hasSerializeBuffer[v.caseName] = true
		}
	}

	missingAny := []string{}
	missingTruncation := []string{}
	missingSerializeBuffer := []string{}
	for _, tc := range cases {
		if !hasAny[tc.name] {
			missingAny = append(missingAny, tc.name)
		}
		if !hasTruncation[tc.name] {
			missingTruncation = append(missingTruncation, tc.name)
		}
		if !hasSerializeBuffer[tc.name] {
			missingSerializeBuffer = append(missingSerializeBuffer, tc.name)
		}
	}
	sort.Strings(missingAny)
	sort.Strings(missingTruncation)
	sort.Strings(missingSerializeBuffer)

	if len(missingAny) > 0 {
		return fmt.Errorf("directed coverage missing any-vector cases: %s", strings.Join(missingAny, ", "))
	}
	if len(missingTruncation) > 0 {
		return fmt.Errorf("directed coverage missing truncation cases: %s", strings.Join(missingTruncation, ", "))
	}
	if len(missingSerializeBuffer) > 0 {
		return fmt.Errorf("directed coverage missing serialize-buffer cases: %s", strings.Join(missingSerializeBuffer, ", "))
	}

	fmt.Printf("PASS directed coverage any=%d truncation=%d serialize_buffer=%d\n", len(cases), len(cases), len(cases))
	return nil
}

func validateCaseInventory(cases []parityCase, directed []directedVector) error {
	seenCases := map[string]bool{}
	for _, tc := range cases {
		if tc.name == "" {
			return fmt.Errorf("empty parity case name")
		}
		if seenCases[tc.name] {
			return fmt.Errorf("duplicate parity case name: %s", tc.name)
		}
		seenCases[tc.name] = true
	}

	seenDirected := map[string]bool{}
	for _, v := range directed {
		if v.name == "" {
			return fmt.Errorf("empty directed vector name for case %s", v.caseName)
		}
		if seenDirected[v.name] {
			return fmt.Errorf("duplicate directed vector name: %s", v.name)
		}
		seenDirected[v.name] = true
		if !seenCases[v.caseName] {
			return fmt.Errorf("directed vector references unknown case: %s -> %s", v.name, v.caseName)
		}
	}

	fmt.Printf("PASS c/go inventory random_cases=%d directed_cases=%d\n", len(cases), len(directed))
	return nil
}

func runParityOnce(tc *parityCase, inputBuf []byte, inputSize int, cOutput []byte, goOutput []byte, outputCapacity int) (parityOutcome, error) {
	outcome := parityOutcome{}
	if outputCapacity < 0 || outputCapacity > tc.maxSerialized {
		return outcome, fmt.Errorf(
			"%s invalid output capacity %d (max %d)",
			tc.name,
			outputCapacity,
			tc.maxSerialized,
		)
	}
	if outputCapacity > len(cOutput) || outputCapacity > len(goOutput) {
		return outcome, fmt.Errorf("%s output buffer too small: need %d", tc.name, outputCapacity)
	}
	if inputSize < 0 || inputSize > len(inputBuf) {
		return outcome, fmt.Errorf("%s input size out of bounds: %d", tc.name, inputSize)
	}

	for i := 0; i < outputCapacity; i++ {
		cOutput[i] = 0xA5
		goOutput[i] = 0xA5
	}

	var cResult C.CCaseResult
	cStatus := tc.cRoundtrip(
		(*C.uint8_t)(unsafe.Pointer(&inputBuf[0])),
		C.size_t(inputSize),
		(*C.uint8_t)(unsafe.Pointer(&cOutput[0])),
		C.size_t(outputCapacity),
		&cResult,
	)
	if cStatus != 0 {
		return outcome, fmt.Errorf("%s C harness failed with status=%d", tc.name, int(cStatus))
	}

	goDesRC, goConsumed, goSerRC, goSerSize := tc.goRoundtrip(inputBuf[:inputSize], goOutput[:outputCapacity])

	outcome.cDeserializeRC = int8(cResult.deserialize_rc)
	outcome.cDeserializeConsumed = int(cResult.deserialize_consumed)
	outcome.cSerializeRC = int8(cResult.serialize_rc)
	outcome.cSerializeSize = int(cResult.serialize_size)
	outcome.goDeserializeRC = goDesRC
	outcome.goDeserializeConsumed = goConsumed
	outcome.goSerializeRC = goSerRC
	outcome.goSerializeSize = goSerSize

	if outcome.goDeserializeRC != outcome.cDeserializeRC || outcome.goDeserializeConsumed != outcome.cDeserializeConsumed {
		return outcome, fmt.Errorf(
			"%s deserialize mismatch input_size=%d c(rc=%d,consumed=%d) go(rc=%d,consumed=%d) input=[%s]",
			tc.name,
			inputSize,
			outcome.cDeserializeRC,
			outcome.cDeserializeConsumed,
			outcome.goDeserializeRC,
			outcome.goDeserializeConsumed,
			formatBytes(inputBuf[:inputSize]),
		)
	}

	if outcome.goDeserializeRC < 0 {
		return outcome, nil
	}

	if outcome.goSerializeRC != outcome.cSerializeRC {
		return outcome, fmt.Errorf(
			"%s serialize rc mismatch c(rc=%d) go(rc=%d)",
			tc.name,
			outcome.cSerializeRC,
			outcome.goSerializeRC,
		)
	}
	if outcome.cSerializeRC < 0 {
		return outcome, nil
	}
	if outcome.goSerializeSize != outcome.cSerializeSize {
		return outcome, fmt.Errorf(
			"%s serialize size mismatch c(size=%d) go(size=%d) c=[%s] go=[%s]",
			tc.name,
			outcome.cSerializeSize,
			outcome.goSerializeSize,
			formatBytes(cOutput[:outcome.cSerializeSize]),
			formatBytes(goOutput[:outcome.goSerializeSize]),
		)
	}

	if tc.requireByteParity && !bytes.Equal(cOutput[:outcome.cSerializeSize], goOutput[:outcome.goSerializeSize]) {
		return outcome, fmt.Errorf(
			"%s serialize bytes mismatch size=%d c=[%s] go=[%s]",
			tc.name,
			outcome.cSerializeSize,
			formatBytes(cOutput[:outcome.cSerializeSize]),
			formatBytes(goOutput[:outcome.goSerializeSize]),
		)
	}

	return outcome, nil
}

func runRandomParityCase(tc *parityCase, rng *uint64) error {
	var input [maxIOBuffer]byte
	var cOutput [maxIOBuffer]byte
	var goOutput [maxIOBuffer]byte

	if tc.maxSerialized < 0 || tc.maxSerialized > maxIOBuffer {
		return fmt.Errorf("%s invalid serialization buffer size: %d", tc.name, tc.maxSerialized)
	}
	inputCeiling := tc.maxSerialized + 17
	if inputCeiling > maxIOBuffer {
		inputCeiling = maxIOBuffer
	}
	for iter := 0; iter < tc.iterations; iter++ {
		inputSize := int(nextRandomU32(rng) % uint32(inputCeiling+1))
		fillRandomBytes(input[:inputSize], rng)
		if _, err := runParityOnce(tc, input[:], inputSize, cOutput[:], goOutput[:], tc.maxSerialized); err != nil {
			return fmt.Errorf("iter=%d: %w", iter, err)
		}
	}

	fmt.Printf("PASS %s random (%d iterations)\n", tc.name, tc.iterations)
	return nil
}

func runDirectedVector(v directedVector, cases map[string]*parityCase) error {
	tc := cases[v.caseName]
	if tc == nil {
		return fmt.Errorf("directed vector %s references unknown case %q", v.name, v.caseName)
	}
	if len(v.input) > maxIOBuffer {
		return fmt.Errorf("directed vector %s input too large: %d", v.name, len(v.input))
	}

	var input [maxIOBuffer]byte
	var cOutput [maxIOBuffer]byte
	var goOutput [maxIOBuffer]byte
	copy(input[:], v.input)

	outputCapacity := tc.maxSerialized
	if v.useOutputCapacity {
		outputCapacity = v.outputCapacity
	}
	outcome, err := runParityOnce(tc, input[:], len(v.input), cOutput[:], goOutput[:], outputCapacity)
	if err != nil {
		return fmt.Errorf("%s: %w", v.name, err)
	}
	if v.expectDeserializeError && outcome.cDeserializeRC >= 0 {
		return fmt.Errorf("%s expected deserialize error, got rc=%d", v.name, outcome.cDeserializeRC)
	}
	if !v.expectDeserializeError && outcome.cDeserializeRC < 0 {
		return fmt.Errorf("%s expected deserialize success, got rc=%d", v.name, outcome.cDeserializeRC)
	}
	if v.expectSerializeError && outcome.cDeserializeRC >= 0 && outcome.cSerializeRC >= 0 {
		return fmt.Errorf("%s expected serialize error, got rc=%d", v.name, outcome.cSerializeRC)
	}
	if !v.expectSerializeError && outcome.cDeserializeRC >= 0 && outcome.cSerializeRC < 0 {
		return fmt.Errorf("%s expected serialize success, got rc=%d", v.name, outcome.cSerializeRC)
	}

	fmt.Printf("PASS %s directed\n", v.name)
	return nil
}

func buildParityCases(baseIterations int) []parityCase {
	return []parityCase{
		{
			name:              "heartbeat",
			maxSerialized:     node.HEARTBEAT@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_heartbeat_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj node.Heartbeat@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "execute_command_request",
			maxSerialized:     node.EXECUTE_COMMAND@V1_3@_REQUEST_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_execute_command_request_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj node.ExecuteCommand@V1_3@_Request
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "execute_command_response",
			maxSerialized:     node.EXECUTE_COMMAND@V1_3@_RESPONSE_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_execute_command_response_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj node.ExecuteCommand@V1_3@_Response
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "node_id",
			maxSerialized:     node.ID@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_node_id_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj node.ID@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "node_mode",
			maxSerialized:     node.MODE@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_node_mode_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj node.Mode@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "node_version",
			maxSerialized:     node.VERSION@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_node_version_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj node.Version@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "node_health",
			maxSerialized:     node.HEALTH@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_node_health_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj node.Health@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "node_io_statistics",
			maxSerialized:     node.IO_STATISTICS@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_node_io_statistics_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj node.IOStatistics@V0_1@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "get_info_response",
			maxSerialized:     node.GET_INFO@V1_0@_RESPONSE_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_get_info_response_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj node.GetInfo@V1_0@_Response
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "diagnostic_record",
			maxSerialized:     diagnosticpkg.RECORD@V1_1@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_diagnostic_record_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj diagnosticpkg.Record@V1_1@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "diagnostic_severity",
			maxSerialized:     diagnosticpkg.SEVERITY@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_diagnostic_severity_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj diagnosticpkg.Severity@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "register_value",
			maxSerialized:     registerpkg.VALUE@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_register_value_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj registerpkg.Value@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "register_access_request",
			maxSerialized:     registerpkg.ACCESS@V1_0@_REQUEST_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_register_access_request_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj registerpkg.Access@V1_0@_Request
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "register_access_response",
			maxSerialized:     registerpkg.ACCESS@V1_0@_RESPONSE_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_register_access_response_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj registerpkg.Access@V1_0@_Response
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "register_name",
			maxSerialized:     registerpkg.NAME@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_register_name_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj registerpkg.Name@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "register_list_request",
			maxSerialized:     registerpkg.LIST@V1_0@_REQUEST_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_register_list_request_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj registerpkg.List@V1_0@_Request
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "register_list_response",
			maxSerialized:     registerpkg.LIST@V1_0@_RESPONSE_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_register_list_response_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj registerpkg.List@V1_0@_Response
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "file_list_request",
			maxSerialized:     filepkg.LIST@V0_2@_REQUEST_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_file_list_request_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj filepkg.List@V0_2@_Request
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "file_list_response",
			maxSerialized:     filepkg.LIST@V0_2@_RESPONSE_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_file_list_response_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj filepkg.List@V0_2@_Response
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "file_read_request",
			maxSerialized:     filepkg.READ@V1_1@_REQUEST_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_file_read_request_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj filepkg.Read@V1_1@_Request
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "file_read_response",
			maxSerialized:     filepkg.READ@V1_1@_RESPONSE_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_file_read_response_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj filepkg.Read@V1_1@_Response
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "file_write_request",
			maxSerialized:     filepkg.WRITE@V1_1@_REQUEST_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_file_write_request_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj filepkg.Write@V1_1@_Request
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "file_write_response",
			maxSerialized:     filepkg.WRITE@V1_1@_RESPONSE_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_file_write_response_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj filepkg.Write@V1_1@_Response
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "file_modify_request",
			maxSerialized:     filepkg.MODIFY@V1_1@_REQUEST_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_file_modify_request_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj filepkg.Modify@V1_1@_Request
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "file_modify_response",
			maxSerialized:     filepkg.MODIFY@V1_1@_RESPONSE_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_file_modify_response_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj filepkg.Modify@V1_1@_Response
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "file_get_info_request",
			maxSerialized:     filepkg.GET_INFO@V0_2@_REQUEST_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_file_get_info_request_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj filepkg.GetInfo@V0_2@_Request
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "file_get_info_response",
			maxSerialized:     filepkg.GET_INFO@V0_2@_RESPONSE_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_file_get_info_response_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj filepkg.GetInfo@V0_2@_Response
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "file_error",
			maxSerialized:     filepkg.ERROR@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_file_error_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj filepkg.Error@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "get_transport_statistics_request",
			maxSerialized:     node.GET_TRANSPORT_STATISTICS@V0_1@_REQUEST_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_get_transport_statistics_request_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj node.GetTransportStatistics@V0_1@_Request
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "get_transport_statistics_response",
			maxSerialized:     node.GET_TRANSPORT_STATISTICS@V0_1@_RESPONSE_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_get_transport_statistics_response_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj node.GetTransportStatistics@V0_1@_Response
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "can_frame",
			maxSerialized:     metacan.FRAME@V0_2@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_can_frame_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj metacan.Frame@V0_2@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "can_data_classic",
			maxSerialized:     metacan.DATA_CLASSIC@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_can_data_classic_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj metacan.DataClassic@V0_1@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "can_error",
			maxSerialized:     metacan.ERROR@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_can_error_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj metacan.Error@V0_1@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "can_data_fd",
			maxSerialized:     metacan.DATA_FD@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_can_data_fd_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj metacan.DataFD@V0_1@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "can_rtr",
			maxSerialized:     metacan.RTR@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_can_rtr_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj metacan.RTR@V0_1@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "can_manifestation",
			maxSerialized:     metacan.MANIFESTATION@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_can_manifestation_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj metacan.Manifestation@V0_1@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "can_arbitration_id",
			maxSerialized:     metacan.ARBITRATION_ID@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_can_arbitration_id_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj metacan.ArbitrationID@V0_1@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "can_base_arbitration_id",
			maxSerialized:     metacan.BASE_ARBITRATION_ID@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_can_base_arbitration_id_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj metacan.BaseArbitrationID@V0_1@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "can_extended_arbitration_id",
			maxSerialized:     metacan.EXTENDED_ARBITRATION_ID@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_can_extended_arbitration_id_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj metacan.ExtendedArbitrationID@V0_1@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "metatransport_serial_fragment",
			maxSerialized:     metaserialpkg.FRAGMENT@V0_2@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_metatransport_serial_fragment_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj metaserialpkg.Fragment@V0_2@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "metatransport_ethernet_frame",
			maxSerialized:     metaethernetpkg.FRAME@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_metatransport_ethernet_frame_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj metaethernetpkg.Frame@V0_1@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "metatransport_ethernet_ethertype",
			maxSerialized:     metaethernetpkg.ETHER_TYPE@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_metatransport_ethernet_ethertype_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj metaethernetpkg.EtherType@V0_1@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "metatransport_udp_endpoint",
			maxSerialized:     metaudppkg.ENDPOINT@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_metatransport_udp_endpoint_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj metaudppkg.Endpoint@V0_1@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "metatransport_udp_frame",
			maxSerialized:     metaudppkg.FRAME@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_metatransport_udp_frame_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj metaudppkg.Frame@V0_1@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "time_synchronization",
			maxSerialized:     timepkg.SYNCHRONIZATION@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_time_synchronization_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj timepkg.Synchronization@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "time_synchronized_timestamp",
			maxSerialized:     timepkg.SYNCHRONIZED_TIMESTAMP@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_time_synchronized_timestamp_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj timepkg.SynchronizedTimestamp@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "time_system",
			maxSerialized:     timepkg.TIME_SYSTEM@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_time_system_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj timepkg.TimeSystem@V0_1@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "time_tai_info",
			maxSerialized:     timepkg.TAI_INFO@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_time_tai_info_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj timepkg.TAIInfo@V0_1@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "get_sync_master_info_request",
			maxSerialized:     timepkg.GET_SYNCHRONIZATION_MASTER_INFO@V0_1@_REQUEST_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_time_get_sync_master_info_request_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj timepkg.GetSynchronizationMasterInfo@V0_1@_Request
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "get_sync_master_info_response",
			maxSerialized:     timepkg.GET_SYNCHRONIZATION_MASTER_INFO@V0_1@_RESPONSE_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_time_get_sync_master_info_response_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj timepkg.GetSynchronizationMasterInfo@V0_1@_Response
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "udp_outgoing_packet",
			maxSerialized:     internetudppkg.OUTGOING_PACKET@V0_2@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_udp_outgoing_packet_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj internetudppkg.OutgoingPacket@V0_2@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "udp_handle_incoming_request",
			maxSerialized:     internetudppkg.HANDLE_INCOMING_PACKET@V0_2@_REQUEST_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_udp_handle_incoming_request_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj internetudppkg.HandleIncomingPacket@V0_2@_Request
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "udp_handle_incoming_response",
			maxSerialized:     internetudppkg.HANDLE_INCOMING_PACKET@V0_2@_RESPONSE_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_udp_handle_incoming_response_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj internetudppkg.HandleIncomingPacket@V0_2@_Response
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:          "si_unit_angle_quaternion",
			maxSerialized: siunitanglepkg.QUATERNION@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:    scaledIterations(baseIterations, 2, 32),
			// Float-heavy fixed arrays may differ in NaN bit canonicalization while preserving behaviour.
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_si_unit_angle_quaternion_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj siunitanglepkg.Quaternion@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:          "si_unit_acceleration_vector3",
			maxSerialized: siunitaccelerationpkg.VECTOR3@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:    scaledIterations(baseIterations, 2, 32),
			// Float-heavy fixed arrays may differ in NaN bit canonicalization while preserving behaviour.
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_si_unit_acceleration_vector3_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj siunitaccelerationpkg.Vector3@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:          "si_unit_force_vector3",
			maxSerialized: siunitforcepkg.VECTOR3@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:    scaledIterations(baseIterations, 2, 32),
			// Float-heavy fixed arrays may differ in NaN bit canonicalization while preserving behaviour.
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_si_unit_force_vector3_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj siunitforcepkg.Vector3@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:          "si_unit_length_wide_vector3",
			maxSerialized: siunitlengthpkg.WIDE_VECTOR3@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:    scaledIterations(baseIterations, 2, 32),
			// Float-heavy fixed arrays may differ in NaN bit canonicalization while preserving behaviour.
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_si_unit_length_wide_vector3_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj siunitlengthpkg.WideVector3@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:          "si_unit_torque_vector3",
			maxSerialized: siunittorquepkg.VECTOR3@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:    scaledIterations(baseIterations, 2, 32),
			// Float-heavy fixed arrays may differ in NaN bit canonicalization while preserving behaviour.
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_si_unit_torque_vector3_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj siunittorquepkg.Vector3@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:          "si_sample_angle_quaternion",
			maxSerialized: sisampleanglepkg.QUATERNION@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:    scaledIterations(baseIterations, 2, 32),
			// Float-heavy fixed arrays may differ in NaN bit canonicalization while preserving behaviour.
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_si_sample_angle_quaternion_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj sisampleanglepkg.Quaternion@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:          "si_sample_acceleration_vector3",
			maxSerialized: sisampleaccelerationpkg.VECTOR3@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:    scaledIterations(baseIterations, 2, 32),
			// Float-heavy fixed arrays may differ in NaN bit canonicalization while preserving behaviour.
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_si_sample_acceleration_vector3_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj sisampleaccelerationpkg.Vector3@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:          "si_sample_force_vector3",
			maxSerialized: sisampleforcepkg.VECTOR3@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:    scaledIterations(baseIterations, 2, 32),
			// Float-heavy fixed arrays may differ in NaN bit canonicalization while preserving behaviour.
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_si_sample_force_vector3_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj sisampleforcepkg.Vector3@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:          "si_sample_torque_vector3",
			maxSerialized: sisampletorquepkg.VECTOR3@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:    scaledIterations(baseIterations, 2, 32),
			// Float-heavy fixed arrays may differ in NaN bit canonicalization while preserving behaviour.
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_si_sample_torque_vector3_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj sisampletorquepkg.Vector3@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:          "si_unit_velocity_vector3",
			maxSerialized: siunitvelocitypkg.VECTOR3@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:    scaledIterations(baseIterations, 2, 32),
			// Float-heavy fixed arrays may differ in NaN bit canonicalization while preserving behaviour.
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_si_unit_velocity_vector3_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj siunitvelocitypkg.Vector3@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:          "si_sample_velocity_vector3",
			maxSerialized: sisamplevelocitypkg.VECTOR3@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:    scaledIterations(baseIterations, 2, 32),
			// Float-heavy fixed arrays may differ in NaN bit canonicalization while preserving behaviour.
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_si_sample_velocity_vector3_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj sisamplevelocitypkg.Vector3@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "si_unit_temperature_scalar",
			maxSerialized:     siunitemperaturepkg.SCALAR@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_si_unit_temperature_scalar_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj siunitemperaturepkg.Scalar@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "si_sample_temperature_scalar",
			maxSerialized:     sisampletemperaturepkg.SCALAR@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_si_sample_temperature_scalar_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj sisampletemperaturepkg.Scalar@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "si_unit_voltage_scalar",
			maxSerialized:     siunitvoltagepkg.SCALAR@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_si_unit_voltage_scalar_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj siunitvoltagepkg.Scalar@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "si_sample_voltage_scalar",
			maxSerialized:     sisamplevoltagepkg.SCALAR@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_si_sample_voltage_scalar_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj sisamplevoltagepkg.Scalar@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "natural8",
			maxSerialized:     primarray.NATURAL8@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_natural8_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primarray.Natural8@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:          "real16",
			maxSerialized: primarray.REAL16@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:    scaledIterations(baseIterations, 2, 32),
			// Float16 NaN payload/canonicalization may differ while wire-level semantics remain equivalent.
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_real16_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primarray.Real16@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "real32",
			maxSerialized:     primarray.REAL32@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_real32_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primarray.Real32@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "bit_array",
			maxSerialized:     primarray.BIT@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_bit_array_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primarray.Bit@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "scalar_bit",
			maxSerialized:     primitivescalarpkg.BIT@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_scalar_bit_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primitivescalarpkg.Bit@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "scalar_integer8",
			maxSerialized:     primitivescalarpkg.INTEGER8@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_scalar_integer8_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primitivescalarpkg.Integer8@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "scalar_integer16",
			maxSerialized:     primitivescalarpkg.INTEGER16@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_scalar_integer16_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primitivescalarpkg.Integer16@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "scalar_integer32",
			maxSerialized:     primitivescalarpkg.INTEGER32@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_scalar_integer32_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primitivescalarpkg.Integer32@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "scalar_integer64",
			maxSerialized:     primitivescalarpkg.INTEGER64@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_scalar_integer64_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primitivescalarpkg.Integer64@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "scalar_natural8",
			maxSerialized:     primitivescalarpkg.NATURAL8@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_scalar_natural8_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primitivescalarpkg.Natural8@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "scalar_natural16",
			maxSerialized:     primitivescalarpkg.NATURAL16@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_scalar_natural16_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primitivescalarpkg.Natural16@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "scalar_natural32",
			maxSerialized:     primitivescalarpkg.NATURAL32@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_scalar_natural32_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primitivescalarpkg.Natural32@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "scalar_natural64",
			maxSerialized:     primitivescalarpkg.NATURAL64@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_scalar_natural64_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primitivescalarpkg.Natural64@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "scalar_real16",
			maxSerialized:     primitivescalarpkg.REAL16@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_scalar_real16_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primitivescalarpkg.Real16@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "scalar_real32",
			maxSerialized:     primitivescalarpkg.REAL32@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_scalar_real32_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primitivescalarpkg.Real32@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "scalar_real64",
			maxSerialized:     primitivescalarpkg.REAL64@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_scalar_real64_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primitivescalarpkg.Real64@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "array_integer8",
			maxSerialized:     primarray.INTEGER8@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_array_integer8_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primarray.Integer8@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "array_integer16",
			maxSerialized:     primarray.INTEGER16@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_array_integer16_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primarray.Integer16@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "array_integer32",
			maxSerialized:     primarray.INTEGER32@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_array_integer32_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primarray.Integer32@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "array_integer64",
			maxSerialized:     primarray.INTEGER64@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_array_integer64_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primarray.Integer64@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "array_natural16",
			maxSerialized:     primarray.NATURAL16@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_array_natural16_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primarray.Natural16@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "array_natural32",
			maxSerialized:     primarray.NATURAL32@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_array_natural32_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primarray.Natural32@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "array_natural64",
			maxSerialized:     primarray.NATURAL64@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_array_natural64_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primarray.Natural64@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "array_real64",
			maxSerialized:     primarray.REAL64@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_array_real64_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primarray.Real64@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "primitive_empty",
			maxSerialized:     primitivepkg.EMPTY@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_primitive_empty_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primitivepkg.Empty@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "primitive_string",
			maxSerialized:     primitivepkg.STRING@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_primitive_string_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primitivepkg.String@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "primitive_unstructured",
			maxSerialized:     primitivepkg.UNSTRUCTURED@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_primitive_unstructured_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj primitivepkg.Unstructured@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "file_path",
			maxSerialized:     filepkg.PATH@V2_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_file_path_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj filepkg.Path@V2_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "node_id_allocation_data",
			maxSerialized:     pnppkg.NODE_ID_ALLOCATION_DATA@V2_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_node_id_allocation_data_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj pnppkg.NodeIDAllocationData@V2_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "pnp_cluster_entry",
			maxSerialized:     pnpclusterpkg.ENTRY@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_pnp_cluster_entry_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj pnpclusterpkg.Entry@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "pnp_cluster_append_entries_request",
			maxSerialized:     pnpclusterpkg.APPEND_ENTRIES@V1_0@_REQUEST_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_pnp_cluster_append_entries_request_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj pnpclusterpkg.AppendEntries@V1_0@_Request
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "pnp_cluster_append_entries_response",
			maxSerialized:     pnpclusterpkg.APPEND_ENTRIES@V1_0@_RESPONSE_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_pnp_cluster_append_entries_response_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj pnpclusterpkg.AppendEntries@V1_0@_Response
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "pnp_cluster_request_vote_request",
			maxSerialized:     pnpclusterpkg.REQUEST_VOTE@V1_0@_REQUEST_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_pnp_cluster_request_vote_request_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj pnpclusterpkg.RequestVote@V1_0@_Request
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "pnp_cluster_request_vote_response",
			maxSerialized:     pnpclusterpkg.REQUEST_VOTE@V1_0@_RESPONSE_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_pnp_cluster_request_vote_response_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj pnpclusterpkg.RequestVote@V1_0@_Response
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "pnp_cluster_discovery",
			maxSerialized:     pnpclusterpkg.DISCOVERY@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_pnp_cluster_discovery_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj pnpclusterpkg.Discovery@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "node_port_service_id",
			maxSerialized:     nodeport.SERVICE_ID@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_node_port_service_id_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj nodeport.ServiceID@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "node_port_subject_id",
			maxSerialized:     nodeport.SUBJECT_ID@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_node_port_subject_id_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj nodeport.SubjectID@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "node_port_service_id_list",
			maxSerialized:     nodeport.SERVICE_ID_LIST@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 2, 32),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_node_port_service_id_list_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj nodeport.ServiceIDList@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "node_port_subject_id_list",
			maxSerialized:     nodeport.SUBJECT_ID_LIST@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 8, 8),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_node_port_subject_id_list_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj nodeport.SubjectIDList@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "node_port_id",
			maxSerialized:     nodeport.ID@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        baseIterations,
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_node_port_id_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj nodeport.ID@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
		{
			name:              "node_port_list",
			maxSerialized:     nodeport.LIST@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES,
			iterations:        scaledIterations(baseIterations, 8, 8),
			requireByteParity: true,
			cRoundtrip: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_port_list_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goRoundtrip: func(input []byte, output []byte) (int8, int, int8, int) {
				var obj nodeport.List@V1_0@
				desRC, consumed := obj.Deserialize(input)
				if desRC < 0 {
					return desRC, consumed, 0, 0
				}
				serRC, serSize := obj.Serialize(output)
				return desRC, consumed, serRC, serSize
			},
		},
	}
}

func runCGoParity(iterations int) error {
	cases := buildParityCases(iterations)
	caseByName := map[string]*parityCase{}
	for i := range cases {
		caseByName[cases[i].name] = &cases[i]
	}
	randomCategoryCounts := map[string]int{}
	directedCategoryCounts := map[string]int{}

	directed := []directedVector{
		{
			name:                   "register_value_invalid_union_tag",
			caseName:               "register_value",
			input:                  []byte{0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "can_frame_invalid_union_tag",
			caseName:               "can_frame",
			input:                  []byte{0x04},
			expectDeserializeError: true,
		},
		{
			name:                   "can_manifestation_invalid_union_tag",
			caseName:               "can_manifestation",
			input:                  []byte{0x04},
			expectDeserializeError: true,
		},
		{
			name:                   "can_arbitration_id_invalid_union_tag",
			caseName:               "can_arbitration_id",
			input:                  []byte{0x02},
			expectDeserializeError: true,
		},
		{
			name:                   "node_port_subject_id_list_invalid_union_tag",
			caseName:               "node_port_subject_id_list",
			input:                  []byte{0x03},
			expectDeserializeError: true,
		},
		{
			name:                   "node_port_id_invalid_union_tag",
			caseName:               "node_port_id",
			input:                  []byte{0x02},
			expectDeserializeError: true,
		},
		{
			name:                   "natural8_bad_length_prefix",
			caseName:               "natural8",
			input:                  []byte{0x01, 0x01},
			expectDeserializeError: true,
		},
		{
			name:                   "real16_bad_length_prefix",
			caseName:               "real16",
			input:                  []byte{0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "real32_bad_length_prefix",
			caseName:               "real32",
			input:                  []byte{0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "can_data_classic_bad_length_prefix",
			caseName:               "can_data_classic",
			input:                  []byte{0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "can_data_fd_bad_length_prefix",
			caseName:               "can_data_fd",
			input:                  []byte{0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "bit_array_bad_length_prefix",
			caseName:               "bit_array",
			input:                  []byte{0xFF, 0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "primitive_string_bad_length_prefix",
			caseName:               "primitive_string",
			input:                  []byte{0xFF, 0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "primitive_unstructured_bad_length_prefix",
			caseName:               "primitive_unstructured",
			input:                  []byte{0xFF, 0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "diagnostic_severity_high_bits_input",
			caseName:               "diagnostic_severity",
			input:                  []byte{0xFF},
			expectDeserializeError: false,
		},
		{
			name:                   "scalar_bit_high_bits_input",
			caseName:               "scalar_bit",
			input:                  []byte{0xFF},
			expectDeserializeError: false,
		},
		{
			name:                   "scalar_integer8_truncated_input",
			caseName:               "scalar_integer8",
			input:                  []byte{},
			expectDeserializeError: false,
		},
		{
			name:                   "scalar_integer16_truncated_input",
			caseName:               "scalar_integer16",
			input:                  []byte{0x34},
			expectDeserializeError: false,
		},
		{
			name:                   "scalar_integer32_truncated_input",
			caseName:               "scalar_integer32",
			input:                  []byte{0x78, 0x56},
			expectDeserializeError: false,
		},
		{
			name:                   "scalar_integer64_truncated_input",
			caseName:               "scalar_integer64",
			input:                  []byte{0xEF, 0xCD, 0xAB},
			expectDeserializeError: false,
		},
		{
			name:                   "scalar_natural8_truncated_input",
			caseName:               "scalar_natural8",
			input:                  []byte{},
			expectDeserializeError: false,
		},
		{
			name:                   "scalar_natural16_truncated_input",
			caseName:               "scalar_natural16",
			input:                  []byte{0x34},
			expectDeserializeError: false,
		},
		{
			name:                   "scalar_natural32_truncated_input",
			caseName:               "scalar_natural32",
			input:                  []byte{0xAA},
			expectDeserializeError: false,
		},
		{
			name:                   "scalar_natural64_truncated_input",
			caseName:               "scalar_natural64",
			input:                  []byte{0xAA, 0x55},
			expectDeserializeError: false,
		},
		{
			name:                   "scalar_natural8_high_bits_input",
			caseName:               "scalar_natural8",
			input:                  []byte{0xFF},
			expectDeserializeError: false,
		},
		{
			name:                   "node_health_high_bits_input",
			caseName:               "node_health",
			input:                  []byte{0xFF},
			expectDeserializeError: false,
		},
		{
			name:                   "time_system_high_bits_input",
			caseName:               "time_system",
			input:                  []byte{0xFF},
			expectDeserializeError: false,
		},
		{
			name:                   "time_tai_info_high_bits_input",
			caseName:               "time_tai_info",
			input:                  []byte{0xFF, 0xFF},
			expectDeserializeError: false,
		},
		{
			name:                   "can_base_arbitration_id_high_bits_input",
			caseName:               "can_base_arbitration_id",
			input:                  []byte{0xFF, 0xFF, 0xFF, 0xFF},
			expectDeserializeError: false,
		},
		{
			name:                   "can_extended_arbitration_id_high_bits_input",
			caseName:               "can_extended_arbitration_id",
			input:                  []byte{0xFF, 0xFF, 0xFF, 0xFF},
			expectDeserializeError: false,
		},
		{
			name:                   "node_port_service_id_high_bits_input",
			caseName:               "node_port_service_id",
			input:                  []byte{0xFF, 0xFF},
			expectDeserializeError: false,
		},
		{
			name:                   "node_port_subject_id_high_bits_input",
			caseName:               "node_port_subject_id",
			input:                  []byte{0xFF, 0xFF},
			expectDeserializeError: false,
		},
		{
			name:                   "scalar_real16_truncated_input",
			caseName:               "scalar_real16",
			input:                  []byte{0x00},
			expectDeserializeError: false,
		},
		{
			name:                   "scalar_real16_nan_payload",
			caseName:               "scalar_real16",
			input:                  []byte{0x01, 0x7E},
			expectDeserializeError: false,
		},
		{
			name:                   "scalar_real32_truncated_input",
			caseName:               "scalar_real32",
			input:                  []byte{0x00, 0x00},
			expectDeserializeError: false,
		},
		{
			name:                   "scalar_real32_nan_payload",
			caseName:               "scalar_real32",
			input:                  []byte{0x01, 0x00, 0xC0, 0x7F},
			expectDeserializeError: false,
		},
		{
			name:                   "scalar_real64_truncated_input",
			caseName:               "scalar_real64",
			input:                  []byte{0x00, 0x00, 0x00},
			expectDeserializeError: false,
		},
		{
			name:                   "scalar_real64_nan_payload",
			caseName:               "scalar_real64",
			input:                  []byte{0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x7F},
			expectDeserializeError: false,
		},
		{
			name:                   "natural8_truncated_payload",
			caseName:               "natural8",
			input:                  []byte{0x01},
			expectDeserializeError: false,
		},
		{
			name:                   "array_integer8_bad_length_prefix",
			caseName:               "array_integer8",
			input:                  []byte{0xFF, 0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "array_integer16_bad_length_prefix",
			caseName:               "array_integer16",
			input:                  []byte{0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "array_integer16_truncated_payload",
			caseName:               "array_integer16",
			input:                  []byte{0x01, 0x34},
			expectDeserializeError: false,
		},
		{
			name:                   "array_integer32_bad_length_prefix",
			caseName:               "array_integer32",
			input:                  []byte{0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "array_integer32_truncated_payload",
			caseName:               "array_integer32",
			input:                  []byte{0x01, 0xAA, 0xBB},
			expectDeserializeError: false,
		},
		{
			name:                   "array_integer64_bad_length_prefix",
			caseName:               "array_integer64",
			input:                  []byte{0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "array_integer64_truncated_payload",
			caseName:               "array_integer64",
			input:                  []byte{0x01, 0xAA, 0xBB, 0xCC},
			expectDeserializeError: false,
		},
		{
			name:                   "array_natural16_bad_length_prefix",
			caseName:               "array_natural16",
			input:                  []byte{0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "array_natural16_truncated_payload",
			caseName:               "array_natural16",
			input:                  []byte{0x01, 0xCC},
			expectDeserializeError: false,
		},
		{
			name:                   "array_natural32_bad_length_prefix",
			caseName:               "array_natural32",
			input:                  []byte{0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "array_natural32_truncated_payload",
			caseName:               "array_natural32",
			input:                  []byte{0x01, 0xDD, 0xEE},
			expectDeserializeError: false,
		},
		{
			name:                   "array_natural64_bad_length_prefix",
			caseName:               "array_natural64",
			input:                  []byte{0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "array_natural64_truncated_payload",
			caseName:               "array_natural64",
			input:                  []byte{0x01, 0xAA, 0xBB},
			expectDeserializeError: false,
		},
		{
			name:                   "array_real64_bad_length_prefix",
			caseName:               "array_real64",
			input:                  []byte{0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "array_real64_truncated_payload",
			caseName:               "array_real64",
			input:                  []byte{0x01, 0x00, 0x00, 0xF0},
			expectDeserializeError: false,
		},
		{
			name:                   "file_path_truncated_input",
			caseName:               "file_path",
			input:                  []byte{0xFF},
			expectDeserializeError: false,
		},
		{
			name:                   "execute_command_response_bad_length_prefix",
			caseName:               "execute_command_response",
			input:                  []byte{0x00, 0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "get_info_response_bad_name_length_prefix",
			caseName:               "get_info_response",
			input:                  makeNodeGetInfoBadNameLengthVector(),
			expectDeserializeError: true,
		},
		{
			name:                   "get_transport_statistics_response_bad_length_prefix",
			caseName:               "get_transport_statistics_response",
			input:                  []byte{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "register_access_response_bad_nested_union_tag",
			caseName:               "register_access_response",
			input:                  []byte{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "udp_outgoing_packet_bad_destination_address_length",
			caseName:               "udp_outgoing_packet",
			input:                  []byte{0x00, 0x00, 0x00, 0x00, 0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "udp_handle_incoming_request_bad_payload_length",
			caseName:               "udp_handle_incoming_request",
			input:                  []byte{0x00, 0x00, 0xFF, 0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "file_list_response_large_delimiter_header_truncation",
			caseName:               "file_list_response",
			input:                  []byte{0x64, 0x00, 0x00, 0x00},
			expectDeserializeError: false,
		},
		{
			name:                   "port_list_bad_delimiter_header",
			caseName:               "node_port_list",
			input:                  []byte{0x64, 0x00, 0x00, 0x00, 0x00},
			expectDeserializeError: true,
		},
		{
			name:                   "heartbeat_truncated_input",
			caseName:               "heartbeat",
			input:                  []byte{0xA5},
			expectDeserializeError: false,
		},
		{
			name:                   "execute_command_request_truncated_input",
			caseName:               "execute_command_request",
			input:                  []byte{0x34, 0x12},
			expectDeserializeError: false,
		},
		{
			name:                   "node_id_allocation_truncated_input",
			caseName:               "node_id_allocation_data",
			input:                  []byte{0x00},
			expectDeserializeError: false,
		},
		{
			name:                   "diagnostic_record_truncated_input",
			caseName:               "diagnostic_record",
			input:                  []byte{0x00, 0x00, 0x00},
			expectDeserializeError: false,
		},
		{
			name:                   "time_synchronization_truncated_input",
			caseName:               "time_synchronization",
			input:                  []byte{0xAA},
			expectDeserializeError: false,
		},
		{
			name:                   "time_synchronized_timestamp_truncated_input",
			caseName:               "time_synchronized_timestamp",
			input:                  []byte{0xAA},
			expectDeserializeError: false,
		},
		{
			name:                   "time_system_truncated_input",
			caseName:               "time_system",
			input:                  []byte{},
			expectDeserializeError: false,
		},
		{
			name:                   "time_tai_info_truncated_input",
			caseName:               "time_tai_info",
			input:                  []byte{0xAB},
			expectDeserializeError: false,
		},
		{
			name:                   "get_sync_master_info_request_truncated_input",
			caseName:               "get_sync_master_info_request",
			input:                  []byte{},
			expectDeserializeError: false,
		},
		{
			name:                   "get_sync_master_info_response_truncated_input",
			caseName:               "get_sync_master_info_response",
			input:                  []byte{},
			expectDeserializeError: false,
		},
		{
			name:                   "si_unit_angle_quaternion_truncated_input",
			caseName:               "si_unit_angle_quaternion",
			input:                  []byte{0x00, 0x00, 0x80, 0x3F},
			expectDeserializeError: false,
		},
		{
			name:                   "si_unit_length_wide_vector3_truncated_input",
			caseName:               "si_unit_length_wide_vector3",
			input:                  []byte{0x00, 0x00, 0x00, 0x00, 0x00},
			expectDeserializeError: false,
		},
		{
			name:                   "si_sample_angle_quaternion_truncated_input",
			caseName:               "si_sample_angle_quaternion",
			input:                  []byte{0x00, 0x00, 0x00},
			expectDeserializeError: false,
		},
		{
			name:                   "si_unit_velocity_vector3_truncated_input",
			caseName:               "si_unit_velocity_vector3",
			input:                  []byte{0x00, 0x00, 0x80, 0x3F},
			expectDeserializeError: false,
		},
		{
			name:                   "si_sample_velocity_vector3_truncated_input",
			caseName:               "si_sample_velocity_vector3",
			input:                  []byte{0x00, 0x00, 0x00},
			expectDeserializeError: false,
		},
		{
			name:                   "si_unit_temperature_scalar_truncated_input",
			caseName:               "si_unit_temperature_scalar",
			input:                  []byte{0x00},
			expectDeserializeError: false,
		},
		{
			name:                   "si_sample_temperature_scalar_truncated_input",
			caseName:               "si_sample_temperature_scalar",
			input:                  []byte{0x00},
			expectDeserializeError: false,
		},
		{
			name:                   "si_unit_acceleration_vector3_truncated_input",
			caseName:               "si_unit_acceleration_vector3",
			input:                  []byte{0x00, 0x00, 0x80, 0x3F},
			expectDeserializeError: false,
		},
		{
			name:                   "si_unit_force_vector3_truncated_input",
			caseName:               "si_unit_force_vector3",
			input:                  []byte{0x00, 0x00, 0x80, 0x3F},
			expectDeserializeError: false,
		},
		{
			name:                   "si_unit_torque_vector3_truncated_input",
			caseName:               "si_unit_torque_vector3",
			input:                  []byte{0x00, 0x00, 0x80, 0x3F},
			expectDeserializeError: false,
		},
		{
			name:                   "si_sample_acceleration_vector3_truncated_input",
			caseName:               "si_sample_acceleration_vector3",
			input:                  []byte{0x00, 0x00, 0x80, 0x3F},
			expectDeserializeError: false,
		},
		{
			name:                   "si_sample_force_vector3_truncated_input",
			caseName:               "si_sample_force_vector3",
			input:                  []byte{0x00, 0x00, 0x80, 0x3F},
			expectDeserializeError: false,
		},
		{
			name:                   "si_sample_torque_vector3_truncated_input",
			caseName:               "si_sample_torque_vector3",
			input:                  []byte{0x00, 0x00, 0x80, 0x3F},
			expectDeserializeError: false,
		},
		{
			name:                   "si_unit_voltage_scalar_truncated_input",
			caseName:               "si_unit_voltage_scalar",
			input:                  []byte{0x00},
			expectDeserializeError: false,
		},
		{
			name:                   "si_sample_voltage_scalar_truncated_input",
			caseName:               "si_sample_voltage_scalar",
			input:                  []byte{0x00},
			expectDeserializeError: false,
		},
		{
			name:                   "file_read_response_truncated_input",
			caseName:               "file_read_response",
			input:                  []byte{0x00},
			expectDeserializeError: false,
		},
		{
			name:                   "can_data_classic_truncated_input",
			caseName:               "can_data_classic",
			input:                  []byte{0x01, 0x02, 0x03},
			expectDeserializeError: false,
		},
		{
			name:                   "can_data_fd_truncated_input",
			caseName:               "can_data_fd",
			input:                  []byte{0x01, 0x02, 0x03},
			expectDeserializeError: false,
		},
		{
			name:                   "can_error_truncated_input",
			caseName:               "can_error",
			input:                  []byte{0x00},
			expectDeserializeError: false,
		},
		{
			name:                   "can_rtr_truncated_input",
			caseName:               "can_rtr",
			input:                  []byte{0x00},
			expectDeserializeError: false,
		},
		{
			name:                   "can_manifestation_truncated_input",
			caseName:               "can_manifestation",
			input:                  []byte{0x00},
			expectDeserializeError: false,
		},
		{
			name:                   "can_arbitration_id_truncated_input",
			caseName:               "can_arbitration_id",
			input:                  []byte{0x00},
			expectDeserializeError: false,
		},
		{
			name:                   "can_base_arbitration_id_truncated_input",
			caseName:               "can_base_arbitration_id",
			input:                  []byte{0xAA},
			expectDeserializeError: false,
		},
		{
			name:                   "can_extended_arbitration_id_truncated_input",
			caseName:               "can_extended_arbitration_id",
			input:                  []byte{0xAA},
			expectDeserializeError: false,
		},
		{
			name:                   "file_write_request_truncated_input",
			caseName:               "file_write_request",
			input:                  []byte{},
			expectDeserializeError: false,
		},
		{
			name:                   "file_write_response_truncated_input",
			caseName:               "file_write_response",
			input:                  []byte{},
			expectDeserializeError: false,
		},
		{
			name:                   "file_modify_request_truncated_input",
			caseName:               "file_modify_request",
			input:                  []byte{},
			expectDeserializeError: false,
		},
		{
			name:                   "file_modify_response_truncated_input",
			caseName:               "file_modify_response",
			input:                  []byte{},
			expectDeserializeError: false,
		},
		{
			name:                   "file_get_info_request_truncated_input",
			caseName:               "file_get_info_request",
			input:                  []byte{},
			expectDeserializeError: false,
		},
		{
			name:                   "file_get_info_response_truncated_input",
			caseName:               "file_get_info_response",
			input:                  []byte{},
			expectDeserializeError: false,
		},
		{
			name:                   "node_version_truncated_input",
			caseName:               "node_version",
			input:                  []byte{0xAB},
			expectDeserializeError: false,
		},
		{
			name:                   "node_health_truncated_input",
			caseName:               "node_health",
			input:                  []byte{},
			expectDeserializeError: false,
		},
		{
			name:                   "node_io_statistics_truncated_input",
			caseName:               "node_io_statistics",
			input:                  []byte{0x01, 0x02},
			expectDeserializeError: false,
		},
		{
			name:                   "get_transport_statistics_request_truncated_input",
			caseName:               "get_transport_statistics_request",
			input:                  []byte{},
			expectDeserializeError: false,
		},
		{
			name:                   "get_transport_statistics_response_truncated_input",
			caseName:               "get_transport_statistics_response",
			input:                  []byte{},
			expectDeserializeError: false,
		},
		{
			name:                   "node_id_truncated_input",
			caseName:               "node_id",
			input:                  []byte{0xAB},
			expectDeserializeError: false,
		},
		{
			name:                   "node_mode_truncated_input",
			caseName:               "node_mode",
			input:                  []byte{},
			expectDeserializeError: false,
		},
		{
			name:                   "register_name_truncated_input",
			caseName:               "register_name",
			input:                  []byte{0x01},
			expectDeserializeError: false,
		},
		{
			name:                   "register_list_request_truncated_input",
			caseName:               "register_list_request",
			input:                  []byte{0xAB},
			expectDeserializeError: false,
		},
		{
			name:                   "register_list_response_truncated_input",
			caseName:               "register_list_response",
			input:                  []byte{0x02, 0x41},
			expectDeserializeError: false,
		},
		{
			name:                   "metatransport_udp_endpoint_truncated_input",
			caseName:               "metatransport_udp_endpoint",
			input:                  []byte{0x01, 0x02},
			expectDeserializeError: false,
		},
		{
			name:                   "metatransport_ethernet_ethertype_truncated_input",
			caseName:               "metatransport_ethernet_ethertype",
			input:                  []byte{0xAA},
			expectDeserializeError: false,
		},
		{
			name:                   "metatransport_serial_fragment_bad_length_prefix",
			caseName:               "metatransport_serial_fragment",
			input:                  []byte{0xFF, 0x0F},
			expectDeserializeError: true,
		},
		{
			name:                   "metatransport_ethernet_frame_bad_length_prefix",
			caseName:               "metatransport_ethernet_frame",
			input:                  []byte{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x3F},
			expectDeserializeError: true,
		},
		{
			name:                   "metatransport_serial_fragment_truncated_input",
			caseName:               "metatransport_serial_fragment",
			input:                  []byte{0xAA},
			expectDeserializeError: false,
		},
		{
			name:                   "metatransport_ethernet_frame_truncated_input",
			caseName:               "metatransport_ethernet_frame",
			input:                  []byte{0xAA, 0xBB},
			expectDeserializeError: false,
		},
		{
			name:                   "metatransport_udp_frame_truncated_input",
			caseName:               "metatransport_udp_frame",
			input:                  []byte{0xAA, 0xBB},
			expectDeserializeError: false,
		},
		{
			name:                   "pnp_cluster_entry_truncated_input",
			caseName:               "pnp_cluster_entry",
			input:                  []byte{0xAA, 0xBB},
			expectDeserializeError: false,
		},
		{
			name:                   "pnp_cluster_append_entries_request_bad_length_prefix",
			caseName:               "pnp_cluster_append_entries_request",
			input:                  []byte{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "pnp_cluster_append_entries_request_truncated_input",
			caseName:               "pnp_cluster_append_entries_request",
			input:                  []byte{0xAA, 0xBB, 0xCC},
			expectDeserializeError: false,
		},
		{
			name:                   "pnp_cluster_append_entries_response_truncated_input",
			caseName:               "pnp_cluster_append_entries_response",
			input:                  []byte{0xAA},
			expectDeserializeError: false,
		},
		{
			name:                   "pnp_cluster_request_vote_request_truncated_input",
			caseName:               "pnp_cluster_request_vote_request",
			input:                  []byte{0xAA, 0xBB},
			expectDeserializeError: false,
		},
		{
			name:                   "pnp_cluster_request_vote_response_truncated_input",
			caseName:               "pnp_cluster_request_vote_response",
			input:                  []byte{0xAA},
			expectDeserializeError: false,
		},
		{
			name:                   "pnp_cluster_discovery_bad_length_prefix",
			caseName:               "pnp_cluster_discovery",
			input:                  []byte{0x00, 0xFF},
			expectDeserializeError: true,
		},
		{
			name:                   "pnp_cluster_discovery_truncated_input",
			caseName:               "pnp_cluster_discovery",
			input:                  []byte{0x00},
			expectDeserializeError: false,
		},
		{
			name:                   "node_port_service_id_truncated_input",
			caseName:               "node_port_service_id",
			input:                  []byte{0xAA},
			expectDeserializeError: false,
		},
		{
			name:                   "node_port_subject_id_truncated_input",
			caseName:               "node_port_subject_id",
			input:                  []byte{0xAA},
			expectDeserializeError: false,
		},
		{
			name:                   "node_port_service_id_list_truncated_input",
			caseName:               "node_port_service_id_list",
			input:                  []byte{0xAA},
			expectDeserializeError: false,
		},
		{
			name:                   "node_port_subject_id_list_truncated_input",
			caseName:               "node_port_subject_id_list",
			input:                  []byte{0x01},
			expectDeserializeError: false,
		},
		{
			name:                   "node_port_id_truncated_input",
			caseName:               "node_port_id",
			input:                  []byte{0x00},
			expectDeserializeError: false,
		},
		{
			name:                   "file_error_truncated_input",
			caseName:               "file_error",
			input:                  []byte{0xAA},
			expectDeserializeError: false,
		},
		{
			name:                   "heartbeat_serialize_small_buffer",
			caseName:               "heartbeat",
			input:                  []byte{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
			useOutputCapacity:      true,
			outputCapacity:         6,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "scalar_real64_serialize_small_buffer",
			caseName:               "scalar_real64",
			input:                  []byte{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
			useOutputCapacity:      true,
			outputCapacity:         7,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "file_path_serialize_small_buffer",
			caseName:               "file_path",
			input:                  []byte{0x00},
			useOutputCapacity:      true,
			outputCapacity:         0,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "file_write_request_serialize_small_buffer",
			caseName:               "file_write_request",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         518,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "file_write_response_serialize_small_buffer",
			caseName:               "file_write_response",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "file_modify_request_serialize_small_buffer",
			caseName:               "file_modify_request",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         515,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "file_modify_response_serialize_small_buffer",
			caseName:               "file_modify_response",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "file_get_info_request_serialize_small_buffer",
			caseName:               "file_get_info_request",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         255,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "file_get_info_response_serialize_small_buffer",
			caseName:               "file_get_info_response",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         12,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "get_transport_statistics_request_serialize_zero_buffer",
			caseName:               "get_transport_statistics_request",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         0,
			expectDeserializeError: false,
			expectSerializeError:   false,
		},
		{
			name:                   "get_transport_statistics_response_serialize_small_buffer",
			caseName:               "get_transport_statistics_response",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         60,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "time_synchronized_timestamp_serialize_small_buffer",
			caseName:               "time_synchronized_timestamp",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         6,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "time_system_serialize_small_buffer",
			caseName:               "time_system",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         0,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "time_tai_info_serialize_small_buffer",
			caseName:               "time_tai_info",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "get_sync_master_info_request_serialize_zero_buffer",
			caseName:               "get_sync_master_info_request",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         0,
			expectDeserializeError: false,
			expectSerializeError:   false,
		},
		{
			name:                   "get_sync_master_info_response_serialize_small_buffer",
			caseName:               "get_sync_master_info_response",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         6,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "primitive_empty_serialize_zero_buffer",
			caseName:               "primitive_empty",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         0,
			expectDeserializeError: false,
			expectSerializeError:   false,
		},
		{
			name:                   "node_id_serialize_small_buffer",
			caseName:               "node_id",
			input:                  []byte{0x00, 0x00},
			useOutputCapacity:      true,
			outputCapacity:         1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "node_mode_serialize_small_buffer",
			caseName:               "node_mode",
			input:                  []byte{0x00},
			useOutputCapacity:      true,
			outputCapacity:         0,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "node_version_serialize_small_buffer",
			caseName:               "node_version",
			input:                  []byte{0x00, 0x00},
			useOutputCapacity:      true,
			outputCapacity:         1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "node_health_serialize_small_buffer",
			caseName:               "node_health",
			input:                  []byte{0x00},
			useOutputCapacity:      true,
			outputCapacity:         0,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "node_io_statistics_serialize_small_buffer",
			caseName:               "node_io_statistics",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         14,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "register_name_serialize_small_buffer",
			caseName:               "register_name",
			input:                  []byte{0x00},
			useOutputCapacity:      true,
			outputCapacity:         0,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "register_list_request_serialize_small_buffer",
			caseName:               "register_list_request",
			input:                  []byte{0x00, 0x00},
			useOutputCapacity:      true,
			outputCapacity:         1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "register_list_response_serialize_small_buffer",
			caseName:               "register_list_response",
			input:                  []byte{0x01, 0x41},
			useOutputCapacity:      true,
			outputCapacity:         128,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "node_port_service_id_serialize_small_buffer",
			caseName:               "node_port_service_id",
			input:                  []byte{0x00, 0x00},
			useOutputCapacity:      true,
			outputCapacity:         nodeport.SERVICE_ID@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "node_port_subject_id_serialize_small_buffer",
			caseName:               "node_port_subject_id",
			input:                  []byte{0x00, 0x00},
			useOutputCapacity:      true,
			outputCapacity:         nodeport.SUBJECT_ID@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "node_port_service_id_list_serialize_small_buffer",
			caseName:               "node_port_service_id_list",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         nodeport.SERVICE_ID_LIST@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "node_port_subject_id_list_serialize_small_buffer",
			caseName:               "node_port_subject_id_list",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         nodeport.SUBJECT_ID_LIST@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "node_port_id_serialize_small_buffer",
			caseName:               "node_port_id",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         nodeport.ID@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "si_unit_velocity_vector3_serialize_small_buffer",
			caseName:               "si_unit_velocity_vector3",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         siunitvelocitypkg.VECTOR3@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "si_sample_velocity_vector3_serialize_small_buffer",
			caseName:               "si_sample_velocity_vector3",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         sisamplevelocitypkg.VECTOR3@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "si_unit_temperature_scalar_serialize_small_buffer",
			caseName:               "si_unit_temperature_scalar",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         siunitemperaturepkg.SCALAR@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "si_sample_temperature_scalar_serialize_small_buffer",
			caseName:               "si_sample_temperature_scalar",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         sisampletemperaturepkg.SCALAR@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "si_unit_acceleration_vector3_serialize_small_buffer",
			caseName:               "si_unit_acceleration_vector3",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         siunitaccelerationpkg.VECTOR3@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "si_unit_force_vector3_serialize_small_buffer",
			caseName:               "si_unit_force_vector3",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         siunitforcepkg.VECTOR3@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "si_unit_torque_vector3_serialize_small_buffer",
			caseName:               "si_unit_torque_vector3",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         siunittorquepkg.VECTOR3@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "si_sample_acceleration_vector3_serialize_small_buffer",
			caseName:               "si_sample_acceleration_vector3",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         sisampleaccelerationpkg.VECTOR3@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "si_sample_force_vector3_serialize_small_buffer",
			caseName:               "si_sample_force_vector3",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         sisampleforcepkg.VECTOR3@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "si_sample_torque_vector3_serialize_small_buffer",
			caseName:               "si_sample_torque_vector3",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         sisampletorquepkg.VECTOR3@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "si_unit_voltage_scalar_serialize_small_buffer",
			caseName:               "si_unit_voltage_scalar",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         siunitvoltagepkg.SCALAR@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "si_sample_voltage_scalar_serialize_small_buffer",
			caseName:               "si_sample_voltage_scalar",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         sisamplevoltagepkg.SCALAR@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "file_error_serialize_small_buffer",
			caseName:               "file_error",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         filepkg.ERROR@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "metatransport_udp_endpoint_serialize_small_buffer",
			caseName:               "metatransport_udp_endpoint",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         31,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "metatransport_ethernet_ethertype_serialize_small_buffer",
			caseName:               "metatransport_ethernet_ethertype",
			input:                  []byte{0x00, 0x00},
			useOutputCapacity:      true,
			outputCapacity:         metaethernetpkg.ETHER_TYPE@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "metatransport_serial_fragment_serialize_small_buffer",
			caseName:               "metatransport_serial_fragment",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         0,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "metatransport_ethernet_frame_serialize_small_buffer",
			caseName:               "metatransport_ethernet_frame",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         15,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "metatransport_udp_frame_serialize_small_buffer",
			caseName:               "metatransport_udp_frame",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         31,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "pnp_cluster_entry_serialize_small_buffer",
			caseName:               "pnp_cluster_entry",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         pnpclusterpkg.ENTRY@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "pnp_cluster_append_entries_request_serialize_small_buffer",
			caseName:               "pnp_cluster_append_entries_request",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         pnpclusterpkg.APPEND_ENTRIES@V1_0@_REQUEST_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "pnp_cluster_append_entries_response_serialize_small_buffer",
			caseName:               "pnp_cluster_append_entries_response",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         pnpclusterpkg.APPEND_ENTRIES@V1_0@_RESPONSE_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "pnp_cluster_request_vote_request_serialize_small_buffer",
			caseName:               "pnp_cluster_request_vote_request",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         pnpclusterpkg.REQUEST_VOTE@V1_0@_REQUEST_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "pnp_cluster_request_vote_response_serialize_small_buffer",
			caseName:               "pnp_cluster_request_vote_response",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         pnpclusterpkg.REQUEST_VOTE@V1_0@_RESPONSE_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "pnp_cluster_discovery_serialize_small_buffer",
			caseName:               "pnp_cluster_discovery",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         pnpclusterpkg.DISCOVERY@V1_0@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "can_data_classic_serialize_small_buffer",
			caseName:               "can_data_classic",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         13,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "can_data_fd_serialize_small_buffer",
			caseName:               "can_data_fd",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         metacan.DATA_FD@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "can_error_serialize_small_buffer",
			caseName:               "can_error",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         3,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "can_rtr_serialize_small_buffer",
			caseName:               "can_rtr",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         metacan.RTR@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "can_manifestation_serialize_small_buffer",
			caseName:               "can_manifestation",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         metacan.MANIFESTATION@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "can_arbitration_id_serialize_small_buffer",
			caseName:               "can_arbitration_id",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         metacan.ARBITRATION_ID@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "can_base_arbitration_id_serialize_small_buffer",
			caseName:               "can_base_arbitration_id",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         metacan.BASE_ARBITRATION_ID@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
		{
			name:                   "can_extended_arbitration_id_serialize_small_buffer",
			caseName:               "can_extended_arbitration_id",
			input:                  []byte{},
			useOutputCapacity:      true,
			outputCapacity:         metacan.EXTENDED_ARBITRATION_ID@V0_1@_SERIALIZATION_BUFFER_SIZE_BYTES - 1,
			expectDeserializeError: false,
			expectSerializeError:   true,
		},
	}
	directed = ensureDirectedBaselineCoverage(cases, directed)
	if err := validateCaseInventory(cases, directed); err != nil {
		return err
	}
	if err := validateDirectedCoverage(cases, directed); err != nil {
		return err
	}

	var rng uint64 = 0xC001D00DCAFEBEEF
	for i := range cases {
		if err := runRandomParityCase(&cases[i], &rng); err != nil {
			return err
		}
		category := classifyRandomCase(cases[i].name)
		randomCategoryCounts[category]++
	}
	for _, v := range directed {
		if err := runDirectedVector(v, caseByName); err != nil {
			return err
		}
		category := classifyDirectedVector(v.name)
		directedCategoryCounts[category]++
	}

	// Float16 coverage: ensure NaN payload handling aligns between C and Go paths.
	if tc, ok := caseByName["real16"]; ok {
		nanInput := []byte{0x01, 0x00, 0x7E} // length=1, qNaN half payload
		var input [maxIOBuffer]byte
		var cOutput [maxIOBuffer]byte
		var goOutput [maxIOBuffer]byte
		copy(input[:], nanInput)
		outcome, err := runParityOnce(tc, input[:], len(nanInput), cOutput[:], goOutput[:], tc.maxSerialized)
		if err != nil {
			return fmt.Errorf("real16_nan_vector: %w", err)
		}
		if outcome.cDeserializeRC >= 0 {
			var obj primarray.Real16@V1_0@
			rc, _ := obj.Deserialize(nanInput)
			if rc >= 0 && len(obj.Value) == 1 && !math.IsNaN(float64(obj.Value[0])) {
				return fmt.Errorf("real16_nan_vector: expected NaN value after deserialize")
			}
		}
		fmt.Println("PASS real16_nan_vector directed")
		directedCategoryCounts[classifyDirectedVector("real16_nan_vector")]++
	}

	// Float32 signaling-NaN payload must survive deserialize->serialize byte-exactly in both C and
	// Go. Regression guard for the float32 -> float64 -> float32 round-trip that quieted signaling
	// NaNs (the quiet bit 0x40 in byte[2] must stay clear, i.e. 0x80 not 0xC0). float16 is
	// intentionally excluded: the shared runtime canonicalizes half-precision NaN payloads to
	// 0x7E00 for every backend, so there is no stable payload to preserve there.
	if tc, ok := caseByName["scalar_real32"]; ok {
		golden := []byte{0x01, 0x00, 0x80, 0x7F} // float32 sNaN
		var input [maxIOBuffer]byte
		var cOutput [maxIOBuffer]byte
		var goOutput [maxIOBuffer]byte
		copy(input[:], golden)
		outcome, err := runParityOnce(tc, input[:], len(golden), cOutput[:], goOutput[:], tc.maxSerialized)
		if err != nil {
			return fmt.Errorf("scalar_real32_signaling_nan_payload: %w", err)
		}
		if outcome.cSerializeSize != len(golden) || outcome.goSerializeSize != len(golden) ||
			!bytes.Equal(cOutput[:len(golden)], golden) || !bytes.Equal(goOutput[:len(golden)], golden) {
			return fmt.Errorf(
				"scalar_real32_signaling_nan_payload: signaling NaN payload not preserved: golden=[%s] c=[%s] go=[%s]",
				formatBytes(golden),
				formatBytes(cOutput[:outcome.cSerializeSize]),
				formatBytes(goOutput[:outcome.goSerializeSize]),
			)
		}
		fmt.Println("PASS scalar_real32_signaling_nan_payload directed")
		directedCategoryCounts[classifyDirectedVector("scalar_real32_signaling_nan_payload")]++
	}

	emitCategorySummary("random", randomCategoryCounts)
	emitCategorySummary("directed", directedCategoryCounts)

	fmt.Printf(
		"PASS c/go parity random_iterations=%d random_cases=%d directed_cases=%d\n",
		iterations,
		len(cases),
		len(directed),
	)
	return nil
}

func main() {
	iterations := 128
	if len(os.Args) > 1 {
		parsed, err := strconv.Atoi(os.Args[1])
		if err != nil || parsed <= 0 {
			fmt.Fprintf(os.Stderr, "invalid iteration count %q\n", os.Args[1])
			os.Exit(2)
		}
		iterations = parsed
	}

	if err := runCGoParity(iterations); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
