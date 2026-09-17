#!/usr/bin/env python3
# ===----------------------------------------------------------------------=== #
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------=== #

"""Check that the manifest reports a definition's fixed port-ID, once, per definition."""

from __future__ import annotations

import json
import pathlib
import sys


def main() -> int:
    manifest = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
    failures = []

    for language, definitions in sorted(manifest["languages"].items()):
        expected = {
            "fixtures_unregulated.Unregulated.1.0": 1234,
            "fixtures_unregulated.PortedCall.1.0": 200,
        }
        for full_name, port_id in expected.items():
            entry = definitions.get(full_name)
            if entry is None:
                failures.append(f"{language}: {full_name} is missing from the manifest")
                continue
            if entry.get("fixed_port_id") != port_id:
                failures.append(
                    f"{language}: {full_name} reports fixed_port_id "
                    f"{entry.get('fixed_port_id')!r}, expected {port_id}"
                )
            # The port-ID belongs to the definition, so it is reported beside the file stem and not
            # inside a section. A service reporting one per section would be reporting the service's
            # twice under two names.
            for section in ("message", "request", "response"):
                if "fixed_port_id" in entry.get(section, {}):
                    failures.append(f"{language}: {full_name} reports a port-ID inside '{section}'")

    for failure in failures:
        print(failure, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
