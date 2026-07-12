#!/usr/bin/env bash
# Run TLC on the CyphalSerdes model. Locates tla2tools.jar via $TLA2TOOLS_JAR or
# common install locations; get it from https://github.com/tlaplus/tlaplus/releases.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"

# Search order: explicit override, then the repo's vendored (version-pinned) jar so
# runs are reproducible, then a local Toolbox / ~/.tla install as a convenience fallback.
jar="${TLA2TOOLS_JAR:-}"
if [[ -z "${jar}" ]]; then
  for candidate in \
    "${here}/vendor/tla2tools.jar" \
    "/Applications/TLA+ Toolbox.app/Contents/Eclipse/tla2tools.jar" \
    "${HOME}/.tla/tla2tools.jar"; do
    if [[ -f "${candidate}" ]]; then jar="${candidate}"; break; fi
  done
fi

if [[ -z "${jar}" || ! -f "${jar}" ]]; then
  echo "error: tla2tools.jar not found." >&2
  echo "  set TLA2TOOLS_JAR=/path/to/tla2tools.jar, or install the TLA+ Toolbox," >&2
  echo "  or drop the jar at ${here}/vendor/tla2tools.jar" >&2
  echo "  releases: https://github.com/tlaplus/tlaplus/releases" >&2
  exit 2
fi

# Run from the spec dir so TLC's generated artifacts (states/, *_TTrace_*) land
# in spec/tla/ where .gitignore expects them, not in the caller's cwd.
cd "${here}"
exec java -XX:+UseParallelGC -cp "${jar}" tlc2.TLC -nowarning CyphalSerdes.tla
