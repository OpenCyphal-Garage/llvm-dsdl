# SPDX-License-Identifier: MIT
"""Differential test: the BitLengthSet algebra vs pydsdl's BitLengthSet.

The C++ tool (BlsDifferentialTool) emits a deterministic corpus of composed expressions as
replayable RPN recipes together with its own answers. This script replays every recipe
through pydsdl's ``BitLengthSet`` — the peer implementation of the DSDL length algebra,
which this project does not author — and diffs each answer:

  - ``min`` / ``max`` / ``fixed`` on every case (including the huge closed-form cases where
    our RunSet answers arithmetically and pydsdl answers lazily);
  - residue sets ``mod 3 / 5 / 8`` on every case (small by construction);
  - the concrete value set whenever the C++ side materialized it (bounded, never truncated);
    plus exact cardinality agreement in that regime.

Agreement corroborates that our denotational semantics matches the ecosystem's reference
understanding. A mismatch is an INVESTIGATION adjudicated by the Cyphal Specification — it
is as likely to be a pydsdl defect as ours, and neither side wins by default (the same
authority hierarchy as the Nunavut parity lane).

Usage: bls_pydsdl_differential.py <tool-binary> <pydsdl-repo>
"""

import subprocess
import sys


def parse_braced(text: str) -> set:
    assert text.startswith("{") and text.endswith("}"), text
    inner = text[1:-1]
    return set(int(v) for v in inner.split(",")) if inner else set()


def replay(recipe: str, bls):
    """Replays an RPN recipe over pydsdl BitLengthSet values."""
    stack = []
    for token in recipe.split():
        if token.startswith("L{"):
            stack.append(bls(parse_braced(token[1:])))
        elif token == "A":
            rhs = stack.pop()
            stack[-1] = stack[-1] + rhs
        elif token == "U":
            rhs = stack.pop()
            stack[-1] = stack[-1] | rhs
        elif token.startswith("P"):
            stack[-1] = stack[-1].pad_to_alignment(int(token[1:]))
        elif token.startswith("R"):
            stack[-1] = stack[-1].repeat(int(token[1:]))
        elif token.startswith("Q"):
            stack[-1] = stack[-1].repeat_range(int(token[1:]))
        else:
            raise ValueError(f"unknown recipe token: {token}")
    assert len(stack) == 1, recipe
    return stack[0]


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    tool, pydsdl_repo = sys.argv[1], sys.argv[2]

    sys.path.insert(0, pydsdl_repo)
    import pydsdl  # noqa: E402
    from pydsdl._bit_length_set import BitLengthSet  # noqa: E402

    out = subprocess.run([tool], capture_output=True, text=True, check=True).stdout
    cases = compared = mismatches = 0
    values_compared = big_cases = 0

    for line in out.splitlines():
        if not line.startswith("CASE "):
            continue
        cases += 1
        _, rest = line.split(" ", 1)
        case_id, recipe, extrema, mods, values_field = (p.strip() for p in rest.split("|"))

        ours = {}
        for kv in extrema.split():
            k, v = kv.split("=")
            ours[k] = int(v)
        for kv in mods.split():
            k, v = kv.split("=")
            ours[k] = v
        # pyenum gates only value ENUMERATION: pydsdl's expansion is combinatorial in repeat
        # counts even when the result is small (the regime our RunSet closed forms handle),
        # while its lazy min/max/fixed/% stay cheap for every case — so extrema and residue
        # parity always run, including on the million-element closed forms.
        py_enumerable = ours.pop("pyenum") == 1

        theirs = replay(recipe, BitLengthSet)

        def check(what, got, expected):
            nonlocal compared, mismatches
            compared += 1
            if got != expected:
                nonlocal_fail = f"MISMATCH case {case_id} ({recipe}): {what}: pydsdl={got} ours={expected}"
                print(nonlocal_fail)
                return 1
            return 0

        mismatches += check("min", theirs.min, ours["min"])
        mismatches += check("max", theirs.max, ours["max"])
        mismatches += check("fixed", 1 if theirs.fixed_length else 0, ours["fixed"])
        for d in (3, 5, 8):
            expected = ours[f"mod{d}"]
            if expected == "REFUSED":
                continue  # exact-or-refuse on our side; nothing to compare
            mismatches += check(f"mod{d}", set(theirs % d), parse_braced(expected))

        assert values_field.startswith("values=")
        payload = values_field[len("values="):]
        if payload.startswith("{") and py_enumerable:
            values_compared += 1
            expected_values = parse_braced(payload)
            got_values = set(theirs)  # bounded by the pyenum cost gate and the C++ limit
            mismatches += check("values", got_values, expected_values)
            mismatches += check("count", len(got_values), len(expected_values))
        else:
            big_cases += 1
            # Closed-form (or pydsdl-expensive) regime: extrema/mod checks above are the
            # comparison; enumeration on the pydsdl side would be combinatorial.

    if cases == 0:
        print("no cases parsed — tool output format drift?", file=sys.stderr)
        return 2

    print(
        f"bls-pydsdl-differential: {cases} cases ({values_compared} with full value sets, "
        f"{big_cases} closed-form), {compared} comparisons, {mismatches} mismatches "
        f"(pydsdl {pydsdl.__version__})"
    )
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
