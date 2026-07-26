#!/usr/bin/env bash
# Prepare a checkout for development inside the toolshed container.
#
# This installs nothing. The toolshed image is the single source of truth for the
# build environment, and filling gaps here would let the image drift while local
# builds still worked -- then break in CI, or worse, silently register fewer
# tests, since the integration suite only registers a language's lanes when its
# toolchain is found at configure time.
#
set -euo pipefail

llvm_version="${LLVM_VERSION:-${TOOLSHED_LLVM_VERSION:-}}"
if [[ -z "${llvm_version}" && "${MLIR_DIR:-}" =~ /llvm-([0-9]+)(/|$) ]]; then
  llvm_version="${BASH_REMATCH[1]}"
fi
if [[ -z "${llvm_version}" && "${LLVM_DIR:-}" =~ /llvm-([0-9]+)(/|$) ]]; then
  llvm_version="${BASH_REMATCH[1]}"
fi
if [[ -z "${llvm_version}" ]]; then
  echo "Could not infer the LLVM version from LLVM_DIR, MLIR_DIR, or TOOLSHED_LLVM_VERSION" >&2
  exit 1
fi

missing=()
for tool in cmake ninja python3 git lit gzip go cargo tsc node "clang-${llvm_version}"; do
  command -v "${tool}" >/dev/null 2>&1 || missing+=("${tool}")
done
for f in "/usr/lib/llvm-${llvm_version}/lib/cmake/mlir/MLIRConfig.cmake" \
         "/usr/lib/llvm-${llvm_version}/lib/cmake/llvm/LLVMConfig.cmake"; do
  [ -f "${f}" ] || missing+=("${f}")
done
if ! find /usr/lib -path "*/cmake/zstd/zstdConfig.cmake" -print -quit | grep -q .; then
  missing+=("zstdConfig.cmake (libzstd-dev)")
fi

if [ "${#missing[@]}" -gt 0 ]; then
  echo "This container is missing prerequisites for a full build:" >&2
  printf '  %s\n' "${missing[@]}" >&2
  echo >&2
  echo "These belong in the toolshed image rather than being installed here." >&2
  exit 1
fi

git submodule update --init --recursive
cmake --preset dev-llvm-env
