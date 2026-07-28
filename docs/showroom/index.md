# Showroom

`lanyard` is a fictional vendor namespace of aerial-vehicle datatypes. It exists so that you can see what dsdlc produces for definitions shaped like the ones you are about to write, before you write them. Nothing here is a test fixture and nothing here is regulated: these are the sort of vendor-specific types a drone programme adds alongside the standard `uavcan` namespace, and they lean on the standard types wherever a standard type exists.

Generate the full output for every supported language and profile with:

```bash
cmake --build <build-dir> --target showroom
```

The tree lands under `<build-dir>/showroom/<variant>/`. The pages below carry the authored DSDL, the resulting wire-layout facts, and a declaration excerpt per language.

## What each type demonstrates

| Type | Port | Tier | Kind |
|---|---:|---|---|
| [`lanyard.flight.ControlSurfaces.1.0`](types/lanyard_flight_ControlSurfaces_1_0.md) | 6212 | CAN FD | message |
| [`lanyard.flight.FixedWingSurfaces.1.0`](types/lanyard_flight_FixedWingSurfaces_1_0.md) | -- | unspecified | message |
| [`lanyard.flight.FlightMode.1.0`](types/lanyard_flight_FlightMode_1_0.md) | -- | unspecified | message |
| [`lanyard.flight.MultirotorMix.1.0`](types/lanyard_flight_MultirotorMix_1_0.md) | -- | unspecified | message |
| [`lanyard.flight.VehicleState.1.0`](types/lanyard_flight_VehicleState_1_0.md) | 6210 | CAN FD | message |
| [`lanyard.flight.VehicleState.1.1`](types/lanyard_flight_VehicleState_1_1.md) | 6210 | CAN FD | message |
| [`lanyard.flight.VehicleState.2.0`](types/lanyard_flight_VehicleState_2_0.md) | 6211 | CAN FD | message |
| [`lanyard.health.BatteryStatus.2.0`](types/lanyard_health_BatteryStatus_2_0.md) | 6251 | CAN FD | message |
| [`lanyard.health.LegacyBatteryPoll.1.0`](types/lanyard_health_LegacyBatteryPoll_1_0.md) | 258 | Classic CAN | service |
| [`lanyard.health.SubsystemReport.1.0`](types/lanyard_health_SubsystemReport_1_0.md) | -- | unspecified | message |
| [`lanyard.health.SystemHealth.1.0`](types/lanyard_health_SystemHealth_1_0.md) | 6250 | CAN FD | message |
| [`lanyard.link.RcInput.1.0`](types/lanyard_link_RcInput_1_0.md) | 6240 | CAN FD | message |
| [`lanyard.link.TelemetryLinkStats.1.0`](types/lanyard_link_TelemetryLinkStats_1_0.md) | 6241 | CAN FD | message |
| [`lanyard.nav.GlobalPosition.1.0`](types/lanyard_nav_GlobalPosition_1_0.md) | 6220 | CAN FD | message |
| [`lanyard.nav.MissionPlan.1.0`](types/lanyard_nav_MissionPlan_1_0.md) | 6221 | Cyphal/UDP ONLY. This message does not fit any CAN | message |
| [`lanyard.nav.UploadMission.1.0`](types/lanyard_nav_UploadMission_1_0.md) | 256 | CAN FD and Cyphal/UDP | service |
| [`lanyard.nav.Waypoint.1.0`](types/lanyard_nav_Waypoint_1_0.md) | -- | unspecified | message |
| [`lanyard.payload.CameraFrameMetadata.1.0`](types/lanyard_payload_CameraFrameMetadata_1_0.md) | 6231 | Cyphal/UDP ONLY | message |
| [`lanyard.payload.CapturePhoto.1.0`](types/lanyard_payload_CapturePhoto_1_0.md) | 257 | CAN FD and Cyphal/UDP | service |
| [`lanyard.payload.GimbalStatus.1.0`](types/lanyard_payload_GimbalStatus_1_0.md) | 6230 | CAN FD | message |
| [`lanyard.propulsion.EscStatus.1.0`](types/lanyard_propulsion_EscStatus_1_0.md) | 6200 | Classic CAN (CAN 2.0B). This definition is sealed and | message |
| [`lanyard.propulsion.EscStatus.2.0`](types/lanyard_propulsion_EscStatus_2_0.md) | 6201 | CAN FD | message |
| [`lanyard.propulsion.ThrottleCommand.0.1`](types/lanyard_propulsion_ThrottleCommand_0_1.md) | 6202 | CAN FD | message |
| [`lanyard.propulsion.ThrottleCommand.1.0`](types/lanyard_propulsion_ThrottleCommand_1_0.md) | 6203 | CAN FD | message |

