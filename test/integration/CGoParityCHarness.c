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
                 uavcan__node__Heartbeat@CV1_0@,
                 uavcan__node__Heartbeat@CV1_0@__deserialize_,
                 uavcan__node__Heartbeat@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_execute_command_request_roundtrip,
                 uavcan__node__ExecuteCommand@CV1_3@__Request,
                 uavcan__node__ExecuteCommand@CV1_3@__Request__deserialize_,
                 uavcan__node__ExecuteCommand@CV1_3@__Request__serialize_)

DEFINE_ROUNDTRIP(c_execute_command_response_roundtrip,
                 uavcan__node__ExecuteCommand@CV1_3@__Response,
                 uavcan__node__ExecuteCommand@CV1_3@__Response__deserialize_,
                 uavcan__node__ExecuteCommand@CV1_3@__Response__serialize_)

DEFINE_ROUNDTRIP(c_node_id_roundtrip, uavcan__node__ID@CV1_0@, uavcan__node__ID@CV1_0@__deserialize_, uavcan__node__ID@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_node_mode_roundtrip,
                 uavcan__node__Mode@CV1_0@,
                 uavcan__node__Mode@CV1_0@__deserialize_,
                 uavcan__node__Mode@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_node_version_roundtrip,
                 uavcan__node__Version@CV1_0@,
                 uavcan__node__Version@CV1_0@__deserialize_,
                 uavcan__node__Version@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_node_health_roundtrip,
                 uavcan__node__Health@CV1_0@,
                 uavcan__node__Health@CV1_0@__deserialize_,
                 uavcan__node__Health@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_node_io_statistics_roundtrip,
                 uavcan__node__IOStatistics@CV0_1@,
                 uavcan__node__IOStatistics@CV0_1@__deserialize_,
                 uavcan__node__IOStatistics@CV0_1@__serialize_)

DEFINE_ROUNDTRIP(c_get_info_response_roundtrip,
                 uavcan__node__GetInfo@CV1_0@__Response,
                 uavcan__node__GetInfo@CV1_0@__Response__deserialize_,
                 uavcan__node__GetInfo@CV1_0@__Response__serialize_)

DEFINE_ROUNDTRIP(c_diagnostic_record_roundtrip,
                 uavcan__diagnostic__Record@CV1_1@,
                 uavcan__diagnostic__Record@CV1_1@__deserialize_,
                 uavcan__diagnostic__Record@CV1_1@__serialize_)

DEFINE_ROUNDTRIP(c_diagnostic_severity_roundtrip,
                 uavcan__diagnostic__Severity@CV1_0@,
                 uavcan__diagnostic__Severity@CV1_0@__deserialize_,
                 uavcan__diagnostic__Severity@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_register_value_roundtrip,
                 uavcan__register___Value@CV1_0@,
                 uavcan__register___Value@CV1_0@__deserialize_,
                 uavcan__register___Value@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_register_access_request_roundtrip,
                 uavcan__register___Access@CV1_0@__Request,
                 uavcan__register___Access@CV1_0@__Request__deserialize_,
                 uavcan__register___Access@CV1_0@__Request__serialize_)

DEFINE_ROUNDTRIP(c_register_access_response_roundtrip,
                 uavcan__register___Access@CV1_0@__Response,
                 uavcan__register___Access@CV1_0@__Response__deserialize_,
                 uavcan__register___Access@CV1_0@__Response__serialize_)

DEFINE_ROUNDTRIP(c_register_name_roundtrip,
                 uavcan__register___Name@CV1_0@,
                 uavcan__register___Name@CV1_0@__deserialize_,
                 uavcan__register___Name@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_register_list_request_roundtrip,
                 uavcan__register___List@CV1_0@__Request,
                 uavcan__register___List@CV1_0@__Request__deserialize_,
                 uavcan__register___List@CV1_0@__Request__serialize_)

DEFINE_ROUNDTRIP(c_register_list_response_roundtrip,
                 uavcan__register___List@CV1_0@__Response,
                 uavcan__register___List@CV1_0@__Response__deserialize_,
                 uavcan__register___List@CV1_0@__Response__serialize_)

