"""🖼️ 真实预报GUI残差导出全值/全像素对账。

输入：HDFSTUDIO_FORECAST_ANCHOR/TARGET只读文件、HDFSTUDIO_FORECAST_RESULT_DIR
下QtTest保存的forecast-<config>-<layer>.csv/.png，变量默认TMP_2maboveground。
输出：同目录forecast-residual-reconciliation.json和控制台时间戳日志；不改源数据。
用法：python tests/26NOV21_v01_forecast_residual.py
"""
import datetime
import importlib.util
import json
import os
from pathlib import Path
import netCDF4
import numpy as np


def main():
    root = Path(__file__).resolve().parents[1]
    spec = importlib.util.spec_from_file_location("existing_png_reader",root/"tests/26NOV21_v01_full_regression.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    folder = Path(os.environ["HDFSTUDIO_FORECAST_RESULT_DIR"])
    variable = os.environ.get("HDFSTUDIO_FORECAST_VARIABLE","TMP_2maboveground")
    palette = json.loads((root/"resources/colormaps/palettes/ncl_BlueWhiteOrangeRed.json").read_text(encoding="utf-8"))
    records = []
    with netCDF4.Dataset(os.environ["HDFSTUDIO_FORECAST_ANCHOR"]) as a,netCDF4.Dataset(os.environ["HDFSTUDIO_FORECAST_TARGET"]) as b:
        for config in ["Debug","Release"]:
            for index in [1,91,180]:
                stem = folder/f"forecast-{config}-{index}"
                actual = np.loadtxt(str(stem)+".csv",delimiter=",")
                av = np.ma.filled(a[variable][index],np.nan).astype(float)
                bv = np.ma.filled(b[variable][index-1],np.nan).astype(float)
                expected = bv-av
                np.testing.assert_array_equal(actual,expected)
                image = module.png_rgb(Path(str(stem)+".png"))
                assert image.shape == (*expected.shape,3)
                nodes = np.asarray(palette["colors"],dtype=float)[:,:3]
                positions = np.asarray(palette["positions"],dtype=float)
                finite = np.isfinite(expected)
                extent = max(abs(expected[finite].min()),abs(expected[finite].max())) or 1
                fraction = np.full(expected.shape,.5)
                fraction[finite] = np.clip(.5+expected[finite]/extent/2,0,1)
                colors = np.stack([np.interp(fraction,positions,nodes[:,channel]) for channel in range(3)],axis=-1)
                expected_image = np.floor(colors*255+.5).astype(np.uint8)
                expected_image[~finite]=(110,110,110)
                np.testing.assert_array_equal(image,expected_image)
                records.append({"configuration":config,"anchor_index":index,"target_index":index-1,"values":int(expected.size),"pixels":int(expected.size),"status":"PASS"})
                print(datetime.datetime.now().isoformat(),config,index,"all residual values / RGB pixels PASS")
    (folder/"forecast-residual-reconciliation.json").write_text(json.dumps(records,indent=2),encoding="utf-8")


if __name__ == "__main__":
    main()
