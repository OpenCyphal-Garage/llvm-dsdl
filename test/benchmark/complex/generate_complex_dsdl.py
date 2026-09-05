#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Write the synthetic civildrone benchmark corpus.

A civilian aerial-survey model, sized to make discovery, parsing, semantic
analysis and codegen show their scaling behaviour rather than their constant
factors: a few thousand definitions, deep cross-namespace references, and heavy
reuse of the regulated uavcan types.

The corpus is a build output. test/benchmark/CMakeLists.txt runs this into the
build tree and every benchmark target depends on the result, so the shape of the
workload is this file rather than a few thousand committed definitions.

Output is a pure function of the three counts below, so two runs of the same
revision produce the same bytes and a benchmark timing means something.
"""
from __future__ import annotations

import argparse
import pathlib
import shutil
import sys

# The synthetic families, sized so the corpus lands near 3,000 definitions.
# Raising one is how the workload is scaled; each count multiplies a family
# whose members reference three others, so path count grows faster than
# file count.
DEFAULT_CLASSIC_WORKLOAD_COUNT = 400
DEFAULT_VISION_WORKLOAD_COUNT = 500
DEFAULT_MEGABUNDLE_COUNT = 180

# Subsystems of the vehicle. Each gets ten types with the same internal shape,
# which is what makes a cross-domain reference in the workload family valid
# whichever domains the arithmetic lands on.
DOMAINS = [
    "airframe", "mission", "navigation", "estimation", "control", "propulsion",
    "power", "payload", "survey", "mapping", "imaging", "lidar", "radar",
    "communications", "avoidance", "safety", "diagnostics", "weather",
    "geofence", "terrain", "autonomy", "perception", "planner", "traffic",
    "energy", "maintenance", "networking", "storage",
]

# Stages of the vision stack, from sensor control through to the scene graph.
# Each gets nineteen types, and a stage's graph edges reach its neighbours in
# this order, so the list order is part of the reference graph.
VISION_MODULES = [
    "camera", "gimbal", "optics", "isp", "stream", "transport",
    "controlchannel", "codec", "encoder", "decoder", "packetizer",
    "depacketizer", "recorder", "synchronization", "calibration", "stereo",
    "depth", "vslam", "localization", "mapping", "feature", "extraction",
    "matching", "triangulation", "detection", "classification", "segmentation",
    "tracking", "reid", "inference", "fusion", "planner", "scenegraph",
    "objectstore", "telemetryvision",
]


def capitalise(token: str) -> str:
    """The token with its first character upper-cased, as a type name wants it."""
    return token[:1].upper() + token[1:]


class Corpus:
    """Accumulates definitions and writes them under one root namespace."""

    def __init__(self, root: pathlib.Path) -> None:
        self.root = root
        self.files: dict[pathlib.Path, str] = {}

    def add(self, relative: str, body: str) -> None:
        path = self.root / relative
        assert path not in self.files, f"{relative} written twice"
        self.files[path] = body.lstrip("\n")

    def write(self) -> int:
        # The root is removed rather than written over: a definition that a
        # smaller count no longer produces would otherwise stay behind and be
        # compiled as part of the next run's corpus.
        if self.root.exists():
            shutil.rmtree(self.root)
        for path, body in self.files.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(body, encoding="utf-8")
        return len(self.files)


def add_core(corpus: Corpus) -> None:
    """The types every other namespace reaches for."""
    corpus.add("core/NodeIdentity.1.0.dsdl", """
uavcan.node.Heartbeat.1.0 heartbeat
uavcan.time.SynchronizedTimestamp.1.0 timestamp
truncated uint8[<=64] vehicle_name
uint16 node_id
uint8 fleet_id
@sealed
""")
    corpus.add("core/GeoPoint.1.0.dsdl", """
float64 latitude_rad
float64 longitude_rad
float32 altitude_m
@sealed
""")
    corpus.add("core/GeoBounds.1.0.dsdl", """
civildrone.core.GeoPoint.1.0 southwest
civildrone.core.GeoPoint.1.0 northeast
@sealed
""")
    corpus.add("core/PoseKinematics.1.0.dsdl", """