DEFINE_ROUNDTRIP(c_file_list_request_roundtrip,
                 uavcan__file__List@CV0_2@__Request,
                 uavcan__file__List@CV0_2@__Request__deserialize_,
                 uavcan__file__List@CV0_2@__Request__serialize_)

DEFINE_ROUNDTRIP(c_file_list_response_roundtrip,
                 uavcan__file__List@CV0_2@__Response,
                 uavcan__file__List@CV0_2@__Response__deserialize_,
                 uavcan__file__List@CV0_2@__Response__serialize_)

DEFINE_ROUNDTRIP(c_file_read_request_roundtrip,
                 uavcan__file__Read@CV1_1@__Request,
                 uavcan__file__Read@CV1_1@__Request__deserialize_,
                 uavcan__file__Read@CV1_1@__Request__serialize_)

DEFINE_ROUNDTRIP(c_file_read_response_roundtrip,
                 uavcan__file__Read@CV1_1@__Response,
                 uavcan__file__Read@CV1_1@__Response__deserialize_,
                 uavcan__file__Read@CV1_1@__Response__serialize_)

DEFINE_ROUNDTRIP(c_file_write_request_roundtrip,
                 uavcan__file__Write@CV1_1@__Request,
                 uavcan__file__Write@CV1_1@__Request__deserialize_,
                 uavcan__file__Write@CV1_1@__Request__serialize_)

DEFINE_ROUNDTRIP(c_file_write_response_roundtrip,
                 uavcan__file__Write@CV1_1@__Response,
                 uavcan__file__Write@CV1_1@__Response__deserialize_,
                 uavcan__file__Write@CV1_1@__Response__serialize_)

DEFINE_ROUNDTRIP(c_file_modify_request_roundtrip,
                 uavcan__file__Modify@CV1_1@__Request,
                 uavcan__file__Modify@CV1_1@__Request__deserialize_,
                 uavcan__file__Modify@CV1_1@__Request__serialize_)

DEFINE_ROUNDTRIP(c_file_modify_response_roundtrip,
                 uavcan__file__Modify@CV1_1@__Response,
                 uavcan__file__Modify@CV1_1@__Response__deserialize_,
                 uavcan__file__Modify@CV1_1@__Response__serialize_)

DEFINE_ROUNDTRIP(c_file_get_info_request_roundtrip,
                 uavcan__file__GetInfo@CV0_2@__Request,
                 uavcan__file__GetInfo@CV0_2@__Request__deserialize_,
                 uavcan__file__GetInfo@CV0_2@__Request__serialize_)

DEFINE_ROUNDTRIP(c_file_get_info_response_roundtrip,
                 uavcan__file__GetInfo@CV0_2@__Response,
                 uavcan__file__GetInfo@CV0_2@__Response__deserialize_,
                 uavcan__file__GetInfo@CV0_2@__Response__serialize_)

DEFINE_ROUNDTRIP(c_file_error_roundtrip,
                 uavcan__file__Error@CV1_0@,
                 uavcan__file__Error@CV1_0@__deserialize_,
                 uavcan__file__Error@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_get_transport_statistics_request_roundtrip,
                 uavcan__node__GetTransportStatistics@CV0_1@__Request,
                 uavcan__node__GetTransportStatistics@CV0_1@__Request__deserialize_,
                 uavcan__node__GetTransportStatistics@CV0_1@__Request__serialize_)

DEFINE_ROUNDTRIP(c_get_transport_statistics_response_roundtrip,
                 uavcan__node__GetTransportStatistics@CV0_1@__Response,
                 uavcan__node__GetTransportStatistics@CV0_1@__Response__deserialize_,
                 uavcan__node__GetTransportStatistics@CV0_1@__Response__serialize_)

DEFINE_ROUNDTRIP(c_can_frame_roundtrip,
                 uavcan__metatransport__can__Frame@CV0_2@,
                 uavcan__metatransport__can__Frame@CV0_2@__deserialize_,
                 uavcan__metatransport__can__Frame@CV0_2@__serialize_)

DEFINE_ROUNDTRIP(c_can_data_classic_roundtrip,
                 uavcan__metatransport__can__DataClassic@CV0_1@,
                 uavcan__metatransport__can__DataClassic@CV0_1@__deserialize_,
                 uavcan__metatransport__can__DataClassic@CV0_1@__serialize_)

