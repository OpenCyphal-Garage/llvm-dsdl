#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Generate the public regulated data types with dsdlc and nnvg, side by side.

Every root namespace of the corpus (``uavcan`` and ``reg``) is generated in every language each
generator produces, into one tree separated by generator::

    <outdir>/dsdlc/<language>/
    <outdir>/nnvg/<language>/

dsdlc is ``--dsdlc``, or is built from a configure preset. nnvg is installed into a virtual
environment at the Nunavut release CI's differential-parity lane pins.

    python3 tools/generate_prdt_corpus.py
    python3 tools/generate_prdt_corpus.py --dsdlc build/matrix/dev-homebrew/tools/dsdlc/RelWithDebInfo/dsdlc
    cmake --build --preset build-dev-homebrew --target prdt-corpus

nnvg runs with ``--jobs 1``: Nunavut 3.0.1b1's worker pool cannot pickle its state under Python 3.14
on macOS. ``cpp`` and ``py`` are experimental in that release and are enabled explicitly.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import shlex
import shutil
import subprocess
import sys

#: The tag the differential-parity lane checks out (NUNAVUT_SHA in .github/workflows/ci.yml).
NUNAVUT_VERSION = "3.0.1b1"

DSDLC_LANGUAGES = ("c", "cpp", "rust", "go", "ts", "python", "obj")
NNVG_LANGUAGES = ("c", "cpp", "py")

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
CORPUS = REPO_ROOT / "submodules" / "public_regulated_data_types"


def default_preset() -> str:
    return "dev-homebrew" if sys.platform == "darwin" else "dev-llvm-env"


def _configure_chain(presets: dict, name: str) -> list[dict]:
    """The configure preset and its ancestors, nearest first."""
    by_name = {preset["name"]: preset for preset in presets.get("configurePresets", [])}
    chain: list[dict] = []
    todo = [name]
    while todo:
        preset = by_name.get(todo.pop(0))
        if preset is None:
            raise KeyError(f"no configure preset named {name!r}")
        chain.append(preset)
        inherits = preset.get("inherits", [])
        todo.extend([inherits] if isinstance(inherits, str) else inherits)
    return chain


def preset_binary_dir(presets: dict, configure_preset: str, source_dir: pathlib.Path) -> pathlib.Path:
    """The build tree a configure preset writes, with ``${sourceDir}`` and ``${presetName}`` expanded."""
    for preset in _configure_chain(presets, configure_preset):
        if "binaryDir" in preset:
            text = preset["binaryDir"]
            text = text.replace("${sourceDir}", str(source_dir)).replace("${presetName}", configure_preset)
            return pathlib.Path(text)
    raise KeyError(f"configure preset {configure_preset!r} declares no binaryDir")


def preset_build_config(presets: dict, configure_preset: str) -> str:
    """The configuration ``cmake --build --preset build-<name>`` produces."""
    for build in presets.get("buildPresets", []):
        if build["name"] == f"build-{configure_preset}" and "configuration" in build:
            return build["configuration"]
    for preset in _configure_chain(presets, configure_preset):
        default = preset.get("cacheVariables", {}).get("CMAKE_DEFAULT_BUILD_TYPE")
        if default:
            return default
    raise KeyError(f"cannot tell which configuration build-{configure_preset} produces")


def root_namespaces(corpus: pathlib.Path) -> list[pathlib.Path]:
    """Immediate subdirectories of the corpus that hold DSDL definitions, sorted by name."""
    if not corpus.is_dir():
        return []
    return sorted(child for child in corpus.iterdir() if child.is_dir() and any(child.rglob("*.dsdl")))


def dsdlc_command(
    dsdlc: pathlib.Path, language: str, roots: list[pathlib.Path], outdir: pathlib.Path
) -> list[str]:
    """One invocation over every root; dependencies resolve against the corpus alone, so the embedded
    uavcan catalogue cannot stand in for a definition the checkout carries."""
    command = [str(dsdlc), "--target-language", language, "--no-embedded-uavcan"]
    for root in roots:
        command += ["--lookup-dir", str(root)]
    command += [str(root) for root in roots]
    return command + ["--outdir", str(outdir)]


def nnvg_command(nnvg: pathlib.Path, language: str, roots: list[pathlib.Path], outdir: pathlib.Path) -> list[str]:
    command = [str(nnvg), "--target-language", language, "--include-experimental-languages", "--jobs", "1"]
    for root in roots:
        command += ["--lookup-dir", str(root)]
    command += [str(root) for root in roots]
    return command + ["--outdir", str(outdir)]


def run(command: list[str], cwd: pathlib.Path | None = None) -> None:
    print("+", shlex.join(command), flush=True)
    try:
        subprocess.run(command, cwd=cwd, check=True)
    except subprocess.CalledProcessError as error:
        raise SystemExit(f"exit status {error.returncode}: {shlex.join(command)}") from None
    except FileNotFoundError:
        raise SystemExit(f"not found: {command[0]}") from None