uavcan.si.sample.length.Vector3.1.0 position
uavcan.si.sample.velocity.Vector3.1.0 velocity
uavcan.si.sample.acceleration.Vector3.1.0 acceleration
uavcan.si.sample.angle.Quaternion.1.0 attitude
uavcan.si.sample.angular_velocity.Vector3.1.0 angular_velocity
@sealed
""")
    corpus.add("core/WindEstimate.1.0.dsdl", """
uavcan.si.sample.velocity.Vector3.1.0 velocity
uavcan.si.sample.pressure.Scalar.1.0 pressure
uavcan.si.sample.temperature.Scalar.1.0 temperature
@sealed
""")
    corpus.add("core/BatterySnapshot.1.0.dsdl", """
uavcan.si.sample.voltage.Scalar.1.0 voltage
uavcan.si.sample.electric_current.Scalar.1.0 current
uavcan.si.sample.power.Scalar.1.0 power
float32 state_of_charge
float32 state_of_health
@sealed
""")
    corpus.add("core/LinkStats.1.0.dsdl", """
uint32 tx_frames
uint32 rx_frames
uint32 dropped_frames
float32 packet_error_rate
uavcan.diagnostic.Record.1.1 last_fault
@sealed
""")
    corpus.add("core/MissionPhase.1.0.dsdl", """
@union
uavcan.primitive.Empty.1.0 idle
uavcan.primitive.String.1.0 phase_name
uint8 phase_code
civildrone.core.GeoPoint.1.0 anchor
@sealed
""")
    corpus.add("core/FaultRecord.1.0.dsdl", """
uavcan.time.SynchronizedTimestamp.1.0 timestamp
uavcan.diagnostic.Record.1.1 diagnostic
uavcan.register.Value.1.0 captured_register
@sealed
""")
    corpus.add("core/SafetyEnvelope.1.0.dsdl", """
float32 max_speed_mps
float32 max_altitude_m
float32 max_climb_rate_mps
float32 max_descend_rate_mps
civildrone.core.GeoBounds.1.0 geofence
@sealed
""")
    corpus.add("core/SurveyTask.1.0.dsdl", """
uint32 task_id
civildrone.core.GeoBounds.1.0 area
float32 desired_ground_resolution_m
uavcan.primitive.String.1.0 sensor_profile
@sealed
""")
    corpus.add("core/SurveyPlan.1.0.dsdl", """
uint32 plan_id
civildrone.core.SurveyTask.1.0[<=32] tasks
civildrone.core.SafetyEnvelope.1.0 constraints
uavcan.time.SynchronizedTimestamp.1.0 created_at
@sealed
""")
    corpus.add("core/ActuatorCommand.1.0.dsdl", """
uint16 actuator_id
float32 command
float32 feed_forward
float32 rate_limit
@sealed
""")
    corpus.add("core/SensorHealth.1.0.dsdl", """
uint16 sensor_id
bool online
float32 confidence
uavcan.diagnostic.Record.1.1 last_diagnostic
@sealed
""")
    corpus.add("core/SystemSnapshot.1.0.dsdl", """
civildrone.core.NodeIdentity.1.0 identity
civildrone.core.PoseKinematics.1.0 pose
civildrone.core.WindEstimate.1.0 wind
civildrone.core.BatterySnapshot.1.0 battery
civildrone.core.LinkStats.1.0 link
civildrone.core.MissionPhase.1.0 mission_phase
civildrone.core.FaultRecord.1.0[<=16] recent_faults
@sealed
""")
    corpus.add("core/Dispatch.1.0.dsdl", """
civildrone.core.NodeIdentity.1.0 requester
civildrone.core.SurveyPlan.1.0 plan
civildrone.core.SafetyEnvelope.1.0 envelope
@sealed
---
bool accepted
uavcan.register.Value.1.0 scheduling_register
uavcan.primitive.String.1.0 message
@sealed
""")


def add_domains(corpus: Corpus) -> None:
    """Ten types per subsystem, each subsystem the same shape."""
    for domain in DOMAINS:
        pfx = capitalise(domain)
        corpus.add(f"{domain}/{pfx}Metric.1.0.dsdl", f"""