DEFINE_ROUNDTRIP(c_can_data_fd_roundtrip,
                 uavcan__metatransport__can__DataFD@CV0_1@,
                 uavcan__metatransport__can__DataFD@CV0_1@__deserialize_,
                 uavcan__metatransport__can__DataFD@CV0_1@__serialize_)

DEFINE_ROUNDTRIP(c_can_error_roundtrip,
                 uavcan__metatransport__can__Error@CV0_1@,
                 uavcan__metatransport__can__Error@CV0_1@__deserialize_,
                 uavcan__metatransport__can__Error@CV0_1@__serialize_)

DEFINE_ROUNDTRIP(c_can_rtr_roundtrip,
                 uavcan__metatransport__can__RTR@CV0_1@,
                 uavcan__metatransport__can__RTR@CV0_1@__deserialize_,
                 uavcan__metatransport__can__RTR@CV0_1@__serialize_)

DEFINE_ROUNDTRIP(c_can_manifestation_roundtrip,
                 uavcan__metatransport__can__Manifestation@CV0_1@,
                 uavcan__metatransport__can__Manifestation@CV0_1@__deserialize_,
                 uavcan__metatransport__can__Manifestation@CV0_1@__serialize_)

DEFINE_ROUNDTRIP(c_can_arbitration_id_roundtrip,
                 uavcan__metatransport__can__ArbitrationID@CV0_1@,
                 uavcan__metatransport__can__ArbitrationID@CV0_1@__deserialize_,
                 uavcan__metatransport__can__ArbitrationID@CV0_1@__serialize_)

DEFINE_ROUNDTRIP(c_can_base_arbitration_id_roundtrip,
                 uavcan__metatransport__can__BaseArbitrationID@CV0_1@,
                 uavcan__metatransport__can__BaseArbitrationID@CV0_1@__deserialize_,
                 uavcan__metatransport__can__BaseArbitrationID@CV0_1@__serialize_)

DEFINE_ROUNDTRIP(c_can_extended_arbitration_id_roundtrip,
                 uavcan__metatransport__can__ExtendedArbitrationID@CV0_1@,
                 uavcan__metatransport__can__ExtendedArbitrationID@CV0_1@__deserialize_,
                 uavcan__metatransport__can__ExtendedArbitrationID@CV0_1@__serialize_)

DEFINE_ROUNDTRIP(c_metatransport_serial_fragment_roundtrip,
                 uavcan__metatransport__serial__Fragment@CV0_2@,
                 uavcan__metatransport__serial__Fragment@CV0_2@__deserialize_,
                 uavcan__metatransport__serial__Fragment@CV0_2@__serialize_)

DEFINE_ROUNDTRIP(c_metatransport_ethernet_frame_roundtrip,
                 uavcan__metatransport__ethernet__Frame@CV0_1@,
                 uavcan__metatransport__ethernet__Frame@CV0_1@__deserialize_,
                 uavcan__metatransport__ethernet__Frame@CV0_1@__serialize_)

DEFINE_ROUNDTRIP(c_metatransport_udp_endpoint_roundtrip,
                 uavcan__metatransport__udp__Endpoint@CV0_1@,
                 uavcan__metatransport__udp__Endpoint@CV0_1@__deserialize_,
                 uavcan__metatransport__udp__Endpoint@CV0_1@__serialize_)

DEFINE_ROUNDTRIP(c_metatransport_udp_frame_roundtrip,
                 uavcan__metatransport__udp__Frame@CV0_1@,
                 uavcan__metatransport__udp__Frame@CV0_1@__deserialize_,
                 uavcan__metatransport__udp__Frame@CV0_1@__serialize_)

DEFINE_ROUNDTRIP(c_time_synchronization_roundtrip,
                 uavcan__time__Synchronization@CV1_0@,
                 uavcan__time__Synchronization@CV1_0@__deserialize_,
                 uavcan__time__Synchronization@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_time_synchronized_timestamp_roundtrip,
                 uavcan__time__SynchronizedTimestamp@CV1_0@,
                 uavcan__time__SynchronizedTimestamp@CV1_0@__deserialize_,
                 uavcan__time__SynchronizedTimestamp@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_time_system_roundtrip,
                 uavcan__time__TimeSystem@CV0_1@,
                 uavcan__time__TimeSystem@CV0_1@__deserialize_,
                 uavcan__time__TimeSystem@CV0_1@__serialize_)

