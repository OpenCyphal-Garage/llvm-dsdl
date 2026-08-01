//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// C ABI harness for C-vs-Go differential parity integration tests.
///
/// This harness exposes roundtrip entry points for generated C types so the Go
/// parity driver can compare deserialize/serialize behaviour and payload bytes.
///
//===----------------------------------------------------------------------===//

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "uavcan/node/Heartbeat_1_0.h"
#include "uavcan/node/ExecuteCommand_1_3.h"
#include "uavcan/node/GetInfo_1_0.h"
#include "uavcan/node/ID_1_0.h"
#include "uavcan/node/Mode_1_0.h"
#include "uavcan/node/Version_1_0.h"
#include "uavcan/node/Health_1_0.h"
#include "uavcan/node/IOStatistics_0_1.h"
#include "uavcan/diagnostic/Record_1_1.h"
#include "uavcan/diagnostic/Severity_1_0.h"
#include "uavcan/register/Value_1_0.h"
#include "uavcan/register/Access_1_0.h"
#include "uavcan/register/Name_1_0.h"
#include "uavcan/register/List_1_0.h"
#include "uavcan/file/List_0_2.h"
#include "uavcan/file/Read_1_1.h"
#include "uavcan/file/Write_1_1.h"
#include "uavcan/file/Modify_1_1.h"
#include "uavcan/file/GetInfo_0_2.h"
#include "uavcan/file/Error_1_0.h"
#include "uavcan/node/GetTransportStatistics_0_1.h"
#include "uavcan/time/Synchronization_1_0.h"
#include "uavcan/time/SynchronizedTimestamp_1_0.h"
#include "uavcan/time/TimeSystem_0_1.h"
#include "uavcan/time/TAIInfo_0_1.h"
#include "uavcan/time/GetSynchronizationMasterInfo_0_1.h"
#include "uavcan/metatransport/can/Frame_0_2.h"
#include "uavcan/metatransport/can/DataClassic_0_1.h"
#include "uavcan/metatransport/can/DataFD_0_1.h"
#include "uavcan/metatransport/can/Error_0_1.h"
#include "uavcan/metatransport/can/RTR_0_1.h"
#include "uavcan/metatransport/can/Manifestation_0_1.h"
#include "uavcan/metatransport/can/ArbitrationID_0_1.h"
#include "uavcan/metatransport/can/BaseArbitrationID_0_1.h"
#include "uavcan/metatransport/can/ExtendedArbitrationID_0_1.h"
#include "uavcan/metatransport/serial/Fragment_0_2.h"
#include "uavcan/metatransport/ethernet/Frame_0_1.h"
#include "uavcan/metatransport/udp/Endpoint_0_1.h"
#include "uavcan/metatransport/udp/Frame_0_1.h"
#include "uavcan/internet/udp/OutgoingPacket_0_2.h"
#include "uavcan/internet/udp/HandleIncomingPacket_0_2.h"
#include "uavcan/si/unit/angle/Quaternion_1_0.h"
#include "uavcan/si/unit/acceleration/Vector3_1_0.h"
#include "uavcan/si/unit/force/Vector3_1_0.h"
#include "uavcan/si/unit/length/WideVector3_1_0.h"
#include "uavcan/si/unit/torque/Vector3_1_0.h"
#include "uavcan/si/unit/velocity/Vector3_1_0.h"
#include "uavcan/si/unit/temperature/Scalar_1_0.h"
#include "uavcan/si/unit/voltage/Scalar_1_0.h"
#include "uavcan/si/sample/angle/Quaternion_1_0.h"
#include "uavcan/si/sample/acceleration/Vector3_1_0.h"
#include "uavcan/si/sample/force/Vector3_1_0.h"
#include "uavcan/si/sample/torque/Vector3_1_0.h"
#include "uavcan/si/sample/velocity/Vector3_1_0.h"
#include "uavcan/si/sample/temperature/Scalar_1_0.h"
#include "uavcan/si/sample/voltage/Scalar_1_0.h"
#include "uavcan/primitive/array/Natural8_1_0.h"
#include "uavcan/primitive/array/Real16_1_0.h"
#include "uavcan/primitive/array/Real32_1_0.h"
#include "uavcan/primitive/array/Bit_1_0.h"
#include "uavcan/primitive/array/Integer8_1_0.h"
#include "uavcan/primitive/array/Integer16_1_0.h"
#include "uavcan/primitive/array/Integer32_1_0.h"
#include "uavcan/primitive/array/Integer64_1_0.h"
#include "uavcan/primitive/array/Natural16_1_0.h"
#include "uavcan/primitive/array/Natural32_1_0.h"
#include "uavcan/primitive/array/Natural64_1_0.h"
#include "uavcan/primitive/array/Real64_1_0.h"
#include "uavcan/primitive/scalar/Bit_1_0.h"
#include "uavcan/primitive/scalar/Integer8_1_0.h"
#include "uavcan/primitive/scalar/Integer16_1_0.h"
#include "uavcan/primitive/scalar/Integer32_1_0.h"
#include "uavcan/primitive/scalar/Integer64_1_0.h"
#include "uavcan/primitive/scalar/Natural8_1_0.h"
#include "uavcan/primitive/scalar/Natural16_1_0.h"
#include "uavcan/primitive/scalar/Natural32_1_0.h"
#include "uavcan/primitive/scalar/Natural64_1_0.h"
#include "uavcan/primitive/scalar/Real16_1_0.h"
#include "uavcan/primitive/scalar/Real32_1_0.h"
#include "uavcan/primitive/scalar/Real64_1_0.h"
#include "uavcan/primitive/Empty_1_0.h"
#include "uavcan/primitive/String_1_0.h"
#include "uavcan/primitive/Unstructured_1_0.h"
#include "uavcan/file/Path_2_0.h"
#include "uavcan/pnp/NodeIDAllocationData_2_0.h"
#include "uavcan/pnp/cluster/Entry_1_0.h"
#include "uavcan/pnp/cluster/AppendEntries_1_0.h"
#include "uavcan/pnp/cluster/RequestVote_1_0.h"
#include "uavcan/pnp/cluster/Discovery_1_0.h"
#include "uavcan/node/port/ServiceID_1_0.h"
#include "uavcan/node/port/SubjectID_1_0.h"
#include "uavcan/node/port/ServiceIDList_1_0.h"
#include "uavcan/node/port/SubjectIDList_1_0.h"
#include "uavcan/node/port/ID_1_0.h"
#include "uavcan/node/port/List_1_0.h"
#include "uavcan/metatransport/ethernet/EtherType_0_1.h"

