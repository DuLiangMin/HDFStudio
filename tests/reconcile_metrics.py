"""🔬 灰度熵/双图独立 NumPy 对账入口。

输入：现有双配置二进制；HDFSTUDIO_ZOOM_SAMPLE、HDFSTUDIO_PACKED_SAMPLE、
HDFSTUDIO_METRICS_TARGET（可选第二个同shape降水文件）。
输出：logs/S4-Metrics-Sync/pair-<timestamp>/，原文件只读。
用法：python tests/reconcile_metrics.py（不重新构建）。
"""
from pathlib import Path
import runpy
import sys

if __name__ == "__main__":
    sys.argv = [sys.argv[0], "--stage", "pair", "--skip-build"]
    runpy.run_path(str(Path(__file__).with_name("26NOV21_v01_metrics_checks.py")), run_name="__main__")
