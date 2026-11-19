"""hdf_cli info 对账：HDFStudio 读到的结构/属性 vs 独立 Python netCDF4。

hdf_cli info 与 GUI 的 Datasets 树、Properties 使用同一个 netCDF-C 读取函数 (nc_classic_inspect),
本脚本逐项比较: 文件格式、维度(含 unlimited)、全局属性、变量类型/维度名/形状、变量属性值。
文本属性逐字比较; 数值属性按数值比较 (float32 先转 float32 再比), 写法不同只记为提示。

输入:
  - 只读样本: 命令行参数, 缺省为环境变量 HDFSTUDIO_CLASSIC_SAMPLE
  - HDFStudio/build/output/Release/hdf_cli.exe (缺省, 不存在时用 Debug)
输出:
  - 控制台与 HDFStudio/logs/S4-UI-ClassicNetcdf/reconcile-cli-info.txt, 全部一致时退出码 0

用法:
  python HDFStudio/tests/reconcile_cli_info.py [file.nc ...]
"""
import os
import subprocess
import sys
from pathlib import Path

import netCDF4 as nc
import numpy as np

s_Root = Path(__file__).resolve().parents[1]
# 业务样本不随仓库发布, 缺省路径只从环境变量取
s_Default_Sample = os.environ.get('HDFSTUDIO_CLASSIC_SAMPLE', '')
s_Log_Dir = s_Root / 'logs' / 'S4-UI-ClassicNetcdf'


def find_cli():
    for s_Config in ('Release', 'Debug'):
        path_Cli = s_Root / 'build' / 'output' / s_Config / 'hdf_cli.exe'
        if path_Cli.is_file():
            return path_Cli
    raise FileNotFoundError('hdf_cli.exe not built')


def parse_listing(s_Text):
    """把 hdf_cli info 的文本拆成 {format, globals, dims, unlimited, variables}。"""
    dict_Out = {'format': '', 'globals': {}, 'dims': '', 'unlimited': '', 'variables': {}}
    dict_Current = None
    for s_Line in s_Text.splitlines():
        if s_Line.startswith('root group (file format '):
            dict_Out['format'] = s_Line[len('root group (file format '):-2]
        elif s_Line.startswith('    dimensions(sizes): '):
            dict_Out['dims'] = s_Line.split(': ', 1)[1]
        elif s_Line.startswith('    variables(dimensions): '):
            continue
        elif s_Line.startswith('unlimited dimensions: '):
            dict_Out['unlimited'] = s_Line.split(': ', 1)[1]
        elif s_Line.startswith('unlimited dimensions:'):
            dict_Out['unlimited'] = ''
        elif s_Line.startswith('variable '):
            s_Decl = s_Line[len('variable '):]
            s_Type, s_Rest = s_Decl.split(' ', 1)
            s_Name = s_Rest.split('(', 1)[0]
            dict_Current = {'type': s_Type, 'decl': s_Rest, 'attrs': {}, 'shape': ''}
            dict_Out['variables'][s_Name] = dict_Current
        elif s_Line.startswith('current shape = '):
            dict_Current['shape'] = s_Line[len('current shape = '):]
        elif s_Line.startswith('    ') and ': ' in s_Line:
            s_Key, s_Value = s_Line[4:].split(': ', 1)
            (dict_Current['attrs'] if dict_Current is not None else dict_Out['globals'])[s_Key] = s_Value
    return dict_Out


def compare_value(s_Label, value_Python, s_Cli, list_Report):
    """返回 True 表示一致; 数值写法不同但数值相同记为提示。"""
    if isinstance(value_Python, str):
        if value_Python == s_Cli:
            return True
        list_Report.append(f'  MISMATCH {s_Label}: python={value_Python!r} hdf_cli={s_Cli!r}')
        return False
    arr_Python = np.atleast_1d(np.asarray(value_Python))
    s_Body = s_Cli.strip()
    if s_Body.startswith('['):
        s_Body = s_Body[1:-1]
    try:
        arr_Cli = np.array([float(s_Item) for s_Item in s_Body.split(', ')], dtype=np.float64)
    except ValueError:
        list_Report.append(f'  MISMATCH {s_Label}: python={arr_Python.tolist()} hdf_cli={s_Cli!r}')
        return False
    if arr_Python.dtype == np.float32:
        arr_Cli = arr_Cli.astype(np.float32)
    b_Same = arr_Cli.shape == arr_Python.shape and np.array_equal(arr_Cli, arr_Python.astype(arr_Cli.dtype), equal_nan=True)
    if not b_Same:
        list_Report.append(f'  MISMATCH {s_Label}: python={arr_Python.tolist()} hdf_cli={s_Cli!r}')
    return b_Same