uavcan.time.SynchronizedTimestamp.1.0 timestamp
float32 score
uavcan.si.sample.temperature.Scalar.1.0 thermal
uavcan.si.sample.pressure.Scalar.1.0 pressure
uavcan.si.sample.power.Scalar.1.0 power
@sealed
""")
        corpus.add(f"{domain}/{pfx}Setpoint.1.0.dsdl", """
float32[3] target_vector
saturated int16 gain_q8
bool enabled
@sealed
""")
        corpus.add(f"{domain}/{pfx}Limits.1.0.dsdl", """
float32 min_value
float32 max_value
float32 max_rate
float32 max_jerk
@sealed
""")
        corpus.add(f"{domain}/{pfx}State.1.0.dsdl", f"""
civildrone.{domain}.{pfx}Metric.1.0 metric
civildrone.{domain}.{pfx}Setpoint.1.0 setpoint
civildrone.{domain}.{pfx}Limits.1.0 limits
uavcan.diagnostic.Record.1.1 last_diagnostic
uavcan.register.Value.1.0 tuning_register
@sealed
""")
        corpus.add(f"{domain}/{pfx}Event.1.0.dsdl", f"""
@union
civildrone.{domain}.{pfx}Metric.1.0 metric
civildrone.{domain}.{pfx}State.1.0 state
uavcan.primitive.String.1.0 text
uavcan.register.Value.1.0 register_value
@sealed
""")
        corpus.add(f"{domain}/{pfx}Batch.1.0.dsdl", f"""
uavcan.time.SynchronizedTimestamp.1.0 collected_at
civildrone.{domain}.{pfx}Event.1.0[<=12] events
civildrone.{domain}.{pfx}State.1.0[<=6] states
uint16 dropped_samples
@sealed
""")
        corpus.add(f"{domain}/{pfx}Report.1.0.dsdl", f"""
uavcan.node.Heartbeat.1.0 heartbeat
civildrone.core.NodeIdentity.1.0 node
civildrone.{domain}.{pfx}Batch.1.0 batch
uavcan.si.sample.voltage.Scalar.1.0 bus_voltage
uavcan.si.sample.electric_current.Scalar.1.0 bus_current
@sealed
""")
        corpus.add(f"{domain}/{pfx}Profile.1.0.dsdl", f"""
uint8 MODE_DISABLED = 0
uint8 MODE_STANDBY  = 1
uint8 MODE_ACTIVE   = 2
uint8 mode
civildrone.{domain}.{pfx}Limits.1.0 limits
civildrone.{domain}.{pfx}Setpoint.1.0 default_setpoint
uavcan.register.Value.1.0 profile_register
@sealed
""")
        corpus.add(f"{domain}/{pfx}History.1.0.dsdl", f"""
civildrone.{domain}.{pfx}Report.1.0[<=4] reports
civildrone.{domain}.{pfx}Event.1.0[<=24] timeline
uavcan.time.SynchronizedTimestamp.1.0 last_update
@sealed
""")
        corpus.add(f"{domain}/{pfx}Control.1.0.dsdl", f"""
uint8 opcode
civildrone.core.NodeIdentity.1.0 requester
civildrone.{domain}.{pfx}Setpoint.1.0 desired
uavcan.register.Value.1.0 register_override
@sealed
---
bool accepted
civildrone.{domain}.{pfx}State.1.0 resulting_state
civildrone.{domain}.{pfx}Event.1.0 resulting_event
uavcan.register.Value.1.0 effective_register
@sealed
""")


def add_vision_common(corpus: Corpus) -> None:
    """The vocabulary every vision stage shares."""
    corpus.add("vision/common/PixelFormat.1.0.dsdl", """
@union
uavcan.primitive.Empty.1.0 unknown
uint8 mono8
uint8 rgb8
uint8 bgr8
uint8 yuv420
uint8 nv12
@sealed
""")
    corpus.add("vision/common/FrameHeader.1.0.dsdl", """
uavcan.time.SynchronizedTimestamp.1.0 timestamp
uint64 frame_index
uint16 stream_id
uint16 camera_id
uavcan.si.sample.angle.Quaternion.1.0 sensor_attitude
@sealed
""")
    corpus.add("vision/common/RegionOfInterest.1.0.dsdl", """
uint16 x
uint16 y
uint16 width
uint16 height
@sealed
""")
    corpus.add("vision/common/BoundingBox2D.1.0.dsdl", """
