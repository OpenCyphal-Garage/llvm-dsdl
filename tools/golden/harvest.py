#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Generate a golden with a pinned peer generator, inside the toolshed image.

A golden under test/golden/<peer>/ is what a peer generator writes for a definition, and it is
committed. Two things make the bytes reproducible on any machine: the peer and its DSDL frontend
are checked out at the revisions pinned in .github/actions/provision-peer-dsdl-compiler, and the
generator runs inside the toolshed container with the repository mounted at /w, so the source
paths a generator writes into its output are the same everywhere.

Nunavut is the peer for C and Python. Its parallel type lookup cannot pickle under Python 3.14,
so it runs with one job, over the `<root>:<path>` target form the CI parity lane used.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

IMAGE = "ghcr.io/opencyphal/toolshed:ts26.4.3"
NUNAVUT_SHA = "37095abb9fac47f924a52500bf4cebe2256dcff5"  # tag 3.0.1b1
PYDSDL_SHA = "a1506ce424589cc049b3fe4fece1e52626b55529"  # pydsdl 1.23.0-dev
MOUNT = "/w"
UAVCAN_ROOT = "submodules/public_regulated_data_types/uavcan"

LANGUAGES = {"c": "c", "python": "py"}


def nunavut_script(language: str, out_dir: str, definitions: list[str]) -> str:
    """The shell the container runs: a venv with the two pins, then one nnvg invocation."""
    targets = " ".join(f"{MOUNT}/{UAVCAN_ROOT}:{d}" for d in definitions)
    return "\n".join(
        [
            "set -euo pipefail",
            "python3 -m venv /tmp/peer",
            f"/tmp/peer/bin/pip install --quiet git+https://github.com/OpenCyphal/pydsdl@{PYDSDL_SHA}",
            f"/tmp/peer/bin/pip install --quiet --no-deps git+https://github.com/OpenCyphal/nunavut@{NUNAVUT_SHA}",
            "/tmp/peer/bin/python -c 'import nunavut, pydsdl; print(\"nunavut\", nunavut.__version__, \"pydsdl\", pydsdl.__version__)'",
            " ".join(
                [
                    "/tmp/peer/bin/python -m nunavut --jobs 1",
                    f"--target-language {LANGUAGES[language]}",
                    f"--outdir {MOUNT}/{out_dir}",
                    f"--lookup-dir {MOUNT}/{UAVCAN_ROOT}",
                    targets,
                ]
            ),
        ]
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--language", choices=sorted(LANGUAGES), required=True)
    parser.add_argument(
        "--definition",
        action="append",
        required=True,
        help="a definition relative to the uavcan root, e.g. node/7509.Heartbeat.1.0.dsdl; repeatable",
    )
    parser.add_argument(
        "--out",
        default=None,
        help="output directory relative to the repository root; default test/golden/nunavut/<language>",
    )
    args = parser.parse_args()

    repo = pathlib.Path(__file__).resolve().parents[2]
    out_dir = args.out or f"test/golden/nunavut/{args.language}"
    (repo / out_dir).mkdir(parents=True, exist_ok=True)

    command = [
        "docker",
        "run",
        "--rm",
        "-v",
        f"{repo}:{MOUNT}",
        "-w",
        MOUNT,
        IMAGE,
        "bash",
        "-c",
        nunavut_script(args.language, out_dir, args.definition),
    ]
    print("harvest:", args.language, *args.definition, "->", out_dir, file=sys.stderr)
    return subprocess.run(command, check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