typedef struct CCaseResult
{
    int8_t deserialize_rc;
    size_t deserialize_consumed;
    int8_t serialize_rc;
    size_t serialize_size;
} CCaseResult;

#define DEFINE_ROUNDTRIP(FN_NAME, TYPE, DESERIALIZE_FN, SERIALIZE_FN)          \
    int FN_NAME(const uint8_t* const input,                                    \
                const size_t         input_size,                               \
                uint8_t* const       output,                                   \
                const size_t         output_capacity,                          \
                CCaseResult* const   result)                                   \
    {                                                                          \
        if ((input == NULL) || (output == NULL) || (result == NULL))           \
        {                                                                      \
            return -1;                                                         \
        }                                                                      \
        TYPE obj;                                                              \
        memset(&obj, 0, sizeof(obj));                                          \
        size_t       consumed        = input_size;                             \
        const int8_t des             = DESERIALIZE_FN(&obj, input, &consumed); \
        result->deserialize_rc       = des;                                    \
        result->deserialize_consumed = consumed;                               \
        result->serialize_rc         = 0;                                      \
        result->serialize_size       = 0;                                      \
        if (des < 0)                                                           \
        {                                                                      \
            result->deserialize_consumed = 0;                                  \
            return 0;                                                          \
        }                                                                      \
        size_t       out_size  = output_capacity;                              \
        const int8_t ser       = SERIALIZE_FN(&obj, output, &out_size);        \
        result->serialize_rc   = ser;                                          \
        result->serialize_size = out_size;                                     \
        return 0;                                                              \
    }

DEFINE_ROUNDTRIP(c_heartbeat_roundtrip,
                 uavcan__node__Heartbeat,
                 uavcan__node__Heartbeat__deserialize_,
                 uavcan__node__Heartbeat__serialize_)

DEFINE_ROUNDTRIP(c_execute_command_request_roundtrip,
                 uavcan__node__ExecuteCommand__Request,
                 uavcan__node__ExecuteCommand__Request__deserialize_,
                 uavcan__node__ExecuteCommand__Request__serialize_)

DEFINE_ROUNDTRIP(c_execute_command_response_roundtrip,
                 uavcan__node__ExecuteCommand__Response,
                 uavcan__node__ExecuteCommand__Response__deserialize_,
                 uavcan__node__ExecuteCommand__Response__serialize_)

DEFINE_ROUNDTRIP(c_node_id_roundtrip, uavcan__node__ID, uavcan__node__ID__deserialize_, uavcan__node__ID__serialize_)

