"""🖼️ 二维纹理熵分阶段验收：先CLI逐值对账，再GUI及完整回归。

输入：src/，build/vs2022/CMakeCache.txt；可选HDFSTUDIO_ZOOM_SAMPLE、
HDFSTUDIO_FORECAST_ANCHOR/TARGET（只读），full需原完整回归的6个环境变量。
输出：build/output/{Debug,Release}/，logs/S4-TextureEntropy/<stage>-<时间>/。
用法：python tests/26NOV21_v01_texture_entropy_checks.py，默认--stage core；
--stage gui/full顺序推进，--clean仅用于最终完整重编译。无联网/安装/部署。
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

import netCDF4
import numpy as np


def texture_reference(values):
    """独立NumPy实现：直方图分块，实际块中心插值，缺测保留。"""
    finite = np.isfinite(values)
    if not finite.any():
        return None
    minimum, maximum = values[finite].min(), values[finite].max()
    levels = np.full(values.shape, -1, dtype=np.int32)
    if minimum == maximum:
        levels[finite] = 0
    else:
        with np.errstate(over="ignore"):
            span = maximum - minimum
        if np.isfinite(span):
            fractions = (values[finite] - minimum) / span
        else:
            scale = max(abs(minimum), abs(maximum))
            fractions = (values[finite] / scale - minimum / scale) / (maximum / scale - minimum / scale)
        levels[finite] = np.clip(np.floor(256 * fractions), 0, 255).astype(np.int32)
    rows, cols = values.shape
    blocks = np.full(((rows + 3) // 4, (cols + 3) // 4), np.nan)
    for row in range(blocks.shape[0]):
        for col in range(blocks.shape[1]):
            block = levels[row * 4:row * 4 + 4, col * 4:col * 4 + 4]
            samples = block[block >= 0]
            if samples.size:
                counts = np.bincount(samples, minlength=256)
                probabilities = counts[counts > 0] / samples.size
                blocks[row, col] = -np.sum(probabilities * np.log2(probabilities))
    coordinates = []
    for length in values.shape:
        centers = np.array([(start + min(start + 4, length) - 1) / 2 for start in range(0, length, 4)])
        coordinate = np.interp(np.arange(length), centers, np.arange(len(centers)))
        coordinates.append(coordinate)
    yy, xx = np.meshgrid(*coordinates, indexing="ij")
    y0, x0 = np.floor(yy).astype(int), np.floor(xx).astype(int)
    y1, x1 = np.minimum(y0 + 1, blocks.shape[0] - 1), np.minimum(x0 + 1, blocks.shape[1] - 1)
    fy, fx = yy - y0, xx - x0
    total = np.zeros(values.shape)
    weight_sum = np.zeros(values.shape)
    for by, bx, weights in [(y0, x0, (1 - fy) * (1 - fx)), (y0, x1, (1 - fy) * fx),
                            (y1, x0, fy * (1 - fx)), (y1, x1, fy * fx)]:
        entropy = blocks[by, bx]
        valid = np.isfinite(entropy) & (weights > 0)
        total[valid] += entropy[valid] * weights[valid]
        weight_sum[valid] += weights[valid]
    output = np.full(values.shape, np.nan)
    valid = finite & (weight_sum > 0)
    output[valid] = total[valid] / weight_sum[valid]
    return blocks, output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", choices=["core", "gui", "full"], default="core")
    parser.add_argument("--clean", action="store_true")
    options = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    folder = root / "logs/S4-TextureEntropy" / (options.stage + "-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    folder.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", handlers=[logging.StreamHandler(sys.stdout), logging.FileHandler(folder / "runner.log", encoding="utf-8")])
    cache = (root / "build/vs2022/CMakeCache.txt").read_text(encoding="utf-8")
    cmake = re.search(r"^CMAKE_COMMAND:INTERNAL=(.*)$", cache, re.MULTILINE).group(1)
    environment = os.environ.copy()
    environment.update(QT_QPA_PLATFORM="windows", QT_LOGGING_TO_CONSOLE="1", HDFSTUDIO_TEST_LOG_DIR=str(folder))
    samples = {}
    for key in ["HDFSTUDIO_ZOOM_SAMPLE", "HDFSTUDIO_PACKED_SAMPLE", "HDFSTUDIO_CLASSIC_SAMPLE", "HDFSTUDIO_METRICS_TARGET", "HDFSTUDIO_FORECAST_ANCHOR", "HDFSTUDIO_FORECAST_TARGET"]:
        if environment.get(key):
            path = Path(environment[key])
            samples[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
        elif options.stage == "full":
            raise AssertionError("Set " + key)
    commands, records, summaries = [], [], {}
    fixed_paths = [root / "CMakeLists.txt", root / "tests/drop_targets_test.cpp", *sorted((root / "src").rglob("*"))]
    fixed_inputs = {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest() for path in fixed_paths if path.is_file()}
    (folder / "input-manifest.json").write_text(json.dumps(fixed_inputs, indent=2), encoding="utf-8")

    def run(arguments, name, expected=0):
        completed = subprocess.run(list(map(str, arguments)), cwd=root, env=environment, capture_output=True, timeout=900)
        (folder / (name + ".stdout.txt")).write_bytes(completed.stdout)
        (folder / (name + ".stderr.txt")).write_bytes(completed.stderr)
        commands.append({"name": name, "arguments": list(map(str, arguments)), "exit": completed.returncode})
        (folder / "commands.json").write_text(json.dumps(commands, indent=2), encoding="utf-8")
        assert completed.returncode == expected, (name, completed.returncode, str(folder))
        return completed

    constant = np.full((9, 9), 42.)
    mixed = np.zeros((8, 12))
    mixed[:4, 4:8] = np.arange(16).reshape(4, 4)
    mixed[4:, :4] = np.indices((4, 4)).sum(axis=0) % 2
    mixed[4:, 4:8] = np.nan
    mixed[:, 8:] = 15.
    mixed[0, 0] = np.nan
    random = np.random.default_rng(20261121).normal(size=(19, 23))
    random[3:7, 6:10] = np.nan
    adjacent = (1e308 + np.arange(16) * 2e292).reshape(4, 4)
    extreme = np.tile([-1e308, 1e308], 8).reshape(4, 4)
    fixtures = [("constant", constant), ("mixed", mixed), ("edges", np.arange(35.).reshape(5, 7)),
                ("random", random), ("adjacent", adjacent), ("extreme", extreme), ("tiny", np.array([[3.]])),
                ("missing", np.full((4, 4), np.nan))]
    cases = []
    for name, values in fixtures:
        path = folder / (name + ".nc")
        with netCDF4.Dataset(path, "w") as dataset:
            dataset.createDimension("y", values.shape[0]); dataset.createDimension("x", values.shape[1])
            dataset.createVariable("field", "f8", ("y", "x"))[:] = values
        cases.append((name, path, "field", [], values))
    for key, variable in [("HDFSTUDIO_ZOOM_SAMPLE", environment.get("HDFSTUDIO_ZOOM_VARIABLE", "PRECT")),
                          ("HDFSTUDIO_PACKED_SAMPLE", "precipitation"),
                          ("HDFSTUDIO_FORECAST_ANCHOR", environment.get("HDFSTUDIO_FORECAST_VARIABLE", "TMP_2maboveground")),
                          ("HDFSTUDIO_FORECAST_TARGET", environment.get("HDFSTUDIO_FORECAST_VARIABLE", "TMP_2maboveground"))]:
        if environment.get(key):
            with netCDF4.Dataset(environment[key]) as dataset:
                field = dataset[variable]
                indices = [0] * (field.ndim - 2)
                values = np.ma.filled(field[tuple(indices)], np.nan).astype(float)
            cases.append((key.lower(), Path(environment[key]), variable, indices, values))
    for configuration in ["Debug", "Release"]:
        environment["HDFSTUDIO_TEST_CONFIG"] = configuration
        targets = ["hdf_explorer_drop_tests"] if options.stage == "core" else ["hdf_explorer_drop_tests", "hdf_explorer"]
        build = [cmake, "--build", "build/vs2022", "--config", configuration]
        if options.clean:
            build.append("--clean-first")
        run([*build, "--target", *targets, "--", "/verbosity:quiet"], configuration + "-build")
        binary = root / "build/output" / configuration / "hdf_explorer_drop_tests.exe"
        tests = ["textureEntropyBlocksAndInterpolation", "imageMetricsEntropyAccuracy", "imageMetricsEntropyAndPairAccuracy"]
        if options.stage != "core":
            tests += ["textureEntropyViewIsIndependent2DImage", "textureEntropySnapshotFollowsMaskAndSourceLifetime", "textureEntropyBudgetAndInvalidRecovery"]
        run([binary, *tests], configuration + "-targeted")
        logging.info("%s build and targeted tests passed", configuration)
        cli = binary.with_name("hdf_cli.exe")
        for name, path, variable, indices, values in cases:
            output = folder / ("cli-" + configuration + "-" + name + ".csv")
            reference = texture_reference(values)
            arguments = [cli, "texture-entropy", path, variable, output, *indices]
            run(arguments, configuration + "-" + name, expected=0 if reference is not None else 3)
            if reference is None:
                assert not output.exists()
                continue
            table = np.genfromtxt(output, delimiter=",", names=True)
            blocks, expected = reference
            actual = table["entropy_bits"].reshape(values.shape)
            np.testing.assert_allclose(actual, expected, rtol=0, atol=3e-14, equal_nan=True)
            block_at_pixel = blocks[np.arange(values.shape[0])[:, None] // 4, np.arange(values.shape[1])[None, :] // 4]
            np.testing.assert_allclose(table["block_entropy_bits"].reshape(values.shape), block_at_pixel, rtol=0, atol=3e-14, equal_nan=True)
            digest = hashlib.sha256(output.read_bytes()).hexdigest()
            run(arguments, configuration + "-" + name + "-no-overwrite", expected=3)
            assert hashlib.sha256(output.read_bytes()).hexdigest() == digest
            records.append({"configuration": configuration, "case": name, "shape": list(values.shape), "pixels_checked": values.size, "status": "PASS"})
        if options.stage == "full":
            run([binary, "-o", str(folder / (configuration + "-qt.txt")) + ",txt", "-o", str(folder / (configuration + "-qt.xml")) + ",xml"], configuration + "-full")
            report = ET.parse(folder / (configuration + "-qt.xml")).getroot()
            assert not report.findall(".//Skip") and not report.findall(".//Incident[@type='fail']")
            summaries[configuration] = len(report.findall(".//Incident[@type='pass']"))
            run([Path(cmake).with_name("ctest.exe"), "--test-dir", "build/vs2022", "-C", configuration, "--output-on-failure"], configuration + "-ctest")
        logging.info("%s all texture values reconciled", configuration)
    for path, digest in samples.items():
        assert hashlib.sha256(Path(path).read_bytes()).hexdigest() == digest, path
    sources = {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest() for path in (root / "src").rglob("*") if path.is_file()}
    for name, digest in fixed_inputs.items():
        assert hashlib.sha256((root / name).read_bytes()).hexdigest() == digest, "Source changed while building: " + name
    binaries = {}
    for configuration in ["Debug", "Release"]:
        for name in ["HDFStudio.exe", "hdf_cli.exe", "hdf_explorer_drop_tests.exe"]:
            path = root / "build/output" / configuration / name
            if path.is_file():
                binaries[configuration + "/" + name] = hashlib.sha256(path.read_bytes()).hexdigest()
    result = {"status": "PASS", "stage": options.stage, "qt_pass": summaries, "records": records, "samples": samples, "source": sources, "binaries": binaries, "fixed_inputs": fixed_inputs}
    (folder / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    logging.info("PASS %s", folder)


if __name__ == "__main__":
    main()
