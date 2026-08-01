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

2. **No home directories.** A committed ``/Users/<somebody>/...`` or ``/home/<somebody>/...`` is
   wrong for every reader but the person who wrote it. These are reported, never rewritten: the
   right replacement is a relative path or ``$PWD``, and only a human knows which. Written with the
   name in angle brackets here because this paragraph is scanned like any other line -- the rule
   exempts the placeholder form, which is what prose describing a path should use anyway.

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

SOURCE = Path(__file__).resolve()


def rule_location(marker: str) -> str:
    """Where `marker` is defined in this file, as ``tools/repo_identity.py:<line>``.

    Every finding this script reports is the work of one rule below, and the reader's next move is
    usually to go and look at it -- to widen it, to teach it a new host, or to decide the rule is
    wrong. Naming the rule without saying where it lives leaves them grepping. The line is found by
    reading this file rather than written down, because a written-down line number is wrong the first
    time anything above it moves, and wrong in a way nobody notices.
    """
    try:
        for number, line in enumerate(SOURCE.read_text(encoding="utf-8").splitlines(), start=1):
            if line.startswith(marker):
                return f"{SOURCE.name}:{number}"
    except OSError:
        pass
    return SOURCE.name  # the file is still the right place to look

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
    """Reads the identity file, reporting where and what rather than raising into pathlib or json.

    A stack trace ending in ``io.open`` names a Python internal and not the file the reader has to
    edit, which is the one thing they need. Each failure here says which path was read and what to
    put in it.
    """
    path = root / IDENTITY_FILE
    try:
        text = path.read_text(encoding="utf-8")
    except FileNotFoundError:
        raise SystemExit(
            f"error: {path}: no such file\n"
            f"       this script reads the owner, repository and documentation URL from it; create "
            f"it, or pass --root pointing at the checkout that has one"
        ) from None
    except OSError as err:
        raise SystemExit(f"error: {path}: cannot be read: {err.strerror}") from None

    try:
        data = json.loads(text)
    except json.JSONDecodeError as err:
        # json already knows the line and column; quoting the line itself saves opening the file.
        lines = text.splitlines()
        offending = lines[err.lineno - 1].strip() if 0 < err.lineno <= len(lines) else ""
        raise SystemExit(
            f"error: {path}:{err.lineno}:{err.colno}: not valid JSON: {err.msg}\n"
            f"       {offending}"
        ) from None

    if not isinstance(data, dict):
        raise SystemExit(
            f"error: {path}:1: expected a JSON object with owner/repo/docs_url, found "
            f"{type(data).__name__}"
        )

    missing = [key for key in ("owner", "repo", "docs_url") if not data.get(key)]
    if missing:
        raise SystemExit(
            f"error: {path}: missing or empty: {', '.join(missing)}\n"
            f"       every key is required: owner (the GitHub account), repo (the repository name), "
            f"docs_url (where the documentation is published)"
        )

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
    """The tracked files under `root`, or an error naming the directory git refused.

    `check=True` here used to surface as a CalledProcessError traceback whose useful content -- git's
    own stderr -- was captured and thrown away, so the reader got an exit status and no reason.
    """
    listing = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z"], capture_output=True, text=True, check=False
    )
    if listing.returncode != 0:
        detail = listing.stderr.strip() or f"git exited {listing.returncode}"
        raise SystemExit(
            f"error: {root}: cannot list tracked files: {detail}\n"
            f"       this script scans what git tracks, so it has to run inside a checkout; pass "
            f"--root if the repository is elsewhere"
        )
    listing = listing.stdout
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
            print(f"       rule: {rule_location('def rules(')}", file=stream)
            print(
                f"       fix: run `python3 tools/repo_identity.py --fix`, or correct {IDENTITY_FILE} "
                f"if the project moved",
                file=stream,
            )

    if home_findings:
        print(
            f"error: {len(home_findings)} line(s) commit a path into somebody's home directory:",
            file=sys.stderr,
        )
        for finding in home_findings:
            print(f"  {finding}", file=sys.stderr)
        print(f"       rule: {rule_location('HOME_PATH')}", file=sys.stderr)
        # The remedy depends on what the line is, and the difference matters: prose that *documents*
        # this rule trips it, and telling its author to use $PWD is advice they cannot take. The
        # placeholder form is the escape the rule is already written to allow.
        print(
            "       fix, in code or config: use a repository-relative path, or $PWD\n"
            "       fix, in prose or an example: write the username as a placeholder -- "
            "/Users/<you>/, /home/$USER/ --\n"
            "         which the rule deliberately exempts\n"
            "       these are never rewritten for you: only a human knows which of the two applies",
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
        print(f"       rule: {rule_location('KNOWN_HOSTS')}", file=sys.stderr)
        print(
            f"       fix: add a rewrite rule for that host to {rule_location('def rules(')} and list "
            f"the host in KNOWN_HOSTS,\n"
            f"         or use a URL that does not carry the owner",
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
