# Robotic Arm DSDL Demo

This is a compact demonstration DSDL namespace for a 6-DOF robotic arm with a generic hand/tool interface.
Physical quantities are encoded with `uavcan.si.unit.*` types.

## Namespace Root

Set `dsdld.rootNamespaceDirs` to this directory:

- `editors/vscode/dsdld-client/examples/robotic_arm`

Set `dsdld.lookupDirs` to include regulated data types for `uavcan.si.*`:

- `submodules/public_regulated_data_types`

The types are under `demo.arm.*`.

## Included Types

- `demo.arm.JointCommand.1.0`: joint-space command for 6 arm joints.
- `demo.arm.JointFeedback.1.0`: measured 6-joint feedback.
- `demo.arm.ToolCommand.1.0`: generic hand/tool command channels.
- `demo.arm.ToolFeedback.1.0`: generic hand/tool feedback channels.
- `demo.arm.ToolInfo.1.0`: tool capability metadata.
- `demo.arm.ArmCommand.1.0`: top-level command (joints + tool).
- `demo.arm.ArmStatus.1.0`: top-level status (joints + tool).
- `demo.arm.GetToolInfo.1.0`: simple service to query mounted tool info.