DEFINE_ROUNDTRIP(c_node_mode_roundtrip,
                 uavcan__node__Mode,
                 uavcan__node__Mode__deserialize_,
                 uavcan__node__Mode__serialize_)

DEFINE_ROUNDTRIP(c_node_version_roundtrip,
                 uavcan__node__Version,
                 uavcan__node__Version__deserialize_,
                 uavcan__node__Version__serialize_)

DEFINE_ROUNDTRIP(c_node_health_roundtrip,
                 uavcan__node__Health,
                 uavcan__node__Health__deserialize_,
                 uavcan__node__Health__serialize_)

DEFINE_ROUNDTRIP(c_node_io_statistics_roundtrip,
                 uavcan__node__IOStatistics,
                 uavcan__node__IOStatistics__deserialize_,
                 uavcan__node__IOStatistics__serialize_)

DEFINE_ROUNDTRIP(c_get_info_response_roundtrip,
                 uavcan__node__GetInfo__Response,
                 uavcan__node__GetInfo__Response__deserialize_,
                 uavcan__node__GetInfo__Response__serialize_)

DEFINE_ROUNDTRIP(c_diagnostic_record_roundtrip,
                 uavcan__diagnostic__Record,
                 uavcan__diagnostic__Record__deserialize_,
                 uavcan__diagnostic__Record__serialize_)

DEFINE_ROUNDTRIP(c_diagnostic_severity_roundtrip,
                 uavcan__diagnostic__Severity,
                 uavcan__diagnostic__Severity__deserialize_,
                 uavcan__diagnostic__Severity__serialize_)

DEFINE_ROUNDTRIP(c_register_value_roundtrip,
                 uavcan__register___Value,
                 uavcan__register___Value__deserialize_,
                 uavcan__register___Value__serialize_)

DEFINE_ROUNDTRIP(c_register_access_request_roundtrip,
                 uavcan__register___Access__Request,
                 uavcan__register___Access__Request__deserialize_,
                 uavcan__register___Access__Request__serialize_)

DEFINE_ROUNDTRIP(c_register_access_response_roundtrip,
                 uavcan__register___Access__Response,
                 uavcan__register___Access__Response__deserialize_,
                 uavcan__register___Access__Response__serialize_)

DEFINE_ROUNDTRIP(c_register_name_roundtrip,
                 uavcan__register___Name,
                 uavcan__register___Name__deserialize_,
                 uavcan__register___Name__serialize_)

DEFINE_ROUNDTRIP(c_register_list_request_roundtrip,
                 uavcan__register___List__Request,
                 uavcan__register___List__Request__deserialize_,
                 uavcan__register___List__Request__serialize_)

DEFINE_ROUNDTRIP(c_register_list_response_roundtrip,
                 uavcan__register___List__Response,
                 uavcan__register___List__Response__deserialize_,
                 uavcan__register___List__Response__serialize_)

DEFINE_ROUNDTRIP(c_file_list_request_roundtrip,
                 uavcan__file__List__Request,
                 uavcan__file__List__Request__deserialize_,
                 uavcan__file__List__Request__serialize_)

DEFINE_ROUNDTRIP(c_file_list_response_roundtrip,
                 uavcan__file__List__Response,
                 uavcan__file__List__Response__deserialize_,
                 uavcan__file__List__Response__serialize_)

DEFINE_ROUNDTRIP(c_file_read_request_roundtrip,
                 uavcan__file__Read__Request,
                 uavcan__file__Read__Request__deserialize_,
                 uavcan__file__Read__Request__serialize_)

DEFINE_ROUNDTRIP(c_file_read_response_roundtrip,
                 uavcan__file__Read__Response,
                 uavcan__file__Read__Response__deserialize_,
                 uavcan__file__Read__Response__serialize_)

DEFINE_ROUNDTRIP(c_file_write_request_roundtrip,
                 uavcan__file__Write__Request,
                 uavcan__file__Write__Request__deserialize_,
                 uavcan__file__Write__Request__serialize_)

DEFINE_ROUNDTRIP(c_file_write_response_roundtrip,
                 uavcan__file__Write__Response,
                 uavcan__file__Write__Response__deserialize_,
                 uavcan__file__Write__Response__serialize_)

DEFINE_ROUNDTRIP(c_file_modify_request_roundtrip,
                 uavcan__file__Modify__Request,
                 uavcan__file__Modify__Request__deserialize_,
                 uavcan__file__Modify__Request__serialize_)

