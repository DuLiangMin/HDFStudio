"""▶️ 分步验收主导航联动与逐层播放。

输入：src/、tests/drop_targets_test.cpp、build/vs2022/CMakeCache.txt；
full阶段要求HDFSTUDIO_CLASSIC_SAMPLE/PACKED_SAMPLE/ZOOM_SAMPLE、
HDFSTUDIO_METRICS_TARGET、HDFSTUDIO_FORECAST_ANCHOR/TARGET只读样本。
输出：build/output/{Debug,Release}、logs/S4-Playback/<stage>-<timestamp>/。
用法：python tests/26NOV21_v01_playback_checks.py --stage navigation
可选--stage playback或full；先专项通过再进入后续阶段，不修改源文件。
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
    parser.add_argument("--stage", choices=["navigation", "playback", "full"], default="navigation")
    parser.add_argument("--clean", action="store_true", help="Clean each configuration before the final build")
    options = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    folder = root / "logs/S4-Playback" / (options.stage + "-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    folder.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", handlers=[logging.StreamHandler(sys.stdout), logging.FileHandler(folder / "runner.log", encoding="utf-8")])
    cache = (root / "build/vs2022/CMakeCache.txt").read_text(encoding="utf-8")
    cmake = re.search(r"^CMAKE_COMMAND:INTERNAL=(.*)$", cache, re.MULTILINE).group(1)
    environment = os.environ.copy()
    environment["QT_QPA_PLATFORM"] = "windows"
    environment["QT_LOGGING_TO_CONSOLE"] = "1"
    environment["HDFSTUDIO_TEST_LOG_DIR"] = str(folder)
    samples = {}
    if options.stage == "full":
        for key in ["HDFSTUDIO_CLASSIC_SAMPLE", "HDFSTUDIO_PACKED_SAMPLE", "HDFSTUDIO_ZOOM_SAMPLE", "HDFSTUDIO_METRICS_TARGET", "HDFSTUDIO_FORECAST_ANCHOR", "HDFSTUDIO_FORECAST_TARGET"]:
            assert environment.get(key) and Path(environment[key]).is_file(), "Set " + key
            samples[environment[key]] = hashlib.sha256(Path(environment[key]).read_bytes()).hexdigest()
    commands = []
    summaries = {}

    def run(arguments, name):
        with (folder / (name + ".txt")).open("wb") as output:
            result = subprocess.run([str(value) for value in arguments], cwd=root, env=environment, stdout=output, stderr=subprocess.STDOUT, timeout=900)
        commands.append({"name": name, "command": [str(value) for value in arguments], "exit": result.returncode})
        (folder / "commands.json").write_text(json.dumps(commands, indent=2), encoding="utf-8")
        logging.info("%s exit=%s", name, result.returncode)
        assert result.returncode == 0, str(folder / (name + ".txt"))

    for configuration in ["Debug", "Release"]:
        environment["HDFSTUDIO_TEST_CONFIG"] = configuration
        targets = ["hdf_explorer_drop_tests"]
        if options.stage == "full":
            targets.append("hdf_explorer")
        build = [cmake, "--build", "build/vs2022", "--config", configuration]
        if options.clean:
            build.append("--clean-first")
        run([*build, "--target", *targets, "--", "/verbosity:quiet"], configuration + "-build")
        binary = root / "build/output" / configuration / "hdf_explorer_drop_tests.exe"
        tests = ["linkedLayerToolbarNavigatesAnchorFromEitherImage", "mainToolbarStepsLayersOfTheCurrentView", "workspaceSyncAnchorLagAndOutOfRange", "comparePanelComputesLivePairMetricsAndExportsCsv"]
        if options.stage != "navigation":
            tests.extend(["layerPlaybackSingleAndLinkedPlanes", "layerPlaybackStopsOnContextChanges", "layerPlaybackFrameCountBoundaries"])
        run([binary, *tests], configuration + "-targeted")
        if options.stage == "full":
            run([binary, "-o", str(folder / (configuration + "-qt.txt")) + ",txt", "-o", str(folder / (configuration + "-qt.xml")) + ",xml"], configuration + "-full")
            report = ET.parse(folder / (configuration + "-qt.xml")).getroot()
            assert not report.findall(".//Skip") and not report.findall(".//Incident[@type='fail']")
            summaries[configuration] = len(report.findall(".//Incident[@type='pass']"))
            run([Path(cmake).with_name("ctest.exe"), "--test-dir", "build/vs2022", "-C", configuration, "--output-on-failure"], configuration + "-ctest")
    for path,digest in samples.items():
        assert hashlib.sha256(Path(path).read_bytes()).hexdigest() == digest, path
    source_hashes = {}
    binary_hashes = {}
    if options.stage == "full":
        for path in sorted((root / "src").rglob("*")):
            if path.is_file():
                source_hashes[str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
        for configuration in ["Debug", "Release"]:
            for name in ["HDFStudio.exe", "hdf_cli.exe", "hdf_explorer_drop_tests.exe"]:
                binary_hashes[configuration + "/" + name] = hashlib.sha256((root / "build/output" / configuration / name).read_bytes()).hexdigest()
    (folder / "result.json").write_text(json.dumps({"stage": options.stage, "status": "PASS", "commands": len(commands), "qt_pass": summaries, "samples": samples, "source": source_hashes, "binaries": binary_hashes}, indent=2), encoding="utf-8")
    logging.info("PASS %s", folder)


if __name__ == "__main__":
    main()
