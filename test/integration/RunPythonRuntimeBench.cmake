cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC OUT_DIR PYTHON_EXECUTABLE)
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

set(source_root "")
if(DEFINED UAVCAN_ROOT AND NOT "${UAVCAN_ROOT}" STREQUAL "" AND EXISTS "${UAVCAN_ROOT}")
  set(source_root "${UAVCAN_ROOT}")
elseif(DEFINED FIXTURES_ROOT AND NOT "${FIXTURES_ROOT}" STREQUAL "" AND EXISTS "${FIXTURES_ROOT}")
  set(source_root "${FIXTURES_ROOT}")
else()
  message(FATAL_ERROR
    "Missing benchmark source root. Provide UAVCAN_ROOT (preferred) or FIXTURES_ROOT.")
endif()

if(NOT DEFINED BENCH_SPECIALIZATIONS OR "${BENCH_SPECIALIZATIONS}" STREQUAL "")
  set(BENCH_SPECIALIZATIONS "portable;fast")
endif()
if(NOT DEFINED BENCH_ITERATIONS_SMALL OR "${BENCH_ITERATIONS_SMALL}" STREQUAL "")
  set(BENCH_ITERATIONS_SMALL 4000)
endif()
if(NOT DEFINED BENCH_ITERATIONS_MEDIUM OR "${BENCH_ITERATIONS_MEDIUM}" STREQUAL "")
  set(BENCH_ITERATIONS_MEDIUM 1500)
endif()
if(NOT DEFINED BENCH_ITERATIONS_LARGE OR "${BENCH_ITERATIONS_LARGE}" STREQUAL "")
  set(BENCH_ITERATIONS_LARGE 500)
endif()
if(NOT DEFINED BENCH_ENABLE_THRESHOLDS OR "${BENCH_ENABLE_THRESHOLDS}" STREQUAL "")
  set(BENCH_ENABLE_THRESHOLDS ON)
endif()
if(NOT DEFINED BENCH_THRESHOLDS_JSON OR "${BENCH_THRESHOLDS_JSON}" STREQUAL "")
  set(BENCH_THRESHOLDS_JSON "")
endif()
if(NOT DEFINED BENCH_REPORT_JSON OR "${BENCH_REPORT_JSON}" STREQUAL "")
  set(BENCH_REPORT_JSON "${OUT_DIR}/python-runtime-bench.json")
endif()

set(source_root_py "${source_root}")
string(REPLACE "\\" "\\\\" source_root_py "${source_root_py}")
string(REPLACE "'" "\\'" source_root_py "${source_root_py}")

set(bench_report_json_py "${BENCH_REPORT_JSON}")
string(REPLACE "\\" "\\\\" bench_report_json_py "${bench_report_json_py}")
string(REPLACE "'" "\\'" bench_report_json_py "${bench_report_json_py}")

set(bench_thresholds_json_py "${BENCH_THRESHOLDS_JSON}")
string(REPLACE "\\" "\\\\" bench_thresholds_json_py "${bench_thresholds_json_py}")
string(REPLACE "'" "\\'" bench_thresholds_json_py "${bench_thresholds_json_py}")

set(bench_thresholds_enabled_py "False")
if(BENCH_ENABLE_THRESHOLDS)
  set(bench_thresholds_enabled_py "True")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(bench_configs_py "")