DEFINE_ROUNDTRIP(c_file_modify_response_roundtrip,
                 uavcan__file__Modify__Response,
                 uavcan__file__Modify__Response__deserialize_,
                 uavcan__file__Modify__Response__serialize_)

DEFINE_ROUNDTRIP(c_file_get_info_request_roundtrip,
                 uavcan__file__GetInfo__Request,
                 uavcan__file__GetInfo__Request__deserialize_,
                 uavcan__file__GetInfo__Request__serialize_)

DEFINE_ROUNDTRIP(c_file_get_info_response_roundtrip,
                 uavcan__file__GetInfo__Response,
                 uavcan__file__GetInfo__Response__deserialize_,
                 uavcan__file__GetInfo__Response__serialize_)

DEFINE_ROUNDTRIP(c_file_error_roundtrip,
                 uavcan__file__Error,
                 uavcan__file__Error__deserialize_,
                 uavcan__file__Error__serialize_)

DEFINE_ROUNDTRIP(c_get_transport_statistics_request_roundtrip,
                 uavcan__node__GetTransportStatistics__Request,
                 uavcan__node__GetTransportStatistics__Request__deserialize_,
                 uavcan__node__GetTransportStatistics__Request__serialize_)

DEFINE_ROUNDTRIP(c_get_transport_statistics_response_roundtrip,
                 uavcan__node__GetTransportStatistics__Response,
                 uavcan__node__GetTransportStatistics__Response__deserialize_,
                 uavcan__node__GetTransportStatistics__Response__serialize_)

DEFINE_ROUNDTRIP(c_can_frame_roundtrip,
                 uavcan__metatransport__can__Frame,
                 uavcan__metatransport__can__Frame__deserialize_,
                 uavcan__metatransport__can__Frame__serialize_)

DEFINE_ROUNDTRIP(c_can_data_classic_roundtrip,
                 uavcan__metatransport__can__DataClassic,
                 uavcan__metatransport__can__DataClassic__deserialize_,
                 uavcan__metatransport__can__DataClassic__serialize_)

DEFINE_ROUNDTRIP(c_can_data_fd_roundtrip,
                 uavcan__metatransport__can__DataFD,
                 uavcan__metatransport__can__DataFD__deserialize_,
                 uavcan__metatransport__can__DataFD__serialize_)

DEFINE_ROUNDTRIP(c_can_error_roundtrip,
                 uavcan__metatransport__can__Error,
                 uavcan__metatransport__can__Error__deserialize_,
                 uavcan__metatransport__can__Error__serialize_)

DEFINE_ROUNDTRIP(c_can_rtr_roundtrip,
                 uavcan__metatransport__can__RTR,
                 uavcan__metatransport__can__RTR__deserialize_,
                 uavcan__metatransport__can__RTR__serialize_)

DEFINE_ROUNDTRIP(c_can_manifestation_roundtrip,
                 uavcan__metatransport__can__Manifestation,
                 uavcan__metatransport__can__Manifestation__deserialize_,
                 uavcan__metatransport__can__Manifestation__serialize_)

DEFINE_ROUNDTRIP(c_can_arbitration_id_roundtrip,
                 uavcan__metatransport__can__ArbitrationID,
                 uavcan__metatransport__can__ArbitrationID__deserialize_,
                 uavcan__metatransport__can__ArbitrationID__serialize_)

DEFINE_ROUNDTRIP(c_can_base_arbitration_id_roundtrip,
                 uavcan__metatransport__can__BaseArbitrationID,
                 uavcan__metatransport__can__BaseArbitrationID__deserialize_,
                 uavcan__metatransport__can__BaseArbitrationID__serialize_)

DEFINE_ROUNDTRIP(c_can_extended_arbitration_id_roundtrip,
                 uavcan__metatransport__can__ExtendedArbitrationID,
                 uavcan__metatransport__can__ExtendedArbitrationID__deserialize_,
                 uavcan__metatransport__can__ExtendedArbitrationID__serialize_)

DEFINE_ROUNDTRIP(c_metatransport_serial_fragment_roundtrip,
                 uavcan__metatransport__serial__Fragment,
                 uavcan__metatransport__serial__Fragment__deserialize_,
                 uavcan__metatransport__serial__Fragment__serialize_)

DEFINE_ROUNDTRIP(c_metatransport_ethernet_frame_roundtrip,
                 uavcan__metatransport__ethernet__Frame,
                 uavcan__metatransport__ethernet__Frame__deserialize_,
                 uavcan__metatransport__ethernet__Frame__serialize_)

