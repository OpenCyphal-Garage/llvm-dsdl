#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Prepare a release: bump the version, write the changelog entry, stage the result.

Automates CONTRIBUTING.md section 14.2. Three things carry the version -- ``VERSION``, the top entry
of ``packaging/deb/changelog``, and the git tag -- and each is checked at a different point. CMake
checks ``VERSION`` against the changelog at configure time; ``release.yml`` checks the tag after it
has been pushed.

The tag is the expensive one: it is compiled into the binaries, and colliding with an existing
release costs an unwind. This script asks GitHub for the tags that already exist and requires the
new version to be strictly ahead of all of them.

A tracked change in the working tree, staged or unstaged, stops the run before anything is written.
``git commit`` commits the whole index, so an unrelated staged change would land in a commit named
after the release. Untracked files are reported and permitted; nothing untracked reaches the commit.

The changelog entry is a draft assembled from commit subjects. The entries in that file are curated
prose, so edit the draft before merging.

Every change it makes is local: it writes two files and, at most, stages and commits them. Reaching
GitHub is limited to reading the tag list. Tagging and publishing are CONTRIBUTING.md sections 14.3
and 14.4.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION_FILE = ROOT / "VERSION"
CHANGELOG_FILE = ROOT / "packaging" / "deb" / "changelog"
IDENTITY_FILE = ROOT / "project-identity.json"

# Only `vX.Y.Z` names a release. The `toolchain-llvmorg-*` tags track the LLVM revision in
# packaging/toolchain/llvm.pin and carry their own numbering.
RELEASE_TAG = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")
VERSION_TEXT = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")

# `llvm-dsdl (0.2.0) unstable; urgency=medium`
CHANGELOG_HEADING = re.compile(r"^(?P<package>[a-z0-9][a-z0-9+.-]*) \((?P<version>[^)]+)\)")
# ` -- Scott Dixon <scott@example.com>  Tue, 05 Aug 2026 12:00:00 +0000`
CHANGELOG_TRAILER = re.compile(r"^ -- (?P<name>.+?) <(?P<email>[^>]+)>  ")

# Debian requires English abbreviations in the trailer date whatever the machine's locale, which
# strftime's %a and %b do not guarantee.
DAYS = ("Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun")
MONTHS = ("Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec")


class Failure(Exception):
    """An error the user can fix."""


def run(cmd: list[str], *, capture: bool = False, check: bool = True) -> str:
    """Run a subcommand, echoing its command line and its output.

    The output is echoed even when it is also parsed.
    """
    print(f"+ {shlex.join(cmd)}", flush=True)
    if capture:
        proc = subprocess.run(cmd, text=True, capture_output=True)
        if proc.stdout:
            print(proc.stdout, end="" if proc.stdout.endswith("\n") else "\n", flush=True)
        if proc.stderr:
            print(proc.stderr, end="" if proc.stderr.endswith("\n") else "\n", file=sys.stderr, flush=True)
    else:
        proc = subprocess.run(cmd, text=True)
    if check and proc.returncode != 0:
        raise Failure(f"`{shlex.join(cmd)}` exited {proc.returncode}")
    return proc.stdout or ""


def read_version() -> tuple[int, int, int]:
    if not VERSION_FILE.is_file():
        raise Failure(f"missing {VERSION_FILE.relative_to(ROOT)}")
    raw = VERSION_FILE.read_text(encoding="utf-8").strip()
    match = VERSION_TEXT.match(raw)
    if not match:
        raise Failure(f"{VERSION_FILE.relative_to(ROOT)} is {raw!r}, which is not X.Y.Z")
    return tuple(int(part) for part in match.groups())  # type: ignore[return-value]


def bump(current: tuple[int, int, int], part: str) -> tuple[int, int, int]:
    major, minor, patch = current
    if part == "major":
        return (major + 1, 0, 0)
    if part == "minor":
        return (major, minor + 1, 0)
    if part == "patch":
        return (major, minor, patch + 1)
    return current  # "current"


def fmt(version: tuple[int, int, int]) -> str:
    return "{}.{}.{}".format(*version)


def identity() -> tuple[str, str]:
    """Owner and repository, from project-identity.json."""
    try:
        data = json.loads(IDENTITY_FILE.read_text(encoding="utf-8"))
        return data["owner"], data["repo"]
    except OSError as exc:
        raise Failure(f"missing {IDENTITY_FILE.relative_to(ROOT)}") from exc
    except (json.JSONDecodeError, KeyError) as exc:
        raise Failure(
            f"invalid {IDENTITY_FILE.relative_to(ROOT)}: expected JSON with 'owner' and 'repo'"
        ) from exc


