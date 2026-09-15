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
set(accel_out "${OUT_DIR}/accel")
set(py_package_portable "llvmdsdl_py_unit_portable")
set(py_package_fast "llvmdsdl_py_unit_fast")
set(py_package_accel "llvmdsdl_py_unit_accel")

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

function(generate_python_package out_dir package specialization)
  execute_process(
    COMMAND "${DSDLC}" --target-language python
      # The harness below is written against versioned type names.
      --versioned-type-names
      "${FIXTURES_ROOT}"
      --outdir "${out_dir}"
      --py-package "${package}"
      --py-runtime-specialization "${specialization}"
    RESULT_VARIABLE gen_result
    OUTPUT_VARIABLE gen_stdout
    ERROR_VARIABLE gen_stderr
  )
  if(NOT gen_result EQUAL 0)
    message(STATUS "${package} dsdlc stdout:\n${gen_stdout}")
    message(STATUS "${package} dsdlc stderr:\n${gen_stderr}")
    message(FATAL_ERROR "Failed to generate Python unit fixtures for ${package}")
  endif()
endfunction()

generate_python_package("${portable_out}" "${py_package_portable}" portable)
generate_python_package("${fast_out}" "${py_package_fast}" fast)

# The accelerator is staged into a package of its own and reached through the runtime loader, the
# way generated code reaches it.
set(has_accel FALSE)
if(DEFINED ACCEL_MODULE AND NOT "${ACCEL_MODULE}" STREQUAL "" AND EXISTS "${ACCEL_MODULE}")
  set(has_accel TRUE)
  generate_python_package("${accel_out}" "${py_package_accel}" portable)
  file(COPY "${ACCEL_MODULE}" DESTINATION "${accel_out}/${py_package_accel}")
elseif(REQUIRE_ACCEL)
  message(FATAL_ERROR
    "Python unit tests require the accelerator module, but ACCEL_MODULE is missing.")
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
from array import array
from pathlib import Path
from types import ModuleType

PORTABLE_OUT = Path("@PORTABLE_OUT@")
FAST_OUT = Path("@FAST_OUT@")
ACCEL_OUT = Path("@ACCEL_OUT@")
PORTABLE_PACKAGE = "@PORTABLE_PACKAGE@"
FAST_PACKAGE = "@FAST_PACKAGE@"
ACCEL_PACKAGE = "@ACCEL_PACKAGE@"
HAS_ACCEL = @HAS_ACCEL@


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

        # A memoryview is addressed by byte whatever the format of its buffer; a read-only view
        # and a strided view are rejected, as the accelerator rejects them.
        words = array("I", [0, 0])
        runtime.write_unsigned(memoryview(words), 32, 16, 0xBEEF, False)
        self.assertEqual(runtime.read_unsigned(memoryview(words), 32, 16), 0xBEEF)
        self.assertEqual(runtime.read_unsigned(memoryview(words).cast("B")[4:], 0, 16), 0xBEEF)
        with self.assertRaises(TypeError):
            runtime.write_unsigned(memoryview(bytes(2)), 0, 8, 1, False)
        with self.assertRaises(TypeError):
            runtime.read_unsigned(memoryview(bytearray(4))[::2], 0, 8)

    def test_runtime_helpers_fast(self) -> None:
        runtime = import_from_generated(FAST_OUT, FAST_PACKAGE, "_dsdl_runtime")

        buf = bytearray(2)
        runtime.copy_bits(buf, 0, bytes([0x12, 0x34]), 0, 16)
        self.assertEqual(bytes(buf), bytes([0x12, 0x34]))
        self.assertEqual(runtime.extract_bits(bytes([0x12, 0x34]), 0, 16), bytes([0x12, 0x34]))

        with self.assertRaises(ValueError):
            runtime.extract_bits(bytes([0xAA]), 0, 16)

        # A memoryview is addressed by byte whatever the format of its buffer; a read-only view
        # and a strided view are rejected, as the accelerator rejects them.
        words = array("I", [0, 0])
        runtime.write_unsigned(memoryview(words), 32, 16, 0xBEEF, False)
        self.assertEqual(runtime.read_unsigned(memoryview(words), 32, 16), 0xBEEF)
        self.assertEqual(runtime.read_unsigned(memoryview(words).cast("B")[4:], 0, 16), 0xBEEF)
        with self.assertRaises(TypeError):
            runtime.write_unsigned(memoryview(bytes(2)), 0, 8, 1, False)
        with self.assertRaises(TypeError):
            runtime.read_unsigned(memoryview(bytearray(4))[::2], 0, 8)

    def all_runtimes(self) -> list[tuple[str, ModuleType]]:
        runtimes = [
            ("portable", import_from_generated(PORTABLE_OUT, PORTABLE_PACKAGE, "_dsdl_runtime")),
            ("fast", import_from_generated(FAST_OUT, FAST_PACKAGE, "_dsdl_runtime")),
        ]
        if HAS_ACCEL:
            os.environ["LLVMDSDL_PY_RUNTIME_MODE"] = "accel"
            loader = import_from_generated(ACCEL_OUT, ACCEL_PACKAGE, "_runtime_loader")
            self.assertEqual(loader.BACKEND, "accel")
            runtimes.append(("accel", loader.runtime))
        return runtimes

    def test_wide_integer_writes_wrap_across_runtimes(self) -> None:
        runtimes = self.all_runtimes()
        wide = 1 << 80
        for value in (wide, -wide, wide | 0xA5A5_5A5A_F00D_BEEF, -wide - 1):
            for len_bits in (1, 7, 8, 16, 33, 64):
                for off_bits in (0, 5):
                    expected = ((value & ((1 << len_bits) - 1)) << off_bits).to_bytes(16, "little")
                    for name, runtime in runtimes:
                        for write in (runtime.write_unsigned, runtime.write_signed):
                            buf = bytearray(16)
                            write(buf, off_bits, len_bits, value, False)
                            self.assertEqual(
                                bytes(buf), expected, (name, write.__name__, value, len_bits, off_bits)
                            )

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
string(REPLACE "@ACCEL_OUT@" "${accel_out}" unit_script_content "${unit_script_content}")
string(REPLACE "@ACCEL_PACKAGE@" "${py_package_accel}" unit_script_content "${unit_script_content}")
if(has_accel)
  string(REPLACE "@HAS_ACCEL@" "True" unit_script_content "${unit_script_content}")
else()
  string(REPLACE "@HAS_ACCEL@" "False" unit_script_content "${unit_script_content}")
endif()
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