set(generated_packages "")
foreach(spec IN LISTS BENCH_SPECIALIZATIONS)
  if(NOT "${spec}" STREQUAL "portable" AND NOT "${spec}" STREQUAL "fast")
    message(FATAL_ERROR "Invalid BENCH_SPECIALIZATIONS entry: ${spec}")
  endif()

  set(py_package "llvmdsdl_py_bench_${spec}")
  set(py_package_path "${py_package}")
  string(REPLACE "." "/" py_package_path "${py_package_path}")
  set(spec_out "${OUT_DIR}/${spec}")
  set(package_root "${spec_out}/${py_package_path}")

  execute_process(
    COMMAND "${DSDLC}" --target-language python --versioned-type-names
      "${source_root}"
      --outdir "${spec_out}"
      --py-package "${py_package}"
      --py-runtime-specialization "${spec}"
    RESULT_VARIABLE gen_result
    OUTPUT_VARIABLE gen_stdout
    ERROR_VARIABLE gen_stderr
  )
  if(NOT gen_result EQUAL 0)
    message(STATUS "dsdlc python (${spec}) stdout:\n${gen_stdout}")
    message(STATUS "dsdlc python (${spec}) stderr:\n${gen_stderr}")
    message(FATAL_ERROR "Python runtime benchmark generation failed for specialization=${spec}")
  endif()

  if(DEFINED ACCEL_MODULE AND NOT "${ACCEL_MODULE}" STREQUAL "" AND EXISTS "${ACCEL_MODULE}")
    file(COPY "${ACCEL_MODULE}" DESTINATION "${package_root}")
  endif()

  list(APPEND generated_packages "${py_package}")
  string(APPEND bench_configs_py
    "    {\n"
    "        'specialization': '${spec}',\n"
    "        'pythonpath': '${spec_out}',\n"
    "        'package': '${py_package}',\n"
    "    },\n")
endforeach()

