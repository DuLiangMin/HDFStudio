"""🖼️ GRIB图像独立闸门：双配置构建、QtTest、业务平面与批量PNG对账。

输入：CMakeCache；HDFSTUDIO_GRIB1_SAMPLE（只读）、HDFSTUDIO_ZOOM_SAMPLE，
可选HDFSTUDIO_GRIB2_SAMPLE（只读）。
输出：logs/S4-GribIntegration/G2d-image-时间/。使用方式：直接python执行本文件。
先完成全部构建/原生测试，再运行desktop.ps1 -Image，避免EXE锁。
"""
from pathlib import Path
import datetime
import importlib.util
import json
import logging
import os
import re
import subprocess
import struct
import zlib

import numpy as np


def png_pixels(p_path):
    """读取Qt导出的8-bit RGB/RGBA PNG，仅用于逐像素比较，无额外图像依赖。"""
    b_png = p_path.read_bytes()
    assert b_png[:8] == b'\x89PNG\r\n\x1a\n'
    i_position = 8
    l_data = []
    while i_position < len(b_png):
        i_length = struct.unpack('>I', b_png[i_position:i_position + 4])[0]
        b_type = b_png[i_position + 4:i_position + 8]
        b_data = b_png[i_position + 8:i_position + 8 + i_length]
        assert zlib.crc32(b_type + b_data) & 0xffffffff == struct.unpack('>I', b_png[i_position + 8 + i_length:i_position + 12 + i_length])[0]
        if b_type == b'IHDR':
            i_width, i_height, i_depth, i_color, i_compression, i_filter, i_interlace = struct.unpack('>IIBBBBB', b_data)
            assert i_depth == 8 and i_color in [2, 6] and i_compression == i_filter == i_interlace == 0
            i_channels = 3 if i_color == 2 else 4
        elif b_type == b'IDAT':
            l_data.append(b_data)
        elif b_type == b'IEND':
            break
        i_position += i_length + 12
    a_raw = np.frombuffer(zlib.decompress(b''.join(l_data)), dtype=np.uint8).reshape(i_height, 1 + i_width * i_channels)
    a_pixels = np.zeros((i_height, i_width * i_channels), dtype=np.uint8)
    for i_row in range(i_height):
        i_method = int(a_raw[i_row, 0])
        assert i_method in range(5)
        for i_column in range(i_width * i_channels):
            i_left = int(a_pixels[i_row, i_column - i_channels]) if i_column >= i_channels else 0
            i_above = int(a_pixels[i_row - 1, i_column]) if i_row else 0
            i_corner = int(a_pixels[i_row - 1, i_column - i_channels]) if i_row and i_column >= i_channels else 0
            i_predictor = 0
            if i_method == 1:
                i_predictor = i_left
            elif i_method == 2:
                i_predictor = i_above
            elif i_method == 3:
                i_predictor = (i_left + i_above) // 2
            elif i_method == 4:
                i_base = i_left + i_above - i_corner
                l_distances = [abs(i_base - i_left), abs(i_base - i_above), abs(i_base - i_corner)]
                i_predictor = [i_left, i_above, i_corner][l_distances.index(min(l_distances))]
            a_pixels[i_row, i_column] = (int(a_raw[i_row, i_column + 1]) + i_predictor) & 255
    return a_pixels.reshape(i_height, i_width, i_channels)[:, :, :3]


def compare_reference(a_actual, a_expected):
    """缺测和零值位置精确相等；非零值只允许double十进制缩放舍入误差。"""
    np.testing.assert_array_equal(np.isnan(a_actual), np.isnan(a_expected))
    a_finite = np.isfinite(a_expected)
    assert np.isfinite(a_actual[a_finite]).all()
    np.testing.assert_array_equal(a_actual == 0, a_expected == 0)
    np.testing.assert_allclose(a_actual, a_expected, rtol=8 * np.finfo(np.float64).eps, atol=0, equal_nan=True)
    a_difference = np.abs(a_actual[a_finite] - a_expected[a_finite])
    a_nonzero = a_finite & (a_expected != 0)
    f_relative = float(np.max(np.abs((a_actual[a_nonzero] - a_expected[a_nonzero]) / a_expected[a_nonzero]))) if a_nonzero.any() else 0.0
    return {'different_finite_values': int(np.count_nonzero(a_difference)), 'max_absolute_error': float(a_difference.max()) if a_difference.size else 0.0, 'max_relative_error': f_relative}