## Versioning

Five migrations are laid out across the namespace, each answering a different question about when a change forces a major version bump.

| Transition | Breaking | What it shows |
|---|---|---|
| `ThrottleCommand` 0.1 -> 1.0 | n/a | A `0.x` definition promises nothing; promotion to 1.0 is where the compatibility promise begins, not a compatible change. |
| `VehicleState` 1.0 -> 1.1 | no | A field appended inside an unchanged `@extent`. Both minor versions share port 6210 and interoperate in both directions. |
| `VehicleState` 1.1 -> 2.0 | yes | A field retyped, a field replaced, and the extent grown -- any one of which forces a new major version and a new port. |
| `EscStatus` 1.0 -> 2.0 | yes | `@sealed` is a one-way door: a sealed type cannot gain a field, so extensibility costs a major version. |
| `LegacyBatteryPoll` 1.0 -> `BatteryStatus` 2.0 | yes | `@deprecated` marking a superseded service while both halves of the migration stay in the namespace. |

## Transport tiers

Every definition states the transport it was sized for, and most of them assert that budget with `@assert _offset_.max <= ...` so that a layout change breaks the build rather than quietly spilling into a multi-frame transfer.

| Tier | Budget | Examples |
|---|---|---|
| Classic CAN | 7 payload bytes in one frame | `EscStatus.1.0`, hand-packed to exactly 56 bits |
| CAN FD | 63 payload bytes in one frame | `GlobalPosition.1.0`, `RcInput.1.0`, `GimbalStatus.1.0` |
| Cyphal/UDP | a datagram, kilobytes | `MissionPlan.1.0`, `CameraFrameMetadata.1.0` |

## Language features covered

| Feature | Where |
|---|---|
| `@sealed` | `EscStatus.1.0`, `RcInput.1.0`, `SubsystemReport.1.0` |
| `@extent` | `VehicleState.1.0`, `MissionPlan.1.0`, `Waypoint.1.0` |
| `@union` | `ControlSurfaces.1.0` |
| `@assert` | `EscStatus.1.0`, `GlobalPosition.1.0`, `CapturePhoto.1.0` |
| `@print` | `GlobalPosition.1.0` |
| `@deprecated` | `LegacyBatteryPoll.1.0` |
| Service request/response sections | `UploadMission.1.0`, `CapturePhoto.1.0` |
| Non-byte-aligned scalars | `EscStatus.1.0` (`uint14`, `int12`, `int9`, `uint4`) |
| `void` padding | `EscStatus.1.0`, `GlobalPosition.1.0`, `SubsystemReport.1.0` |
| Fixed-size arrays | `GlobalPosition.1.0` (`float16[9]`), `RcInput.1.0` (`uint11[16]`) |
| Variable-length arrays | `ThrottleCommand.1.0`, `MissionPlan.1.0`, `BatteryStatus.2.0` |
| Arrays of composites | `MissionPlan.1.0` (delimited), `SystemHealth.1.0` (sealed) |
| Cast modes | `CameraFrameMetadata.1.0` (`truncated` against `saturated`) |
| Constants as enumerations | `FlightMode.1.0`, `Waypoint.1.0`, `GlobalPosition.1.0` |
| Reuse of standard `uavcan` types | throughout; see `SubsystemReport.1.0` |

## A note on documentation

Every comment block in these definitions reaches the generated source in all six languages, attached to the type, field, or constant it documents. That is the reason the definitions are commented as heavily as they are: the DSDL is the only place the documentation is written, and the generated code is where most people will read it.

Comment placement follows the OpenCyphal convention used by the regulated namespace -- the block goes *after* the field it documents and is followed by a blank line. A block placed before a field attaches to whatever precedes it instead.

!!! note

    `@deprecated` reaches generated source in all six languages as a `Deprecated: ...` notice appended to the type's documentation, plus an `IS_DEPRECATED` metadata constant. Go reads the notice as a real deprecation, and TypeScript additionally gets a `/** @deprecated */` JSDoc block. Compile-time enforcement in C, C++, and Rust is opt-in behind `dsdlc --emit-deprecation-attributes`, because it turns documentation into a build diagnostic.
