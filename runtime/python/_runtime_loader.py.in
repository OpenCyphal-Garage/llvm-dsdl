#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#

#===----------------------------------------------------------------------===#
#
# @file
# Runtime backend selector for generated Python DSDL packages.
#
# This loader resolves pure or accelerator-backed runtime implementations from
# environment configuration and exposes the selected backend identity.
#
#===----------------------------------------------------------------------===#

"""Runtime backend loader for generated Python packages."""

from __future__ import annotations

import os

mode = os.environ.get("LLVMDSDL_PY_RUNTIME_MODE", "auto").strip().lower()
if mode not in {"auto", "pure", "accel"}:
    mode = "auto"

runtime = None
accel_error: Exception | None = None

if mode in {"auto", "accel"}:
    try:
        from . import _dsdl_runtime_accel as runtime  # type: ignore[assignment]
    except Exception as ex:  # pragma: no cover - backend availability is environment-dependent.
        accel_error = ex

if runtime is None:
    if mode == "accel":
        message = "LLVMDSDL_PY_RUNTIME_MODE=accel requested but accelerator is unavailable"
        if accel_error is not None:
            message += f": {accel_error}"
        raise RuntimeError(message)
    from . import _dsdl_runtime as runtime  # type: ignore[assignment]

BACKEND = getattr(runtime, "BACKEND", "unknown")