float32 cx
float32 cy
float32 width
float32 height
float32 confidence
@sealed
""")
    corpus.add("vision/common/Keypoint2D.1.0.dsdl", """
float32 x
float32 y
float32 scale
float32 orientation
@sealed
""")
    corpus.add("vision/common/TensorShape.1.0.dsdl", """
uint32[<=8] dimensions
@sealed
""")
    corpus.add("vision/common/Descriptor256.1.0.dsdl", """
uint8[32] bytes
@sealed
""")
    corpus.add("vision/common/ObjectClass.1.0.dsdl", """
@union
uavcan.primitive.Empty.1.0 none
uint16 class_id
uavcan.primitive.String.1.0 class_name
@sealed
""")
    corpus.add("vision/common/InferenceRuntime.1.0.dsdl", """
uint8 BACKEND_CPU   = 0
uint8 BACKEND_GPU   = 1
uint8 BACKEND_NPU   = 2
uint8 BACKEND_FPGA  = 3
uint8 backend
float32 max_latency_ms
float32 target_fps
@sealed
""")
    corpus.add("vision/common/CameraIntrinsics.1.0.dsdl", """
float32 fx
float32 fy
float32 cx
float32 cy
float32[<=8] distortion
@sealed
""")
    corpus.add("vision/common/CameraExtrinsics.1.0.dsdl", """
uavcan.si.sample.length.Vector3.1.0 translation
uavcan.si.sample.angle.Quaternion.1.0 rotation
@sealed
""")
    corpus.add("vision/common/StreamingQoS.1.0.dsdl", """
uint32 bitrate_bps
uint16 mtu_bytes
float32 max_jitter_ms
float32 max_latency_ms
bool reliable_control_channel
@sealed
""")


def add_vision_modules(corpus: Corpus) -> None:
    """Nineteen types per vision stage, cross-linked to its two neighbours."""
    count = len(VISION_MODULES)
    for idx, module in enumerate(VISION_MODULES):
        pfx = capitalise(module)
        next_module = VISION_MODULES[(idx + 1) % count]
        prev_module = VISION_MODULES[(idx + count - 1) % count]
        next_pfx = capitalise(next_module)
        prev_pfx = capitalise(prev_module)
        d = f"vision/{module}"

        corpus.add(f"{d}/{pfx}ControlChannel.1.0.dsdl", """
uint16 channel_id
uint8 priority
bool reliable
float32 target_rate_hz
uavcan.register.Value.1.0 channel_register
civildrone.vision.common.StreamingQoS.1.0 qos
@sealed
""")
        corpus.add(f"{d}/{pfx}LowLevelControlCommand.1.0.dsdl", """
uint8 mode
float32[<=32] gains
float32[<=32] offsets
uavcan.register.Value.1.0 override
@sealed
""")
        corpus.add(f"{d}/{pfx}LowLevelControl.1.0.dsdl", f"""
civildrone.vision.{module}.{pfx}LowLevelControlCommand.1.0 command
@sealed
---
bool accepted
uavcan.register.Value.1.0 effective
uavcan.primitive.String.1.0 status
@sealed
""")
        corpus.add(f"{d}/{pfx}FrameMeta.1.0.dsdl", """
civildrone.vision.common.FrameHeader.1.0 header
civildrone.vision.common.PixelFormat.1.0 format
uint16 width
uint16 height
float32 exposure_ms
float32 gain_db
civildrone.vision.common.CameraIntrinsics.1.0 intrinsics
civildrone.vision.common.CameraExtrinsics.1.0 extrinsics
@sealed
""")
        corpus.add(f"{d}/{pfx}FramePacket.1.0.dsdl", f"""
civildrone.vision.{module}.{pfx}FrameMeta.1.0 meta
uint16 packet_index
uint16 packet_count
truncated uint8[<=1400] payload
@sealed
""")
        corpus.add(f"{d}/{pfx}Feature.1.0.dsdl", """
civildrone.vision.common.Keypoint2D.1.0 keypoint
civildrone.vision.common.Descriptor256.1.0 descriptor
float32 strength
@sealed
""")
        corpus.add(f"{d}/{pfx}FeatureSet.1.0.dsdl", f"""
