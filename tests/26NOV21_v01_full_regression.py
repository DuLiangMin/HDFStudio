"""🧪 固定种子真实样本对账；输入只读，输出位于 logs/S4-FullRegression-20261121。

默认直接运行。可选 --config Debug|Release、--phase before|final、--sample-root PATH。
输入：环境 HDFSTUDIO_SAMPLE_ROOT（默认工作区 no_stand_nc）、可选 HDFSTUDIO_CLASSIC_SAMPLE。
输出：每配置 numeric/<phase>/<config>/，JSON 记录命令、退出码、时间、源 SHA256 与误差。
数值接口：(leading..., H, W) -> float64(H,W)，stored*scale+offset；源缺测变 NaN。
CSV 17 位文本按 atol=1e-14*max(1,|x|)；PNG 尺寸、全部 RGB8 像素精确。
netCDF4-python 与产品 netCDF-C 为跨版本一致性检查；PNG 插值用独立 NumPy 预期。
"""
import argparse
import hashlib
import json
import logging
import math
import os
from pathlib import Path
import random
import re
import struct
import subprocess
import sys
import time
import zlib

import netCDF4
import numpy as np
from reconcile_cli_info import reconcile as reconcile_metadata


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""):
            digest.update(block)
    return digest.hexdigest()


def png_rgb(path):
    """标准 PNG truecolor 解码（CRC、过滤器均验证），无需额外安装 Pillow。"""
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    offset = 8
    compressed = bytearray()
    while offset < len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        assert zlib.crc32(kind + payload) & 0xFFFFFFFF == struct.unpack_from(">I", data, offset + 8 + length)[0]
        if kind == b"IHDR":
            width, height, depth, color, comp, filt, interlace = struct.unpack(">IIBBBBB", payload)
            assert depth == 8 and color in (2, 6) and comp == filt == interlace == 0
        if kind == b"IDAT":
            compressed.extend(payload)
        offset += length + 12
    channels = 3 if color == 2 else 4
    decoded = zlib.decompress(compressed)
    stride = width * channels
    assert len(decoded) == height * (stride + 1)
    image = np.empty((height, stride), dtype=np.uint8)
    for row in range(height):
        start = row * (stride + 1)
        mode = decoded[start]
        scan = decoded[start + 1:start + 1 + stride]
        for col, byte in enumerate(scan):
            left = int(image[row, col - channels]) if col >= channels else 0
            up = int(image[row - 1, col]) if row else 0
            upper_left = int(image[row - 1, col - channels]) if row and col >= channels else 0
            if mode == 0:
                predictor = 0
            elif mode == 1:
                predictor = left
            elif mode == 2:
                predictor = up
            elif mode == 3:
                predictor = (left + up) // 2
            else:
                assert mode == 4
                estimate = left + up - upper_left
                distances = (abs(estimate - left), abs(estimate - up), abs(estimate - upper_left))
                predictor = (left, up, upper_left)[distances.index(min(distances))]
            image[row, col] = (byte + predictor) & 255
    return image.reshape(height, width, channels)[:, :, :3]


