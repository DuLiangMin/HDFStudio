"""🔁 分析/同步最终双配置验收，不覆盖历史证据。

输入：src/、build/vs2022；只读环境变量HDFSTUDIO_CLASSIC_SAMPLE、
HDFSTUDIO_PACKED_SAMPLE、HDFSTUDIO_ZOOM_SAMPLE、HDFSTUDIO_METRICS_TARGET、
HDFSTUDIO_FORECAST_ANCHOR、HDFSTUDIO_FORECAST_TARGET。
输出：logs/S4-Metrics-Sync/final-<timestamp>，构建日志、QtTest/XML、CTest、时序重复和哈希。
用法：python tests/26NOV21_v01_metrics_final.py [--skip-build]
"""
import argparse
import datetime
import hashlib
import json
import logging
import os
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-build", action="store_true")
    options = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    folder = root / "logs/S4-Metrics-Sync" / ("final-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    folder.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", handlers=[logging.StreamHandler(sys.stdout), logging.FileHandler(folder / "runner.log", encoding="utf-8")])
    environment = os.environ.copy()
    environment["QT_QPA_PLATFORM"] = "windows"
    environment["QT_DEBUG_PLUGINS"] = "1"
    environment["QT_LOGGING_TO_CONSOLE"] = "1"
    environment["HDFSTUDIO_TEST_LOG_DIR"] = str(folder)
    samples = {}
    for key in ["HDFSTUDIO_CLASSIC_SAMPLE", "HDFSTUDIO_PACKED_SAMPLE", "HDFSTUDIO_ZOOM_SAMPLE", "HDFSTUDIO_METRICS_TARGET", "HDFSTUDIO_FORECAST_ANCHOR", "HDFSTUDIO_FORECAST_TARGET"]:
        assert environment.get(key) and Path(environment[key]).is_file(), "Set " + key
        samples[environment[key]] = hashlib.sha256(Path(environment[key]).read_bytes()).hexdigest()
    cache = (root / "build/vs2022/CMakeCache.txt").read_text(encoding="utf-8")
    cmake = re.search(r"^CMAKE_COMMAND:INTERNAL=(.*)$", cache, re.MULTILINE).group(1)
    ctest = Path(cmake).with_name("ctest.exe")
    records = []

    def run(arguments, name):
        with (folder / (name+".txt")).open("wb") as output:
            result = subprocess.run([str(item) for item in arguments], cwd=root, env=environment, stdout=output, stderr=subprocess.STDOUT, timeout=900)
        records.append({"name": name, "arguments": [str(item) for item in arguments], "exit": result.returncode})
        (folder / "commands.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
        logging.info("%s exit=%s", name, result.returncode)
        assert result.returncode == 0, str(folder / (name+".txt"))

    summaries = {}
    run_identity = {"samples_before":samples,"started":datetime.datetime.now().isoformat(),"status":"running"}
    (folder / "run.json").write_text(json.dumps(run_identity,indent=2),encoding="utf-8")
    for config in ["Debug", "Release"]:
        environment["HDFSTUDIO_TEST_CONFIG"] = config
        if not options.skip_build:
            run([cmake,"--build","build/vs2022","--config",config,"--clean-first","--target","hdf_explorer_drop_tests","hdf_explorer","--","/verbosity:quiet"], config+"-build")
        runtime = root / "build/output" / config
        xml = folder / (config+"-qt.xml")
        run([runtime / "hdf_explorer_drop_tests.exe", "-o", str(folder / (config+"-qt.txt"))+",txt", "-o",str(xml)+",xml"], config+"-process")
        report = ET.parse(xml).getroot()
        incidents = report.findall(".//Incident")
        summary = {}
        for incident in incidents:
            kind = incident.attrib["type"]
            summary[kind] = summary.get(kind,0)+1
        assert not summary.get("fail") and not report.findall(".//Skip"), summary
        summaries[config] = summary
        run([ctest,"--test-dir","build/vs2022","-C",config,"--output-on-failure"], config+"-ctest")
        for repeat in range(1,4):
            run([runtime / "hdf_explorer_drop_tests.exe", "workspaceSyncAnchorLagAndOutOfRange", "comparePanelComputesLivePairMetricsAndExportsCsv", "colorMapLatestAsyncRenderWins", "regressionAsyncLayersMasksAndRecovery", "closeFileRemovesItsViewsAndReleasesTheFile"], f"{config}-timing-{repeat}")
    identity = {}
    for config in ["Debug", "Release"]:
        for name in ["HDFStudio.exe", "hdf_cli.exe", "hdf_explorer_drop_tests.exe"]:
            identity[config+"/"+name] = hashlib.sha256((root / "build/output" / config / name).read_bytes()).hexdigest()
    for path,digest in samples.items():
        assert hashlib.sha256(Path(path).read_bytes()).hexdigest() == digest, path
    source = {}
    for path in sorted((root / "src").rglob("*")):
        if path.is_file():
            source[str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
    (folder / "result.json").write_text(json.dumps({"qt":summaries,"binaries":identity,"samples":samples,"source":source},indent=2),encoding="utf-8")
    run_identity["status"]="PASS"
    (folder / "run.json").write_text(json.dumps(run_identity,indent=2),encoding="utf-8")
    logging.info("PASS final validation %s %s", summaries, folder)


if __name__ == "__main__":
    main()
