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
# it when either changes. Without that a developer who installed a new dsdlc would keep building
# against the old one until they wiped the repository cache -- silently, since generated output
# carries the generator version but nothing compares it.

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

def _dsdlc_extension_impl(_module_ctx):
    dsdlc_repository(name = "dsdlc")

dsdlc = module_extension(
    implementation = _dsdlc_extension_impl,
    doc = "Brings the host dsdlc into the build as @dsdlc.",
)