def reconcile(s_Sample, path_Cli, list_Report):
    s_Text = subprocess.run([str(path_Cli), 'info', s_Sample], capture_output=True, check=True).stdout.decode('utf-8')
    dict_Cli = parse_listing(s_Text)
    i_Checked = 0
    b_Ok = True
    with nc.Dataset(s_Sample, 'r') as dataset:
        list_Report.append(f'sample: {s_Sample}')
        b_Ok &= compare_value('file format', dataset.data_model, dict_Cli['format'], list_Report)
        s_Dims = ', '.join(f'{s_Name}({len(dim)})' for s_Name, dim in dataset.dimensions.items())
        b_Ok &= compare_value('dimensions', s_Dims, dict_Cli['dims'], list_Report)
        s_Unlimited = ', '.join(s_Name for s_Name, dim in dataset.dimensions.items() if dim.isunlimited())
        b_Ok &= compare_value('unlimited dimensions', s_Unlimited, dict_Cli['unlimited'], list_Report)
        i_Checked += 3
        b_Ok &= compare_value('global attribute names', ','.join(dataset.ncattrs()), ','.join(dict_Cli['globals']), list_Report)
        for s_Name in dataset.ncattrs():
            b_Ok &= compare_value(f'global {s_Name}', dataset.getncattr(s_Name), dict_Cli['globals'].get(s_Name, '<missing>'), list_Report)
            i_Checked += 1
        b_Ok &= compare_value('variable names', ','.join(dataset.variables), ','.join(dict_Cli['variables']), list_Report)
        for s_Name, variable in dataset.variables.items():
            dict_Var = dict_Cli['variables'].get(s_Name)
            if dict_Var is None:
                b_Ok = False
                continue
            b_Ok &= compare_value(f'{s_Name} dtype', str(variable.dtype), dict_Var['type'], list_Report)
            b_Ok &= compare_value(f'{s_Name} dimensions', f'{s_Name}({", ".join(variable.dimensions)})', dict_Var['decl'], list_Report)
            b_Ok &= compare_value(f'{s_Name} shape', str(variable.shape), dict_Var['shape'], list_Report)
            b_Ok &= compare_value(f'{s_Name} attribute names', ','.join(variable.ncattrs()), ','.join(dict_Var['attrs']), list_Report)
            i_Checked += 4
            for s_Attr in variable.ncattrs():
                b_Ok &= compare_value(f'{s_Name}:{s_Attr}', variable.getncattr(s_Attr), dict_Var['attrs'].get(s_Attr, '<missing>'), list_Report)
                i_Checked += 1
    list_Report.append(f'  items checked={i_Checked}  result={"MATCH" if b_Ok else "MISMATCH"}')
    return b_Ok


def main():
    list_Samples = sys.argv[1:] or ([s_Default_Sample] if s_Default_Sample else [])
    if not list_Samples:
        print('Give NetCDF files as arguments or set HDFSTUDIO_CLASSIC_SAMPLE')
        return 2
    path_Cli = find_cli()
    list_Report = [f'hdf_cli: {path_Cli}']
    b_Ok = True
    for s_Sample in list_Samples:
        b_Ok &= reconcile(s_Sample, path_Cli, list_Report)
    list_Report.append('RESULT: ' + ('ALL MATCH' if b_Ok else 'MISMATCH'))
    s_Text = '\n'.join(list_Report)
    print(s_Text)
    s_Log_Dir.mkdir(parents=True, exist_ok=True)
    (s_Log_Dir / 'reconcile-cli-info.txt').write_text(s_Text + '\n', encoding='utf-8')
    return 0 if b_Ok else 1


if __name__ == '__main__':
    sys.exit(main())
