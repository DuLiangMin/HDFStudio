"""🧪 分阶段构建与数值对账，默认双配置执行灰度熵。

输入：build/vs2022；真实只读数据经 HDFSTUDIO_ZOOM_SAMPLE / HDFSTUDIO_PACKED_SAMPLE 传入。
输出：logs/S4-Metrics-Sync/<stage>-<timestamp>/（夹具、控制台日志、命令退出码）。
用法：python tests/26NOV21_v01_metrics_checks.py [--stage entropy] [--skip-build]
"""
import argparse
import datetime
import json
import logging
import os
from pathlib import Path
import re
import subprocess
import sys
import netCDF4
import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", default="entropy")
    parser.add_argument("--skip-build", action="store_true")
    options = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    folder = root / "logs/S4-Metrics-Sync" / (options.stage + "-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    folder.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", handlers=[logging.StreamHandler(sys.stdout), logging.FileHandler(folder / "runner.log", encoding="utf-8")])
    environment = os.environ.copy()
    environment["HDFSTUDIO_TEST_LOG_DIR"] = str(folder)
    cache = (root / "build/vs2022/CMakeCache.txt").read_text(encoding="utf-8")
    cmake = re.search(r"^CMAKE_COMMAND:INTERNAL=(.*)$", cache, re.MULTILINE).group(1)
    records = []

    def run(arguments, name, expected=0):
        result = subprocess.run([str(item) for item in arguments], cwd=root, env=environment, capture_output=True, timeout=600)
        (folder / (name + ".out.txt")).write_bytes(result.stdout)
        (folder / (name + ".err.txt")).write_bytes(result.stderr)
        records.append({"arguments": [str(item) for item in arguments], "exit": result.returncode, "expected": expected})
        (folder / "commands.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
        logging.info("%s exit=%s expected=%s", name, result.returncode, expected)
        assert result.returncode == expected, str(folder / name)
        return result.stdout.decode("utf-8", errors="replace")

    fixture = folder / "metrics.nc"
    with netCDF4.Dataset(fixture, "w", format="NETCDF3_CLASSIC") as dataset:
        dataset.createDimension("y", 16)
        dataset.createDimension("x", 16)
        for name, data in [("uniform", np.arange(256).reshape(16, 16)), ("linear", (np.arange(256)*2+3).reshape(16,16)), ("constant", np.full((16, 16), 42)), ("missing", np.full((16, 16), np.nan))]:
            dataset.createVariable(name, "f8", ("y", "x"))[:] = data
        dataset.createDimension("z", 1)
        dataset.createVariable("small", "f8", ("z", "x"))[:] = 1
    cases = [(fixture, "uniform", []), (fixture, "constant", [])]
    for key in ["HDFSTUDIO_ZOOM_SAMPLE", "HDFSTUDIO_PACKED_SAMPLE"]:
        if environment.get(key):
            sample = Path(environment[key])
            with netCDF4.Dataset(sample) as dataset:
                variable = next(item for item in dataset.variables.values() if item.ndim >= 2 and item.size > 100)
                for layer in range(min(variable.shape[0], 2) if variable.ndim > 2 else 1):
                    indices = [0] * (variable.ndim - 2)
                    if indices:
                        indices[0] = layer
                    cases.append((sample, variable.name, indices))
    for config in ["Debug", "Release"]:
        if not options.skip_build:
            run([cmake, "--build", "build/vs2022", "--config", config, "--target", "hdf_explorer_drop_tests", "hdf_explorer", "--", "/verbosity:quiet"], config + "-build")
        runtime = root / "build/output" / config
        run([runtime / "hdf_explorer_drop_tests.exe", "imageMetricsEntropyAccuracy"], config + "-unit")
        if options.stage != "entropy":
            run([runtime / "hdf_explorer_drop_tests.exe", "imageMetricsEntropyAndPairAccuracy"], config + "-pair-unit")
        if options.stage in ["gui", "sync", "panel"]:
            run([runtime / "hdf_explorer_drop_tests.exe", "histogramOrRasterShowsGrayEntropy", "histogramSamplesOneInTwentyAndBinsDecodedValues", "regressionAsyncLayersMasksAndRecovery"], config + "-gui")
        if options.stage in ["sync", "panel"]:
            run([runtime / "hdf_explorer_drop_tests.exe", "workspaceSyncAnchorLagAndOutOfRange", "colorMapLatestAsyncRenderWins", "closeFileRemovesItsViewsAndReleasesTheFile"], config + "-sync")
        if options.stage in ["residual", "panel"]:
            run([runtime / "hdf_explorer_drop_tests.exe", "imageMetricsResidualAccuracy"], config + "-residual")
        if options.stage == "panel":
            run([runtime / "hdf_explorer_drop_tests.exe", "comparePanelComputesLivePairMetricsAndExportsCsv", "regressionBudgetIsReleasedAfterClose", "regressionGuiExportsCurrentFullResolutionRaster"], config + "-panel")
        cli = runtime / "hdf_cli.exe"
        for case, (sample, variable, indices) in enumerate(cases):
            with netCDF4.Dataset(sample) as dataset:
                values = np.ma.filled(dataset[variable][tuple(indices)], np.nan).astype(float).ravel()
            finite = values[np.isfinite(values)]
            if finite.min() == finite.max():
                entropy = 0.0
            else:
                counts = np.histogram(finite, bins=256)[0]
                probabilities = counts[counts > 0] / finite.size
                entropy = float(-np.sum(probabilities * np.log2(probabilities)))
            output = run([cli, "entropy", sample, variable, 256, *indices], f"{config}-entropy-{case}")
            actual = dict(line.split(": ", 1) for line in output.splitlines())
            assert int(actual["valid_count"]) == finite.size
            assert int(actual["missing_count"]) == values.size - finite.size
            for key, reference in [("minimum", finite.min()), ("maximum", finite.max()), ("entropy_bits", entropy), ("normalized_entropy", entropy / 8)]:
                assert abs(float(actual[key]) - reference) <= 1e-12 * max(1, abs(reference)), (sample, variable, key, actual[key], reference)
        for bins in ["1", "65537", "bad"]:
            run([cli, "entropy", fixture, "uniform", bins], config + "-invalid-" + bins, 2)
        run([cli, "entropy", fixture, "missing"], config + "-empty", 3)
        run([cli, "entropy", fixture, "absent"], config + "-absent", 3)
        run([cli, "entropy", fixture, "uniform", 256, -1], config + "-negative", 2)
        if options.stage != "entropy":
            pairs = [(cases[0], (fixture, "linear", [])), (cases[0], cases[1])]
            if len(cases) >= 4:
                pairs.append((cases[2], cases[3]))
            if environment.get("HDFSTUDIO_METRICS_TARGET") and environment.get("HDFSTUDIO_PACKED_SAMPLE"):
                business = []
                for key in ["HDFSTUDIO_PACKED_SAMPLE", "HDFSTUDIO_METRICS_TARGET"]:
                    with netCDF4.Dataset(environment[key]) as dataset:
                        indices = [0] * (dataset["precipitation"].ndim-2)
                    business.append((Path(environment[key]), "precipitation", indices))
                pairs.append(tuple(business))
            for case, (anchor, target) in enumerate(pairs):
                with netCDF4.Dataset(anchor[0]) as dataset:
                    a = np.ma.filled(dataset[anchor[1]][tuple(anchor[2])], np.nan).astype(float)
                with netCDF4.Dataset(target[0]) as dataset:
                    b = np.ma.filled(dataset[target[1]][tuple(target[2])], np.nan).astype(float)
                assert a.shape == b.shape
                valid = np.isfinite(a) & np.isfinite(b)
                av, bv = a[valid], b[valid]
                difference = bv-av
                reference = {"rmse": float(np.sqrt(np.mean(difference**2))), "mae": float(np.mean(np.abs(difference))), "bias": float(np.mean(difference))}
                reference["pearson_r"] = float(np.corrcoef(av,bv)[0,1]) if np.ptp(av)>0 and np.ptp(bv)>0 else float("nan")
                arguments = [cli,"corr",anchor[0],anchor[1],target[0],target[1]]
                if anchor[2]:
                    arguments.extend(["--anchor-idx", ",".join(str(index) for index in anchor[2])])
                if target[2]:
                    arguments.extend(["--target-idx", ",".join(str(index) for index in target[2])])
                output = run(arguments,f"{config}-corr-{case}")
                actual = dict(line.split(": ",1) for line in output.splitlines())
                assert int(actual["valid_pairs"]) == valid.sum()
                assert int(actual["excluded_pairs"]) == a.size-valid.sum()
                for key,value in reference.items():
                    if np.isnan(value):
                        assert np.isnan(float(actual[key]))
                    else:
                        assert abs(float(actual[key])-value) <= 1e-12*max(1,abs(value)), (key,actual[key],value)
            run([cli,"corr",fixture,"uniform",fixture,"small"],config+"-shape",3)
            run([cli,"corr",fixture,"uniform",fixture,"missing"],config+"-no-pairs",3)
            run([cli,"corr",fixture,"uniform",fixture,"uniform","--anchor-idx","-1"],config+"-bad-pair-idx",2)
            run([cli,"corr",fixture,"uniform",fixture,"uniform","--unknown","1"],config+"-unknown",2)
            run([cli,"corr",fixture,"uniform",fixture,"uniform","--target-idx"],config+"-incomplete",2)
    logging.info("PASS %s: all checks in %s", options.stage, folder)


if __name__ == "__main__":
    main()
