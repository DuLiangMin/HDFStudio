"""📈 G3c灰度熵/曲线与预算验证，默认直接运行。

输入：HDFSTUDIO_GRIB1_SAMPLE、HDFSTUDIO_GRIB2_SAMPLE只读业务文件，
以及已通过的G3b证据（可用HDFSTUDIO_GRIB_TEXTURE_GATE指定）。
输出：logs/S4-GribIntegration/G3c-profile-时间/，逐点JSON和带时间戳日志。
命令：C:\\Python312\\python.exe tests/26NOV23_v01_grib_profile_verify.py
"""
import datetime
import hashlib
import json
import logging
import os
from pathlib import Path
import subprocess

import numpy as np


def reference_samples(a_values, a_points):
    """按已输出曲线坐标独立插值；四邻点缺测时沿用最近点约定。"""
    i_rows, i_cols = a_values.shape
    a_x = a_points[:, 1]
    a_y = a_points[:, 2]
    a_inside = (a_x >= 0) & (a_x <= i_cols - 1) & (a_y >= 0) & (a_y <= i_rows - 1)
    a_result = np.full(a_x.shape, np.nan)
    a_c0 = np.floor(a_x[a_inside]).astype(np.int64)
    a_r0 = np.floor(a_y[a_inside]).astype(np.int64)
    a_c1 = np.minimum(a_c0 + 1, i_cols - 1)
    a_r1 = np.minimum(a_r0 + 1, i_rows - 1)
    a_tx = a_x[a_inside] - a_c0
    a_ty = a_y[a_inside] - a_r0
    a_00 = a_values[a_r0, a_c0]
    a_10 = a_values[a_r0, a_c1]
    a_01 = a_values[a_r1, a_c0]
    a_11 = a_values[a_r1, a_c1]
    a_bilinear = ((a_00 * (1 - a_tx) + a_10 * a_tx) * (1 - a_ty)
                  + (a_01 * (1 - a_tx) + a_11 * a_tx) * a_ty)
    a_nearest = a_values[np.floor(a_y[a_inside] + .5).astype(np.int64), np.floor(a_x[a_inside] + .5).astype(np.int64)]
    a_neighbors = np.isfinite(a_00) & np.isfinite(a_10) & np.isfinite(a_01) & np.isfinite(a_11)
    a_result[a_inside] = np.where(a_neighbors, a_bilinear, a_nearest)
    return a_result


def reference_entropy(a_values):
    a_finite = a_values[np.isfinite(a_values)]
    if np.min(a_finite) == np.max(a_finite):
        return 0.
    a_indices = np.minimum(np.floor((a_finite - a_finite.min()) / (a_finite.max() - a_finite.min()) * 256).astype(np.int64), 255)
    a_counts = np.bincount(a_indices, minlength=256)
    a_probabilities = a_counts[a_counts > 0] / a_finite.size
    return float(-np.sum(a_probabilities * np.log2(a_probabilities)))