DEFINE_ROUNDTRIP(c_metatransport_udp_endpoint_roundtrip,
                 uavcan__metatransport__udp__Endpoint,
                 uavcan__metatransport__udp__Endpoint__deserialize_,
                 uavcan__metatransport__udp__Endpoint__serialize_)

DEFINE_ROUNDTRIP(c_metatransport_udp_frame_roundtrip,
                 uavcan__metatransport__udp__Frame,
                 uavcan__metatransport__udp__Frame__deserialize_,
                 uavcan__metatransport__udp__Frame__serialize_)

DEFINE_ROUNDTRIP(c_time_synchronization_roundtrip,
                 uavcan__time__Synchronization,
                 uavcan__time__Synchronization__deserialize_,
                 uavcan__time__Synchronization__serialize_)

DEFINE_ROUNDTRIP(c_time_synchronized_timestamp_roundtrip,
                 uavcan__time__SynchronizedTimestamp,
                 uavcan__time__SynchronizedTimestamp__deserialize_,
                 uavcan__time__SynchronizedTimestamp__serialize_)

DEFINE_ROUNDTRIP(c_time_system_roundtrip,
                 uavcan__time__TimeSystem,
                 uavcan__time__TimeSystem__deserialize_,
                 uavcan__time__TimeSystem__serialize_)

DEFINE_ROUNDTRIP(c_time_tai_info_roundtrip,
                 uavcan__time__TAIInfo,
                 uavcan__time__TAIInfo__deserialize_,
                 uavcan__time__TAIInfo__serialize_)

DEFINE_ROUNDTRIP(c_time_get_sync_master_info_request_roundtrip,
                 uavcan__time__GetSynchronizationMasterInfo__Request,
                 uavcan__time__GetSynchronizationMasterInfo__Request__deserialize_,
                 uavcan__time__GetSynchronizationMasterInfo__Request__serialize_)

DEFINE_ROUNDTRIP(c_time_get_sync_master_info_response_roundtrip,
                 uavcan__time__GetSynchronizationMasterInfo__Response,
                 uavcan__time__GetSynchronizationMasterInfo__Response__deserialize_,
                 uavcan__time__GetSynchronizationMasterInfo__Response__serialize_)

DEFINE_ROUNDTRIP(c_udp_outgoing_packet_roundtrip,
                 uavcan__internet__udp__OutgoingPacket,
                 uavcan__internet__udp__OutgoingPacket__deserialize_,
                 uavcan__internet__udp__OutgoingPacket__serialize_)

DEFINE_ROUNDTRIP(c_udp_handle_incoming_request_roundtrip,
                 uavcan__internet__udp__HandleIncomingPacket__Request,
                 uavcan__internet__udp__HandleIncomingPacket__Request__deserialize_,
                 uavcan__internet__udp__HandleIncomingPacket__Request__serialize_)

DEFINE_ROUNDTRIP(c_udp_handle_incoming_response_roundtrip,
                 uavcan__internet__udp__HandleIncomingPacket__Response,
                 uavcan__internet__udp__HandleIncomingPacket__Response__deserialize_,
                 uavcan__internet__udp__HandleIncomingPacket__Response__serialize_)

DEFINE_ROUNDTRIP(c_si_unit_angle_quaternion_roundtrip,
                 uavcan__si__unit__angle__Quaternion,
                 uavcan__si__unit__angle__Quaternion__deserialize_,
                 uavcan__si__unit__angle__Quaternion__serialize_)

DEFINE_ROUNDTRIP(c_si_unit_acceleration_vector3_roundtrip,
                 uavcan__si__unit__acceleration__Vector3,
                 uavcan__si__unit__acceleration__Vector3__deserialize_,
                 uavcan__si__unit__acceleration__Vector3__serialize_)

DEFINE_ROUNDTRIP(c_si_unit_force_vector3_roundtrip,
                 uavcan__si__unit__force__Vector3,
                 uavcan__si__unit__force__Vector3__deserialize_,
                 uavcan__si__unit__force__Vector3__serialize_)

DEFINE_ROUNDTRIP(c_si_unit_length_wide_vector3_roundtrip,
                 uavcan__si__unit__length__WideVector3,
                 uavcan__si__unit__length__WideVector3__deserialize_,
                 uavcan__si__unit__length__WideVector3__serialize_)

DEFINE_ROUNDTRIP(c_si_unit_torque_vector3_roundtrip,
                 uavcan__si__unit__torque__Vector3,
                 uavcan__si__unit__torque__Vector3__deserialize_,
                 uavcan__si__unit__torque__Vector3__serialize_)

