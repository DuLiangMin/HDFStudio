"""▶️ 用Python标准库生成绿色播放/暂停位图图标。

输入：本脚本固定图形接口，32×32 RGBA、透明背景、4倍超采样。
输出：resources/images/play_green.png、pause_green.png；仅覆盖这两个生成资源。
用法：python tools/26NOV21_v01_playback_icons.py；无第三方依赖，无网络访问。
"""
from pathlib import Path
import logging
import struct
import sys
import zlib


def inside_triangle(x, y):
    return 9 <= x <= 25 and abs(y - 16) <= (25 - x) * 11 / 16


def inside_pause(x, y):
    return 6 <= y <= 26 and (8 <= x <= 14 or 19 <= x <= 25)


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)


def create_icon(path, shape):
    size = 32
    samples = 4
    rows = bytearray()
    for row in range(size):
        rows.append(0)
        for column in range(size):
            covered = 0
            for offset_y in range(samples):
                for offset_x in range(samples):
                    x = column + (offset_x + 0.5) / samples
                    y = row + (offset_y + 0.5) / samples
                    if shape(x, y):
                        covered += 1
            alpha = round(255 * covered / (samples * samples))
            rows.extend((26, 158, 64, alpha))
    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    image = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b"")
    path.write_bytes(image)
    logging.info("Generated %s %sx%s RGBA %s bytes", path.name, size, size, len(image))


def main():
    root = Path(__file__).resolve().parents[1]
    log = root / "logs/S4-Playback/icons"
    log.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", handlers=[logging.StreamHandler(sys.stdout), logging.FileHandler(log / "generator.log", encoding="utf-8")])
    folder = root / "resources/images"
    create_icon(folder / "play_green.png", inside_triangle)
    create_icon(folder / "pause_green.png", inside_pause)


if __name__ == "__main__":
    main()
