#!/usr/bin/env python3
"""Run one language's style judge over generated code and hold it to a recorded baseline.

`CLEAN_CODE.md` phase 1 points each language's own judge at the generated code, because a compiler
accepts an un-idiomatic name by design. The findings are a backlog rather than a wall: a checked-in
baseline records what each rule reported when it was taken, and a lane fails where a rule reports
more than that. So the count can fall and cannot rise, and a rule that appears for the first time is
a regression even though its baseline is absent.

The comparison is per rule, not on the total. A total alone lets one rule grow behind another
shrinking, which is the drift a baseline exists to catch.

A baseline means something only for the judge that produced it, so it records that judge's version
and this declines to compare against a different one. `clippy` 0.1.93 reports
`collapsible_else_if` thirty-four times where 0.1.95 reports `nonminimal_bool` six, and comparing
across the two would read as a regression and a new rule at once.

Off CI that declining is a skip, exit 77, because a developer's clippy differing from the image's
is not a verdict on the generated code and not their defect to fix. On CI it is a failure: the
image pins every judge, so a mismatch there means the image moved and the baseline has to be
retaken. A gate that could skip in CI would be no gate.

Every judge is asked for structured output. Parsing a judge's prose would make the gate's meaning
depend on its phrasing, and a parse that silently matched nothing would report a clean run.
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

from assert_style_judges import GO_TOOLS, find_command

# What each judge is asked, beside the corpus. The selections are the ones `CLEAN_CODE.md` records
# its counts under; changing one changes what the baseline means, so it belongs here rather than in
# a lane's arguments.
RUFF_SELECT = "E,F,W,N,UP,B,SIM,RUF"
STATICCHECK_CHECKS = "all"

# clang-tidy's rulesets are long enough to carry a reason per subtraction, so they are files beside
# the baselines rather than strings here.
CLANG_TIDY_RULESETS = Path(__file__).resolve().parent.parent / "test" / "integration" / "judge-rulesets"

# The C++ judge reads the `std` profile at the lowest standard it compiles under, so no modernize
# check can suggest what a consumer on that standard could not write.
CLANG_TIDY_FLAGS = {"c": ["-std=c11"], "cpp": ["-x", "c++-header", "-std=c++20"]}
CPP_PROFILE = "std"

# The C++ tree carries the C runtime header for its bodies to call. It is the C tree's header under
# another banner, and the C judge holds it to C's conventions rather than this one to C++'s.
C_RUNTIME_HEADER = "dsdl_runtime.h"

# What ctest is told to read as a skip, matching the other lanes that cannot run everywhere.
SKIP_EXIT = 77


def run(command: list[str], cwd: Path | None = None, env: dict[str, str] | None = None):
    """Run @p command, returning the completed process without raising on a non-zero exit."""
    return subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True, timeout=3600)


def generate(dsdlc: Path, language: str, corpus: Path, outdir: Path, module: str) -> None:
    """Generate @p language for @p corpus into @p outdir, or exit reporting why it could not."""
    command = [str(dsdlc), "--target-language", language, str(corpus), "--outdir", str(outdir)]
    if language == "go":
        command += ["--go-module", module]
    elif language == "ts":
        command += ["--ts-module", module]
    elif language == "cpp":
        command += ["--cpp-profile", CPP_PROFILE]
    result = run(command)
    if result.returncode != 0:
        print(f"generating {language} failed:\n{result.stdout}\n{result.stderr}", file=sys.stderr)
        raise SystemExit(1)


ESLINT_CONFIG = """import tseslint from "typescript-eslint";