DEFINE_ROUNDTRIP(c_si_sample_angle_quaternion_roundtrip,
                 uavcan__si__sample__angle__Quaternion,
                 uavcan__si__sample__angle__Quaternion__deserialize_,
                 uavcan__si__sample__angle__Quaternion__serialize_)

DEFINE_ROUNDTRIP(c_si_sample_acceleration_vector3_roundtrip,
                 uavcan__si__sample__acceleration__Vector3,
                 uavcan__si__sample__acceleration__Vector3__deserialize_,
                 uavcan__si__sample__acceleration__Vector3__serialize_)

DEFINE_ROUNDTRIP(c_si_sample_force_vector3_roundtrip,
                 uavcan__si__sample__force__Vector3,
                 uavcan__si__sample__force__Vector3__deserialize_,
                 uavcan__si__sample__force__Vector3__serialize_)

DEFINE_ROUNDTRIP(c_si_sample_torque_vector3_roundtrip,
                 uavcan__si__sample__torque__Vector3,
                 uavcan__si__sample__torque__Vector3__deserialize_,
                 uavcan__si__sample__torque__Vector3__serialize_)

DEFINE_ROUNDTRIP(c_si_unit_velocity_vector3_roundtrip,
                 uavcan__si__unit__velocity__Vector3,
                 uavcan__si__unit__velocity__Vector3__deserialize_,
                 uavcan__si__unit__velocity__Vector3__serialize_)

DEFINE_ROUNDTRIP(c_si_sample_velocity_vector3_roundtrip,
                 uavcan__si__sample__velocity__Vector3,
                 uavcan__si__sample__velocity__Vector3__deserialize_,
                 uavcan__si__sample__velocity__Vector3__serialize_)

DEFINE_ROUNDTRIP(c_si_unit_temperature_scalar_roundtrip,
                 uavcan__si__unit__temperature__Scalar,
                 uavcan__si__unit__temperature__Scalar__deserialize_,
                 uavcan__si__unit__temperature__Scalar__serialize_)

DEFINE_ROUNDTRIP(c_si_unit_voltage_scalar_roundtrip,
                 uavcan__si__unit__voltage__Scalar,
                 uavcan__si__unit__voltage__Scalar__deserialize_,
                 uavcan__si__unit__voltage__Scalar__serialize_)

DEFINE_ROUNDTRIP(c_si_sample_temperature_scalar_roundtrip,
                 uavcan__si__sample__temperature__Scalar,
                 uavcan__si__sample__temperature__Scalar__deserialize_,
                 uavcan__si__sample__temperature__Scalar__serialize_)

DEFINE_ROUNDTRIP(c_si_sample_voltage_scalar_roundtrip,
                 uavcan__si__sample__voltage__Scalar,
                 uavcan__si__sample__voltage__Scalar__deserialize_,
                 uavcan__si__sample__voltage__Scalar__serialize_)

DEFINE_ROUNDTRIP(c_natural8_roundtrip,
                 uavcan__primitive__array__Natural8,
                 uavcan__primitive__array__Natural8__deserialize_,
                 uavcan__primitive__array__Natural8__serialize_)

DEFINE_ROUNDTRIP(c_real16_roundtrip,
                 uavcan__primitive__array__Real16,
                 uavcan__primitive__array__Real16__deserialize_,
                 uavcan__primitive__array__Real16__serialize_)

DEFINE_ROUNDTRIP(c_real32_roundtrip,
                 uavcan__primitive__array__Real32,
                 uavcan__primitive__array__Real32__deserialize_,
                 uavcan__primitive__array__Real32__serialize_)

DEFINE_ROUNDTRIP(c_bit_array_roundtrip,
                 uavcan__primitive__array__Bit,
                 uavcan__primitive__array__Bit__deserialize_,
                 uavcan__primitive__array__Bit__serialize_)

DEFINE_ROUNDTRIP(c_scalar_bit_roundtrip,
                 uavcan__primitive__scalar__Bit,
                 uavcan__primitive__scalar__Bit__deserialize_,
                 uavcan__primitive__scalar__Bit__serialize_)

DEFINE_ROUNDTRIP(c_scalar_integer8_roundtrip,
                 uavcan__primitive__scalar__Integer8,
                 uavcan__primitive__scalar__Integer8__deserialize_,
                 uavcan__primitive__scalar__Integer8__serialize_)

DEFINE_ROUNDTRIP(c_scalar_integer16_roundtrip,
                 uavcan__primitive__scalar__Integer16,
                 uavcan__primitive__scalar__Integer16__deserialize_,
                 uavcan__primitive__scalar__Integer16__serialize_)