civildrone.vision.{module}.{pfx}FrameMeta.1.0 meta
civildrone.vision.{module}.{pfx}Feature.1.0[<=512] features
@sealed
""")
        corpus.add(f"{d}/{pfx}Detection.1.0.dsdl", """
civildrone.vision.common.BoundingBox2D.1.0 box
civildrone.vision.common.ObjectClass.1.0 object_class
float32 confidence
uavcan.si.sample.length.Vector3.1.0 position_estimate
@sealed
""")
        corpus.add(f"{d}/{pfx}DetectionSet.1.0.dsdl", f"""
civildrone.vision.{module}.{pfx}FrameMeta.1.0 meta
civildrone.vision.{module}.{pfx}Detection.1.0[<=256] detections
@sealed
""")
        corpus.add(f"{d}/{pfx}Classification.1.0.dsdl", """
civildrone.vision.common.RegionOfInterest.1.0 roi
civildrone.vision.common.ObjectClass.1.0 object_class
float32 confidence
float32[<=16] embedding
@sealed
""")
        corpus.add(f"{d}/{pfx}InferenceTensor.1.0.dsdl", """
civildrone.vision.common.TensorShape.1.0 shape
float32 scale
int32 zero_point
truncated uint8[<=4096] quantized_data
@sealed
""")
        corpus.add(f"{d}/{pfx}GraphNode.1.0.dsdl", """
uint32 node_id
civildrone.vision.common.Keypoint2D.1.0 image_point
uavcan.si.sample.length.Vector3.1.0 world_point
float32 uncertainty
@sealed
""")
        corpus.add(f"{d}/{pfx}GraphEdge.1.0.dsdl", f"""
uint32 edge_id
civildrone.vision.{module}.{pfx}GraphNode.1.0 from_node
civildrone.vision.{next_module}.{next_pfx}GraphNode.1.0 to_node
float32 information
@sealed
""")
        corpus.add(f"{d}/{pfx}GraphState.1.0.dsdl", f"""
civildrone.vision.{module}.{pfx}GraphNode.1.0[<=1024] nodes
civildrone.vision.{module}.{pfx}GraphEdge.1.0[<=2048] edges
civildrone.vision.{prev_module}.{prev_pfx}GraphNode.1.0[<=64] upstream_nodes
@sealed
""")
        corpus.add(f"{d}/{pfx}PipelineEvent.1.0.dsdl", f"""
@union
civildrone.vision.{module}.{pfx}FramePacket.1.0 packet
civildrone.vision.{module}.{pfx}FeatureSet.1.0 features
civildrone.vision.{module}.{pfx}DetectionSet.1.0 detections
civildrone.vision.{module}.{pfx}InferenceTensor.1.0 tensor
civildrone.vision.{module}.{pfx}GraphState.1.0 graph
uavcan.diagnostic.Record.1.1 diagnostic
@sealed
""")
        corpus.add(f"{d}/{pfx}BatchReport.1.0.dsdl", f"""
uavcan.node.Heartbeat.1.0 heartbeat
civildrone.core.NodeIdentity.1.0 node
civildrone.vision.{module}.{pfx}PipelineEvent.1.0[<=32] events
civildrone.vision.{next_module}.{next_pfx}PipelineEvent.1.0[<=4] downstream_events
civildrone.vision.common.InferenceRuntime.1.0 runtime
uavcan.register.Value.1.0 active_model
@sealed
""")
        corpus.add(f"{d}/{pfx}ControlPlane.1.0.dsdl", f"""
civildrone.core.NodeIdentity.1.0 requester
civildrone.vision.{module}.{pfx}ControlChannel.1.0 channel
civildrone.vision.{module}.{pfx}LowLevelControlCommand.1.0 control
civildrone.vision.common.InferenceRuntime.1.0 runtime
uavcan.register.Value.1.0 profile
@sealed
---
bool accepted
civildrone.vision.{module}.{pfx}BatchReport.1.0 report
uavcan.primitive.String.1.0 message
@sealed
""")
        model_body = """
