"""🎬 修正系统GIF编码器忽略的帧延迟，并验证README实录动画。

输入/输出：docs/demo-analysis.gif；原始帧位于logs/S4-TextureEntropy/demo-*/。
缺省直接运行：python tools/26NOV21_v01_finalize_demo.py，无第三方依赖。
按GIF块结构解析，不在压缩数据中搜索控制字节；20帧，每帧650毫秒，永久循环。
"""
from pathlib import Path
import hashlib
import json
import struct


def finalize(path):
    data = bytearray(path.read_bytes())
    assert data[:6] in (b"GIF87a", b"GIF89a")
    position = 13
    if data[10] & 128:
        position += 3 * 2 ** ((data[10] & 7) + 1)
    frames = 0
    controls = 0
    loop = False
    while position < len(data):
        kind = data[position]; position += 1
        if kind == 0x3b:
            break
        if kind == 0x21:
            label = data[position]; position += 1
            if label == 0xf9:
                assert data[position] == 4
                struct.pack_into("<H", data, position + 2, 65)
                controls += 1
            chunks = []
            while True:
                size = data[position]; position += 1
                if not size:
                    break
                chunks.append(bytes(data[position:position + size])); position += size
            if label == 0xff and chunks[:1] == [b"NETSCAPE2.0"]:
                assert chunks[1] == b"\x01\x00\x00"
                loop = True
        elif kind == 0x2c:
            flags = data[position + 8]; position += 9
            if flags & 128:
                position += 3 * 2 ** ((flags & 7) + 1)
            position += 1
            while True:
                size = data[position]; position += 1
                if not size:
                    break
                position += size
            frames += 1
        else:
            raise AssertionError((kind, position))
    assert frames == controls == 20 and loop and position == len(data)
    path.write_bytes(data)
    return {"frames": frames, "duration_seconds": frames * .65, "loop": loop,
            "dimensions": list(struct.unpack_from("<HH", data, 6)),
            "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    print(json.dumps(finalize(root / "docs/demo-analysis.gif"), indent=2))
