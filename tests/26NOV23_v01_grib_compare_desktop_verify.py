"""🖱️ G3d比较/残差六组真实桌面检查与导出复核，直接运行。

输入：只读HDFSTUDIO_GRIB1_SAMPLE/HDFSTUDIO_GRIB2_SAMPLE；
可选HDFSTUDIO_GRIB_COMPARE_GATE指定已通过自动化闸门，否则取最近PASS。
输出：对应闸门desktop-all-review.json和各组日志；原始截图/控件保留原目录。
命令：C:\\Python312\\python.exe tests/26NOV23_v01_grib_compare_desktop_verify.py
"""
import csv
import hashlib
import importlib.util
import json
import logging
import os
from pathlib import Path
import subprocess

import numpy as np


def main():
    p_root = Path(__file__).resolve().parents[1]
    if os.environ.get('HDFSTUDIO_GRIB_COMPARE_GATE'):
        p_gate = Path(os.environ['HDFSTUDIO_GRIB_COMPARE_GATE'])
    else:
        l_gates = sorted(p_item.parent for p_item in (p_root / 'logs/S4-GribIntegration').glob('G3d-compare-*/result.json') if json.loads(p_item.read_text(encoding='utf-8'))['status'] == 'PASS')
        assert l_gates, 'Complete the G3d automatic gate first'
        p_gate = l_gates[-1]
    d_gate = json.loads((p_gate / 'result.json').read_text(encoding='utf-8'))
    assert d_gate['status'] == 'PASS'
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s', handlers=[logging.StreamHandler(), logging.FileHandler(p_gate / 'desktop-verify.log', encoding='utf-8')])
    o_spec = importlib.util.spec_from_file_location('png_reference', p_root / 'tests/26NOV21_v01_full_regression.py')
    o_png = importlib.util.module_from_spec(o_spec)
    o_spec.loader.exec_module(o_png)
    d_result = {'status': 'RUNNING', 'records': []}
    d_env = os.environ.copy()
    try:
        for s_config in ['Release', 'Debug']:
            s_hash = hashlib.sha256((p_root / 'build/output' / s_config / 'HDFStudio.exe').read_bytes()).hexdigest()
            assert s_hash == d_gate['configurations'][s_config]['binary_sha256']
            for s_sample, s_switch, s_suffix in [('Fixture', '', 'compare'), ('Grib1', '-ExternalGrib1', 'compare-grib1'), ('Grib2', '-ExternalGrib2', 'compare-grib2')]:
                s_pattern = 'desktop-' + s_suffix + '-' + s_config + '-*'
                l_before = set((p_root / 'logs/S4-GribIntegration').glob(s_pattern))
                l_command = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'tests/26NOV23_v01_grib_browser_desktop.ps1', '-Configuration', s_config, '-Compare']
                if s_switch:
                    l_command.append(s_switch)
                logging.info('Start %s %s desktop', s_config, s_sample)
                o_run = subprocess.run(l_command, cwd=p_root, env=d_env, capture_output=True, timeout=180)
                s_stem = 'desktop-' + s_config + '-' + s_sample
                (p_gate / (s_stem + '.stdout.txt')).write_bytes(o_run.stdout)
                (p_gate / (s_stem + '.stderr.txt')).write_bytes(o_run.stderr)
                assert o_run.returncode == 0, (s_stem, o_run.returncode)
                l_new = set((p_root / 'logs/S4-GribIntegration').glob(s_pattern)) - l_before
                assert len(l_new) == 1, l_new
                p_run = next(iter(l_new))
                l_checks = json.loads((p_run / 'checks.json').read_text(encoding='utf-8-sig'))
                assert len(l_checks) >= 35 and all(d_item['pass'] for d_item in l_checks)
                with (p_run / 'gui-comparison.csv').open(encoding='utf-8-sig', newline='') as o_file:
                    d_csv = next(csv.DictReader(o_file))
                assert d_csv['status'] == 'Ready' and d_csv['pairing'] == 'by index'
                for s_key in ['rmse', 'mae', 'bias_target_minus_anchor']:
                    assert float(d_csv[s_key]) == 0
                a_png = o_png.png_rgb(p_run / 'gui-residual.png')
                p_auto = p_gate / s_config / ('compare-grib' + ('1' if s_sample == 'Grib1' else '2') + '-field-0.png')
                if s_sample != 'Fixture':
                    np.testing.assert_array_equal(a_png, o_png.png_rgb(p_auto))
                else:
                    assert a_png.shape == (3, 4, 3)
                    assert np.unique(a_png.reshape(-1, 3), axis=0).shape[0] == 1
                assert int(d_csv['valid_pairs']) == a_png.shape[0] * a_png.shape[1]
                d_result['records'].append({'folder': str(p_run), 'passed': len(l_checks), 'csv_zero_metrics_exact': True, 'png_pixels_exact': True, 'binary_sha256': s_hash})
                logging.info('PASS %s %s: %s checks; CSV/PNG verified', s_config, s_sample, len(l_checks))
        for s_name, s_hash in d_gate['sources'].items():
            assert hashlib.sha256((p_root / s_name).read_bytes()).hexdigest() == s_hash, s_name
        for d_sample in d_gate['samples'].values():
            assert hashlib.sha256(Path(d_sample['source']).read_bytes()).hexdigest() == d_sample['after']
        d_result['total_checks'] = sum(d_record['passed'] for d_record in d_result['records'])
        d_result['status'] = 'PASS'
    except Exception:
        d_result['status'] = 'FAIL'
        logging.exception('Comparison desktop verification failed')
        raise
    finally:
        (p_gate / 'desktop-all-review.json').write_text(json.dumps(d_result, indent=2), encoding='utf-8')
    logging.info('PASS %s', p_gate)


if __name__ == '__main__':
    main()