DEFINE_ROUNDTRIP(c_time_tai_info_roundtrip,
                 uavcan__time__TAIInfo@CV0_1@,
                 uavcan__time__TAIInfo@CV0_1@__deserialize_,
                 uavcan__time__TAIInfo@CV0_1@__serialize_)

DEFINE_ROUNDTRIP(c_time_get_sync_master_info_request_roundtrip,
                 uavcan__time__GetSynchronizationMasterInfo@CV0_1@__Request,
                 uavcan__time__GetSynchronizationMasterInfo@CV0_1@__Request__deserialize_,
                 uavcan__time__GetSynchronizationMasterInfo@CV0_1@__Request__serialize_)

DEFINE_ROUNDTRIP(c_time_get_sync_master_info_response_roundtrip,
                 uavcan__time__GetSynchronizationMasterInfo@CV0_1@__Response,
                 uavcan__time__GetSynchronizationMasterInfo@CV0_1@__Response__deserialize_,
                 uavcan__time__GetSynchronizationMasterInfo@CV0_1@__Response__serialize_)

DEFINE_ROUNDTRIP(c_udp_outgoing_packet_roundtrip,
                 uavcan__internet__udp__OutgoingPacket@CV0_2@,
                 uavcan__internet__udp__OutgoingPacket@CV0_2@__deserialize_,
                 uavcan__internet__udp__OutgoingPacket@CV0_2@__serialize_)

DEFINE_ROUNDTRIP(c_udp_handle_incoming_request_roundtrip,
                 uavcan__internet__udp__HandleIncomingPacket@CV0_2@__Request,
                 uavcan__internet__udp__HandleIncomingPacket@CV0_2@__Request__deserialize_,
                 uavcan__internet__udp__HandleIncomingPacket@CV0_2@__Request__serialize_)

DEFINE_ROUNDTRIP(c_udp_handle_incoming_response_roundtrip,
                 uavcan__internet__udp__HandleIncomingPacket@CV0_2@__Response,
                 uavcan__internet__udp__HandleIncomingPacket@CV0_2@__Response__deserialize_,
                 uavcan__internet__udp__HandleIncomingPacket@CV0_2@__Response__serialize_)

