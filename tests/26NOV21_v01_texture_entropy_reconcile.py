"""🧮 独立核查二维纹理熵GUI/桌面导出的CSV与PNG每个值和像素。

输入：--folder指向QtTest或桌面输出；业务路径仅从HDFSTUDIO_FORECAST_*传入。
输出：同目录texture-reconciliation.json及带时间戳的控制台/文件日志。
用法：python tests/26NOV21_v01_texture_entropy_reconcile.py --folder logs/S4-TextureEntropy/gui-时间
缺省自动选择最新完成gui/full目录；可用同命令检查desktop-目录的原始/切层/Mask导出。
"""
import argparse
import datetime
import hashlib
import json
import logging
import os
from pathlib import Path
import sys

import netCDF4
import numpy as np

from importlib.util import module_from_spec, spec_from_file_location


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--folder", type=Path)
    options = parser.parse_args()
    folder = options.folder
    if folder is None:
        completed = sorted((path.parent for path in (root / "logs/S4-TextureEntropy").glob("*/result.json") if path.parent.name.startswith(("gui-", "full-"))), key=lambda path: path.name.split("-", 1)[1])
        assert completed, "No completed GUI check"
        folder = completed[-1]
    if not folder.is_absolute():
        folder = root / folder
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", handlers=[logging.StreamHandler(sys.stdout), logging.FileHandler(folder / "reconcile.log", encoding="utf-8")])
    reference_spec = spec_from_file_location("texture_reference", root / "tests/26NOV21_v01_texture_entropy_checks.py")
    reference = module_from_spec(reference_spec); reference_spec.loader.exec_module(reference)
    png_spec = spec_from_file_location("png_reader", root / "tests/26NOV21_v01_full_regression.py")
    png_reader = module_from_spec(png_spec); png_spec.loader.exec_module(png_reader)
    palette = json.loads((root / "resources/colormaps/palettes/bids_viridis.json").read_text(encoding="utf-8"))
    positions = np.asarray(palette["positions"], dtype=float)
    colors = np.asarray(palette["colors"], dtype=float)[:, :3]
    cases = []
    if folder.name.startswith("desktop-"):
        summary = json.loads((folder / "summary.json").read_text(encoding="utf-8-sig"))
        assert summary["all_passed"]
        for name, layer, masked in [("independent-layer0", 0, False), ("independent-layer1", 1, False), ("masked-layer1", 1, True)]:
            cases.append((name, Path(summary["source"]), summary["variable"], layer, masked))
    else:
        variable = os.environ.get("HDFSTUDIO_FORECAST_VARIABLE", "TMP_2maboveground")
        for configuration in ["Debug", "Release"]:
            for key in ["HDFSTUDIO_FORECAST_ANCHOR", "HDFSTUDIO_FORECAST_TARGET"]:
                cases.append((configuration + "-" + key, Path(os.environ[key]), variable, 0, False))
    records = []
    for name, path, variable, layer, masked in cases:
        before = hashlib.sha256(path.read_bytes()).hexdigest()
        with netCDF4.Dataset(path) as dataset:
            values = np.ma.filled(dataset[variable][layer], np.nan).astype(float)
        if masked:
            values[(values >= 270) & (values <= 280)] = np.nan
        blocks, expected = reference.texture_reference(values)
        table = np.genfromtxt(folder / (name + ".csv"), delimiter=",", names=True)
        rows, cols = np.indices(values.shape)
        for column, expected_indices in [("row", rows), ("col", cols), ("block_row", rows // 4), ("block_col", cols // 4)]:
            np.testing.assert_array_equal(table[column].reshape(values.shape), expected_indices)
        actual = table["entropy_bits"].reshape(values.shape)
        np.testing.assert_allclose(actual, expected, rtol=0, atol=3e-14, equal_nan=True)
        block_at_pixel = blocks[np.arange(values.shape[0])[:, None] // 4, np.arange(values.shape[1])[None, :] // 4]
        np.testing.assert_allclose(table["block_entropy_bits"].reshape(values.shape), block_at_pixel, rtol=0, atol=3e-14, equal_nan=True)
        rgb = np.full((*values.shape, 3), 110, dtype=np.uint8)
        finite = np.isfinite(expected)
        fractions = expected[finite] / 4
        # Same continuous sRGB palette table; entropy itself is calculated independently above.
        for channel in range(3):
            mapped = np.interp(fractions, positions, colors[:, channel])
            rgb[:, :, channel][finite] = np.floor(np.clip(mapped, 0, 1) * 255 + .5).astype(np.uint8)
        png = png_reader.png_rgb(folder / (name + ".png"))
        np.testing.assert_array_equal(png, rgb)
        assert hashlib.sha256(path.read_bytes()).hexdigest() == before
        records.append({"case": name, "shape": list(values.shape), "values_verified": values.size, "rgb_pixels_verified": values.size, "masked": masked, "status": "PASS"})
        logging.info("%s %s entropy values and RGB pixels match", name, values.size)
    result = {"status": "PASS", "records": records, "source_unchanged": True, "verified_at": datetime.datetime.now().isoformat()}
    (folder / "texture-reconciliation.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    logging.info("PASS %s", folder)


if __name__ == "__main__":
    main()
