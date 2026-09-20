import os
import sys
import lit.formats

config.name = "LLVMDSDL"
config.test_format = lit.formats.ShTest(True)
config.suffixes = [".mlir", ".txt"]
config.excludes = ["CMakeLists.txt", "lit.cfg.py", "lit.site.cfg.py.in"]
if not getattr(config, "test_source_root", None):
    config.test_source_root = os.path.dirname(__file__)
if not getattr(config, "test_exec_root", None):
    config.test_exec_root = os.path.dirname(__file__)

config.substitutions.append(("%dsdlc", config.dsdlc))
config.substitutions.append(("%dsdl-opt", config.dsdl_opt))
config.substitutions.append(("%{python}", sys.executable or "python3"))

# lit hands a test a filtered environment, so the switch that rewrites the codegen snapshot
# goldens has to be named here to reach compare_codegen_snapshot.py.
if "LLVMDSDL_UPDATE_CODEGEN_SNAPSHOT" in os.environ:
    config.environment["LLVMDSDL_UPDATE_CODEGEN_SNAPSHOT"] = os.environ["LLVMDSDL_UPDATE_CODEGEN_SNAPSHOT"]
