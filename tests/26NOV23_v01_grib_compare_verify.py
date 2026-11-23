"""📊 G3d比较/残差双配置闸门，默认直接运行。

输入：只读HDFSTUDIO_GRIB1_SAMPLE/HDFSTUDIO_GRIB2_SAMPLE；
HDFSTUDIO_GRIB_TEXTURE_GATE可指定已通过G3b参考，否则使用最近PASS。
输出：logs/S4-GribIntegration/G3d-compare-时间/，残差float64[H,W]、CSV、PNG和日志。
命令：C:\\Python312\\python.exe tests/26NOV23_v01_grib_compare_verify.py
"""
import csv
import datetime
import hashlib
import importlib.util
import json
import logging
import os
from pathlib import Path
import subprocess

import numpy as np


def paired_metrics(a_anchor, a_target):
    """独立NumPy参考；有效配对严格取两侧有限值交集。"""
    a_valid = np.isfinite(a_anchor) & np.isfinite(a_target)
    a_left = a_anchor[a_valid]
    a_right = a_target[a_valid]
    a_error = a_right - a_left
    d_result = {'valid_pairs': int(a_valid.sum()), 'excluded_pairs': int(a_valid.size - a_valid.sum()),
                'rmse': float(np.sqrt(np.mean(a_error ** 2))), 'mae': float(np.mean(np.abs(a_error))),
                'bias_target_minus_anchor': float(np.mean(a_error))}
    if len(a_left) > 1 and np.ptp(a_left) > 0 and np.ptp(a_right) > 0:
        d_result['pearson_r'] = float(np.corrcoef(a_left, a_right)[0, 1])
    else:
        d_result['pearson_r'] = None
    return d_result


