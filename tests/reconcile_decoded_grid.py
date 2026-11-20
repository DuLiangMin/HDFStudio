"""解码值 / 存储值双表格对账：HDFStudio 表格导出 vs 独立 Python netCDF4。

输入:
  - 只读样本: 环境变量 HDFSTUDIO_PACKED_SAMPLE 指向含 scale_factor/add_offset 的 NetCDF 文件
  - 变量名: 环境变量 HDFSTUDIO_PACKED_VARIABLE, 缺省 precipitation
  - QtTest externalPackedSampleMatchesPython 导出的 HDFStudio/logs/S4-UI-DecodedGrid/
      grid-decoded.tsv  Data Grid (decoded to real) 第一层, 单元格为 %.15g 文本
      grid-stored.tsv   Stored Grid (raw value)     第一层, 单元格为文件中的原始文本
      grid-open.txt     行列数与两种表格的表头文字
输出:
  - 控制台与 HDFStudio/logs/S4-UI-DecodedGrid/reconcile.txt, 全部一致时退出码 0

对账口径:
  解码值 = 存储值 x scale_factor + add_offset, 按 double 计算后取 15 位有效数字;
  存储值按文件里的整数/浮点原样显示, 不做 _FillValue 掩码。
  另外单独统计 netCDF4 自动掩码会屏蔽多少点 (valid_range / _FillValue),
  这与表格显示数值是两件事, 只作说明, 不计入失败。

用法（先设置环境变量并运行 externalPackedSampleMatchesPython）:
  $env:HDFSTUDIO_PACKED_SAMPLE='<file.nc>'
  python HDFStudio/tests/reconcile_decoded_grid.py
"""
import os
import sys
from pathlib import Path

import netCDF4 as nc
import numpy as np

s_Sample = os.environ.get('HDFSTUDIO_PACKED_SAMPLE', '')
s_Variable = os.environ.get('HDFSTUDIO_PACKED_VARIABLE') or 'precipitation'
s_Log_Dir = Path(__file__).resolve().parents[1] / 'logs' / 'S4-UI-DecodedGrid'


def format_15g(f_Value):
    """与 Qt QString::asprintf('%.15g') 相同的 15 位有效数字文本。"""
    return '%.15g' % f_Value


def format_stored(value, s_Dtype):
    """存储值表格里文件原始数字的文本: 整数原样, 浮点按 %g。"""
    if s_Dtype.startswith('int') or s_Dtype.startswith('uint'):
        return str(int(value))
    return '%g' % float(np.float32(value))


def read_table(path_Table):
    if not path_Table.is_file():
        return None
    return [line.split('\t') for line in path_Table.read_text(encoding='utf-8').splitlines()]


def compare_plane(list_Grid, arr_Expected, format_cell, list_Report, s_Title):
    i_Rows, i_Cols = arr_Expected.shape
    list_Report.append(f'{s_Title}: python=({i_Rows},{i_Cols}) hdfstudio=({len(list_Grid)},{len(list_Grid[0])})'
                       if list_Grid else f'{s_Title}: hdfstudio dump missing')
    if not list_Grid or (len(list_Grid), len(list_Grid[0])) != (i_Rows, i_Cols):
        return False
    i_Bad = 0
    for y in range(i_Rows):
        for x in range(i_Cols):
            s_Expected = format_cell(arr_Expected[y, x])
            if list_Grid[y][x] != s_Expected:
                i_Bad += 1
                if i_Bad <= 5:
                    list_Report.append(f'  cell ({y},{x}): python={s_Expected} hdfstudio={list_Grid[y][x]}')
    list_Report.append(f'  cells={i_Rows * i_Cols} mismatched={i_Bad}')
    return i_Bad == 0


