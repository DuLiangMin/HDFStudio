"""生成离线色表：cmap==0.8.0 + 锁定 NCL 源码，无客户端 Python 依赖。
输入：cmap 包原始节点；首次 --fetch 下载官方 RGB/许可，使用环境代理。
输出：resources/colormaps、licenses/colormaps、tests/testdata 色彩黄金值。
示例：build/colormap-env/Scripts/python tools/26NOV21_v01_export_colormaps.py --fetch
缺省：离线重建并验证本地资源。--check 只验证，不写资源。
"""
import argparse
import hashlib
import json
import logging
import re
import sys
import urllib.request
from pathlib import Path

import cmap
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
RESOURCE = ROOT / "resources/colormaps"
LICENSES = ROOT / "licenses/colormaps"
NCL_COMMIT = "8f9e9476281cc6f6d9d12eaa78729c7003ca24b7"
CMAP_VERSION = "0.8.0"
CMAPS = [
    ("bids:viridis", "Viridis", "General"),
    ("cmocean:rain", "Rain", "Precipitation"),
    ("cmocean:thermal", "Thermal", "Temperature"),
    ("cmocean:balance", "Balance", "Temperature / Circulation"),
    ("cmocean:speed", "Speed", "Circulation"),
    ("cmocean:curl", "Curl", "Circulation"),
    ("cmocean:phase", "Phase", "Circulation"),
    ("crameri:batlow", "Batlow", "General / Circulation"),
]
NCLS = [
    ("precip_11lev", "Precipitation", "sequential", 12),
    ("precip_diff_12lev", "Precipitation", "diverging", 13),
    ("temp_19lev", "Temperature", "diverging", 20),
    ("wind_17lev", "Circulation", "sequential", 18),
    ("BlueWhiteOrangeRed", "Circulation", "diverging", 254),
]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def encode(value):
    return (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def write_or_check(path, data, check):
    if check:
        if not path.is_file() or path.read_bytes() != data:
            raise ValueError(f"Resource differs: {path.relative_to(ROOT)}")
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def download(url, path):
    request = urllib.request.Request(url, headers={"User-Agent": "HDFStudio-colormaps"})
    with urllib.request.urlopen(request, timeout=45) as response:
        data = response.read(2 * 1024 * 1024)
    if not data or b"<html" in data[:100].lower():
        raise ValueError(f"Unexpected download: {url}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    logging.info("Downloaded %s (%d bytes)", path.name, len(data))


def fetch_sources():
    base = f"https://raw.githubusercontent.com/NCAR/ncl/{NCL_COMMIT}/"
    for name, _, _, _ in NCLS:
        download(base + f"ni/src/db/colormaps/{name}.rgb", RESOURCE / "source/ncl" / (name + ".rgb"))
    download(base + "ni/src/db/colormaps/README", RESOURCE / "source/ncl/README.txt")
    download(base + "LICENSE", LICENSES / "NCL-COPYRIGHT.txt")
    # cmap 发布包提供完整 SPDX 文本；REUSE 清单保留具体作者。
    distribution = Path(cmap.__file__).parent.parent / f"cmap-{CMAP_VERSION}.dist-info/licenses/LICENSES"
    for name in ["Apache-2.0", "MIT", "BSD-3-Clause", "CC0-1.0"]:
        LICENSES.mkdir(parents=True, exist_ok=True)
        (LICENSES / (name + ".txt")).write_bytes((distribution / (name + ".txt")).read_bytes())
    download(f"https://raw.githubusercontent.com/pyapp-kit/cmap/v{CMAP_VERSION}/REUSE.toml", LICENSES / "cmap-REUSE.toml")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    log_dir = ROOT / "logs/S4-UI-ColorMap"
    log_dir.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s",
                        handlers=[logging.StreamHandler(), logging.FileHandler(log_dir / "resource-build.log", encoding="utf-8")])
    if cmap.__version__ != CMAP_VERSION:
        raise ValueError(f"Expected cmap {CMAP_VERSION}, got {cmap.__version__}")
    if args.fetch and args.check:
        raise ValueError("--fetch and --check cannot be combined")
    if args.fetch:
        fetch_sources()
    palettes = []
    for upstream_id, name, group in CMAPS:
        color_map = cmap.Colormap(upstream_id)
        info = color_map.info
        source_file = Path(cmap.__file__).parent / "data" / info.namespace / (info.name + ".py")
        if not source_file.is_file():
            source_file = source_file.parent / "__init__.py"
        raw = source_file.read_bytes()
        relative_source = "source/cmap/" + upstream_id.replace(":", "_") + ".py"
        write_or_check(RESOURCE / relative_source, raw, args.check)
        palettes.append(dict(id=upstream_id, name=name, group=group, kind=info.category,
                             stepped=False, colors=[list(map(float, stop.color.rgba)) for stop in color_map.color_stops],
                             positions=[float(stop.position) for stop in color_map.color_stops],
                             source=info.source, authors=info.authors, license="CC0-1.0" if info.license == "CC0" else info.license,
                             revision="cmap==" + CMAP_VERSION, source_file=relative_source,
                             source_sha256=digest(raw), modification="Original cmap color stops; sRGB RGBA, no decimation"))
    for name, group, kind, expected in NCLS:
        relative_source = f"source/ncl/{name}.rgb"
        raw = (RESOURCE / relative_source).read_bytes()
        text = raw.decode("utf-8")
        declared = int(re.search(r"ncolors?\s*=\s*(\d+)", text).group(1))
        rows = []
        for line in text.splitlines():
            line = line.split("#", 1)[0].strip()
            if not line or "=" in line:
                continue
            rgb = [float(value) for value in line.split()]
            if len(rgb) != 3 or not all(0 <= value <= 255 for value in rgb):
                raise ValueError(f"Invalid RGB row in {name}")
            rows.append([value / 255 for value in rgb] + [1.0])
        if len(rows) != declared or declared != expected:
            raise ValueError(f"NCL count mismatch: {name}")
        palettes.append(dict(id="ncl:" + name, name=name + " (NCL)", group=group, kind=kind,
                             stepped=expected < 254, colors=rows, positions=np.linspace(0, 1, expected).tolist(),
                             source=f"https://github.com/NCAR/ncl/blob/{NCL_COMMIT}/ni/src/db/colormaps/{name}.rgb",
                             authors=["UCAR / NCL"] + (["MeteoSwiss NCL library (original file attribution)"] if "MeteoSwiss" in text else []),
                             license="Apache-2.0", revision=NCL_COMMIT, source_file=relative_source,
                             source_sha256=digest(raw), modification="Original RGB rows retained, normalized from 0..255 to sRGB RGBA 0..1"))
    manifest = dict(schema_version=1, generator="cmap==" + CMAP_VERSION, ncl_commit=NCL_COMMIT, palettes=[], licenses=[])
    golden = []
    qrc = ['<RCC><qresource prefix="/colormaps">']
    for palette in palettes:
        palette["schema_version"] = 1
        filename = "palettes/" + palette["id"].replace(":", "_") + ".json"
        data = encode(palette)
        write_or_check(RESOURCE / filename, data, args.check)
        manifest["palettes"].append(dict(id=palette["id"], file=filename, sha256=digest(data), count=len(palette["colors"])))
        qrc.append(f'<file alias="{filename}">colormaps/{filename}</file>')
        colors = np.asarray(palette["colors"])
        positions = np.asarray(palette["positions"])
        samples = []
        for fraction in [0, .001, .13, .37, .5, .73, .999, 1]:
            rgba = [np.interp(fraction, positions, colors[:, channel]) for channel in range(4)]
            samples.append(dict(fraction=fraction, rgba8=np.floor(np.array(rgba) * 255 + .5).astype(int).tolist()))
        golden.append(dict(id=palette["id"], samples=samples))
        logging.info("Validated %s: %d colors, %s", palette["id"], len(colors), palette["license"])
    for path in sorted(LICENSES.iterdir()):
        if path.is_file():
            manifest["licenses"].append(dict(file=path.name, sha256=digest(path.read_bytes())))
            qrc.append(f'<file alias="licenses/{path.name}">../licenses/colormaps/{path.name}</file>')
    write_or_check(RESOURCE / "manifest.json", encode(manifest), args.check)
    qrc.append('<file alias="manifest.json">colormaps/manifest.json</file>')
    write_or_check(RESOURCE / "golden.json", encode(golden), args.check)
    qrc.append('<file alias="golden.json">colormaps/golden.json</file>')
    qrc.append('</qresource></RCC>')
    write_or_check(ROOT / "resources/colormaps.qrc", ("\n".join(qrc) + "\n").encode(), args.check)
    logging.info("%s %d palettes", "Verified" if args.check else "Generated", len(palettes))


if __name__ == "__main__":
    try:
        main()
    except Exception:
        logging.exception("Color-map resource build failed")
        sys.exit(1)
