"""🔬 复核桌面播放暂停后的真实预报指标与时间坐标。

输入：HDFSTUDIO_FORECAST_ANCHOR/TARGET只读样本、可选HDFSTUDIO_FORECAST_VARIABLE；
logs/S4-Playback/desktop-*/results.json及final-metrics.txt（仅选完整通过的轮次）。
输出：logs/S4-Playback/reconcile-<timestamp>/result.json、双通道时间戳日志。
用法：python tests/26NOV21_v01_playback_reconcile.py；不构建、不改数据。
"""
import datetime
import hashlib
import json
import logging
import os
from pathlib import Path
import re
import sys

import netCDF4
import numpy as np


def main():
    root = Path(__file__).resolve().parents[1]
    base = root / "logs/S4-Playback"
    output = base / ("reconcile-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    output.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", handlers=[logging.StreamHandler(sys.stdout), logging.FileHandler(output / "runner.log", encoding="utf-8")])
    paths = [Path(os.environ[key]) for key in ["HDFSTUDIO_FORECAST_ANCHOR", "HDFSTUDIO_FORECAST_TARGET"]]
    hashes = [hashlib.sha256(path.read_bytes()).hexdigest() for path in paths]
    variable = os.environ.get("HDFSTUDIO_FORECAST_VARIABLE", "TMP_2maboveground")
    records = []
    with netCDF4.Dataset(paths[0]) as anchor, netCDF4.Dataset(paths[1]) as target:
        for configuration in ["Debug", "Release"]:
            selected = None
            for folder in sorted(base.glob("desktop-" + configuration + "-*"), reverse=True):
                if not (folder / "results.json").is_file():
                    continue
                checks = json.loads((folder / "results.json").read_text(encoding="utf-8-sig"))
                if checks and all(check["pass"] for check in checks) and checks[-1]["check"] == "Normal exit":
                    selected = folder
                    break
            assert selected is not None, "No completed desktop acceptance for " + configuration
            text = (selected / "final-metrics.txt").read_text(encoding="utf-8-sig")
            pair = re.search(r"Anchor\[(\d+)\] <-> Target\[(\d+)\] \(lag (-?\d+);", text)
            assert pair is not None
            a_index, b_index, lag = map(int, pair.groups())
            assert b_index == a_index + lag
            a_variable, b_variable = anchor[variable], target[variable]
            a_values = np.ma.filled(a_variable[a_index], np.nan).astype(float)
            b_values = np.ma.filled(b_variable[b_index], np.nan).astype(float)
            finite = np.isfinite(a_values) & np.isfinite(b_values)
            a_valid, b_valid = a_values[finite], b_values[finite]
            difference = b_valid - a_valid
            reference = {"Pearson r": float(np.corrcoef(a_valid, b_valid)[0, 1]), "RMSE": float(np.sqrt(np.mean(difference ** 2))), "MAE": float(np.mean(np.abs(difference))), "Bias (Target - Anchor)": float(np.mean(difference))}
            actual = dict(line.split(": ", 1) for line in text.splitlines() if ": " in line)
            for key, value in reference.items():
                assert abs(float(actual[key]) - value) <= 1e-12 * max(1, abs(value)), (configuration, key, actual[key], value)
            assert int(actual["Valid pairs"]) == int(finite.sum())
            assert int(actual["Excluded pairs"]) == int(a_values.size - finite.sum())
            assert actual["Units"] == a_variable.units == b_variable.units
            dates = []
            for dataset, field, index in [(anchor, a_variable, a_index), (target, b_variable, b_index)]:
                time = dataset[field.dimensions[0]]
                dates.append(str(netCDF4.num2date(time[index], time.units, getattr(time, "calendar", "standard"))))
            records.append({"configuration": configuration, "desktop_evidence": selected.name, "anchor_index": a_index, "target_index": b_index, "lag": lag, "coordinate_times": dates, "same_time": dates[0] == dates[1], "valid_pairs": int(finite.sum()), "reference": reference, "status": "PASS"})
            logging.info("%s playback pair [%s]/[%s] metrics and coordinate times verified", configuration, a_index, b_index)
    for path, digest in zip(paths, hashes):
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest
    (output / "result.json").write_text(json.dumps({"status": "PASS", "paths": list(map(str, paths)), "sha256": hashes, "records": records}, indent=2), encoding="utf-8")
    logging.info("PASS %s", output)


if __name__ == "__main__":
    main()