def main():
    if not s_Sample or not os.path.isfile(s_Sample):
        print('Set HDFSTUDIO_PACKED_SAMPLE to the packed sample used by externalPackedSampleMatchesPython')
        return 2

    list_Report = []
    b_Ok = True
    with nc.Dataset(s_Sample, 'r') as dataset:
        list_Report.append(f'sample: {s_Sample}  format={dataset.file_format}')
        var_Value = dataset.variables[s_Variable]
        s_Dtype = str(var_Value.dtype)
        list_Report.append(f'variable: {s_Variable} {s_Dtype} {var_Value.shape}')
        dict_Scale = {name: var_Value.getncattr(name) for name in ('scale_factor', 'add_offset')
                      if name in var_Value.ncattrs()}
        list_Report.append(f'packing: {dict_Scale if dict_Scale else "none"}')

        # 解码值: 与表格一样从存储值按 double 计算, 不使用 netCDF4 的自动掩码/自动换算
        var_Value.set_auto_maskandscale(False)
        array_Raw = np.ma.filled(np.ma.asarray(var_Value[(0,) * (var_Value.ndim - 2)]), np.nan)
        f_Scale = float(dict_Scale.get('scale_factor', 1.0))
        f_Offset = float(dict_Scale.get('add_offset', 0.0))
        array_Decoded = np.asarray(array_Raw, dtype=np.float64) * f_Scale + f_Offset
        array_Stored = np.asarray(array_Raw)

        list_Grid_Decoded = read_table(s_Log_Dir / 'grid-decoded.tsv')
        list_Grid_Stored = read_table(s_Log_Dir / 'grid-stored.tsv')
        b_Ok &= compare_plane(list_Grid_Decoded, array_Decoded, format_15g, list_Report,
                             'decoded grid')
        b_Ok &= compare_plane(list_Grid_Stored, array_Stored,
                             lambda v: format_stored(v, s_Dtype), list_Report, 'stored grid')

        # 并排表格: 偶数列是解码值, 奇数列是同一格的存储值, 交替排列
        list_Grid_Both = read_table(s_Log_Dir / 'grid-both.tsv')
        if list_Grid_Both:
            i_Rows, i_Cols = array_Decoded.shape
            i_Width = len(list_Grid_Both[0]) if list_Grid_Both else 0
            list_Report.append(f'both grid: python=({i_Rows},{i_Cols * 2}) hdfstudio='
                               f'({len(list_Grid_Both)},{i_Width})')
            if len(list_Grid_Both) != i_Rows or i_Width != i_Cols * 2:
                b_Ok = False
            else:
                i_Bad = 0
                for y in range(i_Rows):
                    for x in range(i_Cols):
                        list_Expected = [format_15g(array_Decoded[y, x]), format_stored(array_Stored[y, x], s_Dtype)]
                        if list_Grid_Both[y][2 * x:2 * x + 2] != list_Expected:
                            i_Bad += 1
                            if i_Bad <= 5:
                                list_Report.append(f'  row {y} column pair {x}: python={list_Expected} '
                                                   f'hdfstudio={list_Grid_Both[y][2 * x:2 * x + 2]}')
                list_Report.append(f'  cell pairs={i_Rows * i_Cols} mismatched={i_Bad}')
                b_Ok &= i_Bad == 0
        else:
            list_Report.append('both grid: hdfstudio dump missing')

        # 说明性统计: netCDF4 的自动掩码 (valid_range / _FillValue) 会屏蔽多少点
        var_Value.set_auto_maskandscale(True)
        array_Auto = np.ma.asarray(var_Value[(0,) * (var_Value.ndim - 2)])
        i_Masked = int(np.ma.count_masked(array_Auto))
        list_Report.append(f'note: netCDF4 auto mask would hide {i_Masked} of {array_Auto.size} cells'
                           f' (valid_range / _FillValue); the grids show every stored number instead')

    for line_Line in (s_Log_Dir / 'grid-open.txt').read_text(encoding='utf-8').splitlines():
        list_Report.append(f'open: {line_Line}')
    list_Report.append('RESULT: ' + ('ALL MATCH' if b_Ok else 'MISMATCH'))
    s_Text = '\n'.join(list_Report)
    print(s_Text)
    s_Log_Dir.mkdir(parents=True, exist_ok=True)
    (s_Log_Dir / 'reconcile.txt').write_text(s_Text + '\n', encoding='utf-8')
    return 0 if b_Ok else 1


if __name__ == '__main__':
    sys.exit(main())