def main():
    p_root = Path(__file__).resolve().parents[1]
    p_log = p_root / 'logs/S4-GribIntegration' / ('G3c-profile-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    p_log.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s', handlers=[logging.StreamHandler(), logging.FileHandler(p_log / 'verify.log', encoding='utf-8')])
    d_env = os.environ.copy()
    d_env['QT_LOGGING_TO_CONSOLE'] = '1'
    if d_env.get('HDFSTUDIO_GRIB_TEXTURE_GATE'):
        p_reference = Path(d_env['HDFSTUDIO_GRIB_TEXTURE_GATE'])
    else:
        l_gates = sorted(p_item.parent for p_item in (p_root / 'logs/S4-GribIntegration').glob('G3b-texture-*/result.json') if json.loads(p_item.read_text(encoding='utf-8'))['status'] == 'PASS')
        assert l_gates, 'Complete the G3b gate first'
        p_reference = l_gates[-1]
    d_previous = json.loads((p_reference / 'result.json').read_text(encoding='utf-8'))
    p_histogram = Path(d_previous['reference'])
    s_cache = (p_root / 'build/vs2022/CMakeCache.txt').read_text(encoding='utf-8-sig')
    s_cmake = next(s_line.split('=', 1)[1] for s_line in s_cache.splitlines() if s_line.startswith('CMAKE_COMMAND:INTERNAL='))
    d_report = {'status': 'RUNNING', 'reference': str(p_reference), 'sources': {}, 'samples': {}, 'configurations': {}}
    for p_source in [p_root / 'CMakeLists.txt', *sorted((p_root / 'src').rglob('*.cpp')), *sorted((p_root / 'src').rglob('*.hpp')), p_root / 'tests/grib_analysis_test.cpp', Path(__file__)]:
        d_report['sources'][str(p_source.relative_to(p_root))] = hashlib.sha256(p_source.read_bytes()).hexdigest()

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
            run([s_cmake, '--build', 'build/vs2022', '--config', s_config, '--target', 'grib_analysis_tests', 'hdf_explorer', 'hdf_explorer_drop_tests', '--parallel', '6'], p_config / 'build')
            d_env['HDFSTUDIO_GRIB_EVIDENCE'] = str(p_config)
            run([p_binary / 'grib_analysis_tests.exe', '-o', str(p_config / 'analysis.txt') + ',txt'], p_config / 'analysis', 600)
            assert 'Totals: 16 passed, 0 failed, 0 skipped' in (p_config / 'analysis.txt').read_text(encoding='utf-8-sig')
            d_numeric = {}
            for i_edition, l_cases in d_cases.items():
                d_summary = json.loads((p_config / ('profile-grib' + str(i_edition) + '.json')).read_text(encoding='utf-8'))
                i_points = 0
                f_max_error = 0.
                f_max_entropy_error = 0.
                for d_case in l_cases:
                    s_tag = d_case['tag']
                    s_stem = 'grib' + str(i_edition) + '-' + s_tag
                    d_meta = d_summary[s_tag]
                    a_reference = np.fromfile(p_reference / s_config / ('decoded-' + s_stem + '.f64'), dtype='<f8').reshape(d_meta['rows'], d_meta['cols'])
                    d_curve = json.loads((p_config / ('profile-' + s_stem + '.json')).read_text(encoding='utf-8'))
                    a_points = np.asarray([[np.nan if f_value is None else f_value for f_value in l_point] for l_point in d_curve['points']], dtype=np.float64)
                    a_expected = reference_samples(a_reference, a_points)
                    np.testing.assert_array_equal(np.isnan(a_points[:, 3]), np.isnan(a_expected))
                    np.testing.assert_allclose(a_points[:, 3], a_expected, rtol=8 * np.finfo(float).eps, atol=0, equal_nan=True)
                    a_distances = np.concatenate(([0.], np.cumsum(np.hypot(np.diff(a_points[:, 1]), np.diff(a_points[:, 2])))))
                    np.testing.assert_allclose(a_points[:, 0], a_distances, rtol=1e-13, atol=0)
                    assert d_curve['finite'] == int(np.isfinite(a_expected).sum())
                    assert d_curve['missing'] == int(np.isnan(a_expected).sum())
                    i_points += len(a_points)
                    if np.isfinite(a_expected).any():
                        f_max_error = max(f_max_error, float(np.nanmax(np.abs(a_points[:, 3] - a_expected))))
                    f_entropy = reference_entropy(a_reference)
                    np.testing.assert_allclose(d_meta['entropy_bits'], f_entropy, rtol=0, atol=2e-13)
                    f_max_entropy_error = max(f_max_entropy_error, abs(d_meta['entropy_bits'] - f_entropy))
                d_numeric[str(i_edition)] = {'fields': len(l_cases), 'points': i_points, 'max_interpolation_absolute_error': f_max_error, 'max_entropy_absolute_error': f_max_entropy_error}
            run([p_binary / 'hdf_explorer_drop_tests.exe', 'curveProfileSamplesPolylineAndSplineOnDecodedPlane', 'regressionProfileCsvMatchesAnalyticField', 'histogramOrRasterShowsGrayEntropy', 'textureEntropySnapshotFollowsMaskAndSourceLifetime', '-o', str(p_config / 'adjacent.txt') + ',txt'], p_config / 'adjacent', 180)
            assert 'Totals: 6 passed, 0 failed, 0 skipped' in (p_config / 'adjacent.txt').read_text(encoding='utf-8-sig')
            d_report['configurations'][s_config] = {'analysis_passed': 16, 'adjacent_passed': 6, 'numeric': d_numeric, 'binary_sha256': hashlib.sha256((p_binary / 'HDFStudio.exe').read_bytes()).hexdigest()}
        for d_sample in d_report['samples'].values():
            d_sample['after'] = hashlib.sha256(Path(d_sample['source']).read_bytes()).hexdigest()
            assert d_sample['before'] == d_sample['after']
        for s_name, s_hash in d_report['sources'].items():
            assert hashlib.sha256((p_root / s_name).read_bytes()).hexdigest() == s_hash, s_name
        d_report['status'] = 'PASS'
    except Exception:
        d_report['status'] = 'FAIL'
        logging.exception('Profile gate failed')
        raise
    finally:
        (p_log / 'result.json').write_text(json.dumps(d_report, indent=2), encoding='utf-8')
    logging.info('PASS %s', p_log)


if __name__ == '__main__':
    main()