DEFINE_ROUNDTRIP(c_si_unit_angle_quaternion_roundtrip,
                 uavcan__si__unit__angle__Quaternion@CV1_0@,
                 uavcan__si__unit__angle__Quaternion@CV1_0@__deserialize_,
                 uavcan__si__unit__angle__Quaternion@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_si_unit_acceleration_vector3_roundtrip,
                 uavcan__si__unit__acceleration__Vector3@CV1_0@,
                 uavcan__si__unit__acceleration__Vector3@CV1_0@__deserialize_,
                 uavcan__si__unit__acceleration__Vector3@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_si_unit_force_vector3_roundtrip,
                 uavcan__si__unit__force__Vector3@CV1_0@,
                 uavcan__si__unit__force__Vector3@CV1_0@__deserialize_,
                 uavcan__si__unit__force__Vector3@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_si_unit_length_wide_vector3_roundtrip,
                 uavcan__si__unit__length__WideVector3@CV1_0@,
                 uavcan__si__unit__length__WideVector3@CV1_0@__deserialize_,
                 uavcan__si__unit__length__WideVector3@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_si_unit_torque_vector3_roundtrip,
                 uavcan__si__unit__torque__Vector3@CV1_0@,
                 uavcan__si__unit__torque__Vector3@CV1_0@__deserialize_,
                 uavcan__si__unit__torque__Vector3@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_si_sample_angle_quaternion_roundtrip,
                 uavcan__si__sample__angle__Quaternion@CV1_0@,
                 uavcan__si__sample__angle__Quaternion@CV1_0@__deserialize_,
                 uavcan__si__sample__angle__Quaternion@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_si_sample_acceleration_vector3_roundtrip,
                 uavcan__si__sample__acceleration__Vector3@CV1_0@,
                 uavcan__si__sample__acceleration__Vector3@CV1_0@__deserialize_,
                 uavcan__si__sample__acceleration__Vector3@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_si_sample_force_vector3_roundtrip,
                 uavcan__si__sample__force__Vector3@CV1_0@,
                 uavcan__si__sample__force__Vector3@CV1_0@__deserialize_,
                 uavcan__si__sample__force__Vector3@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_si_sample_torque_vector3_roundtrip,
                 uavcan__si__sample__torque__Vector3@CV1_0@,
                 uavcan__si__sample__torque__Vector3@CV1_0@__deserialize_,
                 uavcan__si__sample__torque__Vector3@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_si_unit_velocity_vector3_roundtrip,
                 uavcan__si__unit__velocity__Vector3@CV1_0@,
                 uavcan__si__unit__velocity__Vector3@CV1_0@__deserialize_,
                 uavcan__si__unit__velocity__Vector3@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_si_sample_velocity_vector3_roundtrip,
                 uavcan__si__sample__velocity__Vector3@CV1_0@,
                 uavcan__si__sample__velocity__Vector3@CV1_0@__deserialize_,
                 uavcan__si__sample__velocity__Vector3@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_si_unit_temperature_scalar_roundtrip,
                 uavcan__si__unit__temperature__Scalar@CV1_0@,
                 uavcan__si__unit__temperature__Scalar@CV1_0@__deserialize_,
                 uavcan__si__unit__temperature__Scalar@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_si_unit_voltage_scalar_roundtrip,
                 uavcan__si__unit__voltage__Scalar@CV1_0@,
                 uavcan__si__unit__voltage__Scalar@CV1_0@__deserialize_,
                 uavcan__si__unit__voltage__Scalar@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_si_sample_temperature_scalar_roundtrip,
                 uavcan__si__sample__temperature__Scalar@CV1_0@,
                 uavcan__si__sample__temperature__Scalar@CV1_0@__deserialize_,
                 uavcan__si__sample__temperature__Scalar@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_si_sample_voltage_scalar_roundtrip,
                 uavcan__si__sample__voltage__Scalar@CV1_0@,
                 uavcan__si__sample__voltage__Scalar@CV1_0@__deserialize_,
                 uavcan__si__sample__voltage__Scalar@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_natural8_roundtrip,
                 uavcan__primitive__array__Natural8@CV1_0@,
                 uavcan__primitive__array__Natural8@CV1_0@__deserialize_,
                 uavcan__primitive__array__Natural8@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_real16_roundtrip,
                 uavcan__primitive__array__Real16@CV1_0@,
                 uavcan__primitive__array__Real16@CV1_0@__deserialize_,
                 uavcan__primitive__array__Real16@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_real32_roundtrip,
                 uavcan__primitive__array__Real32@CV1_0@,
                 uavcan__primitive__array__Real32@CV1_0@__deserialize_,
                 uavcan__primitive__array__Real32@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_bit_array_roundtrip,
                 uavcan__primitive__array__Bit@CV1_0@,
                 uavcan__primitive__array__Bit@CV1_0@__deserialize_,
                 uavcan__primitive__array__Bit@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_scalar_bit_roundtrip,
                 uavcan__primitive__scalar__Bit@CV1_0@,
                 uavcan__primitive__scalar__Bit@CV1_0@__deserialize_,
                 uavcan__primitive__scalar__Bit@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_scalar_integer8_roundtrip,
                 uavcan__primitive__scalar__Integer8@CV1_0@,
                 uavcan__primitive__scalar__Integer8@CV1_0@__deserialize_,
                 uavcan__primitive__scalar__Integer8@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_scalar_integer16_roundtrip,
                 uavcan__primitive__scalar__Integer16@CV1_0@,
                 uavcan__primitive__scalar__Integer16@CV1_0@__deserialize_,
                 uavcan__primitive__scalar__Integer16@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_scalar_integer32_roundtrip,
                 uavcan__primitive__scalar__Integer32@CV1_0@,
                 uavcan__primitive__scalar__Integer32@CV1_0@__deserialize_,
                 uavcan__primitive__scalar__Integer32@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_scalar_integer64_roundtrip,
                 uavcan__primitive__scalar__Integer64@CV1_0@,
                 uavcan__primitive__scalar__Integer64@CV1_0@__deserialize_,
                 uavcan__primitive__scalar__Integer64@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_scalar_natural8_roundtrip,
                 uavcan__primitive__scalar__Natural8@CV1_0@,
                 uavcan__primitive__scalar__Natural8@CV1_0@__deserialize_,
                 uavcan__primitive__scalar__Natural8@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_scalar_natural16_roundtrip,
                 uavcan__primitive__scalar__Natural16@CV1_0@,
                 uavcan__primitive__scalar__Natural16@CV1_0@__deserialize_,
                 uavcan__primitive__scalar__Natural16@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_scalar_natural32_roundtrip,
                 uavcan__primitive__scalar__Natural32@CV1_0@,
                 uavcan__primitive__scalar__Natural32@CV1_0@__deserialize_,
                 uavcan__primitive__scalar__Natural32@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_scalar_natural64_roundtrip,
                 uavcan__primitive__scalar__Natural64@CV1_0@,
                 uavcan__primitive__scalar__Natural64@CV1_0@__deserialize_,
                 uavcan__primitive__scalar__Natural64@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_scalar_real16_roundtrip,
                 uavcan__primitive__scalar__Real16@CV1_0@,
                 uavcan__primitive__scalar__Real16@CV1_0@__deserialize_,
                 uavcan__primitive__scalar__Real16@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_scalar_real32_roundtrip,
                 uavcan__primitive__scalar__Real32@CV1_0@,
                 uavcan__primitive__scalar__Real32@CV1_0@__deserialize_,
                 uavcan__primitive__scalar__Real32@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_scalar_real64_roundtrip,
                 uavcan__primitive__scalar__Real64@CV1_0@,
                 uavcan__primitive__scalar__Real64@CV1_0@__deserialize_,
                 uavcan__primitive__scalar__Real64@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_array_integer8_roundtrip,
                 uavcan__primitive__array__Integer8@CV1_0@,
                 uavcan__primitive__array__Integer8@CV1_0@__deserialize_,
                 uavcan__primitive__array__Integer8@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_array_integer16_roundtrip,
                 uavcan__primitive__array__Integer16@CV1_0@,
                 uavcan__primitive__array__Integer16@CV1_0@__deserialize_,
                 uavcan__primitive__array__Integer16@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_array_integer32_roundtrip,
                 uavcan__primitive__array__Integer32@CV1_0@,
                 uavcan__primitive__array__Integer32@CV1_0@__deserialize_,
                 uavcan__primitive__array__Integer32@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_array_integer64_roundtrip,
                 uavcan__primitive__array__Integer64@CV1_0@,
                 uavcan__primitive__array__Integer64@CV1_0@__deserialize_,
                 uavcan__primitive__array__Integer64@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_array_natural16_roundtrip,
                 uavcan__primitive__array__Natural16@CV1_0@,
                 uavcan__primitive__array__Natural16@CV1_0@__deserialize_,
                 uavcan__primitive__array__Natural16@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_array_natural32_roundtrip,
                 uavcan__primitive__array__Natural32@CV1_0@,
                 uavcan__primitive__array__Natural32@CV1_0@__deserialize_,
                 uavcan__primitive__array__Natural32@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_array_natural64_roundtrip,
                 uavcan__primitive__array__Natural64@CV1_0@,
                 uavcan__primitive__array__Natural64@CV1_0@__deserialize_,
                 uavcan__primitive__array__Natural64@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_array_real64_roundtrip,
                 uavcan__primitive__array__Real64@CV1_0@,
                 uavcan__primitive__array__Real64@CV1_0@__deserialize_,
                 uavcan__primitive__array__Real64@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_primitive_empty_roundtrip,
                 uavcan__primitive__Empty@CV1_0@,
                 uavcan__primitive__Empty@CV1_0@__deserialize_,
                 uavcan__primitive__Empty@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_primitive_string_roundtrip,
                 uavcan__primitive__String@CV1_0@,
                 uavcan__primitive__String@CV1_0@__deserialize_,
                 uavcan__primitive__String@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_primitive_unstructured_roundtrip,
                 uavcan__primitive__Unstructured@CV1_0@,
                 uavcan__primitive__Unstructured@CV1_0@__deserialize_,
                 uavcan__primitive__Unstructured@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_file_path_roundtrip,
                 uavcan__file__Path@CV2_0@,
                 uavcan__file__Path@CV2_0@__deserialize_,
                 uavcan__file__Path@CV2_0@__serialize_)

