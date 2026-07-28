#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# Drive the containerised MkDocs toolchain (packaging/docker/Dockerfile.docs).
#
# The image holds the toolchain; the project being rendered is bind-mounted at run time. That split is
# what makes the toolchain portable: build the image once, then point it at this repository, a
# worktree, a release branch checked out elsewhere, or any other directory containing an mkdocs.yml.
# No Python, virtualenv, or MkDocs install is needed on the host.
#
# Usage:
#   tools/docs/docs-container.sh build            Build the image.
#   tools/docs/docs-container.sh serve [dir]      Serve with live reload (Ctrl-C to stop).
#   tools/docs/docs-container.sh check [dir]      Run `mkdocs build --strict`, exactly as CI does.
#   tools/docs/docs-container.sh shell [dir]      Interactive shell inside the toolchain.
#
# `dir` defaults to the repository this script lives in. Environment overrides:
#   LLVMDSDL_DOCS_IMAGE   image tag           (default: llvm-dsdl-docs:local)
#   LLVMDSDL_DOCS_PORT    host port to serve  (default: 8000)
#   DOCKER                container CLI       (default: docker; podman works too)

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

DOCKER="${DOCKER:-docker}"
IMAGE="${LLVMDSDL_DOCS_IMAGE:-llvm-dsdl-docs:local}"
PORT="${LLVMDSDL_DOCS_PORT:-8000}"
DOCKERFILE="${REPO_ROOT}/packaging/docker/Dockerfile.docs"

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

require_runtime() {
    command -v "${DOCKER}" >/dev/null 2>&1 \
        || die "'${DOCKER}' not found on PATH; install Docker or set DOCKER=podman"
}

# Resolve the project directory to an absolute path and sanity-check it before mounting: a typo would
# otherwise surface as an empty site rather than an error, and Docker silently creates missing host
# paths as root-owned directories.
resolve_project() {
    local dir="${1:-${REPO_ROOT}}"
    [ -d "${dir}" ] || die "not a directory: ${dir}"
    dir="$(cd -- "${dir}" && pwd)"
    [ -f "${dir}/mkdocs.yml" ] || die "no mkdocs.yml in ${dir}; is this a MkDocs project?"
    printf '%s' "${dir}"
}

image_exists() {
    "${DOCKER}" image inspect "${IMAGE}" >/dev/null 2>&1
}

build_image() {
    require_runtime
    # docs/requirements.txt is the only build-context input the Dockerfile reads, so the repository
    # root is the context regardless of which project is later mounted.
    "${DOCKER}" build -f "${DOCKERFILE}" -t "${IMAGE}" "${REPO_ROOT}"
}

ensure_image() {
    image_exists || {
        printf 'image %s not present; building it now\n' "${IMAGE}" >&2
        build_image
    }
}

# Run as the invoking user so that anything written into the bind mount -- `site/` from a strict build,
# most visibly -- belongs to that user rather than to root. Without this, a Linux host ends up with a
# root-owned site/ that the developer cannot delete without sudo.
#
# id(1) has no meaning on Windows hosts, where the flag is also unnecessary, so it is skipped there.
user_args() {
    case "$(uname -s 2>/dev/null || printf unknown)" in
        MINGW* | MSYS* | CYGWIN*) : ;;
        *) printf -- '--user\n%s:%s\n' "$(id -u)" "$(id -g)" ;;
    esac
}

# Allocate a TTY only when there is one to allocate. `docker run -it` aborts outright when stdin is
# not a terminal, which would make `serve` unusable from a script, a CI job, or any non-interactive
# shell -- contexts where serving in the background is a perfectly reasonable thing to want.
tty_args() {
    if [ -t 0 ] && [ -t 1 ]; then
        printf -- '-it\n'
    fi
}

cmd_serve() {
    local project
    project="$(resolve_project "${1:-}")"
    ensure_image

    printf 'serving %s at http://127.0.0.1:%s/ (Ctrl-C to stop)\n' "${project}" "${PORT}" >&2

    # The mount is read-only: serving never writes, and a stray `mkdocs build` inside the container
    # should not drop a site/ into the developer's tree as a side effect of previewing.
    #
    # --init so Ctrl-C reaches mkdocs instead of being swallowed by PID 1.
    local -a run_flags=()
    while IFS= read -r line; do run_flags+=("${line}"); done < <(user_args; tty_args)

    exec "${DOCKER}" run --rm --init \
        "${run_flags[@]}" \
        -p "${PORT}:8000" \
        -v "${project}:/project:ro" \
        "${IMAGE}"
}

cmd_check() {
    local project
    project="$(resolve_project "${1:-}")"
    ensure_image

    # Writable: --strict writes the rendered site into <project>/site, which is gitignored.
    local -a user_flags=()
    while IFS= read -r line; do user_flags+=("${line}"); done < <(user_args)

    "${DOCKER}" run --rm --init \
        "${user_flags[@]}" \
        -v "${project}:/project" \
        "${IMAGE}" \
        mkdocs build --strict
}

cmd_shell() {
    local project
    project="$(resolve_project "${1:-}")"
    ensure_image

    local -a user_flags=()
    while IFS= read -r line; do user_flags+=("${line}"); done < <(user_args)

    exec "${DOCKER}" run --rm -it --init \
        "${user_flags[@]}" \
        -p "${PORT}:8000" \
        -v "${project}:/project" \
        "${IMAGE}" \
        /bin/bash
}

usage() {
    sed -n '10,26p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

main() {
    local subcommand="${1:-}"
    shift || true

    case "${subcommand}" in
        build) require_runtime; build_image ;;
        serve) cmd_serve "${1:-}" ;;
        check) cmd_check "${1:-}" ;;
        shell) cmd_shell "${1:-}" ;;
        -h | --help | help | "") usage ;;
        *) die "unknown subcommand: ${subcommand} (try --help)" ;;
    esac
}

main "$@"
