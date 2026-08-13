# SPDX-License-Identifier: MIT
"""Checks saturated int64 BitLengthSet evaluation against arbitrary-precision Python sets."""

import subprocess
import sys


MAXIMUM = (1 << 63) - 1


def parse_set(text: str) -> set[int]:
    if not text.startswith("{") or not text.endswith("}"):
        raise ValueError(text)
    body = text[1:-1]
    return set(map(int, body.split(","))) if body else set()


def parse_set_or_refused(text: str) -> set[int] | None:
    # The tool refuses instead of truncating (exact-or-refuse); a refusal is a
    # permitted outcome, not a mismatch.
    return None if text == "REFUSED" else parse_set(text)


def saturated_add(lhs: int, rhs: int) -> int:
    return min(MAXIMUM, lhs + rhs)


def add(lhs: set[int], rhs: set[int]) -> set[int]:
    return {saturated_add(x, y) for x in lhs for y in rhs}


def repeat(values: set[int], count: int) -> set[int]:
    result = {0}
    for _ in range(count):
        result = add(result, values)
    return result


def replay(recipe: str) -> set[int]:
    stack: list[set[int]] = []
    for token in recipe.split():
        if token.startswith("L{"):
            stack.append(parse_set(token[1:]))
        elif token == "A":
            rhs = stack.pop()
            stack[-1] = add(stack[-1], rhs)
        elif token == "U":
            rhs = stack.pop()
            stack[-1] |= rhs
        elif token.startswith("P"):
            alignment = int(token[1:])
            stack[-1] = {
                min(MAXIMUM, value if value % alignment == 0 else value + alignment - value % alignment)
                for value in stack[-1]
            }
        elif token.startswith("R"):
            stack[-1] = repeat(stack[-1], int(token[1:]))
        elif token.startswith("Q"):
            values = stack[-1]
            stack[-1] = set().union(*(repeat(values, count) for count in range(int(token[1:]) + 1)))
        else:
            raise ValueError(f"unknown recipe token: {token}")
    if len(stack) != 1:
        raise ValueError(recipe)
    return stack[0]


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <tool>", file=sys.stderr)
        return 2

    output = subprocess.run([sys.argv[1]], capture_output=True, text=True, check=True).stdout
    cases = comparisons = failures = 0
    for line in output.splitlines():
        if not line.startswith("CASE "):
            continue
        cases += 1
        _, payload = line.split(" ", 1)
        case_id, recipe, extrema, residues, values_field = (part.strip() for part in payload.split("|"))
        expected = replay(recipe)

        actual: dict[str, str] = {}
        for field in extrema.split() + residues.split():
            key, value = field.split("=", 1)
            actual[key] = value
        key, value = values_field.split("=", 1)
        actual[key] = value

        checks = {
            "min": (int(actual["min"]), min(expected)),
            "max": (int(actual["max"]), max(expected)),
            "fixed": (int(actual["fixed"]), int(len(expected) == 1)),
            "values": (parse_set_or_refused(actual["values"]), expected),
        }
        for divisor in (3, 5, 8, 16):
            checks[f"mod{divisor}"] = (
                parse_set_or_refused(actual[f"mod{divisor}"]),
                {value % divisor for value in expected},
            )

        for name, (got, wanted) in checks.items():
            if got is None:
                continue
            comparisons += 1
            if got != wanted:
                failures += 1
                print(f"MISMATCH case {case_id} ({recipe}): {name}: C++={got} Python={wanted}")

    if cases == 0:
        print("no cases parsed", file=sys.stderr)
        return 2
    print(f"bls-int64-differential: {cases} cases, {comparisons} comparisons, {failures} mismatches")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
