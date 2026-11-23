"""🖼️ G3b GRIB纹理熵双配置验证，直接运行。

输入：HDFSTUDIO_GRIB1_SAMPLE、HDFSTUDIO_GRIB2_SAMPLE（只读）；
可选HDFSTUDIO_GRIB_HISTOGRAM_GATE指向已通过的G3a参考目录。
输出：logs/S4-GribIntegration/G3b-texture-时间/，float64[H,W]、CSV、PNG与日志。
调用：C:\\Python312\\python.exe tests/26NOV23_v01_grib_texture_verify.py
"""
import datetime
import hashlib
import importlib.util
import json
import logging
import os
from pathlib import Path
import subprocess

import numpy as np


def load_module(s_name, p_path):
    o_spec = importlib.util.spec_from_file_location(s_name, p_path)
    o_module = importlib.util.module_from_spec(o_spec)
    o_spec.loader.exec_module(o_module)
    return o_module


def main():
    p_root = Path(__file__).resolve().parents[1]
    p_folder = p_root / 'logs/S4-GribIntegration' / ('G3b-texture-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    p_folder.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s', handlers=[logging.StreamHandler(), logging.FileHandler(p_folder / 'verify.log', encoding='utf-8')])
    d_env = os.environ.copy()
    d_env['QT_LOGGING_TO_CONSOLE'] = '1'
    p_reference = Path(d_env.get('HDFSTUDIO_GRIB_HISTOGRAM_GATE', ''))
    if not d_env.get('HDFSTUDIO_GRIB_HISTOGRAM_GATE'):
        l_candidates = sorted(p_path.parent for p_path in (p_root / 'logs/S4-GribIntegration').glob('G3a-histogram-*/result.json') if json.loads(p_path.read_text(encoding='utf-8'))['status'] == 'PASS')
        assert l_candidates, 'Complete G3a first'
        p_reference = l_candidates[-1]
    s_cache = (p_root / 'build/vs2022/CMakeCache.txt').read_text(encoding='utf-8-sig')
    s_cmake = next(s_line.split('=', 1)[1] for s_line in s_cache.splitlines() if s_line.startswith('CMAKE_COMMAND:INTERNAL='))
    d_report = {'status': 'RUNNING', 'reference': str(p_reference), 'sources': {}, 'configurations': {}, 'samples': {}}
    for p_path in [p_root / 'CMakeLists.txt', *sorted((p_root / 'src').rglob('*.cpp')), *sorted((p_root / 'src').rglob('*.hpp')), p_root / 'tests/grib_analysis_test.cpp', p_root / 'tests/drop_targets_test.cpp', Path(__file__)]:
        d_report['sources'][str(p_path.relative_to(p_root))] = hashlib.sha256(p_path.read_bytes()).hexdigest()
    o_grid = load_module('grid_reference', p_root / 'tests/26NOV23_v01_grib_grid_verify.py')
    o_texture = load_module('texture_reference', p_root / 'tests/26NOV21_v01_texture_entropy_checks.py')
    o_png = load_module('png_reference', p_root / 'tests/26NOV21_v01_full_regression.py')
    d_expected = {}

    def run(l_arguments, p_prefix, i_timeout=1200):
        logging.info('Start %s', p_prefix)
        o_result = subprocess.run(list(map(str, l_arguments)), cwd=p_root, env=d_env, capture_output=True, timeout=i_timeout)
        p_prefix.with_suffix('.stdout.txt').write_bytes(o_result.stdout)
        p_prefix.with_suffix('.stderr.txt').write_bytes(o_result.stderr)
        assert o_result.returncode == 0, (str(p_prefix), o_result.returncode)
        logging.info('PASS %s', p_prefix)
        return o_result.stdout

    try:
        for i_edition in [1, 2]:
            s_prefix = 'HDFSTUDIO_GRIB' + str(i_edition) + '_'
            p_sample = Path(d_env[s_prefix + 'SAMPLE'])
            d_report['samples'][str(i_edition)] = {'source': str(p_sample), 'before': hashlib.sha256(p_sample.read_bytes()).hexdigest()}
            p_cases = p_reference / ('grib1-cases.json' if i_edition == 1 else 'grib2-reference/external-grib2-cases.json')
            d_env[s_prefix + 'CASES'] = str(p_cases)
            d_cases = json.loads(p_cases.read_text(encoding='utf-8'))
            if i_edition == 1:
                d_catalog = json.loads((p_reference / 'grib1-catalog.stdout.txt').read_text(encoding='utf-8-sig'))
                _, d_arrays = o_grid.external_cases(p_sample, d_catalog)
                d_expected[i_edition] = {d_item['tag']: d_arrays[d_item['tag']] for d_item in d_cases['fields']}
            else:
                d_expected[i_edition] = {d_item['tag']: np.fromfile(p_reference / 'grib2-reference' / (d_item['tag'] + '.f64'), dtype='<f8').reshape(d_item['rows'], d_item['cols']) for d_item in d_cases['fields']}
        for s_config in ['Release', 'Debug']:
            p_config = p_folder / s_config
            p_config.mkdir()
            p_binary = p_root / 'build/output' / s_config
            run([s_cmake, '--build', 'build/vs2022', '--config', s_config, '--target', 'grib_analysis_tests', 'hdf_explorer', 'hdf_explorer_drop_tests', '--parallel', '6'], p_config / 'build')
            d_env['HDFSTUDIO_GRIB_EVIDENCE'] = str(p_config)
            run([p_binary / 'grib_analysis_tests.exe', '-o', str(p_config / 'analysis.txt') + ',txt'], p_config / 'analysis', 420)
            assert 'Totals: 12 passed, 0 failed, 0 skipped' in (p_config / 'analysis.txt').read_text(encoding='utf-8-sig')
            d_numeric = {}
            for i_edition, d_arrays in d_expected.items():
                i_positions = 0
                f_max_error = 0.
                for s_tag, a_reference in d_arrays.items():
                    s_stem = 'grib' + str(i_edition) + '-' + s_tag
                    a_decoded = np.fromfile(p_config / ('decoded-' + s_stem + '.f64'), dtype='<f8').reshape(a_reference.shape)
                    np.testing.assert_array_equal(np.isnan(a_decoded), np.isnan(a_reference))
                    np.testing.assert_allclose(a_decoded, a_reference, rtol=8 * np.finfo(float).eps, atol=0, equal_nan=True)
                    _, a_expected = o_texture.texture_reference(a_decoded)
                    a_actual = np.fromfile(p_config / ('texture-' + s_stem + '.f64'), dtype='<f8').reshape(a_reference.shape)
                    np.testing.assert_allclose(a_actual, a_expected, rtol=0, atol=3e-14, equal_nan=True)
                    f_max_error = max(f_max_error, float(np.nanmax(np.abs(a_actual - a_expected))))
                    i_positions += a_reference.size
                    if s_tag == 'field-0':
                        a_csv = np.genfromtxt(p_config / ('texture-' + s_stem + '.csv'), names=True, delimiter=',')
                        np.testing.assert_array_equal(a_csv['entropy_bits'].reshape(a_reference.shape), a_actual)
                        a_rgb = o_png.png_rgb(p_config / ('texture-' + s_stem + '.png'))
                        assert a_rgb.shape == (*a_reference.shape, 3)
                        d_palette = json.loads((p_root / 'resources/colormaps/palettes/bids_viridis.json').read_text(encoding='utf-8'))
                        a_colors = np.asarray(d_palette['colors'])[:, :3]
                        a_expected_rgb = np.full(a_rgb.shape, 110, dtype=np.uint8)
                        a_valid = np.isfinite(a_expected)
                        for i_channel in range(3):
                            a_mapped = np.interp(a_expected[a_valid] / 4, d_palette['positions'], a_colors[:, i_channel])
                            a_expected_rgb[:, :, i_channel][a_valid] = np.floor(np.clip(a_mapped, 0, 1) * 255 + .5).astype(np.uint8)
                        np.testing.assert_array_equal(a_rgb, a_expected_rgb)
                d_numeric[str(i_edition)] = {'fields': len(d_arrays), 'positions': i_positions, 'max_entropy_absolute_error': f_max_error}
            d_env['HDFSTUDIO_SPLASH_EVIDENCE'] = str(p_config / 'startup-splash.png')
            run([p_binary / 'hdf_explorer_drop_tests.exe', 'startupSplashIsPlainVisibleWidgetNotQSplashScreen', 'textureEntropyBlocksAndInterpolation', 'textureEntropyViewIsIndependent2DImage', 'textureEntropySnapshotFollowsMaskAndSourceLifetime', 'textureEntropyBudgetAndInvalidRecovery', 'histogramSamplesOneInTwentyAndBinsDecodedValues', '-o', str(p_config / 'adjacent.txt') + ',txt'], p_config / 'adjacent', 180)
            assert 'Totals: 8 passed, 0 failed, 0 skipped' in (p_config / 'adjacent.txt').read_text(encoding='utf-8-sig')
            d_report['configurations'][s_config] = {'analysis_passed': 12, 'adjacent_passed': 8, 'numeric': d_numeric, 'binary_sha256': hashlib.sha256((p_binary / 'HDFStudio.exe').read_bytes()).hexdigest()}
        for d_item in d_report['samples'].values():
            d_item['after'] = hashlib.sha256(Path(d_item['source']).read_bytes()).hexdigest()
            assert d_item['before'] == d_item['after']
        for s_name, s_hash in d_report['sources'].items():
            assert hashlib.sha256((p_root / s_name).read_bytes()).hexdigest() == s_hash, s_name
        d_report['status'] = 'PASS'
    except Exception:
        d_report['status'] = 'FAIL'
        logging.exception('Texture gate failed')
        raise
    finally:
        (p_folder / 'result.json').write_text(json.dumps(d_report, indent=2), encoding='utf-8')
    logging.info('PASS %s', p_folder)


if __name__ == '__main__':
    main()