def upstream_tags() -> list[tuple[int, int, int]]:
    owner, repo = identity()
    # The repository is named in the API path: a working tree with both an `origin` fork and an
    # `upstream` remote gives bare `gh` two candidates, and it resolves to the fork.
    out = run(
        ["gh", "api", "--paginate", f"repos/{owner}/{repo}/tags", "--jq", ".[].name"],
        capture=True,
    )
    versions = []
    for line in out.splitlines():
        match = RELEASE_TAG.match(line.strip())
        if match:
            versions.append(tuple(int(part) for part in match.groups()))
    return versions


def check_upstream(target: tuple[int, int, int]) -> None:
    tags = upstream_tags()
    if not tags:
        print("no vX.Y.Z tags upstream yet; nothing to collide with")
        return
    blocking = sorted(tag for tag in tags if tag >= target)
    if blocking:
        newest = fmt(max(tags))
        raise Failure(
            "upstream already has "
            + ", ".join("v" + fmt(tag) for tag in blocking)
            + f", so v{fmt(target)} cannot be cut.\n"
            f"       The newest release upstream is v{newest}; a release must be strictly ahead of it."
        )
    print(f"upstream newest is v{fmt(max(tags))}; v{fmt(target)} is ahead of every existing tag")


def working_tree_state() -> tuple[list[str], list[str]]:
    """Tracked changes and untracked paths, from one `git status`."""
    out = run(["git", "status", "--porcelain"], capture=True)
    tracked, untracked = [], []
    for line in out.splitlines():
        if line.strip():
            (untracked if line.startswith("??") else tracked).append(line[3:])
    return tracked, untracked


def require_clean(*, dry_run: bool) -> None:
    tracked, untracked = working_tree_state()
    if untracked:
        # Flushed so it precedes the failure below: stdout is block-buffered down a pipe, stderr is
        # not, and the two interleave out of order.
        print(
            f"{len(untracked)} untracked file(s) present; permitted, since nothing untracked enters a release commit",
            flush=True,
        )
    if not tracked:
        print("working tree is clean")
        return
    listed = "\n".join(f"         {path}" for path in tracked[:10])
    if len(tracked) > 10:
        listed += f"\n         ... and {len(tracked) - 10} more"
    if dry_run:
        print(
            f"WARNING: {len(tracked)} tracked file(s) have uncommitted changes:\n{listed}\n"
            "         A real run would abort here without modifying anything.\n"
            "         Continuing, because --dry-run writes nothing.",
            file=sys.stderr,
        )
        return
    raise Failure(
        f"the working tree has {len(tracked)} uncommitted change(s), so nothing was modified:\n{listed}\n"
        "       Commit or stash them first, or pass --no-git to prepare the version without\n"
        "       touching git at all."
    )


def changelog_top_version(text: str) -> str | None:
    for line in text.splitlines():
        if line.strip():
            match = CHANGELOG_HEADING.match(line)
            return match.group("version") if match else None
    return None


def changelog_identity(text: str) -> tuple[str, str] | None:
    for line in text.splitlines():
        match = CHANGELOG_TRAILER.match(line)
        if match:
            return match.group("name"), match.group("email")
    return None


def author(changelog: str, *, allow_git: bool) -> tuple[str, str]:
    """Who signs the changelog trailer.

    DEBFULLNAME and DEBEMAIL first, matching ``dch``; then the git identity; then the signature on
    the previous entry, which keeps ``--no-git`` usable.
    """
    name, email = os.environ.get("DEBFULLNAME"), os.environ.get("DEBEMAIL")
    if name and email:
        return name, email
    if allow_git:
        name = run(["git", "config", "user.name"], capture=True, check=False).strip() or None
        email = run(["git", "config", "user.email"], capture=True, check=False).strip() or None
        if name and email:
            return name, email
    previous = changelog_identity(changelog)
    if previous:
        print(f"no DEBFULLNAME/DEBEMAIL and no git identity; reusing {previous[0]} <{previous[1]}> from the previous entry")
        return previous
    raise Failure("cannot determine the changelog author: set DEBFULLNAME and DEBEMAIL")


def bullets(*, allow_git: bool) -> list[str]:
    """Draft bullets from the commit subjects since the newest local release tag."""
    if not allow_git:
        return ["TODO: describe this release."]
    tags = run(["git", "tag", "--list", "v*"], capture=True, check=False).split()
    known = sorted((RELEASE_TAG.match(t).groups() for t in tags if RELEASE_TAG.match(t)), key=lambda g: tuple(map(int, g)))
    span = f"v{'.'.join(known[-1])}..HEAD" if known else "HEAD"
    log = run(["git", "log", "--no-merges", "--format=%s", span], capture=True, check=False)
    subjects = [line.strip() for line in log.splitlines() if line.strip()]
    seen, unique = set(), []
    for subject in subjects:
        if subject not in seen:
            seen.add(subject)
            unique.append(subject)
    return unique or ["TODO: describe this release."]