def main():
    p_root = Path(__file__).resolve().parents[1]
    p_log = p_root / 'logs/S4-GribIntegration' / ('G3d-compare-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    p_log.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s', handlers=[logging.StreamHandler(), logging.FileHandler(p_log / 'verify.log', encoding='utf-8')])
    d_env = os.environ.copy()
    d_env['QT_LOGGING_TO_CONSOLE'] = '1'
    if d_env.get('HDFSTUDIO_GRIB_TEXTURE_GATE'):
        p_reference = Path(d_env['HDFSTUDIO_GRIB_TEXTURE_GATE'])
    else:
        l_gates = sorted(p_item.parent for p_item in (p_root / 'logs/S4-GribIntegration').glob('G3b-texture-*/result.json') if json.loads(p_item.read_text(encoding='utf-8'))['status'] == 'PASS')
        assert l_gates, 'Complete G3b first'
        p_reference = l_gates[-1]
    d_previous = json.loads((p_reference / 'result.json').read_text(encoding='utf-8'))
    p_histogram = Path(d_previous['reference'])
    s_cache = (p_root / 'build/vs2022/CMakeCache.txt').read_text(encoding='utf-8-sig')
    s_cmake = next(s_line.split('=', 1)[1] for s_line in s_cache.splitlines() if s_line.startswith('CMAKE_COMMAND:INTERNAL='))
    d_report = {'status': 'RUNNING', 'reference': str(p_reference), 'sources': {}, 'samples': {}, 'configurations': {}}
    for p_source in [p_root / 'CMakeLists.txt', *sorted((p_root / 'src').rglob('*.cpp')), *sorted((p_root / 'src').rglob('*.hpp')), p_root / 'tests/grib_analysis_test.cpp', p_root / 'tests/drop_targets_test.cpp', Path(__file__)]:
        d_report['sources'][str(p_source.relative_to(p_root))] = hashlib.sha256(p_source.read_bytes()).hexdigest()
    o_spec = importlib.util.spec_from_file_location('png_reference', p_root / 'tests/26NOV21_v01_full_regression.py')
    o_png = importlib.util.module_from_spec(o_spec)
    o_spec.loader.exec_module(o_png)

    def run(l_command, p_prefix, i_timeout=1200):
        logging.info('Start %s', p_prefix)
        o_result = subprocess.run(list(map(str, l_command)), cwd=p_root, env=d_env, capture_output=True, timeout=i_timeout)
        p_prefix.with_suffix('.stdout.txt').write_bytes(o_result.stdout)
        p_prefix.with_suffix('.stderr.txt').write_bytes(o_result.stderr)
        assert o_result.returncode == 0, (str(p_prefix), o_result.returncode)
        logging.info('PASS %s', p_prefix)

    try:
        d_cases = {}
        for i_edition in [1, 2]:
            s_prefix = 'HDFSTUDIO_GRIB' + str(i_edition) + '_'
            p_sample = Path(d_env[s_prefix + 'SAMPLE'])
            s_hash = hashlib.sha256(p_sample.read_bytes()).hexdigest()
            assert s_hash == d_previous['samples'][str(i_edition)]['after']
            d_report['samples'][str(i_edition)] = {'source': str(p_sample), 'before': s_hash}
            p_cases = p_histogram / ('grib1-cases.json' if i_edition == 1 else 'grib2-reference/external-grib2-cases.json')
            d_env[s_prefix + 'CASES'] = str(p_cases)
            d_cases[i_edition] = json.loads(p_cases.read_text(encoding='utf-8'))['fields']
        for s_config in ['Release', 'Debug']:
            p_config = p_log / s_config
            p_config.mkdir()
            p_binary = p_root / 'build/output' / s_config
            run([s_cmake, '--build', 'build/vs2022', '--config', s_config, '--target', 'grib_analysis_tests', 'hdf_explorer', 'hdf_explorer_drop_tests', 'grib_browser_tests', '--parallel', '6'], p_config / 'build')
            d_env['HDFSTUDIO_GRIB_EVIDENCE'] = str(p_config)
            run([p_binary / 'grib_analysis_tests.exe', '-o', str(p_config / 'analysis.txt') + ',txt'], p_config / 'analysis', 900)
            assert 'Totals: 21 passed, 0 failed, 0 skipped' in (p_config / 'analysis.txt').read_text(encoding='utf-8-sig')
            d_numeric = {}
            for i_edition, l_cases in d_cases.items():
                d_first = next(d_case for d_case in l_cases if d_case['tag'] == 'field-0')
                t_shape = (d_first['rows'], d_first['cols']) if 'rows' in d_first else (361, 720)
                a_anchor = np.fromfile(p_reference / s_config / ('decoded-grib' + str(i_edition) + '-field-0.f64'), dtype='<f8').reshape(t_shape)
                i_positions = 0
                d_max_errors = {'rmse': 0., 'mae': 0., 'bias_target_minus_anchor': 0., 'pearson_r': 0.}
                for d_case in l_cases:
                    s_stem = 'grib' + str(i_edition) + '-' + d_case['tag']
                    a_target = np.fromfile(p_reference / s_config / ('decoded-' + s_stem + '.f64'), dtype='<f8').reshape(t_shape)
                    a_actual = np.fromfile(p_config / ('compare-' + s_stem + '.f64'), dtype='<f8').reshape(t_shape)
                    a_expected = np.where(np.isfinite(a_anchor) & np.isfinite(a_target), a_target - a_anchor, np.nan)
                    np.testing.assert_array_equal(a_actual, a_expected)
                    with (p_config / ('compare-' + s_stem + '.csv')).open(encoding='utf-8', newline='') as o_input:
                        d_csv = next(csv.DictReader(o_input))
                    assert d_csv['status'] == 'Ready' and d_csv['pairing'] == 'by index'
                    assert d_csv['anchor_grid_identity'] and d_csv['anchor_time_identity']
                    d_expected = paired_metrics(a_anchor, a_target)
                    for s_key, f_expected in d_expected.items():
                        if s_key in ['valid_pairs', 'excluded_pairs']:
                            assert int(d_csv[s_key]) == f_expected
                        elif f_expected is None:
                            assert d_csv[s_key] == 'N/A'
                        else:
                            f_actual = float(d_csv[s_key])
                            if s_key == 'pearson_r':
                                np.testing.assert_allclose(f_actual, f_expected, rtol=0, atol=4e-13)
                            else:
                                f_residual_scale = float(np.nanmax(np.abs(a_expected)))
                                assert abs(f_actual - f_expected) <= 4e-14 * f_residual_scale, (s_key, s_stem, f_actual, f_expected)
                            d_max_errors[s_key] = max(d_max_errors[s_key], abs(f_actual - f_expected))
                    a_rgb = o_png.png_rgb(p_config / ('compare-' + s_stem + '.png'))
                    assert a_rgb.shape == (*t_shape, 3)
                    d_palette = json.loads((p_root / 'resources/colormaps/palettes/ncl_BlueWhiteOrangeRed.json').read_text(encoding='utf-8'))
                    a_colors = np.asarray(d_palette['colors'])[:, :3]
                    a_finite = np.isfinite(a_expected)
                    f_extent = float(np.nanmax(np.abs(a_expected))) or 1.
                    a_fraction = .5 + .5 * a_expected[a_finite] / f_extent
                    a_expected_rgb = np.full(a_rgb.shape, 110, dtype=np.uint8)
                    for i_channel in range(3):
                        a_mapped = np.interp(a_fraction, d_palette['positions'], a_colors[:, i_channel])
                        a_expected_rgb[:, :, i_channel][a_finite] = np.floor(np.clip(a_mapped, 0, 1) * 255 + .5).astype(np.uint8)
                    np.testing.assert_array_equal(a_rgb, a_expected_rgb)
                    i_positions += a_target.size
                d_numeric[str(i_edition)] = {'fields': len(l_cases), 'positions': i_positions, 'residual_exact': True, 'png_pixels_exact': True, 'max_metric_absolute_errors': d_max_errors}
            run([p_binary / 'hdf_explorer_drop_tests.exe', 'imageMetricsEntropyAndPairAccuracy', 'imageMetricsResidualAccuracy', 'comparePanelComputesLivePairMetricsAndExportsCsv', 'workspaceSyncAnchorLagAndOutOfRange', '-o', str(p_config / 'adjacent.txt') + ',txt'], p_config / 'adjacent', 240)
            assert 'Totals: 6 passed, 0 failed, 0 skipped' in (p_config / 'adjacent.txt').read_text(encoding='utf-8-sig')
            run([p_binary / 'grib_browser_tests.exe', '-o', str(p_config / 'browser.txt') + ',txt'], p_config / 'browser', 240)
            assert '0 failed, 0 skipped' in (p_config / 'browser.txt').read_text(encoding='utf-8-sig')
            d_report['configurations'][s_config] = {'analysis_passed': 21, 'adjacent_passed': 6, 'numeric': d_numeric, 'binary_sha256': hashlib.sha256((p_binary / 'HDFStudio.exe').read_bytes()).hexdigest()}
        for d_sample in d_report['samples'].values():
            d_sample['after'] = hashlib.sha256(Path(d_sample['source']).read_bytes()).hexdigest()
            assert d_sample['before'] == d_sample['after']
        for s_name, s_hash in d_report['sources'].items():
            assert hashlib.sha256((p_root / s_name).read_bytes()).hexdigest() == s_hash, s_name
        d_report['status'] = 'PASS'
    except Exception:
        d_report['status'] = 'FAIL'
        logging.exception('Comparison gate failed')
        raise
    finally:
        (p_log / 'result.json').write_text(json.dumps(d_report, indent=2), encoding='utf-8')
    logging.info('PASS %s', p_log)


if __name__ == '__main__':
    main()
