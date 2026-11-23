"""📈 G4a任意轴剖面CLI/worker双配置与独立位流数值验收。

输入：只读HDFSTUDIO_GRIB1_SAMPLE/HDFSTUDIO_GRIB2_SAMPLE及现有小夹具。
输出：logs/S4-GribIntegration/G4a-slice-时间/，构建、QtTest、参考与hash记录。
命令：C:\\Python312\\python.exe tests/26NOV23_v01_grib_slice_verify.py
可选HDFSTUDIO_GRIB_SLICE_RED=1仅Release；不安装依赖，不修改业务源。
"""
import datetime
import hashlib
import importlib.util
import json
import logging
import os
from pathlib import Path
import struct
import subprocess

import numpy as np


def digest(p_file):
    with p_file.open('rb') as o_file:
        return hashlib.file_digest(o_file, 'sha256').hexdigest()


def load_module(s_name, p_file):
    o_spec = importlib.util.spec_from_file_location(s_name, p_file)
    o_module = importlib.util.module_from_spec(o_spec)
    o_spec.loader.exec_module(o_module)
    return o_module


def main():
    p_root = Path(__file__).resolve().parents[1]
    p_log = p_root / 'logs/S4-GribIntegration' / ('G4a-slice-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    p_log.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s', handlers=[logging.StreamHandler(), logging.FileHandler(p_log / 'verify.log', encoding='utf-8')])
    d_env = os.environ.copy()
    d_env['QT_LOGGING_TO_CONSOLE'] = '1'
    s_cache = (p_root / 'build/vs2022/CMakeCache.txt').read_text(encoding='utf-8-sig')
    s_cmake = next(s_line.split('=', 1)[1] for s_line in s_cache.splitlines() if s_line.startswith('CMAKE_COMMAND:INTERNAL='))
    d_report = {'status': 'RUNNING', 'sources': {}, 'samples': {}, 'configurations': {}}
    for p_file in [p_root / 'CMakeLists.txt', *sorted((p_root / 'src').rglob('*.cpp')), *sorted((p_root / 'src').rglob('*.hpp')), p_root / 'tests/grib_slice_test.cpp', Path(__file__)]:
        d_report['sources'][str(p_file.relative_to(p_root))] = digest(p_file)
    for i_edition in [1, 2]:
        p_file = Path(d_env['HDFSTUDIO_GRIB' + str(i_edition) + '_SAMPLE'])
        d_report['samples'][str(i_edition)] = {'source': str(p_file), 'before': digest(p_file)}

    def run(l_command, p_prefix, i_timeout=1200):
        logging.info('Start %s', p_prefix)
        with p_prefix.with_suffix('.stdout.txt').open('wb') as o_stdout, p_prefix.with_suffix('.stderr.txt').open('wb') as o_stderr:
            o_run = subprocess.run(list(map(str, l_command)), cwd=p_root, env=d_env, stdout=o_stdout, stderr=o_stderr, timeout=i_timeout)
        assert o_run.returncode == 0, (str(p_prefix), o_run.returncode)
        logging.info('PASS %s', p_prefix)

    try:
        run([s_cmake, '-S', '.', '-B', 'build/vs2022'], p_log / 'configure')
        l_configs = ['Release'] if d_env.get('HDFSTUDIO_GRIB_SLICE_RED') == '1' else ['Release', 'Debug']
        o_grib1 = load_module('slice_grib1_reference', p_root / 'tests/26NOV23_v01_grib_grid_verify.py')
        o_grib2 = load_module('slice_grib2_reference', p_root / 'tests/26NOV23_v01_grib2_complex_reference.py')
        logging.info('Decode independent GRIB2 reference')
        d_cases2, d_arrays2 = o_grib2.make_cases(Path(d_report['samples']['2']['source']), p_log / 'grib2-reference')
        d_ref2 = {int(d_case['target'].split('/')[-1]): d_arrays2[d_case['tag']] for d_case in d_cases2['fields']}
        for s_config in l_configs:
            p_config = p_log / s_config
            p_config.mkdir()
            p_binary = p_root / 'build/output' / s_config
            run([s_cmake, '--build', 'build/vs2022', '--config', s_config, '--target', 'grib_slice_tests', 'hovmoller_tests', 'grib_integration_tests', 'grib_gui_tests', 'hdf_explorer', '--parallel', '6'], p_config / 'build')
            d_env['HDFSTUDIO_GRIB_EVIDENCE'] = str(p_config)
            run([p_binary / 'grib_slice_tests.exe', '-o', str(p_config / 'slice.txt') + ',txt'], p_config / 'slice', 300)
            assert 'Totals: 11 passed, 0 failed, 0 skipped' in (p_config / 'slice.txt').read_text(encoding='utf-8-sig')
            run([p_binary / 'hovmoller_tests.exe', '-o', str(p_config / 'adjacent.txt') + ',txt'], p_config / 'adjacent', 300)
            assert 'Totals: 20 passed, 0 failed, 0 skipped' in (p_config / 'adjacent.txt').read_text(encoding='utf-8-sig')
            run([p_binary / 'grib_integration_tests.exe', '-o', str(p_config / 'core.txt') + ',txt'], p_config / 'core', 180)
            run([p_binary / 'grib_gui_tests.exe', '-o', str(p_config / 'scheduler.txt') + ',txt'], p_config / 'scheduler', 180)
            for s_suite in ['core', 'scheduler']:
                s_text = (p_config / (s_suite + '.txt')).read_text(encoding='utf-8-sig')
                assert '0 failed, 0 skipped' in s_text
            run([p_binary / 'hdf_cli.exe', 'info', d_report['samples']['1']['source']], p_config / 'catalog-grib1', 180)
            d_catalog = json.loads((p_config / 'catalog-grib1.stdout.txt').read_text(encoding='utf-8-sig'))
            d_numeric = {}
            for i_edition in [1, 2]:
                d_sections = json.loads((p_config / ('section-grib' + str(i_edition) + '.json')).read_text(encoding='utf-8'))
                i_positions = 0
                for d_field in d_sections['fields']:
                    i_index = d_field['field']
                    if i_edition == 1:
                        a_expected = o_grib1.decode_grib1_simple(Path(d_report['samples']['1']['source']), d_catalog['fields'][i_index])
                    else:
                        a_expected = d_ref2[i_index]
                    a_actual = np.fromfile(p_config / d_field['file'], dtype='<f8').reshape(int(d_field['rows']), int(d_field['cols']))
                    assert np.array_equal(np.isnan(a_actual), np.isnan(a_expected.T))
                    assert np.array_equal(a_actual == 0, a_expected.T == 0)
                    if i_edition == 1:
                        np.testing.assert_array_equal(a_actual, a_expected.T)
                    else:
                        np.testing.assert_allclose(a_actual, a_expected.T, rtol=8 * np.finfo(float).eps, atol=0, equal_nan=True)
                    i_positions += a_actual.size
                d_numeric[str(i_edition)] = {'fields': len(d_sections['fields']), 'positions': i_positions}
                if 'cube' in d_sections:
                    d_cube = d_sections['cube']
                    b_binary = (p_config / d_cube['file']).read_bytes()
                    t_header = struct.unpack('<8sIIQQQQ', b_binary[:48])
                    assert t_header == (b'HDFSLICE', 2, 0, int(d_cube['rows']), int(d_cube['cols']), 9007199254741009, len(b_binary) - 48)
                    a_actual = np.frombuffer(b_binary, dtype='<f8', offset=48).reshape(int(d_cube['rows']), int(d_cube['cols']))
                    a_expected = np.full(a_actual.shape, np.nan)
                    for i_col, i_field in enumerate(d_cube['fields']):
                        if i_field >= 0:
                            a_expected[:, i_col] = o_grib1.decode_grib1_simple(Path(d_report['samples']['1']['source']), d_catalog['fields'][i_field])[:, d_cube['fixed_x']]
                    np.testing.assert_array_equal(a_actual, a_expected)
                    d_numeric[str(i_edition)]['cube_worker_positions'] = a_actual.size
                    d_numeric[str(i_edition)]['cube_source_fields'] = d_cube['fields']
            d_report['configurations'][s_config] = {'special_passed': 11, 'adjacent_passed': 20, 'numeric': d_numeric, 'binary_sha256': digest(p_binary / 'HDFStudio.exe'), 'cli_sha256': digest(p_binary / 'hdf_cli.exe')}
        for s_name, s_hash in d_report['sources'].items():
            assert digest(p_root / s_name) == s_hash, s_name
        for d_sample in d_report['samples'].values():
            d_sample['after'] = digest(Path(d_sample['source']))
            assert d_sample['after'] == d_sample['before']
        d_report['status'] = 'PASS'
    except Exception:
        d_report['status'] = 'FAIL'
        logging.exception('GRIB section verification failed')
        raise
    finally:
        (p_log / 'result.json').write_text(json.dumps(d_report, indent=2), encoding='utf-8')
    logging.info('PASS %s', p_log)


if __name__ == '__main__':
    main()
