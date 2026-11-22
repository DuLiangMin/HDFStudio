"""时间表格对账：HDFStudio 网格表格导出 vs 独立 Python netCDF4 解码。

输入:
  - HDFSTUDIO_FORECAST_ANCHOR/TARGET 指定只读样本的 time 变量（netCDF4 直接读取，不经过 netcdf_report.py）
  - QtTest timeGridShowsDecodedDatesBesideRawValues 导出的 HDFStudio/logs/S4-UI-TimeGrid/grid-<文件名>.tsv
    每行: 原始值(%.15g)<TAB>yyyymmdd<TAB>yyyymmdd hh:mm:ss
输出:
  - 控制台与 HDFStudio/logs/S4-UI-TimeGrid/reconcile.txt：逐文件行数、不一致行数、首尾样例；全部一致时退出码 0

用法（缺省参数即可）:
  C:\\Python312\\python.exe HDFStudio/tests/reconcile_time_grid.py
"""
import sys
import os
from pathlib import Path

import netCDF4 as nc
import numpy as np

s_Project_Root = Path(__file__).resolve().parents[2]
s_Log_Dir = s_Project_Root / 'HDFStudio' / 'logs' / 'S4-UI-TimeGrid'


def python_rows(s_Path):
    """独立解码: 读 units/calendar，原始值按 15 位有效数字格式化，num2date 转日期。"""
    with nc.Dataset(s_Path, 'r') as dataset:
        var_time = dataset.variables['time']
        s_Units = var_time.units
        s_Calendar = getattr(var_time, 'calendar', 'standard')
        arr_Values = np.asarray(var_time[:], dtype=np.float64)
    list_Rows = []
    for f_Value in arr_Values:
        moment = nc.num2date(float(f_Value), s_Units, calendar=s_Calendar)
        list_Rows.append(f'{float(f_Value):.15g}\t{moment:%Y%m%d}\t{moment:%Y%m%d %H:%M:%S}')
    return list_Rows, s_Units, s_Calendar


def main():
    list_Report = []
    b_All_Match = True
    list_Sample_Paths = []
    for s_Key in ['HDFSTUDIO_FORECAST_ANCHOR', 'HDFSTUDIO_FORECAST_TARGET']:
        s_Value = os.environ.get(s_Key, '')
        if not s_Value or not Path(s_Value).is_file():
            raise ValueError(f'Set {s_Key} to an existing read-only sample')
        list_Sample_Paths.append(Path(s_Value))
    for s_Path in list_Sample_Paths:
        s_Name = s_Path.name
        list_Expected, s_Units, s_Calendar = python_rows(s_Path)
        s_Grid = s_Log_Dir / f'grid-{s_Name}.tsv'
        if not s_Grid.is_file():
            list_Report.append(f'{s_Name}: MISSING {s_Grid}')
            b_All_Match = False
            continue
        list_Actual = s_Grid.read_text(encoding='utf-8').splitlines()
        list_Bad = [i for i in range(max(len(list_Expected), len(list_Actual)))
                    if i >= len(list_Expected) or i >= len(list_Actual) or list_Expected[i] != list_Actual[i]]
        b_All_Match &= not list_Bad
        list_Report.append(f'{s_Name}: units="{s_Units}" calendar={s_Calendar}')
        list_Report.append(f'  rows python={len(list_Expected)} hdfstudio={len(list_Actual)} mismatched={len(list_Bad)}')
        list_Report.append(f'  first: {list_Actual[0]}')
        list_Report.append(f'  last:  {list_Actual[-1]}')
        for i in list_Bad[:5]:
            s_Exp = list_Expected[i] if i < len(list_Expected) else '-'
            s_Act = list_Actual[i] if i < len(list_Actual) else '-'
            list_Report.append(f'  row {i}: python={s_Exp} | hdfstudio={s_Act}')
    list_Report.append('RESULT: ' + ('ALL ROWS MATCH' if b_All_Match else 'MISMATCH'))
    s_Text = '\n'.join(list_Report)
    print(s_Text)
    (s_Log_Dir / 'reconcile.txt').write_text(s_Text + '\n', encoding='utf-8')
    return 0 if b_All_Match else 1


if __name__ == '__main__':
    sys.exit(main())