def ensure_corpus(corpus: pathlib.Path) -> list[pathlib.Path]:
    """The corpus's root namespaces, initialising the submodule first when the checkout is empty."""
    roots = root_namespaces(corpus)
    if roots:
        return roots
    try:
        relative = corpus.relative_to(REPO_ROOT)
    except ValueError:
        raise SystemExit(f"no DSDL root namespaces under {corpus}") from None
    run(["git", "submodule", "update", "--init", "--", str(relative)], cwd=REPO_ROOT)
    roots = root_namespaces(corpus)
    if not roots:
        raise SystemExit(f"no DSDL root namespaces under {corpus} after initialising the submodule")
    return roots


def build_dsdlc(preset: str) -> pathlib.Path:
    presets = json.loads((REPO_ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
    run(["cmake", "--preset", preset], cwd=REPO_ROOT)
    run(["cmake", "--build", "--preset", f"build-{preset}", "--target", "dsdlc"], cwd=REPO_ROOT)
    binary_dir = preset_binary_dir(presets, preset, REPO_ROOT)
    config = preset_build_config(presets, preset)
    dsdlc = binary_dir / "tools" / "dsdlc" / config / ("dsdlc.exe" if sys.platform == "win32" else "dsdlc")
    if not dsdlc.is_file():
        raise SystemExit(f"built dsdlc, but it is not at {dsdlc}")
    return dsdlc


def ensure_nnvg(venv: pathlib.Path, version: str) -> pathlib.Path:
    """Install Nunavut at ``version`` into ``venv``, creating it on first use, and return its nnvg."""
    scripts = venv / ("Scripts" if sys.platform == "win32" else "bin")
    python = scripts / ("python.exe" if sys.platform == "win32" else "python")
    if not python.exists():
        run([sys.executable, "-m", "venv", str(venv)])
    run([str(python), "-m", "pip", "install", "--quiet", "--disable-pip-version-check", f"nunavut=={version}"])
    nnvg = scripts / ("nnvg.exe" if sys.platform == "win32" else "nnvg")
    if not nnvg.exists():
        raise SystemExit(f"nunavut=={version} is installed in {venv}, but nnvg is not at {nnvg}")
    return nnvg


def count_files(directory: pathlib.Path) -> int:
    return sum(1 for path in directory.rglob("*") if path.is_file())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dsdlc", type=pathlib.Path, help="dsdlc to use instead of building one")
    parser.add_argument(
        "--preset",
        default=default_preset(),
        help="configure preset dsdlc is built from when --dsdlc is not given (default: %(default)s)",
    )
    parser.add_argument(
        "--corpus", type=pathlib.Path, default=CORPUS, help="public_regulated_data_types checkout (default: %(default)s)"
    )
    parser.add_argument(
        "--outdir",
        type=pathlib.Path,
        default=REPO_ROOT / "build" / "prdt-corpus",
        help="output tree; its dsdlc/ and nnvg/ subtrees are replaced (default: %(default)s)",
    )
    parser.add_argument(
        "--venv",
        type=pathlib.Path,
        default=REPO_ROOT / "build" / "nnvg-venv",
        help="virtual environment nnvg is installed into (default: %(default)s)",
    )
    parser.add_argument(
        "--nunavut-version", default=NUNAVUT_VERSION, help="Nunavut release to install (default: %(default)s)"
    )
    parser.add_argument(
        "--dsdlc-languages",
        nargs="*",
        default=list(DSDLC_LANGUAGES),
        metavar="LANG",
        help=f"dsdlc target languages; none skips dsdlc (default: {' '.join(DSDLC_LANGUAGES)})",
    )
    parser.add_argument(
        "--nnvg-languages",
        nargs="*",
        default=list(NNVG_LANGUAGES),
        metavar="LANG",
        help=f"nnvg target languages; none skips nnvg (default: {' '.join(NNVG_LANGUAGES)})",
    )
    args = parser.parse_args()

    roots = ensure_corpus(args.corpus.resolve())

    dsdlc: pathlib.Path | None = None
    if args.dsdlc_languages:
        if args.dsdlc is None:
            dsdlc = build_dsdlc(args.preset)
        else:
            dsdlc = args.dsdlc.resolve()
            if not dsdlc.is_file():
                raise SystemExit(f"not a file: {dsdlc}")
    nnvg = ensure_nnvg(args.venv.resolve(), args.nunavut_version) if args.nnvg_languages else None

    outdir = args.outdir.resolve()
    summary: list[tuple[str, str, int]] = []
    plan = (("dsdlc", dsdlc, args.dsdlc_languages, dsdlc_command), ("nnvg", nnvg, args.nnvg_languages, nnvg_command))
    for generator, tool, languages, make_command in plan:
        tree = outdir / generator
        shutil.rmtree(tree, ignore_errors=True)
        for language in languages:
            assert tool is not None
            target = tree / language
            target.mkdir(parents=True)
            run(make_command(tool, language, roots, target))
            summary.append((generator, language, count_files(target)))

    print()
    print("roots:", ", ".join(root.name for root in roots))
    for generator, language, files in summary:
        print(f"{generator:<6} {language:<8} {files:>5} files  {outdir / generator / language}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
