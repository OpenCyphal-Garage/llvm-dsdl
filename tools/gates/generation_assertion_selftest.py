#!/usr/bin/env python3
"""Proves the generated-C assertions in RunUavcanGeneration.cmake still discriminate.

Those assertions match generated C by substring. The text renderer and the operation builder
spell the same call differently -- one names its variables and casts its arguments, the other
is named by EmitC -- so a pattern anchored on either spelling passes for one and fails for the
other, and a pattern anchored on neither passes for anything.

The patterns are read out of the checker rather than copied, so this cannot drift from what
ships. Each is required to match a renderer-shaped body and a builder-shaped body, and to
fail on the same body with the call removed.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# One case per assertion under test: the pattern's variable name in the checker, a body as the
# text renderer writes it, the same body as the operation builder produces it, and the edit
# that should make both fail.
CASES = {
    "capacity_call_pos": {
        "renderer": "  const int8_t _err_capacity = llvmdsdl_plan_capacity_check__demo_T_1_0((int64_t)(capacity_bytes * 8U));",
        "builder": "  int8_t v27 = llvmdsdl_plan_capacity_check__demo_T_1_0(v26);",
        "broken": "  int8_t v27 = 0;",
    },
    "scalar_call_match": {
        "renderer": "  const uint64_t _norm_0 = (uint64_t)llvmdsdl_plan_scalar_unsigned__demo_T_1_0__0__ser((int64_t)(obj->a));",
        "builder": "  int64_t v32 = llvmdsdl_plan_scalar_unsigned__demo_T_1_0__0__ser(v31);",
        "broken": "  int64_t v32 = v31;",
    },
    "scalar_signed_call_match": {
        "renderer": "  const int64_t _norms_0 = (int64_t)llvmdsdl_plan_scalar_signed__demo_T_1_0__0__ser((int64_t)(obj->a));",
        "builder": "  int64_t v32 = llvmdsdl_plan_scalar_signed__demo_T_1_0__0__ser(v31);",
        "broken": "  int64_t v32 = v31;",
    },
    "array_lenchk_call_pos": {
        "renderer": "  const int8_t _err_lenchk_0 = llvmdsdl_plan_validate_array_length__demo_T_1_0__0((int64_t)(obj->a.count));",
        "builder": "  int8_t v101 = llvmdsdl_plan_validate_array_length__demo_T_1_0__0(v100);",
        "broken": "  int8_t llvmdsdl_plan_validate_array_length__demo_T_1_0__0(int64_t);",
    },
    "union_tag_io_call_match": {
        "renderer": "  const uint64_t _tag_value = (uint64_t)llvmdsdl_plan_union_tag__demo_T_1_0__ser((int64_t)(obj->_tag_));",
        "builder": "  int64_t v31 = llvmdsdl_plan_union_tag__demo_T_1_0__ser(v30);",
        "broken": "  int64_t v31 = v30;",
    },
    "union_tag_call_pos": {
        "renderer": "  const int8_t _err_union_tag = llvmdsdl_plan_validate_union_tag__demo_T_1_0((int64_t)_tag_value);",
        "builder": "  int8_t v32 = llvmdsdl_plan_validate_union_tag__demo_T_1_0(v31);",
        "broken": "  int8_t llvmdsdl_plan_validate_union_tag__demo_T_1_0(int64_t);",
    },
    "delimiter_chk_call_pos": {
        "renderer": "  const int8_t _delim_chk_1 = llvmdsdl_plan_validate_delimiter_header__demo_T_1_0__1((int64_t)_size_bytes_1, (int64_t)_remaining_bytes_1);",
        "builder": "  int8_t v45 = llvmdsdl_plan_validate_delimiter_header__demo_T_1_0__1(v33, v41);",
        "broken": "  int8_t llvmdsdl_plan_validate_delimiter_header__demo_T_1_0__1(int64_t, int64_t);",
    },
    "scalar_float_call_pos": {
        "renderer": "  const float _normf_0 = llvmdsdl_plan_scalar_float__demo_T_1_0__0__ser((float)(obj->a));",
        "builder": "  float v32 = llvmdsdl_plan_scalar_float__demo_T_1_0__0__ser(v31);",
        "broken": "  float v32 = v31;",
    },
}

LITERAL = re.compile(r'string\(FIND\s+"\$\{impl_text\}"\s+"([^"]+)"\s*\n?\s*(\w+)\)', re.M)
REGEXP = re.compile(r'string\(REGEX MATCH\s+"([^"]+)"\s*\n?\s*(\w+)\s+"\$\{impl_text\}"\)', re.M)


def read_patterns(checker: Path) -> dict[str, tuple[str, bool]]:
    """Every assertion, with whether it is a regex. A literal is matched as one."""
    text = checker.read_text(encoding="utf-8")
    found = {name: (pat, False) for pat, name in LITERAL.findall(text)}
    found.update({name: (pat, True) for pat, name in REGEXP.findall(text)})
    missing = sorted(set(CASES) - set(found))
    if missing:
        raise SystemExit(
            f"error: {checker} no longer defines: {', '.join(missing)}\n"
            "       the assertion was renamed or removed; update this selftest with it")
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--checker", required=True, type=Path)
    args = parser.parse_args()

    patterns = read_patterns(args.checker)
    failures: list[str] = []

    def matches(pattern: str, is_regex: bool, text: str) -> bool:
        return bool(re.search(pattern, text)) if is_regex else pattern in text

    for name, case in CASES.items():
        pattern, is_regex = patterns[name]
        for shape in ("renderer", "builder"):
            if not matches(pattern, is_regex, case[shape]):
                failures.append(
                    f"{name}: pattern {pattern!r} does not match a {shape}-shaped call\n"
                    f"    {case[shape].strip()}")
        if matches(pattern, is_regex, case["broken"]):
            failures.append(
                f"{name}: pattern {pattern!r} matches a body with the call removed, so it "
                f"asserts nothing\n    {case['broken'].strip()}")

    if failures:
        print("generation assertion selftest failed:")
        for failure in failures:
            print(f"  {failure}")
        return 1

    print(f"generation assertion selftest: {len(CASES)} assertion(s) match both spellings "
          "and reject a body with the call removed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
