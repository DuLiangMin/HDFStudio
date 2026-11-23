"""📊 GRIB表格独立闸门：双配置构建、数值/生命周期及内存有界检查。

输入：build/vs2022/CMakeCache.txt；只读testdata/grib/；HDFSTUDIO_ZOOM_SAMPLE。
输出：logs/S4-GribIntegration/G2c-grid-时间/（日志、CSV、截图、hash及结果）。
用法：C:/Python312/python.exe tests/26NOV23_v01_grib_grid_verify.py
缺省只验证Data Grid与相邻浏览，不加入图像功能；桌面另运行desktop.ps1 -Grid。
"""
from pathlib import Path
import datetime
import hashlib
import json
import logging
import os
import re
import subprocess
import time
import math

import numpy as np
import psutil


def digest(p_path):
    with p_path.open('rb') as o_stream:
        return hashlib.file_digest(o_stream, 'sha256').hexdigest()


def signed_magnitude(b_value):
    i_value = int.from_bytes(b_value, 'big')
    i_sign = 1 << (len(b_value) * 8 - 1)
    return -(i_value & (i_sign - 1)) if i_value & i_sign else i_value


def decode_grib1_simple(p_source, d_field):
    """独立解析GRIB1规则经纬simple packing；明确拒绝未实现的网格/位图/复杂packing。"""
    with p_source.open('rb') as o_file:
        o_file.seek(int(d_field['offset']))
        b_message = o_file.read(int(d_field['length']))
    assert b_message[:4] == b'GRIB' and b_message[7] == 1 and b_message[-4:] == b'7777'
    assert int.from_bytes(b_message[4:7], 'big') == len(b_message)
    i_cursor = 8
    i_length = int.from_bytes(b_message[i_cursor:i_cursor + 3], 'big')
    b_product = b_message[i_cursor:i_cursor + i_length]
    i_cursor += i_length
    assert b_product[7] & 128 and not b_product[7] & 64, 'Reference parser requires explicit GDS and no bitmap'
    i_length = int.from_bytes(b_message[i_cursor:i_cursor + 3], 'big')
    b_grid = b_message[i_cursor:i_cursor + i_length]
    i_cursor += i_length
    assert b_grid[5] == 0 and b_grid[27] == 0, 'Reference parser requires regular_ll source scanning mode 0'
    i_cols, i_rows = int.from_bytes(b_grid[6:8], 'big'), int.from_bytes(b_grid[8:10], 'big')
    i_length = int.from_bytes(b_message[i_cursor:i_cursor + 3], 'big')
    b_packed = b_message[i_cursor:i_cursor + i_length]
    assert i_cursor + i_length == len(b_message) - 4 and b_packed[3] & 0xe0 == 0
    i_binary = signed_magnitude(b_packed[4:6])
    i_decimal = signed_magnitude(b_product[26:28])
    i_ibm = int.from_bytes(b_packed[6:10], 'big')
    f_reference = (-1. if i_ibm & 0x80000000 else 1.) * (i_ibm & 0xffffff) * 2.**(4 * (((i_ibm >> 24) & 127) - 64) - 24)
    i_bits = b_packed[10]
    assert i_bits <= 32 and len(b_packed[11:]) * 8 - (b_packed[3] & 15) == i_rows * i_cols * i_bits
    if i_bits:
        a_bits = np.unpackbits(np.frombuffer(b_packed[11:], dtype=np.uint8))[:i_rows * i_cols * i_bits].reshape(-1, i_bits)
        a_weights = np.left_shift(np.uint64(1), np.arange(i_bits - 1, -1, -1, dtype=np.uint64))
        a_quantized = a_bits @ a_weights
    else:
        a_quantized = np.zeros(i_rows * i_cols, dtype=np.uint64)
    a_values = (f_reference + a_quantized.astype(np.float64) * math.ldexp(1., i_binary)) * 10.**(-i_decimal)
    assert (i_rows, i_cols) == (int(d_field['Nj']), int(d_field['Ni']))
    return a_values.reshape(i_rows, i_cols)


