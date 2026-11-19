"""NetCDF-3 网格对账：HDFStudio 网格表格导出 vs 独立 Python netCDF4 读取。

输入:
  - 只读样本: 环境变量 HDFSTUDIO_CLASSIC_SAMPLE 指向的 NetCDF-3 文件 (含 4 维变量 ttr 与 time)
  - QtTest externalClassicSampleMatchesPython 导出的 HDFStudio/logs/S4-UI-ClassicNetcdf/
      grid-ttr-t7-lev42.tsv   ttr 第 (time=7, lev=42) 个 lat x lon 平面, 单元格为 %g 文本
      grid-time.tsv           time 的 原始值 / yyyymmdd / yyyymmdd hh:mm:ss 三列
输出:
  - 控制台与 HDFStudio/logs/S4-UI-ClassicNetcdf/reconcile.txt, 全部一致时退出码 0

用法（先设置 HDFSTUDIO_CLASSIC_SAMPLE 并运行 externalClassicSampleMatchesPython）:
  python HDFStudio/tests/reconcile_classic_grid.py
"""
import os
import sys
from pathlib import Path

import netCDF4 as nc
import numpy as np

# 业务样本不随仓库发布, 路径只从环境变量取
s_Sample = os.environ.get('HDFSTUDIO_CLASSIC_SAMPLE', '')
s_Log_Dir = Path(__file__).resolve().parents[1] / 'logs' / 'S4-UI-ClassicNetcdf'
i_Time_Index = 7
i_Lev_Index = 42


def format_g(f_Value):
    """与 Qt QString::asprintf('%g') 相同的 6 位有效数字文本。"""
    return '%g' % f_Value


def main():
    if not s_Sample or not os.path.isfile(s_Sample):
        print('Set HDFSTUDIO_CLASSIC_SAMPLE to the NetCDF-3 sample used by externalClassicSampleMatchesPython')
        return 2
    list_Report = []
    b_Ok = True
    with nc.Dataset(s_Sample, 'r') as dataset:
        list_Report.append(f'sample: {s_Sample}  format={dataset.file_format}')
        var_ttr = dataset.variables['ttr']
        var_ttr.set_auto_maskandscale(False)
        arr_Plane = np.asarray(var_ttr[i_Time_Index, i_Lev_Index, :, :], dtype=np.float32)

        list_Grid = [line.split('\t') for line in
                     (s_Log_Dir / 'grid-ttr-t7-lev42.tsv').read_text(encoding='utf-8').splitlines()]
        i_Rows, i_Cols = arr_Plane.shape
        list_Report.append(f'plane shape python=({i_Rows},{i_Cols}) hdfstudio=({len(list_Grid)},{len(list_Grid[0])})')
        if (len(list_Grid), len(list_Grid[0])) != (i_Rows, i_Cols):
            b_Ok = False
        else:
            i_Bad = 0
            for y in range(i_Rows):
                for x in range(i_Cols):
                    if list_Grid[y][x] != format_g(float(arr_Plane[y, x])):
                        i_Bad += 1
                        if i_Bad <= 5:
                            list_Report.append(f'  cell ({y},{x}): python={format_g(float(arr_Plane[y, x]))} hdfstudio={list_Grid[y][x]}')
            list_Report.append(f'ttr[{i_Time_Index},{i_Lev_Index}] cells={i_Rows * i_Cols} mismatched={i_Bad}'
                               f'  min={arr_Plane.min():g} max={arr_Plane.max():g}')
            b_Ok &= i_Bad == 0

        var_time = dataset.variables['time']
        arr_Time = np.asarray(var_time[:], dtype=np.float64)
        s_Units = var_time.units
        list_Time = [line.split('\t') for line in
                     (s_Log_Dir / 'grid-time.tsv').read_text(encoding='utf-8').splitlines()]
        i_Bad = 0
        if len(list_Time) != len(arr_Time):
            i_Bad = max(len(list_Time), len(arr_Time))
        else:
            for i, f_Value in enumerate(arr_Time):
                moment = nc.num2date(float(f_Value), s_Units, calendar='standard')
                list_Expected = [format_g(f_Value) if f_Value != int(f_Value) else str(int(f_Value)),
                                 f'{moment:%Y%m%d}', f'{moment:%Y%m%d %H:%M:%S}']
                if list_Time[i][1:] != list_Expected[1:]:
                    i_Bad += 1
                    if i_Bad <= 5:
                        list_Report.append(f'  time row {i}: python={list_Expected} hdfstudio={list_Time[i]}')
        list_Report.append(f'time rows python={len(arr_Time)} hdfstudio={len(list_Time)} mismatched={i_Bad}  units="{s_Units}"')
        list_Report.append(f'  first: {list_Time[0]}  last: {list_Time[-1]}')
        b_Ok &= i_Bad == 0
    s_Meta = (s_Log_Dir / 'grid-open.txt').read_text(encoding='utf-8').strip().replace('\n', '  ')
    list_Report.append(f'open cost: {s_Meta}')
    list_Report.append('RESULT: ' + ('ALL MATCH' if b_Ok else 'MISMATCH'))
    s_Text = '\n'.join(list_Report)
    print(s_Text)
    (s_Log_Dir / 'reconcile.txt').write_text(s_Text + '\n', encoding='utf-8')
    return 0 if b_Ok else 1


if __name__ == '__main__':
    sys.exit(main())