def entry(version: str, items: list[str], name: str, email: str) -> str:
    now = datetime.now(timezone.utc).astimezone()
    stamp = (
        f"{DAYS[now.weekday()]}, {now.day:02d} {MONTHS[now.month - 1]} {now.year} "
        f"{now:%H:%M:%S} {now:%z}"
    )
    lines = [f"llvm-dsdl ({version}) unstable; urgency=medium", ""]
    for item in items:
        wrapped = item if len(item) <= 72 else item[:69] + "..."
        lines.append(f"  * {wrapped}")
    lines += ["", f" -- {name} <{email}>  {stamp}", ""]
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    try:
        current_text = VERSION_FILE.read_text(encoding="utf-8").strip()
    except OSError:
        current_text = "unknown"

    parser = argparse.ArgumentParser(
        prog="prepare_release.py",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "bump",
        choices=("major", "minor", "patch", "current"),
        help="which part of the version to increment, or 'current' to use VERSION unchanged",
    )
    parser.add_argument(
        "--version",
        action="version",
        version=current_text,
        help="print the project version from VERSION (not this script's version) and exit",
    )
    parser.add_argument("--dry-run", action="store_true", help="print what would be done, change nothing")
    parser.add_argument("--offline", action="store_true", help="skip the upstream tag check; make local changes only")
    parser.add_argument("--commit", action="store_true", help="also create a git commit (default: stage only)")
    parser.add_argument(
        "--no-stage",
        action="store_true",
        help="do not stage and do not commit; the clean-tree check still applies",
    )
    parser.add_argument(
        "--no-git",
        action="store_true",
        help="issue no git commands at all, which also disables the clean-tree check",
    )
    args = parser.parse_args(argv)

    if args.commit and (args.no_stage or args.no_git):
        parser.error("--commit cannot be combined with --no-stage or --no-git")

    # --no-git is the stronger of the two: no git at all implies nothing staged. They stay separate
    # values so the reason a step was skipped can be reported.
    allow_git = not args.no_git
    stage = allow_git and not args.no_stage

    try:
        current = read_version()
        target = bump(current, args.bump)
        print(f"VERSION is {fmt(current)}; {args.bump} -> {fmt(target)}")

        # Runs first: it decides whether writing is allowed.
        if allow_git:
            require_clean(dry_run=args.dry_run)
        else:
            print("--no-git: skipping the clean-tree check")

        if args.offline:
            print("--offline: skipping the upstream tag check")
        else:
            check_upstream(target)

        changelog = CHANGELOG_FILE.read_text(encoding="utf-8")
        top = changelog_top_version(changelog)
        needs_entry = top != fmt(target)
        if not needs_entry:
            print(f"packaging/deb/changelog already has a {fmt(target)} entry on top; leaving it alone")

        new_entry = ""
        if needs_entry:
            name, email = author(changelog, allow_git=allow_git)
            new_entry = entry(fmt(target), bullets(allow_git=allow_git), name, email)
            print("\n--- packaging/deb/changelog (new top entry) ---")
            print(new_entry, end="")
            print("--- end ---\n")

        touched: list[str] = []
        if fmt(current) != fmt(target):
            touched.append(str(VERSION_FILE.relative_to(ROOT)))
        if needs_entry:
            touched.append(str(CHANGELOG_FILE.relative_to(ROOT)))

        if not touched:
            print("nothing to change: VERSION and the changelog already say " + fmt(target))
        elif args.dry_run:
            print("--dry-run: would write " + ", ".join(touched))
        else:
            if fmt(current) != fmt(target):
                VERSION_FILE.write_text(fmt(target) + "\n", encoding="utf-8")
                print(f"wrote {VERSION_FILE.relative_to(ROOT)}: {fmt(target)}")
            if needs_entry:
                CHANGELOG_FILE.write_text(new_entry + "\n" + changelog.lstrip("\n"), encoding="utf-8")
                print(f"wrote {CHANGELOG_FILE.relative_to(ROOT)}")

        if not stage:
            print(("--no-git" if args.no_git else "--no-stage") + ": leaving the working tree unstaged")
        elif not touched:
            pass
        elif args.dry_run:
            print("--dry-run: would run `git add " + " ".join(touched) + "`")
            if args.commit:
                print(f"--dry-run: would run `git commit -m 'Prepare release {fmt(target)}'`")
        else:
            run(["git", "add", *touched])
            if args.commit:
                run(["git", "commit", "-m", f"Prepare release {fmt(target)}"])

        print(
            f"\n{fmt(target)} is prepared in this working tree. Edit the changelog draft into prose,"
            "\nthen follow CONTRIBUTING.md section 14 for the dry run, the tag, and the release."
        )
    except Failure as err:
        print(f"error: {err}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