set(bench_script "${OUT_DIR}/python_runtime_bench.py")
file(WRITE
  "${bench_script}"
  "from __future__ import annotations\n"
  "\n"
  "import importlib\n"
  "import json\n"
  "import os\n"
  "import sys\n"
  "import time\n"
  "from datetime import datetime, timezone\n"
  "from pathlib import Path\n"
  "\n"
  "BENCH_CONFIGS = [\n${bench_configs_py}]\n"
  "BENCH_ITERATIONS = {\n"
  "    'small': ${BENCH_ITERATIONS_SMALL},\n"
  "    'medium': ${BENCH_ITERATIONS_MEDIUM},\n"
  "    'large': ${BENCH_ITERATIONS_LARGE},\n"
  "}\n"
  "SOURCE_ROOT = '${source_root_py}'\n"
  "REPORT_PATH = Path('${bench_report_json_py}')\n"
  "THRESHOLDS_ENABLED = ${bench_thresholds_enabled_py}\n"
  "THRESHOLDS_PATH = Path('${bench_thresholds_json_py}') if '${bench_thresholds_json_py}' else None\n"
  "\n"
  "def clear_package_modules(prefix: str) -> None:\n"
  "    for name in list(sys.modules):\n"
  "        if name.startswith(prefix):\n"
  "            del sys.modules[name]\n"
  "\n"
  "def _checksum_update(checksum: int, payload: bytes) -> int:\n"
  "    first = payload[0] if payload else 0\n"
  "    last = payload[-1] if payload else 0\n"
  "    value = ((len(payload) << 16) ^ (first << 8) ^ last) & 0xFFFFFFFF\n"
  "    return ((checksum * 16777619) ^ value) & 0xFFFFFFFF\n"
  "\n"
  "def _run_case(case_name, cls, factory, iterations):\n"
  "    # Two clocks over one region. perf_counter is what the thresholds have always used;\n"
  "    # process_time excludes time the process spent off-CPU, which is the difference between\n"
  "    # measuring this code and measuring what else the machine was doing. On a 6x\n"
  "    # oversubscribed box the wall reading inflated 12x while the CPU reading inflated 1.8x.\n"
  "    start = time.perf_counter()\n"
  "    start_cpu = time.process_time()\n"
  "    payload_bytes = 0\n"
  "    checksum = 2166136261\n"
  "    operations = 0\n"
  "    for i in range(iterations):\n"
  "        value = factory(i)\n"
  "        payload = value.serialize()\n"
  "        roundtrip = cls.deserialize(payload).serialize()\n"
  "        payload_bytes += len(roundtrip)\n"
  "        checksum = _checksum_update(checksum, roundtrip)\n"
  "        operations += 1\n"
  "    elapsed = time.perf_counter() - start\n"
  "    cpu = time.process_time() - start_cpu\n"
  "    return {\n"
  "        'name': case_name,\n"
  "        'iterations': iterations,\n"
  "        'elapsedSec': elapsed,\n"
  "        'cpuSec': cpu,\n"
  "        'payloadBytes': payload_bytes,\n"
  "        'operations': operations,\n"
  "        'operationsPerSec': (operations / elapsed) if elapsed > 0.0 else 0.0,\n"
  "        'checksum': checksum,\n"
  "    }\n"
  "\n"
  "def build_case_families(package: str):\n"
  "    # Preferred: uavcan corpus families for realistic small/medium/large payload shape.\n"
  "    try:\n"
  "        heartbeat_mod = importlib.import_module(f'{package}.uavcan.node.heartbeat_1_0')\n"
  "        health_mod = importlib.import_module(f'{package}.uavcan.node.health_1_0')\n"
  "        mode_mod = importlib.import_module(f'{package}.uavcan.node.mode_1_0')\n"
  "        execute_mod = importlib.import_module(f'{package}.uavcan.node.execute_command_1_3')\n"
  "        string_mod = importlib.import_module(f'{package}.uavcan.primitive.string_1_0')\n"
  "        unstructured_mod = importlib.import_module(f'{package}.uavcan.primitive.unstructured_1_0')\n"
  "        natural32_mod = importlib.import_module(f'{package}.uavcan.primitive.array.natural32_1_0')\n"
  "\n"
  "        Heartbeat = heartbeat_mod.Heartbeat_1_0\n"
  "        ExecuteReq = execute_mod.ExecuteCommand_1_3_Request\n"
  "        ExecuteResp = execute_mod.ExecuteCommand_1_3_Response\n"
  "        String = string_mod.String_1_0\n"
  "        Unstructured = unstructured_mod.Unstructured_1_0\n"
  "        Natural32 = natural32_mod.Natural32_1_0\n"
  "\n"
  "        small = [\n"
  "            ('heartbeat', Heartbeat, lambda i: Heartbeat(\n"
  "                uptime=(i & 0xFFFFFFFF),\n"
  "                health=health_mod.Health_1_0(value=0),\n"
  "                mode=mode_mod.Mode_1_0(value=0),\n"
  "                vendor_specific_status_code=(i & 0xFF),\n"
  "            )),\n"
  "        ]\n"
  "\n"
  "        medium = [\n"
  "            ('execute_request', ExecuteReq, lambda i: ExecuteReq(\n"
  "                command=65529,\n"
  "                parameter=[(i + j) & 0xFF for j in range(64)],\n"
  "            )),\n"
  "            ('execute_response', ExecuteResp, lambda i: ExecuteResp(\n"
  "                status=0,\n"
  "                output=[(255 - i - j) & 0xFF for j in range(46)],\n"
  "            )),\n"
  "        ]\n"
  "\n"
  "        large = [\n"
  "            ('string_256', String, lambda i: String(\n"
  "                value=[(i + j) & 0xFF for j in range(256)]\n"
  "            )),\n"
  "            ('unstructured_256', Unstructured, lambda i: Unstructured(\n"
  "                value=[(i * 3 + j) & 0xFF for j in range(256)]\n"
  "            )),\n"
  "            ('natural32_64', Natural32, lambda i: Natural32(\n"
  "                value=[(i + j) & 0xFFFFFFFF for j in range(64)]\n"
  "            )),\n"
  "        ]\n"
  "\n"
  "        return {\n"
  "            'small': small,\n"
  "            'medium': medium,\n"
  "            'large': large,\n"
  "            'source': 'uavcan',\n"
  "        }\n"
  "    except Exception:\n"
  "        # Fallback: fixture corpus families.\n"
  "        type_mod = importlib.import_module(f'{package}.fixtures.vendor.widget_1_0')\n"
  "        helpers_mod = importlib.import_module(f'{package}.fixtures.vendor.helpers_1_0')\n"
  "        union_mod = importlib.import_module(f'{package}.fixtures.vendor.union_tag_1_0')\n"
  "        delim_mod = importlib.import_module(f'{package}.fixtures.vendor.delimited_1_0')\n"
  "        uses_delim_mod = importlib.import_module(f'{package}.fixtures.vendor.uses_delimited_1_0')\n"
  "        empty_service_mod = importlib.import_module(f'{package}.fixtures.vendor.empty_service_1_0')\n"
  "\n"
  "        Type = type_mod.Widget_1_0\n"
  "        Helpers = helpers_mod.Helpers_1_0\n"
  "        UnionTag = union_mod.UnionTag_1_0\n"
  "        Delimited = delim_mod.Delimited_1_0\n"
  "        UsesDelimited = uses_delim_mod.UsesDelimited_1_0\n"
  "        EmptyReq = empty_service_mod.EmptyService_1_0_Request\n"
  "        EmptyResp = empty_service_mod.EmptyService_1_0_Response\n"
  "\n"
  "        return {\n"
  "            'small': [('type', Type, lambda i: Type(foo=i & 0xFF, bar=(i * 13) & 0xFFFF))],\n"
  "            'medium': [\n"
  "                ('helpers', Helpers, lambda i: Helpers(a=(i % 1024) - 512, b=1.25 + (i % 7), c=[(i + j) & 0xFF for j in range(5)])),\n"
  "                ('union', UnionTag, lambda i: UnionTag(_tag=1, second=(i * 19) & 0xFFFF)),\n"
  "            ],\n"
  "            'large': [\n"
  "                ('uses_delimited', UsesDelimited, lambda i: UsesDelimited(nested=Delimited(value=(i * 17) & 0xFF))),\n"
  "                ('service_req', EmptyReq, lambda i: EmptyReq(request_value=i & 0xFF)),\n"
  "                ('service_resp', EmptyResp, lambda _i: EmptyResp()),\n"
  "            ],\n"
  "            'source': 'fixtures',\n"
  "        }\n"
  "\n"
  "def run_mode(config, mode):\n"
  "    package = config['package']\n"
  "    pythonpath = config['pythonpath']\n"
  "    if pythonpath not in sys.path:\n"
  "        sys.path.insert(0, pythonpath)\n"
  "\n"
  "    os.environ['LLVMDSDL_PY_RUNTIME_MODE'] = mode\n"
  "    clear_package_modules(package)\n"
  "\n"
  "    runtime_loader = importlib.import_module(f'{package}._runtime_loader')\n"
  "    case_families = build_case_families(package)\n"
  "\n"
  "    families_report = {}\n"
  "    total_elapsed = 0.0\n"
  "    total_cpu = 0.0\n"
  "    total_payload = 0\n"
  "    total_operations = 0\n"
  "    aggregate_checksum = 2166136261\n"
  "\n"
  "    for family, cases in (('small', case_families['small']), ('medium', case_families['medium']), ('large', case_families['large'])):\n"
  "        iter_count = BENCH_ITERATIONS[family]\n"
  "        family_elapsed = 0.0\n"
  "        family_cpu = 0.0\n"
  "        family_payload = 0\n"
  "        family_operations = 0\n"
  "        family_checksum = 2166136261\n"
  "\n"
  "        case_reports = []\n"
  "        for case_name, cls, factory in cases:\n"
  "            case_report = _run_case(case_name, cls, factory, iter_count)\n"
  "            case_reports.append(case_report)\n"
  "            family_elapsed += case_report['elapsedSec']\n"
  "            family_cpu += case_report['cpuSec']\n"
  "            family_payload += case_report['payloadBytes']\n"
  "            family_operations += case_report['operations']\n"
  "            family_checksum = ((family_checksum * 16777619) ^ case_report['checksum']) & 0xFFFFFFFF\n"
  "\n"
  "        families_report[family] = {\n"
  "            'elapsedSec': family_elapsed,\n"
  "            'cpuSec': family_cpu,\n"
  "            'payloadBytes': family_payload,\n"
  "            'operations': family_operations,\n"
  "            'operationsPerSec': (family_operations / family_elapsed) if family_elapsed > 0.0 else 0.0,\n"
  "            'checksum': family_checksum,\n"
  "            'cases': case_reports,\n"
  "        }\n"
  "\n"
  "        total_elapsed += family_elapsed\n"
  "        total_cpu += family_cpu\n"
  "        total_payload += family_payload\n"
  "        total_operations += family_operations\n"
  "        aggregate_checksum = ((aggregate_checksum * 16777619) ^ family_checksum) & 0xFFFFFFFF\n"
  "\n"
  "    return {\n"
  "        'status': 'ok',\n"
  "        'requestedMode': mode,\n"
  "        'backend': getattr(runtime_loader, 'BACKEND', 'unknown'),\n"
  "        'caseSource': case_families['source'],\n"
  "        'families': families_report,\n"
  "        'totals': {\n"
  "            'elapsedSec': total_elapsed,\n"
  "            'cpuSec': total_cpu,\n"
  "            'payloadBytes': total_payload,\n"
  "            'operations': total_operations,\n"
  "            'operationsPerSec': (total_operations / total_elapsed) if total_elapsed > 0.0 else 0.0,\n"
  "            'checksum': aggregate_checksum,\n"
  "        },\n"
  "    }\n"
  "\n"
  "def safe_run_mode(config, mode):\n"
  "    try:\n"
  "        return run_mode(config, mode)\n"
  "    except RuntimeError as ex:\n"
  "        return {\n"
  "            'status': 'unavailable',\n"
  "            'requestedMode': mode,\n"
  "            'error': str(ex),\n"
  "        }\n"
  "\n"
  "def get_family_metric(report, specialization, mode, family, key='elapsedSec'):\n"
  "    spec = report['specializations'].get(specialization)\n"
  "    if spec is None:\n"
  "        return None\n"
  "    mode_data = spec['modes'].get(mode)\n"
  "    if not mode_data or mode_data.get('status') != 'ok':\n"
  "        return None\n"
  "    family_data = mode_data['families'].get(family)\n"
  "    if family_data is None:\n"
  "        return None\n"
  "    return family_data.get(key)\n"
  "\n"
  "def get_family_elapsed(report, specialization, mode, family):\n"
  "    return get_family_metric(report, specialization, mode, family, 'elapsedSec')\n"
  "\n"
  "def compute_comparisons(report):\n"
  "    comparisons = {\n"
  "        'accelSpeedupRatio': {},\n"
  "        'fastVsPortableRatio': {},\n"
  "    }\n"
  "\n"
  "    for specialization in report['specializations']:\n"
  "        accel_map = {}\n"
  "        for family in ('small', 'medium', 'large'):\n"
  "            pure_elapsed = get_family_elapsed(report, specialization, 'pure', family)\n"
  "            accel_elapsed = get_family_elapsed(report, specialization, 'accel', family)\n"
  "            if pure_elapsed is not None and accel_elapsed is not None and accel_elapsed > 0.0:\n"
  "                accel_map[family] = pure_elapsed / accel_elapsed\n"
  "            else:\n"
  "                accel_map[family] = None\n"
  "        comparisons['accelSpeedupRatio'][specialization] = accel_map\n"
  "\n"
  "    for mode in ('pure', 'accel', 'auto'):\n"
  "        ratio_map = {}\n"
  "        for family in ('small', 'medium', 'large'):\n"
  "            portable_elapsed = get_family_elapsed(report, 'portable', mode, family)\n"
  "            fast_elapsed = get_family_elapsed(report, 'fast', mode, family)\n"
  "            if portable_elapsed is not None and fast_elapsed is not None and fast_elapsed > 0.0:\n"
  "                ratio_map[family] = portable_elapsed / fast_elapsed\n"
  "            else:\n"
  "                ratio_map[family] = None\n"
  "        comparisons['fastVsPortableRatio'][mode] = ratio_map\n"
  "\n"
  "    report['comparisons'] = comparisons\n"
  "\n"
  "def evaluate_thresholds(report):\n"
  "    if not THRESHOLDS_ENABLED:\n"
  "        return {'enabled': False, 'failures': []}\n"
  "\n"
  "    if THRESHOLDS_PATH is None or not THRESHOLDS_PATH.exists():\n"
  "        return {\n"
  "            'enabled': True,\n"
  "            'failures': [\n"
  "                f'thresholds enabled but thresholds file is missing: {THRESHOLDS_PATH}'\n"
  "            ],\n"
  "        }\n"
  "\n"
  "    thresholds = json.loads(THRESHOLDS_PATH.read_text(encoding='utf-8'))\n"
  "    failures = []\n"
  "\n"
  "    for spec, mode_map in thresholds.get('max_elapsed_sec', {}).items():\n"
  "        for mode, family_map in mode_map.items():\n"
  "            for family, limit in family_map.items():\n"
  "                observed = get_family_elapsed(report, spec, mode, family)\n"
  "                if observed is None:\n"
  "                    failures.append(f'missing benchmark sample for max_elapsed_sec[{spec}][{mode}][{family}]')\n"
  "                    continue\n"
  "                if observed > float(limit):\n"
  "                    failures.append(\n"
  "                        f'max_elapsed_sec violation {spec}/{mode}/{family}: observed={observed:.6f}s limit={float(limit):.6f}s'\n"
  "                    )\n"
  "\n"
  "    # process_time rather than perf_counter. Same shape, different clock: this one does not\n"
  "    # count the time the process spent waiting for a CPU, so it survives a machine that is\n"
  "    # busy in a way the wall-clock budget above cannot.\n"
  "    for spec, mode_map in thresholds.get('max_cpu_sec', {}).items():\n"
  "        for mode, family_map in mode_map.items():\n"
  "            for family, limit in family_map.items():\n"
  "                observed = get_family_metric(report, spec, mode, family, 'cpuSec')\n"
  "                if observed is None:\n"
  "                    failures.append(f'missing benchmark sample for max_cpu_sec[{spec}][{mode}][{family}]')\n"
  "                    continue\n"
  "                if observed > float(limit):\n"
  "                    failures.append(\n"
  "                        f'max_cpu_sec violation {spec}/{mode}/{family}: observed={observed:.6f}s limit={float(limit):.6f}s'\n"
  "                    )\n"
  "\n"
  "    for spec, family_map in thresholds.get('min_accel_speedup_ratio', {}).items():\n"
  "        for family, minimum in family_map.items():\n"
  "            ratio = report['comparisons']['accelSpeedupRatio'].get(spec, {}).get(family)\n"
  "            if ratio is None:\n"
  "                failures.append(f'missing accel speedup sample for {spec}/{family}')\n"
  "                continue\n"
  "            if ratio < float(minimum):\n"
  "                failures.append(\n"
  "                    f'min_accel_speedup_ratio violation {spec}/{family}: observed={ratio:.6f} minimum={float(minimum):.6f}'\n"
  "                )\n"
  "\n"
  "    return {'enabled': True, 'path': str(THRESHOLDS_PATH), 'failures': failures}\n"
  "\n"
  "def main() -> int:\n"
  "    report = {\n"
  "        'schemaVersion': 1,\n"
  "        'generatedAtUtc': datetime.now(timezone.utc).isoformat(),\n"
  "        'sourceRoot': SOURCE_ROOT,\n"
  "        'pythonVersion': sys.version,\n"
  "        'iterationsPerFamily': BENCH_ITERATIONS,\n"
  "        'specializations': {},\n"
  "    }\n"
  "\n"
  "    for config in BENCH_CONFIGS:\n"
  "        specialization = config['specialization']\n"
  "        modes = {}\n"
  "        for mode in ('pure', 'accel', 'auto'):\n"
  "            modes[mode] = safe_run_mode(config, mode)\n"
  "        report['specializations'][specialization] = {\n"
  "            'pythonPath': config['pythonpath'],\n"
  "            'package': config['package'],\n"
  "            'modes': modes,\n"
  "        }\n"
  "\n"
  "    compute_comparisons(report)\n"
  "    threshold_eval = evaluate_thresholds(report)\n"
  "    report['thresholdEvaluation'] = threshold_eval\n"
  "\n"
  "    REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)\n"
  "    REPORT_PATH.write_text(json.dumps(report, indent=2, sort_keys=True) + '\\n', encoding='utf-8')\n"
  "\n"
  "    for specialization, spec_data in report['specializations'].items():\n"
  "        for mode, mode_data in spec_data['modes'].items():\n"
  "            if mode_data.get('status') != 'ok':\n"
  "                print(f'python-bench specialization={specialization} mode={mode} status={mode_data.get(\'status\')} detail={mode_data.get(\'error\', \'\')}')\n"
  "                continue\n"
  "            totals = mode_data['totals']\n"
  "            print(\n"
  "                f'python-bench specialization={specialization} mode={mode} backend={mode_data.get(\'backend\')} '\n"
  "                f'elapsed={totals[\'elapsedSec\']:.6f}s ops={totals[\'operations\']} payload_bytes={totals[\'payloadBytes\']} '\n"
  "                f'ops_per_sec={totals[\'operationsPerSec\']:.2f}'\n"
  "            )\n"
  "            for family in ('small', 'medium', 'large'):\n"
  "                family_data = mode_data['families'][family]\n"
  "                print(\n"
  "                    f'  family={family} elapsed={family_data[\'elapsedSec\']:.6f}s ops={family_data[\'operations\']} '\n"
  "                    f'payload_bytes={family_data[\'payloadBytes\']} ops_per_sec={family_data[\'operationsPerSec\']:.2f}'\n"
  "                )\n"
  "\n"
  "    print(f'python-bench report={REPORT_PATH}')\n"
  "\n"
  "    failures = threshold_eval.get('failures', [])\n"
  "    if failures:\n"
  "        print('python-bench threshold failures:')\n"
  "        for failure in failures:\n"
  "            print(f'  - {failure}')\n"
  "        return 2\n"
  "\n"
  "    return 0\n"
  "\n"
  "if __name__ == '__main__':\n"
  "    raise SystemExit(main())\n")

execute_process(
  COMMAND
    "${PYTHON_EXECUTABLE}" "${bench_script}"
  RESULT_VARIABLE bench_result
  OUTPUT_VARIABLE bench_stdout
  ERROR_VARIABLE bench_stderr
)

file(WRITE "${OUT_DIR}/python-runtime-bench.txt" "${bench_stdout}\n${bench_stderr}\n")

if(NOT bench_result EQUAL 0)
  message(STATUS "python bench stdout:\n${bench_stdout}")
  message(STATUS "python bench stderr:\n${bench_stderr}")
  if(EXISTS "${BENCH_REPORT_JSON}")
    message(STATUS "python bench report: ${BENCH_REPORT_JSON}")
  endif()
  message(FATAL_ERROR "Python runtime benchmark execution failed")
endif()

message(STATUS "Python runtime benchmark finished")
message(STATUS "${bench_stdout}")
message(STATUS "Python runtime benchmark report: ${BENCH_REPORT_JSON}")