DEFINE_ROUNDTRIP(c_scalar_integer32_roundtrip,
                 uavcan__primitive__scalar__Integer32,
                 uavcan__primitive__scalar__Integer32__deserialize_,
                 uavcan__primitive__scalar__Integer32__serialize_)

DEFINE_ROUNDTRIP(c_scalar_integer64_roundtrip,
                 uavcan__primitive__scalar__Integer64,
                 uavcan__primitive__scalar__Integer64__deserialize_,
                 uavcan__primitive__scalar__Integer64__serialize_)

DEFINE_ROUNDTRIP(c_scalar_natural8_roundtrip,
                 uavcan__primitive__scalar__Natural8,
                 uavcan__primitive__scalar__Natural8__deserialize_,
                 uavcan__primitive__scalar__Natural8__serialize_)

DEFINE_ROUNDTRIP(c_scalar_natural16_roundtrip,
                 uavcan__primitive__scalar__Natural16,
                 uavcan__primitive__scalar__Natural16__deserialize_,
                 uavcan__primitive__scalar__Natural16__serialize_)

DEFINE_ROUNDTRIP(c_scalar_natural32_roundtrip,
                 uavcan__primitive__scalar__Natural32,
                 uavcan__primitive__scalar__Natural32__deserialize_,
                 uavcan__primitive__scalar__Natural32__serialize_)

DEFINE_ROUNDTRIP(c_scalar_natural64_roundtrip,
                 uavcan__primitive__scalar__Natural64,
                 uavcan__primitive__scalar__Natural64__deserialize_,
                 uavcan__primitive__scalar__Natural64__serialize_)

DEFINE_ROUNDTRIP(c_scalar_real16_roundtrip,
                 uavcan__primitive__scalar__Real16,
                 uavcan__primitive__scalar__Real16__deserialize_,
                 uavcan__primitive__scalar__Real16__serialize_)

DEFINE_ROUNDTRIP(c_scalar_real32_roundtrip,
                 uavcan__primitive__scalar__Real32,
                 uavcan__primitive__scalar__Real32__deserialize_,
                 uavcan__primitive__scalar__Real32__serialize_)

DEFINE_ROUNDTRIP(c_scalar_real64_roundtrip,
                 uavcan__primitive__scalar__Real64,
                 uavcan__primitive__scalar__Real64__deserialize_,
                 uavcan__primitive__scalar__Real64__serialize_)

DEFINE_ROUNDTRIP(c_array_integer8_roundtrip,
                 uavcan__primitive__array__Integer8,
                 uavcan__primitive__array__Integer8__deserialize_,
                 uavcan__primitive__array__Integer8__serialize_)

DEFINE_ROUNDTRIP(c_array_integer16_roundtrip,
                 uavcan__primitive__array__Integer16,
                 uavcan__primitive__array__Integer16__deserialize_,
                 uavcan__primitive__array__Integer16__serialize_)

DEFINE_ROUNDTRIP(c_array_integer32_roundtrip,
                 uavcan__primitive__array__Integer32,
                 uavcan__primitive__array__Integer32__deserialize_,
                 uavcan__primitive__array__Integer32__serialize_)

DEFINE_ROUNDTRIP(c_array_integer64_roundtrip,
                 uavcan__primitive__array__Integer64,
                 uavcan__primitive__array__Integer64__deserialize_,
                 uavcan__primitive__array__Integer64__serialize_)

DEFINE_ROUNDTRIP(c_array_natural16_roundtrip,
                 uavcan__primitive__array__Natural16,
                 uavcan__primitive__array__Natural16__deserialize_,
                 uavcan__primitive__array__Natural16__serialize_)

DEFINE_ROUNDTRIP(c_array_natural32_roundtrip,
                 uavcan__primitive__array__Natural32,
                 uavcan__primitive__array__Natural32__deserialize_,
                 uavcan__primitive__array__Natural32__serialize_)

DEFINE_ROUNDTRIP(c_array_natural64_roundtrip,
                 uavcan__primitive__array__Natural64,
                 uavcan__primitive__array__Natural64__deserialize_,
                 uavcan__primitive__array__Natural64__serialize_)

DEFINE_ROUNDTRIP(c_array_real64_roundtrip,
                 uavcan__primitive__array__Real64,
                 uavcan__primitive__array__Real64__deserialize_,
                 uavcan__primitive__array__Real64__serialize_)

DEFINE_ROUNDTRIP(c_primitive_empty_roundtrip,
                 uavcan__primitive__Empty,
                 uavcan__primitive__Empty__deserialize_,
                 uavcan__primitive__Empty__serialize_)