export default [
  ...tseslint.configs.recommended,
  {
    rules: {
      "no-useless-assignment": "error",
      "@typescript-eslint/naming-convention": [
        "warn",
        { selector: "typeLike", format: ["PascalCase"] },
        { selector: "function", format: ["camelCase"] },
        { selector: "variable", format: ["camelCase", "UPPER_CASE"], leadingUnderscore: "allow" },
      ],
    },
  },
];
"""


def prepare(language: str, outdir: Path, root: Path) -> dict[str, str]:
    """Make @p language's generated tree judgeable, and return the environment to judge it in.

    Every cache is placed under the output directory. A judge that wrote to the invoking user's
    home would make the lane's result depend on what a previous run left there.
    """
    env = dict(os.environ)
    if language == "go":
        env["GOCACHE"] = str(outdir / ".gocache")
        env["GOMODCACHE"] = str(outdir / ".gomodcache")
    elif language == "rust":
        env["CARGO_HOME"] = str(outdir / ".cargo")
        env["CARGO_TARGET_DIR"] = str(outdir / "target")
    elif language == "ts":
        npm = shutil.which("npm")
        if npm is None:
            print("npm is needed to find the TypeScript parser eslint reads", file=sys.stderr)
            raise SystemExit(1)
        global_root = run([npm, "root", "-g"]).stdout.strip()
        if not global_root:
            print("npm could not say where its global packages are", file=sys.stderr)
            raise SystemExit(1)
        (root / "eslint.config.js").write_text(ESLINT_CONFIG)
        # The generated package.json says nothing about module format, and the config is ESM.
        manifest = root / "package.json"
        content = json.loads(manifest.read_text()) if manifest.is_file() else {}
        content["type"] = "module"
        manifest.write_text(json.dumps(content, indent=2) + "\n")
        # eslint resolves `typescript-eslint` the way node would, so the global root is linked in
        # rather than the packages being copied.
        modules = root / "node_modules"
        if not modules.exists():
            modules.symlink_to(global_root)
    return env


def judge_go(judge: Path, root: Path, env: dict[str, str]) -> collections.Counter:
    """Count staticcheck's findings by check."""
    result = run([str(judge), f"-checks={STATICCHECK_CHECKS}", "-f", "json", "./..."], cwd=root, env=env)
    counts: collections.Counter = collections.Counter()
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        counts[json.loads(line)["code"]] += 1
    if not counts and result.returncode not in (0, 1):
        print(f"staticcheck did not run:\n{result.stderr}", file=sys.stderr)
        raise SystemExit(1)
    return counts


def judge_python(judge: Path, root: Path, env: dict[str, str]) -> collections.Counter:
    """Count ruff's findings by rule code."""
    result = run(
        [str(judge), "check", "--isolated", "--select", RUFF_SELECT, "--output-format", "json", "."],
        cwd=root,
        env=env,
    )
    if not result.stdout.strip():
        if result.returncode not in (0, 1):
            print(f"ruff did not run:\n{result.stderr}", file=sys.stderr)
            raise SystemExit(1)
        return collections.Counter()
    return collections.Counter(item["code"] for item in json.loads(result.stdout))


def judge_typescript(judge: Path, root: Path, env: dict[str, str]) -> collections.Counter:
    """Count eslint's findings by rule.

    A message with no rule is a parse or configuration failure rather than a finding, and is fatal:
    eslint reports a file it could not read the same way it reports one it read and liked.
    """
    result = run([str(judge), ".", "-f", "json"], cwd=root, env=env)
    if not result.stdout.strip():
        print(f"eslint did not run:\n{result.stderr}", file=sys.stderr)
        raise SystemExit(1)
    counts: collections.Counter = collections.Counter()
    unruled = []
    for entry in json.loads(result.stdout):
        for message in entry["messages"]:
            if message.get("ruleId"):
                counts[message["ruleId"]] += 1
            else:
                unruled.append(f"{entry['filePath']}: {message.get('message')}")
    if unruled:
        print("eslint reported messages carrying no rule, which is a parse or config failure:",
              file=sys.stderr)
        for item in unruled[:10]:
            print(f"  {item}", file=sys.stderr)
        raise SystemExit(1)
    return counts


def judge_rust(judge: Path, root: Path, env: dict[str, str]) -> collections.Counter:
    """Count clippy's findings by lint."""
    result = run(
        [str(judge), "--quiet", "--message-format=json"],
        cwd=root,
        env=env,
    )
    counts: collections.Counter = collections.Counter()
    for line in result.stdout.splitlines():
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        message = record.get("message") or {}
        code = (message.get("code") or {}).get("code")
        if code and message.get("level") in ("warning", "error"):
            counts[code] += 1
    if not counts and result.returncode not in (0, 101):
        print(f"clippy did not run:\n{result.stderr}", file=sys.stderr)
        raise SystemExit(1)
    return counts


