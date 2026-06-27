cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC PYTHON_EXECUTABLE FIXTURES_ROOT OUT_DIR)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

if(NOT EXISTS "${DSDLC}")
  message(FATAL_ERROR "dsdlc executable not found: ${DSDLC}")
endif()
if(NOT EXISTS "${PYTHON_EXECUTABLE}")
  message(FATAL_ERROR "python executable not found: ${PYTHON_EXECUTABLE}")
endif()
if(NOT EXISTS "${FIXTURES_ROOT}")
  message(FATAL_ERROR "fixtures root not found: ${FIXTURES_ROOT}")
endif()

set(portable_out "${OUT_DIR}/portable")
set(fast_out "${OUT_DIR}/fast")
set(py_package_portable "llvmdsdl_py_unit_portable")
set(py_package_fast "llvmdsdl_py_unit_fast")

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

execute_process(
  COMMAND "${DSDLC}" --target-language python
    "${FIXTURES_ROOT}"
    --outdir "${portable_out}"
    --py-package "${py_package_portable}"
    --py-runtime-specialization portable
  RESULT_VARIABLE portable_result
  OUTPUT_VARIABLE portable_stdout
  ERROR_VARIABLE portable_stderr
)
if(NOT portable_result EQUAL 0)
  message(STATUS "portable dsdlc stdout:\n${portable_stdout}")
  message(STATUS "portable dsdlc stderr:\n${portable_stderr}")
  message(FATAL_ERROR "Failed to generate portable Python unit fixtures")
endif()

execute_process(
  COMMAND "${DSDLC}" --target-language python
    "${FIXTURES_ROOT}"
    --outdir "${fast_out}"
    --py-package "${py_package_fast}"
    --py-runtime-specialization fast
  RESULT_VARIABLE fast_result
  OUTPUT_VARIABLE fast_stdout
  ERROR_VARIABLE fast_stderr
)
if(NOT fast_result EQUAL 0)
  message(STATUS "fast dsdlc stdout:\n${fast_stdout}")
  message(STATUS "fast dsdlc stderr:\n${fast_stderr}")
  message(FATAL_ERROR "Failed to generate fast Python unit fixtures")
endif()

