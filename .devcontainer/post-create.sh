#!/usr/bin/env bash
set -euo pipefail

need_llvm_mlir=0
need_zstd=0

llvm_version="${LLVM_VERSION:-${TOOLSHED_LLVM_VERSION:-}}"
if [[ -z "${llvm_version}" && "${MLIR_DIR:-}" =~ /llvm-([0-9]+)(/|$) ]]; then
  llvm_version="${BASH_REMATCH[1]}"
fi
if [[ -z "${llvm_version}" && "${LLVM_DIR:-}" =~ /llvm-([0-9]+)(/|$) ]]; then
  llvm_version="${BASH_REMATCH[1]}"
fi
if [[ -z "${llvm_version}" ]]; then
  echo "Could not infer LLVM version from LLVM_DIR/MLIR_DIR/TOOLSHED_LLVM_VERSION" >&2
  exit 1
fi

mlir_dir="${MLIR_DIR:-/usr/lib/llvm-${llvm_version}/lib/cmake/mlir}"

if ! [ -f "${mlir_dir}/MLIRConfig.cmake" ]; then
  need_llvm_mlir=1
fi

if ! find /usr/lib -path "*/cmake/zstd/zstdConfig.cmake" -print -quit | grep -q .; then
  need_zstd=1
fi

if [ "$need_llvm_mlir" -eq 1 ] || [ "$need_zstd" -eq 1 ]; then
  if command -v sudo >/dev/null 2>&1 && [ "$(id -u)" -ne 0 ]; then
    sudo apt-get update
    sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      libzstd-dev \
      "libmlir-${llvm_version}-dev" \
      "mlir-${llvm_version}-tools"
  else
    apt-get update
    env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      libzstd-dev \
      "libmlir-${llvm_version}-dev" \
      "mlir-${llvm_version}-tools"
  fi
fi

if ! command -v lit >/dev/null 2>&1 && ! command -v llvm-lit >/dev/null 2>&1; then
  python3 -m pip install --break-system-packages lit
fi

git submodule update --init --recursive
cmake --preset dev-llvm-env
