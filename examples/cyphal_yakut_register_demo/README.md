# Yakut + Register Node Example (Native, Go, or Rust)

This example pairs Yakut with either:

1. A native C++ Cyphal node built from this repository using libudpard + POSIX UDP shim.
2. A Go Cyphal node built from this repository using generated Go types + libudpard via cgo.
3. A Rust Cyphal node built from this repository using generated Rust types + libudpard FFI.

Both node variants implement:

1. `uavcan.node.Heartbeat.1.0` publication on fixed subject-ID `7509`.
2. `uavcan.register.List.1.0` server on fixed service-ID `385`.
3. `uavcan.register.Access.1.0` server on fixed service-ID `384`.

## Build

Every command below runs from the repository root.

```bash
cmake -S . -B build/dev-homebrew
cmake --build build/dev-homebrew --target cyphal-yakut-register-node -j
cmake --build build/dev-homebrew --target cyphal-yakut-register-go-node -j
cmake --build build/dev-homebrew --target cyphal-yakut-register-rust-node -j
```

## Run Native Node

```bash
build/dev-homebrew/examples/cyphal_yakut_register_demo/cyphal-yakut-register-node \
  --name native \
  --node-id 42 \
  --iface 127.0.0.1 \
  --heartbeat-rate-hz 1
```

## Run Go Node

```bash
build/dev-homebrew/examples/cyphal_yakut_register_demo/go-node/cyphal-yakut-register-go-node-bin \
  --name go \
  --node-id 42 \
  --iface 127.0.0.1 \
  --heartbeat-rate-hz 1
```

## Run Rust Node

```bash
build/dev-homebrew/examples/cyphal_yakut_register_demo/rust-node/target/debug/cyphal-yakut-register-rust-node \
  --name rust \
  --node-id 42 \
  --iface 127.0.0.1 \
  --heartbeat-rate-hz 1
```

## Yakut Environment

In a second terminal:

```bash
export CYPHAL_PATH="$PWD/submodules/public_regulated_data_types"
export UAVCAN__UDP__IFACE="127.0.0.1"
export UAVCAN__NODE__ID="100"
```

## Observe Heartbeat

```bash
yakut monitor
```

You should see node `42` online and publishing heartbeat traffic.

## Register List / Read / Write

```bash
yakut register-list 42
yakut register-access 42 demo.rate_hz
yakut register-access 42 demo.rate_hz 10
yakut register-access 42 demo.rate_hz
yakut register-access 42 uavcan.node.description "yakut configured description"
yakut register-access 42 uavcan.node.description
yakut register-access 42 sys.version
yakut register-access 42 sys.version "attempted overwrite"
yakut register-access 42 sys.version
```

Expected behaviour:

1. `demo.rate_hz` changes and readback reflects new value.
2. `uavcan.node.description` changes and readback reflects new value.
3. `sys.version` remains unchanged because it is immutable.
