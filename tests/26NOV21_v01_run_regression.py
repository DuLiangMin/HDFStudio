"""🔁 本轮 Debug/Release 构建、QtTest、CTest 与时序重复。

输入：既有 build/vs2022，HDFSTUDIO_CLASSIC_SAMPLE、HDFSTUDIO_PACKED_SAMPLE 必须显式设置。
输出：logs/S4-FullRegression-20261121/final 和 timing，UTF-8 日志/JSON/XML/失败截图。
默认直接运行；--skip-build 使用已完成构建；--config Debug|Release 限定单配置。
不与源码写入或真实桌面自动化并行运行。每个子进程检查退出码，设超时并等待退出。
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import logging
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET


class MemoryCounters(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD), ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t), ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t), ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t), ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t), ("PrivateUsage", ctypes.c_size_t)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--config", choices=["Debug", "Release"])
    options = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    folder = root / "logs/S4-FullRegression-20261121/final"
    folder.mkdir(parents=True, exist_ok=True)
    timing = folder.parent / "timing"
    timing.mkdir(exist_ok=True)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", handlers=[logging.StreamHandler(sys.stdout), logging.FileHandler(folder / "runner.log", encoding="utf-8")])
    environment = os.environ.copy()
    for name in ["HDFSTUDIO_CLASSIC_SAMPLE", "HDFSTUDIO_PACKED_SAMPLE"]:
        assert environment.get(name) and Path(environment[name]).is_file(), f"Set {name}"
    environment["QT_QPA_PLATFORM"] = "windows"
    environment["QT_DEBUG_PLUGINS"] = "1"
    environment["HDFSTUDIO_TEST_LOG_DIR"] = str(folder)
    cache = (root / "build/vs2022/CMakeCache.txt").read_text(encoding="utf-8")
    cmake = re.search(r"^CMAKE_COMMAND:INTERNAL=(.*)$", cache, re.MULTILINE).group(1)
    ctest = str(Path(cmake).with_name("ctest.exe"))
    records = []
    psapi = ctypes.WinDLL("psapi")
    psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(MemoryCounters), wintypes.DWORD]
    psapi.GetProcessMemoryInfo.restype = wintypes.BOOL

    def run(arguments, output, limit):
        start = time.perf_counter()
        with output.open("wb") as stream:
            child = subprocess.Popen([str(item) for item in arguments], stdout=stream, stderr=subprocess.STDOUT, cwd=root, env=environment)
            peak = 0
            while child.poll() is None:
                memory = MemoryCounters()
                memory.cb = ctypes.sizeof(memory)
                if psapi.GetProcessMemoryInfo(wintypes.HANDLE(int(child._handle)), ctypes.byref(memory), memory.cb):
                    peak = max(peak, int(memory.PeakWorkingSetSize))
                try:
                    child.wait(timeout=0.2)
                except subprocess.TimeoutExpired:
                    if time.perf_counter() - start > limit:
                        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], capture_output=True)
                        child.wait(timeout=10)
                        raise TimeoutError(arguments)
        record = {"command": [str(item) for item in arguments], "exit": child.returncode, "seconds": time.perf_counter() - start, "peak_working_set_bytes": peak, "evidence": str(output.relative_to(root))}
        records.append(record)
        (folder / "commands.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
        logging.info("exit=%s %.2fs peak=%.1fMiB %s", child.returncode, record["seconds"], peak / 1048576, output.name)
        assert child.returncode == 0, f"Failed: {output}"

    configurations = [options.config] if options.config else ["Debug", "Release"]
    for config in configurations:
        environment["HDFSTUDIO_TEST_CONFIG"] = config
        if not options.skip_build:
            run([cmake, "--build", "build/vs2022", "--config", config, "--clean-first", "--target", "hdf_explorer_drop_tests", "hdf_explorer", "--", "/verbosity:quiet"], folder / f"build-{config}.txt", 600)
        runtime = root / "build/output" / config
        xml = folder / f"qt-{config}.xml"
        run([runtime / "hdf_explorer_drop_tests.exe", "-o", str(folder / f"qt-{config}.txt") + ",txt", "-o", str(xml) + ",xml"], folder / f"process-{config}.txt", 600)
        report = ET.parse(xml).getroot()
        incidents = report.findall(".//Incident")
        assert not [incident for incident in incidents if incident.attrib.get("type") == "fail"]
        assert not report.findall(".//Skip"), "Configured full suite unexpectedly skipped a test"
        run([ctest, "--test-dir", "build/vs2022", "-C", config, "--output-on-failure"], folder / f"ctest-{config}.txt", 600)
        # Each repeat is a fresh process, and includes complete-image equality and close safety.
        for repeat in range(1, 4):
            run([runtime / "hdf_explorer_drop_tests.exe", "colorMapLatestAsyncRenderWins", "regressionAsyncLayersMasksAndRecovery", "regressionGuiExportsCurrentFullResolutionRaster", "closeFileRemovesItsViewsAndReleasesTheFile", "regressionBudgetIsReleasedAfterClose", "-o", str(timing / f"qt-{config}-{repeat}.txt") + ",txt"], timing / f"process-{config}-{repeat}.txt", 90)
    identity = {}
    for config in configurations:
        for name in ["HDFStudio.exe", "hdf_cli.exe", "hdf_explorer_drop_tests.exe"]:
            path = root / "build/output" / config / name
            identity[f"{config}/{name}"] = hashlib.sha256(path.read_bytes()).hexdigest()
    (folder / "binaries.json").write_text(json.dumps(identity, indent=2), encoding="utf-8")
    logging.info("Completed all configurations and independent timing repeats")
    return 0


if __name__ == "__main__":
    sys.exit(main())