def judge_clang_tidy(language: str, judge: Path, root: Path, env: dict[str, str]) -> collections.Counter:
    """Count clang-tidy's findings by check.

    Every header is a translation unit of its own as well as being included by others, because
    `misc-include-cleaner` reads only a unit's main file: a header judged only through its includers
    would never have its own includes checked. A header several units reach reports a finding once
    per unit, so a finding is counted once for its check and location.

    A `clang-diagnostic-error` is a unit that did not compile rather than a finding, and is fatal:
    clang-tidy reports it the same way it reports a check.
    """
    try:
        import yaml
    except ImportError:
        print("PyYAML is needed to read clang-tidy's exported findings", file=sys.stderr)
        raise SystemExit(1)

    # Absolute throughout: the header filter is matched against the path clang resolved an include
    # to, and a relative root would match nothing and judge only the main files.
    root = root.resolve()
    patterns = ("*.c", "*.h") if language == "c" else ("*.hpp", "*.h")
    units = sorted(
        path
        for pattern in patterns
        for path in root.rglob(pattern)
        if not (language == "cpp" and path.name == C_RUNTIME_HEADER)
    )
    if not units:
        print(f"no {language} sources under {root}", file=sys.stderr)
        raise SystemExit(1)

    exported = root / ".clang-tidy-findings"
    shutil.rmtree(exported, ignore_errors=True)
    exported.mkdir(parents=True)
    ruleset = CLANG_TIDY_RULESETS / f"{language}.clang-tidy"
    within = "^" + re.escape(str(root)) + "/"
    runtime = [f"--exclude-header-filter=/{re.escape(C_RUNTIME_HEADER)}$"] if language == "cpp" else []

    def judge_unit(index_unit: tuple[int, Path]) -> tuple[Path, list[dict], str]:
        index, unit = index_unit
        fixes = exported / f"{index}.yaml"
        result = run(
            [
                str(judge),
                str(unit),
                f"--config-file={ruleset}",
                f"--header-filter={within}",
                *runtime,
                f"--export-fixes={fixes}",
                "--quiet",
                "--",
                *CLANG_TIDY_FLAGS[language],
                f"-I{root}",
            ],
            env=env,
        )
        if not fixes.is_file():
            return unit, [], result.stderr if result.returncode != 0 else ""
        return unit, (yaml.safe_load(fixes.read_text()) or {}).get("Diagnostics") or [], ""

    findings: dict[tuple[str, str, int], str] = {}
    failed = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 1) as pool:
        for unit, diagnostics, error in pool.map(judge_unit, enumerate(units)):
            if error:
                failed.append(f"{unit}: {error.strip()[:500]}")
            for diagnostic in diagnostics:
                message = diagnostic["DiagnosticMessage"]
                if diagnostic["DiagnosticName"] == "clang-diagnostic-error":
                    failed.append(f"{message.get('FilePath')}: {message.get('Message')}")
                    continue
                path = message.get("FilePath", "")
                # The header filter admits a diagnostic any of whose notes is admitted, and an
                # analyzer path into the C runtime starts in a generated header.
                if language == "cpp" and Path(path).name == C_RUNTIME_HEADER:
                    continue
                key = (diagnostic["DiagnosticName"], path, message.get("FileOffset", 0))
                findings[key] = message.get("Message", "")
    if failed:
        print("clang-tidy could not compile what it was given:", file=sys.stderr)
        for item in failed[:10]:
            print(f"  {item}", file=sys.stderr)
        raise SystemExit(1)
    return collections.Counter(name for name, _, _ in findings)


def judge_c(judge: Path, root: Path, env: dict[str, str]) -> collections.Counter:
    """Count clang-tidy's findings over generated C."""
    return judge_clang_tidy("c", judge, root, env)


def judge_cpp(judge: Path, root: Path, env: dict[str, str]) -> collections.Counter:
    """Count clang-tidy's findings over generated C++."""
    return judge_clang_tidy("cpp", judge, root, env)


JUDGES = {
    "c": ("clang-tidy", judge_c),
    "cpp": ("clang-tidy", judge_cpp),
    "go": ("staticcheck", judge_go),
    "python": ("ruff", judge_python),
    "ts": ("eslint", judge_typescript),
    "rust": ("cargo-clippy", judge_rust),
}


def judge_version(judge: Path) -> str:
    """Return the judge's own version line, which is what a baseline is valid for."""
    result = run([str(judge), "--version"])
    line = (result.stdout or result.stderr).strip().splitlines()
    return line[0].strip() if line else "unknown"


