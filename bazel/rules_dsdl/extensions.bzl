# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# Locating dsdlc.
#
# dsdlc is a host tool installed outside the build, like a compiler, so it is brought in as an
# external repository holding one symlink rather than built from source. `DSDLC` names it explicitly;
# otherwise it is looked up on PATH.
#
# The repository is `local = True` and declares `DSDLC` and `PATH` in `environ`, so Bazel refetches
# it when either changes. Note what that does *not* cover: a new dsdlc installed at the same path
# leaves both unchanged. It is harmless here, because this repository only publishes a symlink and
# the macros in defs.bzl pass it to genrules as a tool, where Bazel digests it like any other action
# input -- swap the binary and the generating actions rerun. The fetch-time path in repositories.bzl
# has no such backstop and has to watch() the binary explicitly.

load("//:repositories.bzl", "dsdl_namespace_repository")

def _dsdlc_repository_impl(repository_ctx):
    explicit = repository_ctx.os.environ.get("DSDLC", "")
    if explicit:
        resolved = repository_ctx.path(explicit)
        if not resolved.exists:
            fail("DSDLC is set to '{}', which does not exist.".format(explicit))
    else:
        resolved = repository_ctx.which("dsdlc")
        if resolved == None:
            fail(
                "rules_dsdl could not find dsdlc.\n" +
                "Install llvm-dsdl and put dsdlc on PATH, or set the DSDLC environment variable " +
                "to its absolute path (`build --action_env=DSDLC=/path/to/dsdlc` in .bazelrc).",
            )

    repository_ctx.symlink(resolved, "dsdlc")
    repository_ctx.file(
        "BUILD.bazel",
        "exports_files([\"dsdlc\"])\n",
        executable = False,
    )

dsdlc_repository = repository_rule(
    implementation = _dsdlc_repository_impl,
    environ = ["DSDLC", "PATH"],
    local = True,
    doc = "Exposes the host's dsdlc as @dsdlc//:dsdlc.",
)

def _dsdlc_extension_impl(module_ctx):
    dsdlc_repository(name = "dsdlc")

    for module in module_ctx.modules:
        for namespace in module.tags.namespace:
            dsdl_namespace_repository(
                name = namespace.name,
                anchor = namespace.anchor,
                root = namespace.root,
                language = namespace.language,
                library_name = namespace.library_name or namespace.name,
                archive_name = namespace.archive_name,
                options = namespace.options,
            )

_namespace = tag_class(
    attrs = {
        "name": attr.string(mandatory = True, doc = "Repository name to expose the namespace as."),
        "root": attr.string(mandatory = True, doc = "Root namespace directory, relative to the anchor."),
        "anchor": attr.label(default = Label("@@//:MODULE.bazel")),
        "language": attr.string(default = "c"),
        "library_name": attr.string(),
        "archive_name": attr.string(default = "llvmdsdl_generated"),
        "options": attr.string_list(),
    },
    doc = "Generates one root namespace at fetch time; see repositories.bzl for why fetch time.",
)

dsdlc = module_extension(
    implementation = _dsdlc_extension_impl,
    tag_classes = {"namespace": _namespace},
    doc = "Brings the host dsdlc into the build as @dsdlc, and generates namespaces on request.",
)