def main():
    p_root = Path(__file__).resolve().parents[1]
    p_log = p_root / 'logs/S4-GribIntegration' / ('G2d-image-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    p_log.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s', handlers=[logging.StreamHandler(), logging.FileHandler(p_log / 'run.log', encoding='utf-8')])
    o_spec = importlib.util.spec_from_file_location('grib_grid_reference', p_root / 'tests/26NOV23_v01_grib_grid_verify.py')
    o_reference = importlib.util.module_from_spec(o_spec)
    o_spec.loader.exec_module(o_reference)
    d_env = os.environ.copy()
    d_env.update(QT_QPA_PLATFORM='windows', QT_LOGGING_TO_CONSOLE='1')
    p_source = Path(d_env['HDFSTUDIO_GRIB1_SAMPLE'])
    assert p_source.is_file() and Path(d_env['HDFSTUDIO_ZOOM_SAMPLE']).is_file()
    s_cache = (p_root / 'build/vs2022/CMakeCache.txt').read_text(encoding='utf-8-sig')
    s_cmake = next(s_line.split('=', 1)[1] for s_line in s_cache.splitlines() if s_line.startswith('CMAKE_COMMAND:INTERNAL='))
    p_grib2 = Path(d_env['HDFSTUDIO_GRIB2_SAMPLE']) if d_env.get('HDFSTUDIO_GRIB2_SAMPLE') else None
    d_grib2_cases = None
    d_grib2_expected = None
    if p_grib2:
        o_grib2_spec = importlib.util.spec_from_file_location('grib2_complex_reference', p_root / 'tests/26NOV23_v01_grib2_complex_reference.py')
        o_grib2 = importlib.util.module_from_spec(o_grib2_spec)
        o_grib2_spec.loader.exec_module(o_grib2)
        d_grib2_cases, d_grib2_expected = o_grib2.make_cases(p_grib2, p_log / 'grib2-reference')
        d_env['HDFSTUDIO_GRIB2_CASES'] = str(p_log / 'grib2-reference/external-grib2-cases.json')
    l_sources = [p_root / 'CMakeLists.txt', *sorted((p_root / 'src').rglob('*.hpp')), *sorted((p_root / 'src').rglob('*.cpp')), p_root / 'tests/grib_browser_test.cpp', Path(__file__), p_root / 'tests/26NOV23_v01_grib_grid_verify.py', p_root / 'tests/26NOV23_v01_grib2_complex_reference.py']
    d_report = {'status': 'RUNNING', 'source_sha256': o_reference.digest(p_source), 'sources': {str(p_path.relative_to(p_root)): o_reference.digest(p_path) for p_path in l_sources}, 'configurations': {}}
    if p_grib2:
        d_report['grib2'] = {'source': str(p_grib2), 'sha256_before': o_reference.digest(p_grib2), 'fields': len(d_grib2_cases['fields'])}

    def save():
        (p_log / 'result.json').write_text(json.dumps(d_report, indent=2), encoding='utf-8')

    def run(l_command, p_prefix, i_timeout=1200):
        logging.info('Start %s', p_prefix)
        with p_prefix.with_suffix('.stdout.txt').open('wb') as o_out, p_prefix.with_suffix('.stderr.txt').open('wb') as o_err:
            o_result = subprocess.run(list(map(str, l_command)), cwd=p_root, env=d_env, stdout=o_out, stderr=o_err, timeout=i_timeout)
        assert o_result.returncode == 0, (p_prefix, o_result.returncode)
        logging.info('PASS %s', p_prefix.name)

    def totals(p_path, i_count):
        s_text = p_path.read_text(encoding='utf-8-sig')
        o_match = re.search(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped', s_text)
        assert o_match and tuple(map(int, o_match.groups())) == (i_count, 0, 0), p_path

    save()
    try:
        for s_config in ['Release', 'Debug']:
            p_config = p_log / s_config
            p_config.mkdir()
            p_binary = p_root / 'build/output' / s_config
            d_env['HDFSTUDIO_GRIB_EVIDENCE'] = str(p_config)
            run([s_cmake, '--build', 'build/vs2022', '--config', s_config, '--target', 'grib_browser_tests', 'hdf_explorer', 'hdf_explorer_drop_tests', 'hovmoller_tests', '--parallel', '6'], p_config / 'build')
            o_catalog = subprocess.run([str(p_binary / 'hdf_cli.exe'), 'info', str(p_source)], env=d_env, capture_output=True, timeout=180)
            assert o_catalog.returncode == 0
            (p_config / 'external-catalog.json').write_bytes(o_catalog.stdout)
            d_cases, d_expected = o_reference.external_cases(p_source, json.loads(o_catalog.stdout))
            p_cases = p_config / 'external-cases.json'
            p_cases.write_text(json.dumps(d_cases, indent=2), encoding='utf-8')
            d_env['HDFSTUDIO_GRIB1_CASES'] = str(p_cases)
            run([p_binary / 'grib_browser_tests.exe', '-o', str(p_config / 'grib.txt') + ',txt'], p_config / 'grib', 180)
            i_tests = 18 if p_grib2 else 17
            if p_grib2:
                totals(p_config / 'grib.txt', i_tests)
            else:
                s_text = (p_config / 'grib.txt').read_text(encoding='utf-8-sig')
                assert 'Totals: 17 passed, 0 failed, 1 skipped' in s_text
            i_values = 0
            i_pixels = 0
            for d_field in d_cases['fields']:
                s_tag = d_field['tag']
                a_expected = d_expected[s_tag]
                np.testing.assert_array_equal(np.fromfile(p_config / ('raster-' + s_tag + '.f64'), dtype='<f8').reshape(a_expected.shape), a_expected)
                i_values += a_expected.size
                p_output = p_config / ('batch-' + s_tag + '.png')
                run([p_binary / 'HDFStudio.exe', '--preview', d_field['target'], '--png', p_output, p_source], p_config / ('batch-' + s_tag), 180)
                a_gui = png_pixels(p_config / ('raster-' + s_tag + '.png'))
                a_batch = png_pixels(p_output)
                np.testing.assert_array_equal(a_gui, a_batch)
                i_pixels += a_gui.shape[0] * a_gui.shape[1]
                d_meta = json.loads(Path(str(p_output) + '.json').read_text(encoding='utf-8-sig'))
                assert d_meta['reader'] == 'ecCodes' and d_meta['decodeProfile'] == 'GribDecodedV1'
                assert d_meta['minimum'] == float(a_expected.min()) and d_meta['maximum'] == float(a_expected.max())
            d_grib2_counts = {}
            if p_grib2:
                i_grib2_values = 0
                i_grib2_missing = 0
                d_differences = {}
                for d_field in d_grib2_cases['fields']:
                    s_tag = d_field['tag']
                    a_expected = d_grib2_expected[s_tag]
                    a_raster = np.fromfile(p_config / ('grib2-raster-' + s_tag + '.f64'), dtype='<f8').reshape(a_expected.shape)
                    a_grid = np.genfromtxt(p_config / ('grib2-' + s_tag + '.csv'), delimiter=',', comments='#')
                    p_cli_csv = p_config / ('grib2-cli-' + s_tag + '.csv')
                    run([p_binary / 'hdf_cli.exe', 'csv', p_grib2, d_field['target'], p_cli_csv], p_config / ('grib2-cli-' + s_tag), 60)
                    a_cli = np.genfromtxt(p_cli_csv, delimiter=',')
                    d_differences[s_tag] = compare_reference(a_raster, a_expected)
                    np.testing.assert_array_equal(a_grid, a_raster)
                    np.testing.assert_array_equal(a_cli, a_raster)
                    i_grib2_values += a_expected.size
                    i_grib2_missing += int(np.isnan(a_expected).sum())
                i_grib2_pixels = 0
                for i_index in d_grib2_cases['png_fields']:
                    s_tag = 'field-' + str(i_index)
                    p_output = p_config / ('grib2-batch-' + s_tag + '.png')
                    run([p_binary / 'HDFStudio.exe', '--preview', '/field/' + str(i_index), '--png', p_output, p_grib2], p_config / ('grib2-batch-' + s_tag), 90)
                    a_png = png_pixels(p_config / ('grib2-raster-' + s_tag + '.png'))
                    np.testing.assert_array_equal(a_png, png_pixels(p_output))
                    i_grib2_pixels += a_png.shape[0] * a_png.shape[1]
                (p_config / 'grib2-reference-errors.json').write_text(json.dumps(d_differences, indent=2), encoding='utf-8')
                d_grib2_counts = {'fields': len(d_grib2_cases['fields']), 'values_per_path': i_grib2_values, 'missing_positions': i_grib2_missing, 'png_pixels': i_grib2_pixels, 'max_relative_error': max(d_entry['max_relative_error'] for d_entry in d_differences.values()), 'reference_rtol': 8 * np.finfo(np.float64).eps, 'reference_atol': 0}
            l_adjacent = ['classicNetcdfOpensWithTreeGridImagePropertiesAndExport', 'colorMapUiUsesCachedDecodedPlane', 'colorMapLatestAsyncRenderWins', 'colorMapMissingCategoriesColorbarAndBudget', 'maskRangeUsesDecodedValuesWithOpenEnds', 'rasterFlipAndResetRestoreOriginalView', 'rasterZoomButtonsWheelAndCyclesPreserveData', 'regressionGuiExportsCurrentFullResolutionRaster', 'regressionAsyncLayersMasksAndRecovery', 'regressionBudgetIsReleasedAfterClose', 'workspaceSyncAnchorLagAndOutOfRange', 'layerPlaybackSingleAndLinkedPlanes', 'layerPlaybackStopsOnContextChanges', 'closeFileRemovesItsViewsAndReleasesTheFile']
            run([p_binary / 'hdf_explorer_drop_tests.exe', *l_adjacent, '-o', str(p_config / 'adjacent.txt') + ',txt'], p_config / 'adjacent', 240)
            totals(p_config / 'adjacent.txt', 16)
            run([p_binary / 'hovmoller_tests.exe', 'workerProtocol', 'viewLatestAndPlayback', 'viewCloseLifecycle', 'heartbeatAndCancel', '-o', str(p_config / 'section.txt') + ',txt'], p_config / 'section', 180)
            totals(p_config / 'section.txt', 6)
            d_report['configurations'][s_config] = {'tests': i_tests, 'adjacent': 16, 'section': 6, 'business_values': i_values, 'png_pixels': i_pixels, 'grib2': d_grib2_counts, 'binary_sha256': o_reference.digest(p_binary / 'HDFStudio.exe')}
            save()
        assert o_reference.digest(p_source) == d_report['source_sha256']
        if p_grib2:
            d_report['grib2']['sha256_after'] = o_reference.digest(p_grib2)
            assert d_report['grib2']['sha256_after'] == d_report['grib2']['sha256_before']
        for s_name, s_hash in d_report['sources'].items():
            assert o_reference.digest(p_root / s_name) == s_hash, s_name
        d_report['status'] = 'PASS'
        logging.info('PASS image gate: %s', p_log)
    except Exception:
        d_report['status'] = 'FAIL'
        logging.exception('Image gate failed; preserve evidence, do not add another feature')
        raise
    finally:
        save()


if __name__ == '__main__':
    main()
