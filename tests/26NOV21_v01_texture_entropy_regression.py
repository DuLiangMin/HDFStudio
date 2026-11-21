"""🖱️ clean构建后的真实桌面回归：二维熵、原分析、绿色联合播放。

输入：HDFSTUDIO_FORECAST_ANCHOR/TARGET只读预报，变量缺省TMP_2maboveground。
输出：logs/S4-TextureEntropy/regression-时间/，各桌面脚本另存截图/控件/原始导出。
用法：python tests/26NOV21_v01_texture_entropy_regression.py
两配置顺序占用桌面，不与其他鼠标测试并行；不构建、不打包、不发布。
"""
import csv
import datetime
import hashlib
import json
import logging
import os
from pathlib import Path
import re
import subprocess
import sys

import netCDF4
import numpy as np


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pair_reference(paths, variable, anchor_index, target_index):
    planes = []
    for path, index in zip(paths, [anchor_index, target_index]):
        with netCDF4.Dataset(path) as dataset:
            planes.append(np.ma.filled(dataset[variable][index], np.nan).astype(float))
    anchor, target = planes
    assert anchor.shape == target.shape
    finite = np.isfinite(anchor) & np.isfinite(target)
    anchor = anchor[finite]; target = target[finite]
    difference = target - anchor
    return {"pearson_r": float(np.corrcoef(anchor, target)[0, 1]),
            "rmse": float(np.sqrt(np.mean(difference ** 2))),
            "mae": float(np.mean(np.abs(difference))),
            "bias_target_minus_anchor": float(np.mean(difference)),
            "valid_pairs": int(finite.sum()), "excluded_pairs": int(finite.size - finite.sum())}