def expected_rgb(values):
    finite = np.isfinite(values)
    low = float(np.min(values[finite]))
    high = float(np.max(values[finite]))
    fraction = np.full(values.shape, 0.5)
    if high > low:
        fraction[finite] = np.clip((values[finite] - low) / (high - low), 0, 1)
    nodes = np.array([[68, 1, 84], [59, 82, 139], [33, 145, 140], [94, 201, 98], [253, 231, 37]], dtype=float) / 255
    position = fraction * 4
    index = np.minimum(position.astype(int), 3)
    blend = position - index
    colors = nodes[index] * (1 - blend[:, :, None]) + nodes[index + 1] * blend[:, :, None]
    result = np.floor(colors * 255 + 0.5).astype(np.uint8)
    result[~finite] = (110, 110, 110)
    return result, low, high


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", choices=["Debug", "Release"], default="Release")
    parser.add_argument("--phase", default="final")
    parser.add_argument("--sample-root", type=Path)
    options = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    directory = options.sample_root or Path(os.environ.get("HDFSTUDIO_SAMPLE_ROOT", root.parent / "no_stand_nc"))
    folder = root / "logs/S4-FullRegression-20261121/numeric" / options.phase / options.config
    folder.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s", handlers=[logging.StreamHandler(), logging.FileHandler(folder / "run.log", encoding="utf-8")])
    rng = random.Random(20261121)
    groups = {
        "packed-1e4": sorted(directory.rglob("*NC_i32_1e4_zlib4.nc")),
        "packed-step": sorted(directory.rglob("*NC_i32_step_9p8e4.nc")),
        "float64": sorted(directory.rglob("*NC_f64_zlib4.nc")),
        "float32": sorted(directory.rglob("*NC_f32_zlib9.nc")),
        "nonstandard-time": sorted(directory.glob("tmp2m*.nc")),
    }
    selected = []
    for category, candidates in groups.items():
        for path in rng.sample(candidates, min(2, len(candidates))):
            selected.append((category, path))
    prect = os.environ.get("HDFSTUDIO_ZOOM_SAMPLE", "")
    if prect and Path(prect).is_file():
        selected.append(("PRECT", Path(prect)))
    classic = os.environ.get("HDFSTUDIO_CLASSIC_SAMPLE", "")
    if classic and Path(classic).is_file():
        selected.append(("classic", Path(classic)))
    assert selected, "No samples found"
    results = []
    commands = []
    runtime = root / "build/output" / options.config

    def run(arguments, stem, expected=0):
        started = time.perf_counter()
        result = subprocess.run([str(arg) for arg in arguments], capture_output=True, timeout=90)
        (folder / (stem + ".stdout.txt")).write_bytes(result.stdout)
        (folder / (stem + ".stderr.txt")).write_bytes(result.stderr)
        commands.append({"arguments": [str(arg) for arg in arguments], "exit": result.returncode, "seconds": time.perf_counter() - started, "evidence": stem})
        assert result.returncode == expected, (arguments, result.returncode, result.stderr[-1000:])
        return result.stdout.decode("utf-8", errors="replace")

    for number, (category, path) in enumerate(selected):
        record = {"category": category, "path": str(path), "sha256_before": sha256(path), "status": "FAIL"}
        started = time.perf_counter()
        stem = f"sample-{number:02}"
        try:
            with netCDF4.Dataset(path) as dataset:
                variables = [var for var in dataset.variables.values() if var.ndim >= 2 and np.issubdtype(var.dtype, np.number)]
                assert variables
                variable = variables[0]
                indices = [rng.randrange(size) for size in variable.shape[:-2]]
                index = tuple(indices) + (slice(None), slice(None))
                variable.set_auto_maskandscale(False)
                stored = np.asarray(variable[index])
                values = stored.astype(np.float64)
                missing = ~np.isfinite(values)
                for attr in ["_FillValue", "missing_value"]:
                    if attr in variable.ncattrs():
                        for value in np.asarray(variable.getncattr(attr)).reshape(-1):
                            missing |= stored == value
                scale = float(getattr(variable, "scale_factor", 1))
                offset = float(getattr(variable, "add_offset", 0))
                values = values * scale + offset
                values[missing] = np.nan
                record.update(model=dataset.data_model, variable=variable.name, shape=list(variable.shape), dtype=str(variable.dtype), indices=indices, units=str(getattr(variable, "units", "unspecified")), filters=variable.filters(), scale=scale, offset=offset)
            run([runtime / "hdf_cli.exe", "probe", path], stem + "-probe")
            metadata_lines = []
            assert reconcile_metadata(str(path), runtime / "hdf_cli.exe", metadata_lines), metadata_lines
            (folder / (stem + "-info-reconcile.txt")).write_text("\n".join(metadata_lines), encoding="utf-8")
            record["metadata"] = metadata_lines[-1]
            run([runtime / "hdf_cli.exe", "props", path, "/" + record["variable"]], stem + "-props")
            csv = folder / (stem + ".csv")
            if csv.exists():
                csv.unlink()  # Only this script's previous derived output, never an input.
            run([runtime / "hdf_cli.exe", "csv", path, "/" + record["variable"], csv, *indices], stem + "-csv")
            actual = np.genfromtxt(csv, delimiter=",")
            assert actual.shape == values.shape
            assert np.array_equal(np.isnan(actual), np.isnan(values))
            finite = np.isfinite(values)
            difference = np.abs(actual[finite] - values[finite])
            allowed = 1e-14 * np.maximum(1, np.abs(values[finite]))
            assert np.all(difference <= allowed)
            record["csv"] = {"count": values.size, "missing": int(missing.sum()), "max_abs_error": float(difference.max(initial=0)), "exact": bool(np.array_equal(actual[finite], values[finite]))}
            # Histogram: same read path as the GUI, all values and exact total count.
            hist = run([runtime / "hdf_cli.exe", "hist", path, "/" + record["variable"], "1", "32", *indices], stem + "-hist")
            match = re.search(r"values: (\d+) finite, (\d+) missing\s+sampled (\d+)", hist)
            assert match and tuple(map(int, match.groups())) == (int(finite.sum()), int((~finite).sum()), int(finite.sum()))
            observed_counts = [int(match.group(1)) for match in re.finditer(r"^\s*\d+\s+\[[^,\r\n]+,\s*[^)\r\n]+\)\s+(\d+)", hist, re.MULTILINE)]
            finite_values = values[finite]
            low, high = float(finite_values.min()), float(finite_values.max())
            if high > low:
                bin_index = np.clip(np.floor((finite_values - low) / ((high - low) / 32)).astype(int), 0, 31)
                expected_counts = np.bincount(bin_index, minlength=32).tolist()
            else:
                expected_counts = [int(finite.sum())]
            assert observed_counts == expected_counts, (observed_counts, expected_counts)
            moments = re.search(r"range: \[([^,]+), ([^\]]+)\]\s+mean (\S+)\s+stddev (\S+)", hist)
            assert moments
            actual_moments = list(map(float, moments.groups()))
            expected_moments = [low, high, float(finite_values.mean()), float(finite_values.std(ddof=1))]
            # CLI serializes statistics to 15 significant digits; tolerances cover that text
            # quantization and Welford accumulation (not a per-value decoding tolerance).
            errors = [abs(actual - reference) for actual, reference in zip(actual_moments, expected_moments)]
            for error, reference in zip(errors, expected_moments):
                assert error <= 1e-9 * max(1, abs(reference)), (actual_moments, expected_moments)
            record["histogram"] = {"bins_exact": True, "moments_max_abs_error": max(errors), "tolerance": "1e-9*max(1,abs(reference)); CLI 15 significant digits"}
            # Batch accepts the first leading index. Remaining leading indices default to zero.
            png = folder / (stem + ".png")
            for output in [png, Path(str(png) + ".json")]:
                if output.exists():
                    output.unlink()
            first = indices[0] if indices else 0
            run([runtime / "HDFStudio.exe", "--preview", record["variable"], "--layer", first, "--png", png, path], stem + "-png")
            png_indices = [first] + [0] * (len(indices) - 1) if indices else []
            with netCDF4.Dataset(path) as dataset:
                variable = dataset.variables[record["variable"]]
                variable.set_auto_maskandscale(False)
                raw = np.asarray(variable[tuple(png_indices) + (slice(None), slice(None))])
                image_values = raw.astype(float) * scale + offset
                for attr in ["_FillValue", "missing_value"]:
                    if attr in variable.ncattrs():
                        for fill in np.asarray(variable.getncattr(attr)).reshape(-1):
                            image_values[raw == fill] = np.nan
            expected, low, high = expected_rgb(image_values)
            rgb = png_rgb(png)
            assert rgb.shape == expected.shape
            count = int(np.count_nonzero(np.any(rgb != expected, axis=2)))
            assert count == 0, ("RGB mismatch", count, expected.size // 3)
            metadata = json.loads(Path(str(png) + ".json").read_text(encoding="utf-8"))
            assert metadata["width"] == expected.shape[1] and metadata["height"] == expected.shape[0]
            assert metadata["minimum"] == low and metadata["maximum"] == high
            record["png"] = {"width": rgb.shape[1], "height": rgb.shape[0], "mismatched_pixels": count, "indices": png_indices}
            record["status"] = "PASS"
        except Exception as error:
            record["error"] = repr(error)
            logging.exception("sample failed %s", path)
        record["sha256_after"] = sha256(path)
        assert record["sha256_before"] == record["sha256_after"], "Input changed"
        record["seconds"] = time.perf_counter() - started
        results.append(record)
        logging.info("%s %s %s", record["status"], category, path.name)
        (folder / "results.json").write_text(json.dumps({"seed": 20261121, "netcdf_python": netCDF4.__version__, "netcdf_c_python": netCDF4.__netcdf4libversion__, "hdf5_python": netCDF4.__hdf5libversion__, "results": results, "commands": commands}, indent=2), encoding="utf-8")
    logging.info("PASS %s / %s", sum(item["status"] == "PASS" for item in results), len(results))
    return 0 if all(item["status"] == "PASS" for item in results) else 1


if __name__ == "__main__":
    sys.exit(main())