DEFINE_ROUNDTRIP(c_primitive_string_roundtrip,
                 uavcan__primitive__String,
                 uavcan__primitive__String__deserialize_,
                 uavcan__primitive__String__serialize_)

DEFINE_ROUNDTRIP(c_primitive_unstructured_roundtrip,
                 uavcan__primitive__Unstructured,
                 uavcan__primitive__Unstructured__deserialize_,
                 uavcan__primitive__Unstructured__serialize_)

DEFINE_ROUNDTRIP(c_file_path_roundtrip,
                 uavcan__file__Path,
                 uavcan__file__Path__deserialize_,
                 uavcan__file__Path__serialize_)

DEFINE_ROUNDTRIP(c_node_id_allocation_data_roundtrip,
                 uavcan__pnp__NodeIDAllocationData,
                 uavcan__pnp__NodeIDAllocationData__deserialize_,
                 uavcan__pnp__NodeIDAllocationData__serialize_)

DEFINE_ROUNDTRIP(c_pnp_cluster_entry_roundtrip,
                 uavcan__pnp__cluster__Entry,
                 uavcan__pnp__cluster__Entry__deserialize_,
                 uavcan__pnp__cluster__Entry__serialize_)

DEFINE_ROUNDTRIP(c_pnp_cluster_append_entries_request_roundtrip,
                 uavcan__pnp__cluster__AppendEntries__Request,
                 uavcan__pnp__cluster__AppendEntries__Request__deserialize_,
                 uavcan__pnp__cluster__AppendEntries__Request__serialize_)

DEFINE_ROUNDTRIP(c_pnp_cluster_append_entries_response_roundtrip,
                 uavcan__pnp__cluster__AppendEntries__Response,
                 uavcan__pnp__cluster__AppendEntries__Response__deserialize_,
                 uavcan__pnp__cluster__AppendEntries__Response__serialize_)

DEFINE_ROUNDTRIP(c_pnp_cluster_request_vote_request_roundtrip,
                 uavcan__pnp__cluster__RequestVote__Request,
                 uavcan__pnp__cluster__RequestVote__Request__deserialize_,
                 uavcan__pnp__cluster__RequestVote__Request__serialize_)

DEFINE_ROUNDTRIP(c_pnp_cluster_request_vote_response_roundtrip,
                 uavcan__pnp__cluster__RequestVote__Response,
                 uavcan__pnp__cluster__RequestVote__Response__deserialize_,
                 uavcan__pnp__cluster__RequestVote__Response__serialize_)

DEFINE_ROUNDTRIP(c_pnp_cluster_discovery_roundtrip,
                 uavcan__pnp__cluster__Discovery,
                 uavcan__pnp__cluster__Discovery__deserialize_,
                 uavcan__pnp__cluster__Discovery__serialize_)

DEFINE_ROUNDTRIP(c_node_port_service_id_roundtrip,
                 uavcan__node__port__ServiceID,
                 uavcan__node__port__ServiceID__deserialize_,
                 uavcan__node__port__ServiceID__serialize_)

DEFINE_ROUNDTRIP(c_node_port_subject_id_roundtrip,
                 uavcan__node__port__SubjectID,
                 uavcan__node__port__SubjectID__deserialize_,
                 uavcan__node__port__SubjectID__serialize_)

DEFINE_ROUNDTRIP(c_node_port_service_id_list_roundtrip,
                 uavcan__node__port__ServiceIDList,
                 uavcan__node__port__ServiceIDList__deserialize_,
                 uavcan__node__port__ServiceIDList__serialize_)

DEFINE_ROUNDTRIP(c_node_port_subject_id_list_roundtrip,
                 uavcan__node__port__SubjectIDList,
                 uavcan__node__port__SubjectIDList__deserialize_,
                 uavcan__node__port__SubjectIDList__serialize_)

DEFINE_ROUNDTRIP(c_node_port_id_roundtrip,
                 uavcan__node__port__ID,
                 uavcan__node__port__ID__deserialize_,
                 uavcan__node__port__ID__serialize_)

DEFINE_ROUNDTRIP(c_port_list_roundtrip,
                 uavcan__node__port__List,
                 uavcan__node__port__List__deserialize_,
                 uavcan__node__port__List__serialize_)

DEFINE_ROUNDTRIP(c_metatransport_ethernet_ethertype_roundtrip,
                 uavcan__metatransport__ethernet__EtherType,
                 uavcan__metatransport__ethernet__EtherType__deserialize_,
                 uavcan__metatransport__ethernet__EtherType__serialize_)