def compare(counts: collections.Counter, baseline: dict[str, int]) -> list[str]:
    """Return a line per rule that reports more than its baseline, or is not in it at all."""
    regressions = []
    for rule in sorted(set(counts) | set(baseline)):
        now = counts.get(rule, 0)
        was = baseline.get(rule)
        if was is None:
            regressions.append(f"{rule}: {now} findings, and no baseline -- a rule not seen before")
        elif now > was:
            regressions.append(f"{rule}: {now} findings, baseline {was}")
    return regressions


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--language", required=True, choices=sorted(JUDGES))
    parser.add_argument("--dsdlc", type=Path, help="required unless --skip-generate")
    parser.add_argument("--corpus", type=Path, help="the DSDL root to generate from")
    parser.add_argument("--outdir", required=True, type=Path)
    parser.add_argument(
        "--judge",
        type=Path,
        help="the judge's path; found as tools/assert_style_judges.py finds it if omitted",
    )
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--module", default="judged_dsdl", help="module name for Go and TypeScript")
    parser.add_argument(
        "--update",
        action="store_true",
        help="rewrite the baseline from this run rather than comparing against it",
    )
    parser.add_argument(
        "--skip-generate",
        action="store_true",
        help="judge the tree already at --outdir, which was generated elsewhere;"
        " a baseline taken on one machine and judged in the container needs this",
    )
    arguments = parser.parse_args()
    if not arguments.skip_generate and arguments.dsdlc is None:
        parser.error("--dsdlc is required unless --skip-generate is given")

    judge_name, judge_counts = JUDGES[arguments.language]
    judge = arguments.judge
    if judge is None:
        found, looked = find_command(judge_name, go_tool=judge_name in GO_TOOLS)
        if found is None:
            print(f"{judge_name} not found; looked in {', '.join(looked)}", file=sys.stderr)
            return 1
        judge = Path(found)
    version = judge_version(judge)
    on_ci = os.environ.get("GITHUB_ACTIONS") == "true"

    recorded = None
    if not arguments.update:
        if not arguments.baseline.is_file():
            print(f"no baseline at {arguments.baseline}; take one with --update", file=sys.stderr)
            return 1
        recorded = json.loads(arguments.baseline.read_text())
        if recorded.get("judge") != version:
            print(
                f"this baseline was taken with {recorded.get('judge')!r} and the judge here is"
                f" {version!r}. A count is only comparable within one version of a judge, so this is"
                " not a verdict on the generated code.",
                file=sys.stderr,
            )
            # On CI the judge still runs, so the log holds the counts the baseline is retaken from.
            if not on_ci:
                print(
                    "Skipping. The toolshed container pins every judge -- CONTRIBUTING.md has the"
                    " recipe.",
                    file=sys.stderr,
                )
                return SKIP_EXIT

    if not arguments.skip_generate:
        generate(
            arguments.dsdlc, arguments.language, arguments.corpus, arguments.outdir, arguments.module
        )
    root = next((p for p in (arguments.outdir / "src", arguments.outdir) if p.is_dir()), arguments.outdir)
    counts = judge_counts(judge, root, prepare(arguments.language, arguments.outdir, root))

    total = sum(counts.values())
    for rule, count in sorted(counts.items(), key=lambda kv: (-kv[1], kv[0])):
        print(f"{count:>6}  {rule}")
    print(f"{total:>6}  total")

    print(f"        judge: {version}")

    if arguments.update:
        arguments.baseline.parent.mkdir(parents=True, exist_ok=True)
        arguments.baseline.write_text(
            json.dumps({"judge": version, "counts": dict(sorted(counts.items()))}, indent=2) + "\n"
        )
        print(f"baseline rewritten: {arguments.baseline}")
        return 0

    if recorded.get("judge") != version:
        print(
            "::error::the image's judge has moved; retake this baseline with --update and say"
            " in the commit what changed",
            file=sys.stderr,
        )
        return 1

    regressions = compare(counts, recorded["counts"])
    if not regressions:
        print(f"{arguments.language}: no rule reports more than its baseline")
        return 0
    print(f"the {arguments.language} judge reports more than the baseline allows:", file=sys.stderr)
    for line in regressions:
        print(f"  {line}", file=sys.stderr)
    print(
        "A count may fall and not rise. If a rise is intended, retake the baseline with --update"
        " and say in the commit what changed.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
