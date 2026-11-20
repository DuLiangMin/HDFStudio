"""直方图对账：HDFStudio 的 `hdf_cli hist` vs 独立 Python netCDF4 + numpy。

输入:
  - 只读样本: 环境变量 HDFSTUDIO_PACKED_SAMPLE 指向含 scale_factor/add_offset 的 NetCDF 文件
  - 变量名: 环境变量 HDFSTUDIO_PACKED_VARIABLE, 缺省 precipitation
  - 命令行: 环境变量 HDFSTUDIO_CLI, 缺省为脚本同级的 build/output/{Release,Debug}/hdf_cli.exe
输出:
  - 控制台与 HDFStudio/logs/S4-UI-Histogram/reconcile.txt, 全部一致时退出码 0

对账口径:
  📐 1 in 1 (全量): 有限值个数、NaN 个数、每箱计数与 Python numpy 逐箱比对;
     最小值/最大值/均值/标准差按 double 计算, 允许 1e-9 相对误差。
  🎲 1 in 20 (随机抽样): 样本数必须精确等于 round(有限值 / 20); 每箱计数之和等于样本数;
     样本最小值/最大值落在全量范围内; 均值在 5 个标准误之内 (随机抽样, 不能逐点比对)。
  🔁 同一种子连跑两次, 输出必须逐字节相同 (可复现)。
  ⛔ 抽出样本只是"读文件 + 抽样", 不改文件: 文件大小与修改时间前后一致。

用法:
  $env:HDFSTUDIO_PACKED_SAMPLE='<file.nc>'
  $env:HDFSTUDIO_CLI='HDFStudio/build/output/Release/hdf_cli.exe'
  python HDFStudio/tests/reconcile_histogram.py
"""
import os
import re
import subprocess
import sys
from pathlib import Path

import netCDF4 as nc
import numpy as np

s_Sample = os.environ.get('HDFSTUDIO_PACKED_SAMPLE', '')
s_Variable = os.environ.get('HDFSTUDIO_PACKED_VARIABLE') or 'precipitation'
s_Log_Dir = Path(__file__).resolve().parents[1] / 'logs' / 'S4-UI-Histogram'

# 一行的样子: "  0  [0, 1249.875)       527  ######"
s_Bin_Line = re.compile(r'^\s*(\d+)\s+\[([^,]+),\s*([^)]+)\)\s+(\d+)')


def find_cli():
    """命令行程序: 先用环境变量, 再按 Release/Debug 顺序找脚本所在工程的构建产物。"""
    if os.environ.get('HDFSTUDIO_CLI'):
        return Path(os.environ['HDFSTUDIO_CLI'])
    s_Root = Path(__file__).resolve().parents[1]
    for s_Config in ('Release', 'Debug'):
        path_Cli = s_Root / 'build' / 'output' / s_Config / 'hdf_cli.exe'
        if path_Cli.is_file():
            return path_Cli
    return s_Root / 'build' / 'output' / 'Release' / 'hdf_cli.exe'


def parse_histogram(s_Text):
    """把 hdf_cli hist 的文本解析成 {headline, range, bins, counts, edges}。"""
    dict_Out = {'counts': [], 'edges': [], 'lines': s_Text.splitlines()}
    for s_Line in s_Text.splitlines():
        if s_Line.startswith('values:'):
            dict_Out['headline'] = s_Line
            m_Num = re.search(r'values: (\d+) finite, (\d+) missing\s+sampled (\d+) at random', s_Line)
            if m_Num:
                dict_Out['finite'] = int(m_Num.group(1))
                dict_Out['missing'] = int(m_Num.group(2))
                dict_Out['sampled'] = int(m_Num.group(3))
            m_Seed = re.search(r'\(1 in (\d+), seed (\d+)\)', s_Line)
            if m_Seed:
                dict_Out['every_nth'] = int(m_Seed.group(1))
                dict_Out['seed'] = int(m_Seed.group(2))
        elif s_Line.startswith('range:'):
            m_Range = re.search(r'range: \[([^,]+), ([^\]]+)\]\s+mean ([^ ]+)\s+stddev (.*)$', s_Line)
            if m_Range:
                dict_Out['minimum'] = float(m_Range.group(1))
                dict_Out['maximum'] = float(m_Range.group(2))
                dict_Out['mean'] = float(m_Range.group(3))
                dict_Out['stddev'] = float(m_Range.group(4))
        elif s_Line.startswith('bins:'):
            m_Bins = re.match(r'bins: (\d+)', s_Line)
            if m_Bins:
                dict_Out['bins'] = int(m_Bins.group(1))
        else:
            m_Bin = s_Bin_Line.match(s_Line)
            if m_Bin:
                dict_Out['counts'].append(int(m_Bin.group(4)))
                dict_Out['edges'].append(float(m_Bin.group(2)))
                dict_Out['edges'].append(float(m_Bin.group(3)))
    return dict_Out