uavcan.primitive.String.1.0 model_name
uint32 model_version
civildrone.vision.common.TensorShape.1.0 input_shape
truncated uint8[<=4096] model_chunk
uavcan.register.Value.1.0 checksum
@sealed
"""
        corpus.add(f"{d}/{pfx}ModelUpdate.1.0.dsdl", model_body)
        corpus.add(f"{d}/{pfx}ModelChunk.1.0.dsdl", model_body)


def add_vision_toplevel(corpus: Corpus) -> None:
    """The types that gather every vision stage into one definition."""
    reports = "".join(
        f"civildrone.vision.{module}.{capitalise(module)}BatchReport.1.0 {module}_report\n"
        for module in VISION_MODULES
    )
    corpus.add("vision/VisionNodeSnapshot.1.0.dsdl", f"""
civildrone.core.NodeIdentity.1.0 node
civildrone.core.PoseKinematics.1.0 pose
{reports}@sealed
""")
    corpus.add("vision/VisionFleetSnapshot.1.0.dsdl", """
uavcan.time.SynchronizedTimestamp.1.0 timestamp
civildrone.vision.VisionNodeSnapshot.1.0[<=64] nodes
uavcan.primitive.String.1.0 deployment_name
@sealed
""")
    corpus.add("vision/VisionMissionControl.1.0.dsdl", """
civildrone.core.NodeIdentity.1.0 requester
civildrone.vision.VisionFleetSnapshot.1.0 baseline
uavcan.register.Value.1.0 mission_register
@sealed
---
bool accepted
civildrone.vision.VisionFleetSnapshot.1.0 updated
uavcan.primitive.String.1.0 status
@sealed
""")
    corpus.add("vision/VslamMapChunk.1.0.dsdl", """
uint32 chunk_id
civildrone.vision.vslam.VslamGraphState.1.0 graph
civildrone.vision.localization.LocalizationGraphState.1.0 localization_graph
truncated uint8[<=8192] compressed_payload
@sealed
""")
    corpus.add("vision/SceneUnderstanding.1.0.dsdl", """
civildrone.vision.detection.DetectionDetectionSet.1.0 detections
civildrone.vision.segmentation.SegmentationDetectionSet.1.0 segments
civildrone.vision.classification.ClassificationClassification.1.0[<=256] classes
civildrone.vision.tracking.TrackingGraphState.1.0 tracks
@sealed
""")
    corpus.add("vision/VideoSessionControl.1.0.dsdl", """
uint32 session_id
civildrone.vision.stream.StreamControlChannel.1.0 stream_channel
civildrone.vision.codec.CodecControlChannel.1.0 codec_channel
civildrone.vision.transport.TransportControlChannel.1.0 transport_channel
uavcan.register.Value.1.0 session_register
@sealed
---
bool accepted
civildrone.vision.VisionNodeSnapshot.1.0 snapshot
uavcan.primitive.String.1.0 message
@sealed
""")
    corpus.add("vision/VideoArchiveChunk.1.0.dsdl", """
uint32 archive_id
uint32 chunk_index
civildrone.vision.stream.StreamFramePacket.1.0[<=8] packets
truncated uint8[<=8192] compressed_index
@sealed
""")
    corpus.add("vision/InferenceCampaign.1.0.dsdl", """
uint32 campaign_id
civildrone.vision.inference.InferenceModelChunk.1.0[<=64] updates
civildrone.vision.fusion.FusionControlChannel.1.0[<=64] controls
civildrone.vision.VisionFleetSnapshot.1.0 fleet
@sealed
""")


def add_fleet(corpus: Corpus) -> None:
    """High-fanout types: one field per subsystem, and fleets of those."""
    digest_reports = "".join(
        f"civildrone.{domain}.{capitalise(domain)}Report.1.0 {domain}_report\n"
        for domain in DOMAINS
    )
    corpus.add("fleet/PlatformDigest.1.0.dsdl", f"""
civildrone.core.SystemSnapshot.1.0 snapshot
civildrone.vision.VisionNodeSnapshot.1.0 vision
{digest_reports}@sealed
""")
    control_setpoints = "".join(
        f"civildrone.{domain}.{capitalise(domain)}Setpoint.1.0 {domain}_setpoint\n"
        for domain in DOMAINS
    )
    corpus.add("fleet/PlatformControlVector.1.0.dsdl", f"""
civildrone.core.NodeIdentity.1.0 target
{control_setpoints}@sealed
""")
    corpus.add("fleet/FleetSnapshot.1.0.dsdl", """
