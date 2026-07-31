#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Keep every mention of where this project lives true to ``project-identity.json``.

The repository moves. It began in a personal fork, lives in OpenCyphal-Garage today, and is promoted
to OpenCyphal at v1.0. Before this script there were 49 places that had to be edited by hand for that
to be true, and the tree had already drifted: the Debian packaging, the manpage, and CPack said
OpenCyphal-Garage while the documentation, the README, and mkdocs said something else.

The approach is normalisation rather than templating. The URLs stay written out in full, because
``docs/*.md`` is read directly on GitHub and its raw text is what ``llms-full.txt`` ships to agents;
a ``{repo}`` placeholder would be visible, and wrong, in both. So the literals remain -- they just
stop being *maintained*. This script rewrites them from one file, and running it in CI means a stale
one cannot survive a pull request.

    python3 tools/repo_identity.py            # report anything that disagrees (exit 1 if so)
    python3 tools/repo_identity.py --fix      # rewrite it

Three things are reported:

1. **Owner.** Any ``github.com/<someone>/<repo>`` or ``<someone>.github.io/<repo>`` is rewritten to
   the owner and documentation URL declared in ``project-identity.json``. Anchored on *this*
   repository's name, so references to other projects -- OpenCyphal/nunavut, OpenCyphal-Garage/
   libudpard, pavel-kirienko/o1heap -- are left alone.

2. **No home directories.** A committed ``/Users/somebody/...`` or ``/home/somebody/...`` is wrong
   for every reader but the person who wrote it. These are reported, never rewritten: the right
   replacement is a relative path or ``$PWD``, and only a human knows which.

3. **No unrecognised hosts.** A URL naming this repository somewhere the rules do not reach -- a
   badge service nobody has taught this script -- is reported, because otherwise it would sit there
   looking maintained while being the one thing a move leaves behind. That is how the shields.io
   badge in the README was caught: rule 1 fixed the link around it and left the image URL stale.

Deliberately not covered: ``runtime/go/go.mod``. A Go module path is an identity rather than a
location, and that one is ``opencyphal.org/...`` precisely so that it encodes no GitHub owner and
survives these moves untouched.

Known limit: the owner rule is anchored on the repository *name*, so renaming the repository itself
still needs a one-time sweep. Update ``repo`` in the identity file, sweep once, and this script
keeps it true from then on.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

IDENTITY_FILE = "project-identity.json"

# Tracked paths that are someone else's to define, or that state the identity rather than repeat it.
SKIP_PREFIXES = ("submodules/",)
SKIP_FILES = (IDENTITY_FILE,)

# A committed path into somebody's home directory. `[a-z]` and not `[^/]` so that placeholders a
# reader is meant to substitute -- /Users/<you>/, /home/$USER/ -- are not reported as defects.
HOME_PATH = re.compile(r"/(?:Users|home)/[a-z][A-Za-z0-9._-]*/")


@dataclass(frozen=True)
class Identity:
    owner: str
    repo: str
    docs_url: str

    @property
    def repo_url(self) -> str:
        return f"https://github.com/{self.owner}/{self.repo}"


def load_identity(root: Path) -> Identity:
    data = json.loads((root / IDENTITY_FILE).read_text(encoding="utf-8"))
    missing = [key for key in ("owner", "repo", "docs_url") if not data.get(key)]
    if missing:
        raise SystemExit(f"error: {IDENTITY_FILE} is missing: {', '.join(missing)}")
    docs_url = data["docs_url"]
    return Identity(data["owner"], data["repo"], docs_url if docs_url.endswith("/") else docs_url + "/")


def rules(identity: Identity) -> list[tuple[re.Pattern[str], str]]:
    """(pattern, replacement) pairs, anchored on this repository's name."""
    repo = re.escape(identity.repo)
    return [
        # https://github.com/<someone>/<repo>  ->  the declared owner
        (
            re.compile(rf"(?P<scheme>https?://github\.com/)[A-Za-z0-9._-]+/(?P<repo>{repo})\b"),
            rf"\g<scheme>{identity.owner}/\g<repo>",
        ),
        # https://<someone>.github.io/<repo>/  ->  the declared documentation URL
        (
            re.compile(rf"https?://[A-Za-z0-9._-]+\.github\.io/{repo}/?"),
            identity.docs_url,
        ),
        # https://img.shields.io/github/v/release/<someone>/<repo>  ->  the declared owner.
        # Badge services carry the owner in a path segment of their own, which the github.com rule
        # above does not see. The prefix is greedy so that the owner matched is the segment
        # immediately before the repository name, whatever the metric path in front of it.
        (
            re.compile(
                rf"(?P<prefix>https?://img\.shields\.io/[A-Za-z0-9/._-]+/)"
                rf"[A-Za-z0-9._-]+/(?P<repo>{repo})\b"
            ),
            rf"\g<prefix>{identity.owner}/\g<repo>",
        ),
    ]