def run_cli(path_Cli, list_Args):
    process = subprocess.run([str(path_Cli)] + list_Args, capture_output=True, text=True,
                             encoding='utf-8', errors='replace')
    return process.returncode, process.stdout, process.stderr


def bins_by_the_documented_rule(array_Values, i_Bins):
    """[min, max] 等宽分箱, 最大值归入最后一箱: 与 nc_histogram 的规则相同。"""
    f_Low = float(array_Values.min())
    f_High = float(array_Values.max())
    if not f_High > f_Low:
        return f_Low, f_High, [int(array_Values.size)]
    f_Step = (f_High - f_Low) / i_Bins
    array_Index = np.floor((array_Values - f_Low) / f_Step).astype(np.int64)
    array_Index = np.clip(array_Index, 0, i_Bins - 1)
    return f_Low, f_High, np.bincount(array_Index, minlength=i_Bins).tolist()


def close_enough(f_A, f_B, f_Tolerance=1e-9):
    return abs(f_A - f_B) <= f_Tolerance * max(1.0, abs(f_A), abs(f_B))


def main():
    if not s_Sample or not os.path.isfile(s_Sample):
        print('Set HDFSTUDIO_PACKED_SAMPLE to the packed sample (int32 with scale_factor)')
        return 2
    path_Cli = find_cli()
    if not path_Cli.is_file():
        print(f'hdf_cli not found: {path_Cli}')
        return 2

    list_Report = []
    b_Ok = True
    list_Report.append(f'sample: {s_Sample}')
    list_Report.append(f'cli: {path_Cli}')

    # 文件在读取前后不能被改动
    dict_Before = (os.path.getsize(s_Sample), os.path.getmtime(s_Sample))

    with nc.Dataset(s_Sample, 'r') as dataset:
        var_Value = dataset.variables[s_Variable]
        list_Report.append(f'variable: {s_Variable} {var_Value.dtype} {var_Value.shape}')
        var_Value.set_auto_maskandscale(False)
        i_Plane_Index = (0,) * (var_Value.ndim - 2)
        array_Raw = np.ma.filled(np.ma.asarray(var_Value[i_Plane_Index]), np.nan)

        dict_Attrs = {name: var_Value.getncattr(name) for name in
                      ('scale_factor', 'add_offset', '_FillValue', 'missing_value')
                      if name in var_Value.ncattrs()}
        f_Scale = float(dict_Attrs.get('scale_factor', 1.0))
        f_Offset = float(dict_Attrs.get('add_offset', 0.0))
        array_Stored = np.asarray(array_Raw, dtype=np.float64)
        array_Decoded = array_Stored * f_Scale + f_Offset
        # _FillValue / missing_value 变成 NaN, 与读取代码一致
        for s_Name in ('_FillValue', 'missing_value'):
            if s_Name in dict_Attrs:
                array_Decoded[array_Stored == float(dict_Attrs[s_Name])] = np.nan
        list_Report.append(f'packing: scale={f_Scale} offset={f_Offset} '
                           f'fill={dict_Attrs.get("_FillValue")} missing={dict_Attrs.get("missing_value")}')

    array_Finite = array_Decoded[np.isfinite(array_Decoded)]
    i_Total = int(array_Finite.size)
    i_Missing = int(array_Decoded.size - array_Finite.size)
    f_Mean = float(array_Finite.mean())
    f_Stddev = float(array_Finite.std(ddof=1)) if i_Total > 1 else 0.0
    list_Report.append(f'python: finite={i_Total} missing={i_Missing} '
                       f'min={float(array_Finite.min())} max={float(array_Finite.max())} '
                       f'mean={f_Mean:.12g} stddev={f_Stddev:.12g}')

    list_Args_Base = ['hist', s_Sample, '/' + s_Variable]

    # 📐 全量: 每箱计数逐箱比对
    i_Code, s_Out, s_Err = run_cli(path_Cli, list_Args_Base + ['1.0', '32', '0'])
    if i_Code != 0:
        list_Report.append(f'FULL: hdf_cli exited {i_Code}: {s_Err.strip()}')
        b_Ok = False
    else:
        dict_Full = parse_histogram(s_Out)
        list_Report.append(f'FULL headline: {dict_Full.get("headline", "?")}')
        b_Ok &= dict_Full.get('finite') == i_Total
        b_Ok &= dict_Full.get('missing') == i_Missing
        b_Ok &= dict_Full.get('sampled') == i_Total
        f_Low, f_High, list_Counts = bins_by_the_documented_rule(array_Finite, 32)
        b_Ok &= close_enough(dict_Full.get('minimum', np.nan), f_Low)
        b_Ok &= close_enough(dict_Full.get('maximum', np.nan), f_High)
        b_Ok &= close_enough(dict_Full.get('mean', np.nan), f_Mean)
        b_Ok &= close_enough(dict_Full.get('stddev', np.nan), f_Stddev, 1e-7)
        if len(list_Counts) != len(dict_Full['counts']):
            list_Report.append(f'FULL: bin count mismatch python={len(list_Counts)} '
                               f'hdf_cli={len(dict_Full["counts"])}')
            b_Ok = False
        else:
            i_Bad = 0
            for i_Bin, (i_Expected, i_Got) in enumerate(zip(list_Counts, dict_Full['counts'])):
                if i_Expected != i_Got:
                    i_Bad += 1
                    if i_Bad <= 8:
                        list_Report.append(f'  bin {i_Bin}: python={i_Expected} hdf_cli={i_Got}')
            list_Report.append(f'FULL bins: {len(list_Counts)} compared, {i_Bad} mismatched')
            b_Ok &= i_Bad == 0

    # 🎲 随机抽样: 样本数与统计量在允许误差内
    i_Code, s_Out, s_Err = run_cli(path_Cli, list_Args_Base + ['0.05', '32', '0'])
    if i_Code != 0:
        list_Report.append(f'SAMPLE: hdf_cli exited {i_Code}: {s_Err.strip()}')
        b_Ok = False
    else:
        dict_Sample = parse_histogram(s_Out)
        list_Report.append(f'SAMPLE headline: {dict_Sample.get("headline", "?")}')
        i_Want = max(1, int(round(i_Total / 20.0)))
        if dict_Sample.get('sampled') != i_Want:
            list_Report.append(f'SAMPLE: sampled={dict_Sample.get("sampled")} expected={i_Want}')
            b_Ok = False
        if dict_Sample.get('every_nth') != 20:
            list_Report.append(f'SAMPLE: step 1 in {dict_Sample.get("every_nth")}, expected 20')
            b_Ok = False
        if sum(dict_Sample['counts']) != dict_Sample.get('sampled'):
            list_Report.append(f'SAMPLE: bars sum to {sum(dict_Sample["counts"])} but sampled is '
                               f'{dict_Sample.get("sampled")}')
            b_Ok = False
        if not (dict_Sample['edges'] and
                float(array_Finite.min()) - 1e-9 <= dict_Sample['edges'][0] and
                dict_Sample['edges'][-1] <= float(array_Finite.max()) + 1e-9):
            list_Report.append('SAMPLE: range of the sample falls outside the plane')
            b_Ok = False
        # 样本均值: 随机误差允许 5 个标准误
        f_Error = f_Stddev / np.sqrt(max(1, i_Want))
        f_Diff = abs(dict_Sample.get('mean', np.nan) - f_Mean)
        list_Report.append(f'SAMPLE mean: python={f_Mean:.12g} hdf_cli={dict_Sample.get("mean")} '
                           f'diff={f_Diff:.6g} allowed(5 s.e.)={5 * f_Error:.6g}')
        b_Ok &= bool(f_Diff <= 5 * f_Error)

        # 🔁 同一种子可复现
        i_Code2, s_Out2, _ = run_cli(path_Cli, list_Args_Base + ['0.05', '32', '0'])
        b_Ok &= i_Code2 == 0 and s_Out2.replace('\r\n', '\n') == s_Out.replace('\r\n', '\n')
        list_Report.append('SAMPLE repeat: ' + ('identical' if s_Out2 == s_Out else 'DIFFERENT'))

    # ⛔ 读取只读: 文件没有被改写
    dict_After = (os.path.getsize(s_Sample), os.path.getmtime(s_Sample))
    b_Ok &= dict_Before == dict_After
    list_Report.append('file unchanged: ' + ('yes' if dict_Before == dict_After else 'NO'))

    list_Report.append('RESULT: ' + ('ALL MATCH' if b_Ok else 'MISMATCH'))
    s_Text = '\n'.join(list_Report)
    print(s_Text)
    s_Log_Dir.mkdir(parents=True, exist_ok=True)
    (s_Log_Dir / 'reconcile.txt').write_text(s_Text + '\n', encoding='utf-8')
    return 0 if b_Ok else 1


if __name__ == '__main__':
    sys.exit(main())