def external_cases(p_source, d_catalog):
    """检查全部消息边界，选择首/中/末场及可导航温度变量的首末层。"""
    l_fields = d_catalog['fields']
    assert d_catalog['format'] == 'GRIB1' and all(d_field['edition'] == '1' for d_field in l_fields)
    i_cursor = 0
    with p_source.open('rb') as o_file:
        for d_field in l_fields:
            assert int(d_field['offset']) == i_cursor and d_field['sub_field'] == 0
            o_file.seek(i_cursor)
            b_header = o_file.read(8)
            i_length = int.from_bytes(b_header[4:7], 'big')
            assert b_header[:4] == b'GRIB' and b_header[7] == 1 and i_length == int(d_field['length'])
            o_file.seek(i_cursor + i_length - 4)
            assert o_file.read(4) == b'7777'
            i_cursor += i_length
    assert i_cursor == p_source.stat().st_size
    l_cubes = [d_cube for d_cube in d_catalog['cubes'] if len(d_cube['axes']) == 1 and len(d_cube['lookup']) > 1 and all(i_field >= 0 for i_field in d_cube['lookup'])]
    d_cube = next((d_item for d_item in l_cubes if l_fields[d_item['lookup'][0]]['shortName'] in ['ta', 't']), l_cubes[0])
    l_selected = sorted({0, len(l_fields) // 2, len(l_fields) - 1, d_cube['lookup'][0], d_cube['lookup'][-1]})
    d_cases = {'field_count': len(l_fields), 'cube_count': len(d_catalog['cubes']), 'fields': [], 'navigation': {'target': d_cube['id'], 'first': d_cube['lookup'][0], 'last': d_cube['lookup'][-1]}}
    d_expected = {}
    for i_index in l_selected:
        d_field = l_fields[i_index]
        a_values = decode_grib1_simple(p_source, d_field)
        s_tag = f'field-{i_index}'
        d_expected[s_tag] = a_values
        l_probes = [0, 1, a_values.shape[1] - 1, a_values.size // 2, a_values.size - 1]
        d_cases['fields'].append({'target': f'/field/{i_index}', 'tag': s_tag, 'rows': a_values.shape[0], 'cols': a_values.shape[1], 'offset': d_field['offset'], 'length': d_field['length'], 'probes': [{'position': i_position, 'value': float(a_values.flat[i_position])} for i_position in l_probes]})
    for s_end in ['first', 'last']:
        d_expected['cube-' + s_end] = decode_grib1_simple(p_source, l_fields[d_cases['navigation'][s_end]])
    return d_cases, d_expected


def main():
    p_root = Path(__file__).resolve().parents[1]
    p_log = p_root / 'logs/S4-GribIntegration' / ('G2c-grid-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    p_log.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s', handlers=[logging.StreamHandler(), logging.FileHandler(p_log / 'run.log', encoding='utf-8')])
    s_cache = (p_root / 'build/vs2022/CMakeCache.txt').read_text(encoding='utf-8-sig')
    s_cmake = next(s_line.split('=', 1)[1] for s_line in s_cache.splitlines() if s_line.startswith('CMAKE_COMMAND:INTERNAL='))
    d_env = os.environ.copy()
    d_env.update(QT_QPA_PLATFORM='windows', QT_LOGGING_TO_CONSOLE='1')
    p_sample = Path(d_env['HDFSTUDIO_ZOOM_SAMPLE'])
    assert p_sample.is_file(), p_sample
    d_sources = {str(p_path.relative_to(p_root)): digest(p_path) for p_path in [p_root / 'CMakeLists.txt', *sorted((p_root / 'src').rglob('*.hpp')), *sorted((p_root / 'src').rglob('*.cpp')), p_root / 'tests/grib_browser_test.cpp']}
    d_report = {'status': 'RUNNING', 'sources': d_sources, 'sample_sha256': digest(p_sample), 'configurations': {}}
    s_external = d_env.get('HDFSTUDIO_GRIB1_SAMPLE', '')
    p_external = Path(s_external) if s_external else None
    if p_external:
        assert p_external.is_file()
        d_report['external'] = {'file': str(p_external), 'size': p_external.stat().st_size, 'sha256_before': digest(p_external)}

    def save():
        (p_log / 'result.json').write_text(json.dumps(d_report, indent=2), encoding='utf-8')

    def run(l_command, p_prefix, i_timeout=1200):
        logging.info('Start %s', p_prefix.name)
        with p_prefix.with_suffix('.stdout.txt').open('wb') as o_out, p_prefix.with_suffix('.stderr.txt').open('wb') as o_err:
            o_result = subprocess.run(list(map(str, l_command)), cwd=p_root, env=d_env, stdout=o_out, stderr=o_err, timeout=i_timeout)
        assert o_result.returncode == 0, (p_prefix, o_result.returncode)
        logging.info('PASS %s', p_prefix.name)

    def totals(p_file, i_expected, i_skipped=0):
        s_text = p_file.read_text(encoding='utf-8-sig')
        o_match = re.search(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped', s_text)
        assert o_match and tuple(map(int, o_match.groups())) == (i_expected, 0, i_skipped), p_file

    save()
    try:
        for s_config in ['Release', 'Debug']:
            p_config = p_log / s_config
            p_config.mkdir()
            p_binary = p_root / 'build/output' / s_config
            d_env['HDFSTUDIO_GRIB_EVIDENCE'] = str(p_config)
            run([s_cmake, '--build', 'build/vs2022', '--config', s_config, '--target', 'grib_browser_tests', 'hdf_explorer', 'hdf_explorer_drop_tests', '--parallel', '6'], p_config / 'build')
            d_expected = {}
            if p_external:
                o_catalog = subprocess.run([str(p_binary / 'hdf_cli.exe'), 'info', str(p_external)], cwd=p_root, env=d_env, capture_output=True, timeout=180)
                (p_config / 'external-catalog.json').write_bytes(o_catalog.stdout)
                (p_config / 'external-catalog.stderr.txt').write_bytes(o_catalog.stderr)
                assert o_catalog.returncode == 0
                d_cases, d_expected = external_cases(p_external, json.loads(o_catalog.stdout))
                p_cases = p_config / 'external-cases.json'
                p_cases.write_text(json.dumps(d_cases, indent=2), encoding='utf-8')
                d_env['HDFSTUDIO_GRIB1_CASES'] = str(p_cases)
                logging.info('GRIB1: %d fields, %d cubes; %d reference planes', d_cases['field_count'], d_cases['cube_count'], len(d_expected))
            # 异步模型起始为空；加载超百万格后必须重新安装有界辅助接口。
            f_start = time.perf_counter()
            i_peak = 0
            with (p_config / 'large.stdout.txt').open('wb') as o_out, (p_config / 'large.stderr.txt').open('wb') as o_err:
                o_process = subprocess.Popen([str(p_binary / 'grib_browser_tests.exe'), 'gridLargeAsyncModelUsesBoundedAccessibility', '-o', str(p_config / 'large.txt') + ',txt'], cwd=p_root, env=d_env, stdout=o_out, stderr=o_err)
                o_monitor = psutil.Process(o_process.pid)
                try:
                    while o_process.poll() is None:
                        try:
                            i_peak = max(i_peak, o_monitor.memory_info().rss)
                        except psutil.NoSuchProcess:
                            break
                        assert i_peak < 768 * 1024 * 1024, ('RSS limit exceeded', i_peak)
                        assert time.perf_counter() - f_start < 45, 'Large grid exceeded 45 seconds'
                        time.sleep(0.025)
                    assert o_process.wait(timeout=5) == 0
                finally:
                    if o_process.poll() is None:
                        o_process.kill()
                        o_process.wait(timeout=5)
                    (p_config / 'large-memory.json').write_text(json.dumps({'peak_rss': i_peak, 'seconds': time.perf_counter() - f_start, 'limit_bytes': 768 * 1024 * 1024}, indent=2), encoding='utf-8')
            totals(p_config / 'large.txt', 3)
            l_grid_cases = ['virtualAndPhysicalProperties', 'pagesAndSessionsStaySeparate', 'unicodeAndMixedContainers', 'queuedIndexCancellationPublishesNothing', 'corruptIndexPublishesNothing', 'gridDecodedQuantizedCopyAndCsv', 'gridLayersMissingAndUnsupportedQuantized', 'gridTreeNavigationAndClose', 'gridSourceChangeAndInvalidObject', 'gridLargeAsyncModelUsesBoundedAccessibility', 'gridExternalGrib1Sample']
            run([p_binary / 'grib_browser_tests.exe', *l_grid_cases, '-o', str(p_config / 'grid.txt') + ',txt'], p_config / 'grid', 90)
            totals(p_config / 'grid.txt', 13 if p_external else 12, 0 if p_external else 1)
            for s_tag, a_expected in d_expected.items():
                a_actual = np.loadtxt(p_config / ('external-' + s_tag + '.csv'), delimiter=',')
                np.testing.assert_array_equal(a_actual, a_expected)
            if p_external:
                # CLI与GUI使用相同解码器；独立位流解析作为额外数值真值。
                d_first = d_cases['fields'][0]
                p_csv = p_config / 'external-cli.csv'
                run([p_binary / 'hdf_cli.exe', 'csv', p_external, d_first['target'], p_csv], p_config / 'external-cli', 180)
                np.testing.assert_array_equal(np.genfromtxt(p_csv, delimiter=','), d_expected[d_first['tag']])
            l_adjacent = ['propertiesInspectorReadsDatasetLayoutAndAttributes', 'bottomFileInfoPanelShowsCurrentFileMetadata', 'closeFileRemovesItsViewsAndReleasesTheFile', 'classicNetcdfOpensWithTreeGridImagePropertiesAndExport', 'propertiesDialogCopiesReportAndWindowMenuListsViews', 'fileDropOpensFromEveryAreaOfTheMainWindow']
            run([p_binary / 'hdf_explorer_drop_tests.exe', *l_adjacent, '-o', str(p_config / 'adjacent.txt') + ',txt'], p_config / 'adjacent', 120)
            totals(p_config / 'adjacent.txt', 8)
            for i_mode in range(3):
                p_csv = p_config / f'grid-mode-{i_mode}.csv'
                a_decoded = 85000 + np.arange(12).reshape(3, 4)
                a_quantized = np.arange(12).reshape(3, 4) * 2**20
                a_expected = a_decoded if i_mode == 0 else a_quantized
                if i_mode == 2:
                    a_expected = np.stack([a_decoded, a_quantized], axis=-1).reshape(3, 8)
                np.testing.assert_array_equal(np.loadtxt(p_csv, delimiter=','), a_expected)
                d_meta = json.loads(p_csv.read_text(encoding='utf-8-sig').splitlines()[1].split('=', 1)[1])
                assert d_meta['reader'] == 'ecCodes' and d_meta['decode_profile'] == 'GribDecodedV1' and d_meta['source_session_id']
            d_report['configurations'][s_config] = {'tests': 13 if p_external else 12, 'adjacent': 8, 'csv_values': 48, 'external_planes': len(d_expected), 'external_values': sum(a_values.size for a_values in d_expected.values()), 'peak_rss': i_peak, 'binary_sha256': digest(p_binary / 'HDFStudio.exe')}
            save()
        for s_name, s_hash in d_sources.items():
            assert digest(p_root / s_name) == s_hash, s_name
        assert digest(p_sample) == d_report['sample_sha256']
        if p_external:
            d_report['external']['sha256_after'] = digest(p_external)
            assert d_report['external']['sha256_after'] == d_report['external']['sha256_before']
        for d_item in json.loads((p_root / 'testdata/grib/manifest.json').read_text(encoding='utf-8-sig')):
            assert digest(p_root / 'testdata/grib' / d_item['name']) == d_item['sha256']
        d_report['status'] = 'PASS'
        logging.info('PASS both configurations: %s', p_log)
    except Exception:
        d_report['status'] = 'FAIL'
        logging.exception('Grid gate failed; preserve evidence and fix this step only')
        raise
    finally:
        save()


if __name__ == '__main__':
    main()