# Hosts the rules above know how to rewrite. A URL naming this repository on any other host is
# reported rather than fixed: it may well encode the owner in a shape nobody has taught this script.
KNOWN_HOSTS = re.compile(r"^https?://(?:github\.com|img\.shields\.io|[A-Za-z0-9._-]+\.github\.io)/")

# Loopback is exempt. A local preview serves the site under its base path, so `127.0.0.1:8000/
# llvm-dsdl/` names a URL prefix rather than an owner, and there is nothing there to keep current.
LOOPBACK_HOSTS = re.compile(r"^https?://(?:localhost|127\.0\.0\.1|0\.0\.0\.0|\[::1\])(?::\d+)?/")

URL = re.compile(r"https?://[^\s)\]\"'<>]+")


def unrecognised_hosts(text: str, identity: Identity) -> list[str]:
    """URLs naming this repository that no rule covers, so nobody can promise they are current."""
    found = []
    for match in URL.finditer(text):
        url = match.group(0)
        if f"/{identity.repo}" not in url or KNOWN_HOSTS.match(url) or LOOPBACK_HOSTS.match(url):
            continue
        found.append(url)
    return found


def tracked_files(root: Path) -> list[Path]:
    listing = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z"], capture_output=True, text=True, check=True
    ).stdout
    files = []
    for name in listing.split("\0"):
        if not name or name.startswith(SKIP_PREFIXES) or name in SKIP_FILES:
            continue
        files.append(root / name)
    return files


def scan(root: Path, identity: Identity, fix: bool) -> tuple[list[str], list[str], list[str]]:
    """Returns (owner, home-directory, unrecognised-host) findings. Rewrites in place when `fix`."""
    owner_findings: list[str] = []
    home_findings: list[str] = []
    host_findings: list[str] = []
    substitutions = rules(identity)

    for path in tracked_files(root):
        try:
            original = path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue  # binary, or unreadable: nothing to normalise

        updated = original
        for pattern, replacement in substitutions:
            updated = pattern.sub(replacement, updated)

        rel = path.relative_to(root).as_posix()

        if updated != original:
            for number, (before, after) in enumerate(
                zip(original.splitlines(), updated.splitlines()), start=1
            ):
                if before != after:
                    owner_findings.append(f"{rel}:{number}: {before.strip()}")
            if fix:
                path.write_text(updated, encoding="utf-8")

        for number, line in enumerate(original.splitlines(), start=1):
            if HOME_PATH.search(line):
                home_findings.append(f"{rel}:{number}: {line.strip()}")
            for url in unrecognised_hosts(line, identity):
                host_findings.append(f"{rel}:{number}: {url}")

    return owner_findings, home_findings, host_findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fix", action="store_true", help="rewrite instead of reporting")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()

    identity = load_identity(args.root)
    owner_findings, home_findings, host_findings = scan(args.root, identity, args.fix)

    if owner_findings:
        verb = "rewrote" if args.fix else "found"
        stream = sys.stdout if args.fix else sys.stderr
        print(
            f"{'' if args.fix else 'error: '}{verb} {len(owner_findings)} line(s) naming an owner "
            f"other than `{identity.owner}`:",
            file=stream,
        )
        for finding in owner_findings[:30]:
            print(f"  {finding}", file=stream)
        if len(owner_findings) > 30:
            print(f"  ... and {len(owner_findings) - 30} more", file=stream)
        if not args.fix:
            print(
                f"       run `python3 tools/repo_identity.py --fix`, or correct {IDENTITY_FILE} if "
                f"the project moved",
                file=stream,
            )

    if home_findings:
        print(
            f"error: {len(home_findings)} line(s) commit a path into somebody's home directory:",
            file=sys.stderr,
        )
        for finding in home_findings:
            print(f"  {finding}", file=sys.stderr)
        print(
            "       use a repository-relative path or $PWD; these are not rewritten for you",
            file=sys.stderr,
        )

    if host_findings:
        print(
            f"error: {len(host_findings)} URL(s) name this repository on a host no rule covers, so "
            f"nothing keeps them current:",
            file=sys.stderr,
        )
        for finding in host_findings:
            print(f"  {finding}", file=sys.stderr)
        print(
            "       add a rewrite rule for that host in tools/repo_identity.py, or use a URL that "
            "does not carry the owner",
            file=sys.stderr,
        )

    if not owner_findings and not home_findings and not host_findings:
        print(
            f"repo identity: every mention agrees with {IDENTITY_FILE} "
            f"({identity.owner}/{identity.repo})"
        )
        return 0
    return 1 if (home_findings or host_findings or not args.fix) else 0


if __name__ == "__main__":
    raise SystemExit(main())