set(unit_script "${OUT_DIR}/python_unit_tests.py")
file(WRITE
  "${unit_script}"
  [=[
from __future__ import annotations

import importlib
import json
import os
import sys
import unittest
from pathlib import Path

PORTABLE_OUT = Path("@PORTABLE_OUT@")
FAST_OUT = Path("@FAST_OUT@")
PORTABLE_PACKAGE = "@PORTABLE_PACKAGE@"
FAST_PACKAGE = "@FAST_PACKAGE@"


def clear_package_modules(prefix: str) -> None:
    for name in list(sys.modules):
        if name == prefix or name.startswith(prefix + "."):
            del sys.modules[name]


def import_from_generated(out_dir: Path, package: str, module: str):
    out_dir_str = str(out_dir)
    if out_dir_str not in sys.path:
        sys.path.insert(0, out_dir_str)
    clear_package_modules(package)
    return importlib.import_module(f"{package}.{module}")


class PythonEmitterRuntimeUnitTests(unittest.TestCase):
    def setUp(self) -> None:
        self.old_mode = os.environ.get("LLVMDSDL_PY_RUNTIME_MODE")

    def tearDown(self) -> None:
        if self.old_mode is None:
            os.environ.pop("LLVMDSDL_PY_RUNTIME_MODE", None)
        else:
            os.environ["LLVMDSDL_PY_RUNTIME_MODE"] = self.old_mode

    def test_emitted_artifacts_and_metadata(self) -> None:
        for out_dir, package, specialization in (
            (PORTABLE_OUT, PORTABLE_PACKAGE, "portable"),
            (FAST_OUT, FAST_PACKAGE, "fast"),
        ):
            package_root = out_dir / package.replace(".", "/")
            self.assertTrue((out_dir / "pyproject.toml").is_file())
            self.assertTrue((package_root / "_dsdl_runtime.py").is_file())
            self.assertTrue((package_root / "_runtime_loader.py").is_file())
            self.assertTrue((package_root / "py.typed").is_file())
            pyproject_text = (out_dir / "pyproject.toml").read_text(encoding="utf-8")
            self.assertIn("[tool.setuptools.package-data]", pyproject_text)
            self.assertIn("_dsdl_runtime_accel*.so", pyproject_text)
            self.assertIn("_dsdl_runtime_accel*.dylib", pyproject_text)
            self.assertIn("_dsdl_runtime_accel*.pyd", pyproject_text)
            metadata = json.loads((package_root / "llvmdsdl_codegen.json").read_text(encoding="utf-8"))
            self.assertEqual(metadata["llvmdsdl"]["pythonRuntimeSpecialization"], specialization)

    def test_generated_type_contract(self) -> None:
        mod = import_from_generated(PORTABLE_OUT, PORTABLE_PACKAGE, "fixtures.vendor.widget_1_0")
        type_cls = mod.Widget_1_0
        self.assertTrue(hasattr(type_cls, "__slots__"))

        value = type_cls(foo=0x12, bar=0x3456)
        payload = value.serialize()
        roundtrip = type_cls.deserialize(payload)

        self.assertEqual(roundtrip.foo, 0x12)
        self.assertEqual(roundtrip.bar, 0x3456)
        self.assertEqual(roundtrip.serialize(), payload)

    def test_runtime_helpers_portable(self) -> None:
        runtime = import_from_generated(PORTABLE_OUT, PORTABLE_PACKAGE, "_dsdl_runtime")

        self.assertEqual(runtime.byte_length_for_bits(0), 0)
        self.assertEqual(runtime.byte_length_for_bits(9), 2)

        buf = bytearray(2)
        runtime.write_unsigned(buf, 0, 16, 0xABCD, False)
        self.assertEqual(bytes(buf), bytes([0xCD, 0xAB]))
        self.assertEqual(runtime.read_unsigned(buf, 0, 16), 0xABCD)

        signed = runtime.read_signed(bytes([0xFF]), 0, 8)
        self.assertEqual(signed, -1)
        self.assertFalse(runtime.get_bit(bytes([0x01]), 100))

        extracted = runtime.extract_bits(bytes([0xAA]), 0, 16)
        self.assertEqual(extracted, bytes([0xAA, 0x00]))

    def test_runtime_helpers_fast(self) -> None:
        runtime = import_from_generated(FAST_OUT, FAST_PACKAGE, "_dsdl_runtime")

        buf = bytearray(2)
        runtime.copy_bits(buf, 0, bytes([0x12, 0x34]), 0, 16)
        self.assertEqual(bytes(buf), bytes([0x12, 0x34]))
        self.assertEqual(runtime.extract_bits(bytes([0x12, 0x34]), 0, 16), bytes([0x12, 0x34]))

        with self.assertRaises(ValueError):
            runtime.extract_bits(bytes([0xAA]), 0, 16)

    def test_runtime_loader_modes(self) -> None:
        package_root = PORTABLE_OUT / PORTABLE_PACKAGE.replace(".", "/")
        accel_stub = package_root / "_dsdl_runtime_accel.py"

        os.environ["LLVMDSDL_PY_RUNTIME_MODE"] = "pure"
        loader = import_from_generated(PORTABLE_OUT, PORTABLE_PACKAGE, "_runtime_loader")
        self.assertEqual(loader.BACKEND, "pure")

        os.environ["LLVMDSDL_PY_RUNTIME_MODE"] = "auto"
        loader = import_from_generated(PORTABLE_OUT, PORTABLE_PACKAGE, "_runtime_loader")
        self.assertEqual(loader.BACKEND, "pure")

        os.environ["LLVMDSDL_PY_RUNTIME_MODE"] = "invalid-value"
        loader = import_from_generated(PORTABLE_OUT, PORTABLE_PACKAGE, "_runtime_loader")
        self.assertEqual(loader.BACKEND, "pure")

        accel_stub.write_text("BACKEND = 'accel'\n", encoding="utf-8")
        try:
            os.environ["LLVMDSDL_PY_RUNTIME_MODE"] = "accel"
            loader = import_from_generated(PORTABLE_OUT, PORTABLE_PACKAGE, "_runtime_loader")
            self.assertEqual(loader.BACKEND, "accel")

            os.environ["LLVMDSDL_PY_RUNTIME_MODE"] = "auto"
            loader = import_from_generated(PORTABLE_OUT, PORTABLE_PACKAGE, "_runtime_loader")
            self.assertEqual(loader.BACKEND, "accel")
        finally:
            if accel_stub.exists():
                accel_stub.unlink()


if __name__ == "__main__":
    unittest.main(verbosity=2)
]=]
)

file(READ "${unit_script}" unit_script_content)
string(REPLACE "@PORTABLE_OUT@" "${portable_out}" unit_script_content "${unit_script_content}")
string(REPLACE "@FAST_OUT@" "${fast_out}" unit_script_content "${unit_script_content}")
string(REPLACE "@PORTABLE_PACKAGE@" "${py_package_portable}" unit_script_content "${unit_script_content}")
string(REPLACE "@FAST_PACKAGE@" "${py_package_fast}" unit_script_content "${unit_script_content}")
file(WRITE "${unit_script}" "${unit_script_content}")

execute_process(
  COMMAND "${PYTHON_EXECUTABLE}" "${unit_script}"
  RESULT_VARIABLE unit_result
  OUTPUT_VARIABLE unit_stdout
  ERROR_VARIABLE unit_stderr
)
if(NOT unit_result EQUAL 0)
  message(STATUS "python unit stdout:\n${unit_stdout}")
  message(STATUS "python unit stderr:\n${unit_stderr}")
  message(FATAL_ERROR "Python unit tests failed")
endif()

message(STATUS "Python unit tests passed")