def main():
    root = Path(__file__).resolve().parents[1]
    folder = root / "logs/S4-TextureEntropy" / ("regression-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    folder.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", handlers=[logging.StreamHandler(sys.stdout), logging.FileHandler(folder / "runner.log", encoding="utf-8")])
    environment = os.environ.copy()
    paths = [Path(environment[key]) for key in ["HDFSTUDIO_FORECAST_ANCHOR", "HDFSTUDIO_FORECAST_TARGET"]]
    variable = environment.get("HDFSTUDIO_FORECAST_VARIABLE", "TMP_2maboveground")
    zoom_sample = Path(environment["HDFSTUDIO_ZOOM_SAMPLE"])
    zoom_variable = environment.get("HDFSTUDIO_ZOOM_VARIABLE", "PRECT")
    hashes = {str(path): digest(path) for path in [*paths, zoom_sample]}
    binaries = {str(path.relative_to(root)): digest(path) for configuration in ["Debug", "Release"] for path in [root / "build/output" / configuration / "HDFStudio.exe"]}
    environment.update(HDFSTUDIO_ZOOM_SAMPLE=str(paths[0]), HDFSTUDIO_COMPARE_TARGET=str(paths[1]), HDFSTUDIO_ZOOM_VARIABLE=variable)
    initial = pair_reference(paths, variable, 0, 0)
    common = pair_reference(paths, variable, 1, 0)
    # UI smoke checks use a stable numeric prefix; exported metrics are checked numerically below.
    environment.update(HDFSTUDIO_EXPECTED_R=format(initial["pearson_r"], ".15g")[:12],
                       HDFSTUDIO_EXPECTED_RMSE=format(initial["rmse"], ".15g")[:12],
                       HDFSTUDIO_EXPECTED_COMMON_RMSE=format(common["rmse"], ".15g")[:12])
    report = {"status": "RUNNING", "inputs": hashes, "binaries": binaries, "records": []}

    def run(arguments, label):
        logging.info("Start %s", label)
        completed = subprocess.run(list(map(str, arguments)), cwd=root, env=environment, capture_output=True, timeout=360)
        (folder / (label + ".stdout.txt")).write_bytes(completed.stdout)
        (folder / (label + ".stderr.txt")).write_bytes(completed.stderr)
        assert completed.returncode == 0, (label, completed.returncode, str(folder))

    def save():
        (folder / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")

    save()
    try:
        for configuration in ["Debug", "Release"]:
            for feature, directory in [("original", "S4-TextureEntropy"), ("texture_entropy", "S4-TextureEntropy"), ("metrics", "S4-Metrics-Sync"), ("playback", "S4-Playback")]:
                evidence_root = root / "logs" / directory
                before = set(evidence_root.glob("desktop-" + configuration + "-*"))
                label = configuration + "-" + feature
                script = root / "tests" / ("26NOV21_v01_desktop_regression.ps1" if feature == "original" else "26NOV21_v01_" + feature + "_desktop.ps1")
                arguments = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script, "-Configuration", configuration]
                if feature == "original":
                    environment.update(HDFSTUDIO_ZOOM_SAMPLE=str(zoom_sample), HDFSTUDIO_ZOOM_VARIABLE=zoom_variable)
                    evidence = folder / (configuration + "-original")
                    arguments += ["-LogDirectory", str(evidence.relative_to(root))]
                else:
                    environment.update(HDFSTUDIO_ZOOM_SAMPLE=str(paths[0]), HDFSTUDIO_ZOOM_VARIABLE=variable)
                run(arguments, label)
                if feature != "original":
                    created = set(evidence_root.glob("desktop-" + configuration + "-*")) - before
                    assert len(created) == 1, created
                    evidence = created.pop()
                checks = json.loads((evidence / "results.json").read_text(encoding="utf-8-sig"))
                expected_counts = {"original": 23, "texture_entropy": 24, "metrics": 16, "playback": 21}
                assert len(checks) == expected_counts[feature] and all(check["pass"] for check in checks), (label, checks)
                record = {"configuration": configuration, "feature": feature, "checks": len(checks), "evidence": str(evidence.relative_to(root)), "status": "PASS"}
                if feature == "texture_entropy":
                    run([sys.executable, root / "tests/26NOV21_v01_texture_entropy_reconcile.py", "--folder", evidence], label + "-reconcile")
                    record["png_csv_reconciled"] = True
                elif feature == "metrics":
                    with (evidence / "comparison.csv").open(encoding="utf-8-sig", newline="") as stream:
                        rows = list(csv.DictReader(stream))
                    assert len(rows) == 1
                    actual = rows[0]
                    assert not any("entropy" in name for name in actual)
                    reference = pair_reference(paths, variable, int(actual["anchor_indices"]), int(actual["target_indices"]))
                    for name, value in reference.items():
                        np.testing.assert_allclose(float(actual[name]), value, rtol=2e-12, atol=2e-12)
                    record["comparison_csv_reference"] = reference
                elif feature == "playback":
                    text = (evidence / "final-metrics.txt").read_text(encoding="utf-8-sig")
                    match = re.search(r"Anchor\[(\d+)\] <-> Target\[(\d+)\]", text)
                    assert match
                    reference = pair_reference(paths, variable, int(match[1]), int(match[2]))
                    for name, title in [("pearson_r", "Pearson r"), ("rmse", "RMSE"), ("mae", "MAE"), ("bias_target_minus_anchor", "Bias (Target - Anchor)")]:
                        match_value = re.search(re.escape(title) + r": ([^\s]+)", text)
                        assert match_value
                        np.testing.assert_allclose(float(match_value[1]), reference[name], rtol=2e-12, atol=2e-12)
                    record["played_pair_reference"] = reference
                report["records"].append(record); save()
                logging.info("PASS %s: %s checks and independent exported-data reconciliation", label, len(checks))
        for path, expected in {**hashes, **{str(root / path): value for path, value in binaries.items()}}.items():
            assert digest(Path(path)) == expected, path
        report["status"] = "PASS"; save()
        logging.info("PASS %s", folder)
    except Exception:
        report["status"] = "FAIL"; save()
        logging.exception("Desktop regression failed; preserve evidence")
        raise


if __name__ == "__main__":
    main()
