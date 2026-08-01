# No documentation ghosts (no bananas rule)

When you **remove** something, remove it cleanly — don't leave behind a note that it
used to be there or a negative explaining its absence. The deletion does not need to
be written down.

Turning a removal into a stated negative weakens the logic of the text:

> V1: We want bananas and oranges for the table.
> *(remove bananas)*
> ❌ V2: We want oranges but not bananas for the table.

"Not bananas" is bizarre on its face — it invites the reader to ask *why bananas? why
not apples? Does excluding bananas imply apples are fine?* A clean elision carries
no such baggage:

> ✅ V2: We want oranges for the table.

**The exception — a strong reappearance signal.** Keep the negative only when there
is a concrete, logical force that would otherwise pull the removed thing back: a
common recommendation, an obvious-looking default, something the wider world (or a
model's priors) will keep suggesting. Then the negative is doing real work —
pre-empting the wrong answer — and should carry its *reason*:

> ✅ Use apple juice when sick. We tried orange juice — the usual cold remedy — and it
> did nothing for us.

Here "we tried orange juice" earns its place because the OJ-for-a-cold prior is
strong and would otherwise creep back in. Absent that kind of pull, prefer the
clean elision.

## Oracle variation

Ghosts can also appear as future predictions that have no embodiment. For example:

> ❌ The current UI uses blue and red colours.

"Current" suggests there is a future UI that does not use blue and red colours yet this
plan is not visible and the user is left wondering, "is this UI deprecated? Should I 
be using it? Is blue and red a temporary decision?".

> ✅ The UI uses blue and red colours.

Here we are providing actionable documentation. Yes, the UI is supported. Yes, you
should continue using blue and red colours if you want to be consistent with the
documented GUI.

# No Stupid Docs Rule

Be Precise and Concise When Writing Documentation

## Do not pontificate.

> ❌ The obj backend never emits these attributes: it compiles the C or C++ it generates as part of its own pipeline, and warnings there would be about code the user never sees.

> ✅ The obj backend never emits these attributes.

## Do not prevaricate.

> ❌ There are ways this is implemented and these ways include several which are dangerous and should be used with caution.

> ✅ Do not use raw pointers.

## Do not ramble

Where the banner is self explanatory:

> ❌ Each page carries a gating mode banner. Structural means coverage was inferred from registered test names; behavioural means a cell counts as covered only if a matching test actually executed and passed. The published pages are structural: the docs build compiles the compiler but does not run the test suite. The behavioural run is a release gate in CI, where the generators consume the suite's JUnit results and fail the build on a regression. Read the banner rather than assuming.

> ✅ Each page carries a gating mode banner.

## Do not be inane.

Do not assume the user is an idiot.

> ❌ ... are written by the report generators and rebuilt whenever the site is published. Editing them by hand accomplishes nothing — the next build overwrites the file.

> ✅ ... are written by the report generators and rebuilt whenever the site is published.

# Prefer Python over Shell Scripts

Shell scripts suffer from multiple compatibility issues including different shells on
different platforms using different system tools. Python is Python. Prefer using Python
where scripting is needed. 

## Scripting Use Refinement

Only use scripting where another technology does not already provide adequate automation
capabilities. For example, if working in the build system prefer using cmake first and 
Python only if cmake is not suitable.

# CLIs Are Self Documenting

Do not add markdown or other copy-and-paste documentation for CLI flags when `--help` is
adequate and authoritative. There are times when an example of using the CLI to perform
some action is relevant but this is only in service of documenting some other concern
where the CLI can be used, _not_ in documenting the CLI itself.
