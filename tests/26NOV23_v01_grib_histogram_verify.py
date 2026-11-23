"""📊 GRIB直方图独立小步闸门；默认直接运行。

输入：HDFSTUDIO_GRIB1_SAMPLE / HDFSTUDIO_GRIB2_SAMPLE、HDFSTUDIO_ZOOM_SAMPLE。
独立参考来自既有GRIB1/GRIB2位流parser，float64[H,W]，NaN为源缺测。
输出：logs/S4-GribIntegration/G3a-histogram-时间/，控制台和带时间戳日志。
调用：C:\\Python312\\python.exe tests/26NOV23_v01_grib_histogram_verify.py
"""
import hashlib
import importlib.util
import json
import logging
import os
import re
from pathlib import Path
import subprocess
import time

import numpy as np


def load_module(s_name, p_file):
    o_spec = importlib.util.spec_from_file_location(s_name, p_file)
    o_module = importlib.util.module_from_spec(o_spec)
    o_spec.loader.exec_module(o_module)
    return o_module


def main():
    p_root = Path(__file__).resolve().parents[1]
    p_log = p_root / 'logs/S4-GribIntegration' / time.strftime('G3a-histogram-%Y%m%d-%H%M%S')
    p_log.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s', handlers=[logging.StreamHandler(), logging.FileHandler(p_log / 'verify.log', encoding='utf-8')])
    d_env = os.environ.copy()
    d_env['QT_LOGGING_TO_CONSOLE'] = '1'
    s_cache = (p_root / 'build/vs2022/CMakeCache.txt').read_text(encoding='utf-8-sig')
    s_cmake = next(s_line.split('=', 1)[1] for s_line in s_cache.splitlines() if s_line.startswith('CMAKE_COMMAND:INTERNAL='))
    d_expected = {}
    d_report = {'status': 'RUNNING', 'sources': {}, 'samples': {}, 'configurations': {}}
    for p_source in [p_root / 'CMakeLists.txt', *sorted((p_root / 'src').rglob('*.cpp')), *sorted((p_root / 'src').rglob('*.hpp')), p_root / 'tests/grib_analysis_test.cpp', Path(__file__)]:
        d_report['sources'][str(p_source.relative_to(p_root))] = hashlib.sha256(p_source.read_bytes()).hexdigest()

    def run(l_command, p_prefix, i_timeout=1200):
        logging.info('Start %s', p_prefix)
        o_result = subprocess.run(list(map(str, l_command)), cwd=p_root, env=d_env, capture_output=True, timeout=i_timeout)
        p_prefix.with_suffix('.stdout.txt').write_bytes(o_result.stdout)
        p_prefix.with_suffix('.stderr.txt').write_bytes(o_result.stderr)
        assert o_result.returncode == 0, (p_prefix, o_result.returncode)
        logging.info('PASS %s', p_prefix)
        return o_result.stdout

    try:
        for i_edition in [1, 2]:
            s_prefix = 'HDFSTUDIO_GRIB' + str(i_edition) + '_'
            assert d_env.get(s_prefix + 'SAMPLE'), 'Set ' + s_prefix + 'SAMPLE'
            p_sample = Path(d_env[s_prefix + 'SAMPLE'])
            d_report['samples'][str(i_edition)] = {'source': str(p_sample), 'sha256_before': hashlib.sha256(p_sample.read_bytes()).hexdigest()}
            if i_edition == 1:
                o_reference = load_module('grib1_reference', p_root / 'tests/26NOV23_v01_grib_grid_verify.py')
                d_catalog = json.loads(run([p_root / 'build/output/Release/hdf_cli.exe', 'info', p_sample], p_log / 'grib1-catalog'))
                d_cases, d_arrays = o_reference.external_cases(p_sample, d_catalog)
                p_cases = p_log / 'grib1-cases.json'
                p_cases.write_text(json.dumps(d_cases, indent=2), encoding='utf-8')
            else:
                o_reference = load_module('grib2_reference', p_root / 'tests/26NOV23_v01_grib2_complex_reference.py')
                d_cases, d_arrays = o_reference.make_cases(p_sample, p_log / 'grib2-reference')
                p_cases = p_log / 'grib2-reference/external-grib2-cases.json'
            d_env[s_prefix + 'CASES'] = str(p_cases)
            d_expected[str(i_edition)] = {d_field['tag']: d_arrays[d_field['tag']] for d_field in d_cases['fields']}
        for s_config in ['Release', 'Debug']:
            p_config = p_log / s_config
            p_config.mkdir()
            p_binary = p_root / 'build/output' / s_config
            run([s_cmake, '--build', 'build/vs2022', '--config', s_config, '--target', 'grib_analysis_tests', 'grib_browser_tests', 'hdf_explorer', 'hdf_explorer_drop_tests', '--parallel', '6'], p_config / 'build')
            d_env['HDFSTUDIO_GRIB_EVIDENCE'] = str(p_config)
            run([p_binary / 'grib_analysis_tests.exe', '-o', str(p_config / 'histogram.txt') + ',txt'], p_config / 'histogram', 300)
            assert 'Totals: 7 passed, 0 failed, 0 skipped' in (p_config / 'histogram.txt').read_text(encoding='utf-8-sig')
            d_numeric = {}
            for s_edition, d_arrays in d_expected.items():
                d_actual = json.loads((p_config / ('histogram-grib' + s_edition + '.json')).read_text(encoding='utf-8'))
                assert set(d_actual) == set(d_arrays)
                d_boundary_review = {}
                for s_tag, a_array in d_arrays.items():
                    a_finite = a_array[np.isfinite(a_array)]
                    d_histogram = d_actual[s_tag]
                    assert d_histogram['total'] == d_histogram['sampled'] == a_finite.size
                    assert d_histogram['missing'] == a_array.size - a_finite.size
                    a_counts, a_edges = np.histogram(a_finite, bins=32)
                    # Only cumulative counts at a rounded bin boundary may differ.
                    # Bound each boundary independently by 16 eps of the actual data range.
                    a_product_counts = np.asarray(d_histogram['counts'], dtype=np.int64)
                    if not np.array_equal(a_counts, a_product_counts):
                        a_product_edges = np.asarray(d_histogram['edges'])
                        f_rounding = 16 * np.finfo(float).eps * np.max(np.abs(a_finite))
                        assert np.max(np.abs(a_product_edges - a_edges)) <= f_rounding
                        a_sorted = np.sort(a_finite)
                        a_lower = np.searchsorted(a_sorted, a_product_edges[1:-1] - f_rounding, side='left')
                        a_upper = np.searchsorted(a_sorted, a_product_edges[1:-1] + f_rounding, side='right')
                        a_cumulative = np.cumsum(a_product_counts)[:-1]
                        assert ((a_lower <= a_cumulative) & (a_cumulative <= a_upper)).all(), (s_edition, s_tag)
                        assert a_product_counts.sum() == a_counts.sum()
                        s_cli = run([p_binary / 'hdf_cli.exe', 'hist', d_report['samples'][s_edition]['source'], '/field/' + s_tag.split('-')[-1], '1', '32'], p_config / ('boundary-cli-' + s_tag), 60).decode('utf-8-sig')
                        l_cli_counts = [int(o_match.group(1)) for o_match in re.finditer(r'^\s*\d+\s+\[[^\n]+?\)\s+(\d+)', s_cli, re.MULTILINE)]
                        np.testing.assert_array_equal(l_cli_counts, a_product_counts)
                        d_boundary_review[s_tag] = {'absolute_count_difference': int(np.abs(a_counts - a_product_counts).sum()), 'boundary_tolerance': float(f_rounding), 'boundary_ambiguities': int(np.sum(a_upper - a_lower))}
                    for s_key, f_expected in [('minimum', np.min(a_finite)), ('maximum', np.max(a_finite)), ('mean', np.mean(a_finite)), ('stddev', np.std(a_finite, ddof=1))]:
                        np.testing.assert_allclose(d_histogram[s_key], f_expected, rtol=5e-12, atol=0)
                    assert d_histogram['reader'] == 'ecCodes' and d_histogram['decodeProfile'] == 'GribDecodedV1'
                d_numeric[s_edition] = {'fields': len(d_arrays), 'positions': sum(a_values.size for a_values in d_arrays.values()), 'missing': sum(int(np.isnan(a_values).sum()) for a_values in d_arrays.values()), 'rounded_bin_boundaries': d_boundary_review}
            run([p_binary / 'hdf_explorer_drop_tests.exe', 'histogramSamplesOneInTwentyAndBinsDecodedValues', 'histogramOrRasterShowsGrayEntropy', 'bottomFileInfoPanelShowsCurrentFileMetadata', 'propertiesDialogCopiesReportAndWindowMenuListsViews', '-o', str(p_config / 'adjacent.txt') + ',txt'], p_config / 'adjacent', 180)
            assert 'Totals: 6 passed, 0 failed, 0 skipped' in (p_config / 'adjacent.txt').read_text(encoding='utf-8-sig')
            # Existing browser has two optional corpus tests; this environment supplies both.
            run([p_binary / 'grib_browser_tests.exe', '-o', str(p_config / 'browser.txt') + ',txt'], p_config / 'browser', 180)
            assert 'Totals: 18 passed, 0 failed, 0 skipped' in (p_config / 'browser.txt').read_text(encoding='utf-8-sig')
            d_report['configurations'][s_config] = {'histogram_passed': 7, 'adjacent_passed': 6, 'browser_passed': 18, 'numeric': d_numeric, 'binary_sha256': hashlib.sha256((p_binary / 'HDFStudio.exe').read_bytes()).hexdigest()}
        for s_edition, d_sample in d_report['samples'].items():
            d_sample['sha256_after'] = hashlib.sha256(Path(d_sample['source']).read_bytes()).hexdigest()
            assert d_sample['sha256_before'] == d_sample['sha256_after']
        d_report['status'] = 'PASS'
    except Exception:
        d_report['status'] = 'FAIL'
        logging.exception('Histogram gate failed')
        raise
    finally:
        (p_log / 'result.json').write_text(json.dumps(d_report, indent=2), encoding='utf-8')
    logging.info('PASS histogram gate %s', p_log)


if __name__ == '__main__':
    main()
