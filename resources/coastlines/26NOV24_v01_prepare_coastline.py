"""将固定Basemap l原始多边形转换为海岸折线；只读本地、无需第三方包。

输入：同目录low/{manifest.json,gshhs_l.dat,gshhsmeta_l.dat}，原字节hash固定。
输出：low/coastlines_l.json及derived_manifest.json；拒绝未知接缝结构。
运行：C:\\Python312\\python.exe -X utf8 resources/coastlines/26NOV24_v01_prepare_coastline.py
参数：--check只重建到内存并核对文件，不写入。无参数生成确定性JSON。
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import logging
import struct


def prepare():
    p_root = Path(__file__).resolve().parent / "low"
    d_manifest = json.loads((p_root / "manifest.json").read_text(encoding="utf-8"))
    for s_name, s_hash in (("gshhs_l.dat", "2d27087de6a6c27b07063983fdf23b3cc2f374165c5da204d1d413991f9f4f2c"),
                           ("gshhsmeta_l.dat", next(d_file["sha256"] for d_file in d_manifest["files"] if d_file["path"].endswith("gshhsmeta_l.dat")))):
        if hashlib.sha256((p_root / s_name).read_bytes()).hexdigest() != s_hash:
            raise ValueError("Original file hash mismatch: " + s_name)
    if d_manifest["upstream_commit"] != "6fed00b9b6146d19ffa98fd00a19cbb0ee448141":
        raise ValueError("Unrecognized source commit")
    a_data = (p_root / "gshhs_l.dat").read_bytes()
    d_records = {}
    for s_line in (p_root / "gshhsmeta_l.dat").read_text(encoding="ascii").splitlines():
        l_row = s_line.split()
        if int(l_row[0]) not in (1, 5) or float(l_row[1]) <= 1000:
            continue
        n_offset, n_size = int(l_row[5]), int(l_row[6])
        l_points = list(struct.iter_unpack("<ff", a_data[n_offset:n_offset + n_size]))
        if len(l_points) != int(l_row[2]) or l_points[0] != l_points[-1]:
            raise ValueError("Unexpected original polygon: " + l_row[7])
        d_records[l_row[7]] = (l_row, l_points)
    l_features, l_removed = [], []
    for s_id, (l_row, l_points) in d_records.items():
        d_remove = {}
        if s_id in ("4-E", "4-W"):
            # 固定版本南极结构：前三条边将冰缘经0/180连到南极，仅多边形填充所需。
            if s_id == "4-E":
                b_expected = l_points[0][0] == 180 and l_points[1] == (180, -90) and l_points[2] == (0, -90) and l_points[3][0] == 0
            else:
                b_expected = l_points[0][0] == 0 and l_points[1] == (0, -90) and l_points[2] == (-180, -90) and l_points[3][0] == -180
            if not b_expected:
                raise ValueError("Unknown Antarctic closure")
            d_remove = {0: "pinned Antarctic polygon closure", 1: "pinned Antarctic polygon closure", 2: "pinned Antarctic polygon closure"}
        elif s_id.endswith(("-E", "-W")):
            s_pair = s_id[:-1] + ("W" if s_id.endswith("E") else "E")
            if s_pair not in d_records:
                raise ValueError("Unpaired dateline polygon")
            l_other = d_records[s_pair][1]
            for n_edge in range(len(l_points) - 1):
                t_start, t_end = l_points[n_edge:n_edge + 2]
                if abs(t_start[0]) == 180 and t_start[0] == t_end[0]:
                    b_match = any(abs(t_a[0]) == 180 and t_a[0] == t_b[0] and
                                  t_a[1] == t_end[1] and t_b[1] == t_start[1]
                                  for t_a, t_b in zip(l_other, l_other[1:]))
                    if not b_match:
                        raise ValueError("Dateline edge has no exact reversed paired edge")
                    d_remove[n_edge] = "exact reverse edge in paired " + s_pair
            if not d_remove:
                raise ValueError("Expected pinned dateline cut absent")
        l_parts = []
        if d_remove:
            # Start immediately after a removed edge and walk the original ring exactly once.
            n_edges = len(l_points) - 1
            n_start = (min(d_remove) + 1) % n_edges
            l_part = []
            for n_step in range(n_edges):
                n_edge = (n_start + n_step) % n_edges
                if n_edge in d_remove:
                    if len(l_part) >= 2:
                        l_parts.append({"coordinates": l_part})
                    l_part = []
                    l_removed.append({"source_id": s_id, "edge_index": n_edge, "reason": d_remove[n_edge],
                                      "coordinates": [l_points[n_edge], l_points[n_edge + 1]]})
                else:
                    if not l_part:
                        l_part.append(l_points[n_edge])
                    l_part.append(l_points[n_edge + 1])
            if len(l_part) >= 2:
                l_parts.append({"coordinates": l_part})
        else:
            l_parts = [{"coordinates": l_points}]
        l_features.append({"source_id": s_id, "level": int(l_row[0]), "area_km2": float(l_row[1]), "parts": l_parts})
    if len(l_features) != 302 or len(l_removed) != 12:
        raise ValueError("Pinned selection/removed-edge count changed")
    d_asset = {"schema_version": 1, "dataset": "Basemap v2.0.0 / GSHHG 2.3.6", "resolution": "l",
               "upstream_commit": d_manifest["upstream_commit"], "license": "LGPL-3.0-or-later",
               "coordinate_order": "lon,lat", "units": "degree", "prime_meridian": "Greenwich",
               "area_threshold_km2": 1000, "position_semantics": "angular reference only; no datum transformation",
               "features": l_features, "removed_artificial_edges": l_removed}
    a_asset = (json.dumps(d_asset, ensure_ascii=False, separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")
    d_derived = {"schema_version": 1, "file": "coastlines_l.json", "sha256": hashlib.sha256(a_asset).hexdigest(),
                 "size_bytes": len(a_asset), "features": len(l_features),
                 "vertices": sum(len(d_part["coordinates"]) for d_feature in l_features for d_part in d_feature["parts"]),
                 "parts": sum(len(d_feature["parts"]) for d_feature in l_features), "removed_artificial_edges": len(l_removed),
                 "converter": "../26NOV24_v01_prepare_coastline.py", "source_manifest_sha256": hashlib.sha256((p_root / "manifest.json").read_bytes()).hexdigest(),
                 "modified": True, "changes": "Level 1/5 area >1000; remove paired dateline and pinned Antarctic polygon closure edges; no coordinate simplification"}
    return p_root, a_asset, (json.dumps(d_derived, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


if __name__ == "__main__":
    o_parser = argparse.ArgumentParser(description=__doc__)
    o_parser.add_argument("--check", action="store_true")
    o_args = o_parser.parse_args()
    p_log = Path(__file__).resolve().parents[2] / "logs/PROJ-001"
    p_log.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", handlers=[logging.StreamHandler(), logging.FileHandler(p_log / "coastline-conversion.log", encoding="utf-8")])
    p_root, a_asset, a_manifest = prepare()
    for s_name, a_bytes in (("coastlines_l.json", a_asset), ("derived_manifest.json", a_manifest)):
        if o_args.check:
            if (p_root / s_name).read_bytes() != a_bytes:
                raise ValueError("Derived file differs: " + s_name)
        else:
            (p_root / s_name).write_bytes(a_bytes)
        logging.info("%s %s %s bytes", "Verified" if o_args.check else "Generated", s_name, len(a_bytes))