DEFINE_ROUNDTRIP(c_node_id_allocation_data_roundtrip,
                 uavcan__pnp__NodeIDAllocationData@CV2_0@,
                 uavcan__pnp__NodeIDAllocationData@CV2_0@__deserialize_,
                 uavcan__pnp__NodeIDAllocationData@CV2_0@__serialize_)

DEFINE_ROUNDTRIP(c_pnp_cluster_entry_roundtrip,
                 uavcan__pnp__cluster__Entry@CV1_0@,
                 uavcan__pnp__cluster__Entry@CV1_0@__deserialize_,
                 uavcan__pnp__cluster__Entry@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_pnp_cluster_append_entries_request_roundtrip,
                 uavcan__pnp__cluster__AppendEntries@CV1_0@__Request,
                 uavcan__pnp__cluster__AppendEntries@CV1_0@__Request__deserialize_,
                 uavcan__pnp__cluster__AppendEntries@CV1_0@__Request__serialize_)

DEFINE_ROUNDTRIP(c_pnp_cluster_append_entries_response_roundtrip,
                 uavcan__pnp__cluster__AppendEntries@CV1_0@__Response,
                 uavcan__pnp__cluster__AppendEntries@CV1_0@__Response__deserialize_,
                 uavcan__pnp__cluster__AppendEntries@CV1_0@__Response__serialize_)