uavcan.time.SynchronizedTimestamp.1.0 timestamp
civildrone.fleet.PlatformDigest.1.0[<=64] platforms
uavcan.primitive.String.1.0 region_name
@sealed
""")
    corpus.add("fleet/FleetEvent.1.0.dsdl", """
@union
civildrone.fleet.PlatformDigest.1.0 digest
civildrone.fleet.FleetSnapshot.1.0 snapshot
uavcan.diagnostic.Record.1.1 diagnostic
uavcan.primitive.String.1.0 text
@sealed
""")
    corpus.add("fleet/FleetPlan.1.0.dsdl", """
uint32 campaign_id
civildrone.core.SurveyPlan.1.0[<=8] plans
civildrone.fleet.PlatformControlVector.1.0[<=64] initial_controls
@sealed
""")
    corpus.add("fleet/FleetCommand.1.0.dsdl", """
civildrone.core.NodeIdentity.1.0 requester
civildrone.fleet.FleetPlan.1.0 plan
uavcan.register.Value.1.0 command_register
@sealed
---
bool accepted
civildrone.fleet.FleetSnapshot.1.0 resulting_snapshot
uavcan.primitive.String.1.0 status
@sealed
""")
    corpus.add("fleet/FleetDiagnostics.1.0.dsdl", """
civildrone.fleet.FleetEvent.1.0[<=128] events
uavcan.diagnostic.Record.1.1[<=128] records
@sealed
""")
    corpus.add("fleet/SurveyCampaign.1.0.dsdl", """
uint32 campaign_id
civildrone.core.SurveyTask.1.0[<=256] tasks
civildrone.core.SafetyEnvelope.1.0 global_safety
@sealed
""")
    corpus.add("fleet/SurveyCampaignState.1.0.dsdl", """
civildrone.fleet.SurveyCampaign.1.0 campaign
civildrone.fleet.FleetSnapshot.1.0 latest_snapshot
civildrone.fleet.FleetDiagnostics.1.0 diagnostics
@sealed
""")
    corpus.add("fleet/SurveyCampaignControl.1.0.dsdl", """
uint8 action
civildrone.fleet.SurveyCampaign.1.0 campaign
uavcan.register.Value.1.0 config
@sealed
---
bool accepted
civildrone.fleet.SurveyCampaignState.1.0 state
@sealed
""")
    corpus.add("fleet/FleetRegisterMirror.1.0.dsdl", """
civildrone.core.NodeIdentity.1.0 node
uavcan.register.Value.1.0[<=128] values
@sealed
""")
    corpus.add("fleet/FleetRegisterSync.1.0.dsdl", """
civildrone.fleet.FleetRegisterMirror.1.0[<=64] mirrors
uavcan.time.SynchronizedTimestamp.1.0 synchronized_at
@sealed
""")


def add_workload(corpus: Corpus, classic: int, vision: int, mega: int) -> None:
    """The synthetic families. Each member reaches three others, chosen by
    stepping through the domain and module lists at coprime strides, so a
    family of n members spans far more than n reference paths."""
    domain_count = len(DOMAINS)
    module_count = len(VISION_MODULES)

    for i in range(1, classic + 1):
        d1 = DOMAINS[(i - 1) % domain_count]
        d2 = DOMAINS[(i + 7) % domain_count]
        d3 = DOMAINS[(i + 13) % domain_count]
        p1, p2, p3 = capitalise(d1), capitalise(d2), capitalise(d3)
        corpus.add(f"workload/Composite{i}.1.0.dsdl", f"""
civildrone.core.NodeIdentity.1.0 node
civildrone.core.PoseKinematics.1.0 pose
civildrone.{d1}.{p1}State.1.0 {d1}_state
civildrone.{d2}.{p2}Report.1.0 {d2}_report
civildrone.{d3}.{p3}History.1.0 {d3}_history
uavcan.register.Value.1.0 tuning
truncated uint8[<=256] opaque_payload
@sealed
""")
        corpus.add(f"workload/Variant{i}.1.0.dsdl", f"""