DEFINE_ROUNDTRIP(c_pnp_cluster_request_vote_request_roundtrip,
                 uavcan__pnp__cluster__RequestVote@CV1_0@__Request,
                 uavcan__pnp__cluster__RequestVote@CV1_0@__Request__deserialize_,
                 uavcan__pnp__cluster__RequestVote@CV1_0@__Request__serialize_)

DEFINE_ROUNDTRIP(c_pnp_cluster_request_vote_response_roundtrip,
                 uavcan__pnp__cluster__RequestVote@CV1_0@__Response,
                 uavcan__pnp__cluster__RequestVote@CV1_0@__Response__deserialize_,
                 uavcan__pnp__cluster__RequestVote@CV1_0@__Response__serialize_)

DEFINE_ROUNDTRIP(c_pnp_cluster_discovery_roundtrip,
                 uavcan__pnp__cluster__Discovery@CV1_0@,
                 uavcan__pnp__cluster__Discovery@CV1_0@__deserialize_,
                 uavcan__pnp__cluster__Discovery@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_node_port_service_id_roundtrip,
                 uavcan__node__port__ServiceID@CV1_0@,
                 uavcan__node__port__ServiceID@CV1_0@__deserialize_,
                 uavcan__node__port__ServiceID@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_node_port_subject_id_roundtrip,
                 uavcan__node__port__SubjectID@CV1_0@,
                 uavcan__node__port__SubjectID@CV1_0@__deserialize_,
                 uavcan__node__port__SubjectID@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_node_port_service_id_list_roundtrip,
                 uavcan__node__port__ServiceIDList@CV1_0@,
                 uavcan__node__port__ServiceIDList@CV1_0@__deserialize_,
                 uavcan__node__port__ServiceIDList@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_node_port_subject_id_list_roundtrip,
                 uavcan__node__port__SubjectIDList@CV1_0@,
                 uavcan__node__port__SubjectIDList@CV1_0@__deserialize_,
                 uavcan__node__port__SubjectIDList@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_node_port_id_roundtrip,
                 uavcan__node__port__ID@CV1_0@,
                 uavcan__node__port__ID@CV1_0@__deserialize_,
                 uavcan__node__port__ID@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_port_list_roundtrip,
                 uavcan__node__port__List@CV1_0@,
                 uavcan__node__port__List@CV1_0@__deserialize_,
                 uavcan__node__port__List@CV1_0@__serialize_)

DEFINE_ROUNDTRIP(c_metatransport_ethernet_ethertype_roundtrip,
                 uavcan__metatransport__ethernet__EtherType@CV0_1@,
                 uavcan__metatransport__ethernet__EtherType@CV0_1@__deserialize_,
                 uavcan__metatransport__ethernet__EtherType@CV0_1@__serialize_)