@union
civildrone.{d1}.{p1}Event.1.0 {d1}_event
civildrone.{d2}.{p2}Event.1.0 {d2}_event
civildrone.{d3}.{p3}Event.1.0 {d3}_event
civildrone.workload.Composite{i}.1.0 composite
uavcan.primitive.String.1.0 text
@sealed
""")

    for i in range(1, vision + 1):
        m1 = VISION_MODULES[(i - 1) % module_count]
        m2 = VISION_MODULES[(i + 5) % module_count]
        m3 = VISION_MODULES[(i + 11) % module_count]
        p1, p2, p3 = capitalise(m1), capitalise(m2), capitalise(m3)
        corpus.add(f"workload/VisionComposite{i}.1.0.dsdl", f"""
civildrone.core.NodeIdentity.1.0 node
civildrone.vision.{m1}.{p1}BatchReport.1.0 {m1}_batch
civildrone.vision.{m2}.{p2}DetectionSet.1.0 {m2}_detections
civildrone.vision.{m3}.{p3}GraphState.1.0 {m3}_graph
civildrone.vision.VisionNodeSnapshot.1.0 snapshot
civildrone.fleet.PlatformDigest.1.0 fleet_digest
truncated uint8[<=4096] opaque_payload
@sealed
""")
        corpus.add(f"workload/VisionVariant{i}.1.0.dsdl", f"""
@union
civildrone.vision.{m1}.{p1}PipelineEvent.1.0 {m1}_event
civildrone.vision.{m2}.{p2}PipelineEvent.1.0 {m2}_event
civildrone.vision.{m3}.{p3}PipelineEvent.1.0 {m3}_event
civildrone.workload.VisionComposite{i}.1.0 composite
uavcan.primitive.String.1.0 text
@sealed
""")

    for i in range(1, mega + 1):
        m1 = VISION_MODULES[(i + 2) % module_count]
        m2 = VISION_MODULES[(i + 9) % module_count]
        p1, p2 = capitalise(m1), capitalise(m2)
        corpus.add(f"workload/MegaBundle{i}.1.0.dsdl", f"""
uavcan.time.SynchronizedTimestamp.1.0 timestamp
civildrone.workload.Composite{(i % classic) + 1}.1.0 classic_composite
civildrone.workload.VisionComposite{(i % vision) + 1}.1.0 vision_composite
civildrone.vision.{m1}.{p1}InferenceTensor.1.0 {m1}_tensor
civildrone.vision.{m2}.{p2}InferenceTensor.1.0 {m2}_tensor
civildrone.vision.InferenceCampaign.1.0 campaign
uavcan.register.Value.1.0 registry_hint
@sealed
""")


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Write the synthetic civildrone benchmark corpus.")
    ap.add_argument("--outdir", type=pathlib.Path, required=True,
                    help="directory to write the civildrone root namespace into")
    ap.add_argument("--classic-workload-count", type=int,
                    default=DEFAULT_CLASSIC_WORKLOAD_COUNT)
    ap.add_argument("--vision-workload-count", type=int,
                    default=DEFAULT_VISION_WORKLOAD_COUNT)
    ap.add_argument("--megabundle-count", type=int,
                    default=DEFAULT_MEGABUNDLE_COUNT)
    ap.add_argument("--stamp", type=pathlib.Path,
                    help="file to touch once the corpus is written, for a build "
                         "system that wants one output to depend on")
    args = ap.parse_args()

    for name, value in (("--classic-workload-count", args.classic_workload_count),
                        ("--vision-workload-count", args.vision_workload_count),
                        ("--megabundle-count", args.megabundle_count)):
        if value < 1:
            print(f"error: {name} must be at least 1", file=sys.stderr)
            return 1

    corpus = Corpus(args.outdir / "civildrone")
    add_core(corpus)
    add_domains(corpus)
    add_vision_common(corpus)
    add_vision_modules(corpus)
    add_vision_toplevel(corpus)
    add_fleet(corpus)
    add_workload(corpus,
                 args.classic_workload_count,
                 args.vision_workload_count,
                 args.megabundle_count)
    written = corpus.write()

    if args.stamp:
        args.stamp.parent.mkdir(parents=True, exist_ok=True)
        args.stamp.write_text(f"{written}\n", encoding="utf-8")

    print(f"benchmark corpus: {written} definitions under {corpus.root}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
