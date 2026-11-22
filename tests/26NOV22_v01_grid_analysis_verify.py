"""表格微调的双配置完整编译/QtTest/CTest与只读样本核验。

输入：build/vs2022/CMakeCache.txt；既有HDFSTUDIO_*样本环境变量。
输出：logs/S4-GridAnalysis/full-时间/，源码/样本/二进制hash、日志与网格截图。
用法：python tests/26NOV22_v01_grid_analysis_verify.py。
不安装依赖、不推送、不覆盖已发布的0.12.10附件。
"""
from pathlib import Path
import datetime
import hashlib
import json
import logging
import os
import re
import subprocess
import sys
import time
import psutil
import netCDF4
import numpy as np


def digest(p_path):
    o_digest = hashlib.sha256()
    with p_path.open('rb') as o_stream:
        for b_block in iter(lambda: o_stream.read(1024 * 1024), b''):
            o_digest.update(b_block)
    return o_digest.hexdigest()


def main():
    p_root = Path(__file__).resolve().parents[1]
    p_folder = p_root / 'logs/S4-GridAnalysis' / ('full-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    p_folder.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s', handlers=[logging.StreamHandler(), logging.FileHandler(p_folder / 'run.log', encoding='utf-8')])
    s_cache = (p_root / 'build/vs2022/CMakeCache.txt').read_text()
    s_cmake = next(s_line.split('=', 1)[1] for s_line in s_cache.splitlines() if s_line.startswith('CMAKE_COMMAND:INTERNAL='))
    p_ctest = Path(s_cmake).with_name('ctest.exe')
    d_environment = os.environ.copy()
    d_environment.update(QT_QPA_PLATFORM='windows', QT_LOGGING_TO_CONSOLE='1', HDFSTUDIO_PYTHON=sys.executable)
    l_samples = ['HDFSTUDIO_ZOOM_SAMPLE', 'HDFSTUDIO_CLASSIC_SAMPLE', 'HDFSTUDIO_PACKED_SAMPLE', 'HDFSTUDIO_FORECAST_ANCHOR', 'HDFSTUDIO_FORECAST_TARGET']
    for s_name in l_samples:
        assert d_environment.get(s_name) and Path(d_environment[s_name]).is_file(), s_name
    d_samples = {d_environment[s_name]: digest(Path(d_environment[s_name])) for s_name in l_samples}
    l_sources = [p_root / 'CMakeLists.txt', *sorted((p_root / 'src').rglob('*.cpp')), *sorted((p_root / 'src').rglob('*.hpp')), p_root / 'tests/drop_targets_test.cpp', p_root / 'tests/hovmoller_test.cpp', Path(__file__).resolve(), p_root / 'tests/26NOV22_v01_grid_analysis_desktop.ps1']
    d_sources = {str(p_path.relative_to(p_root)): digest(p_path) for p_path in l_sources}
    d_report = {'status': 'RUNNING', 'sources': d_sources, 'samples': d_samples, 'commands': [], 'configurations': []}

    def save():
        (p_folder / 'result.json').write_text(json.dumps(d_report, indent=2), encoding='utf-8')

    def run(l_command, s_label, i_timeout=600):
        logging.info('Start %s', s_label)
        f_start = time.perf_counter()
        o_result = subprocess.run(list(map(str, l_command)), cwd=p_root, env=d_environment, capture_output=True, timeout=i_timeout)
        (p_folder / (s_label + '.stdout.txt')).write_bytes(o_result.stdout)
        (p_folder / (s_label + '.stderr.txt')).write_bytes(o_result.stderr)
        d_report['commands'].append({'label': s_label, 'exit': o_result.returncode, 'seconds': time.perf_counter() - f_start})
        save()
        assert o_result.returncode == 0, (s_label, o_result.returncode, p_folder)
        logging.info('PASS %s', s_label)

    save()
    try:
        p_fixture = p_folder / 'tmp-grid-fixture.nc'
        with netCDF4.Dataset(p_fixture, 'w', format='NETCDF4') as o_dataset:
            o_dataset.createDimension('row', 12)
            o_dataset.createDimension('column', 20)
            o_variable = o_dataset.createVariable('analysis', 'f8', ('row', 'column'))
            o_variable[:] = np.arange(12)[:, None] * 100 + np.arange(20)[None, :] * 0.125
        d_environment['HDFSTUDIO_GRID_SAMPLE'] = str(p_fixture)
        for s_config in ['Debug', 'Release']:
            p_config = p_folder / s_config
            p_config.mkdir()
            d_environment.update(HDFSTUDIO_GRID_EVIDENCE=str(p_config), HDFSTUDIO_TEST_CONFIG=s_config,
                                 HDFSTUDIO_TEST_LOG_DIR=str(p_config), HDFSTUDIO_HOVMOLLER_EXPORT=str(p_config))
            run([s_cmake, '--build', 'build/vs2022', '--config', s_config, '--target', 'hdf_explorer', 'hdf_cli', 'hdf_explorer_drop_tests', 'hovmoller_tests', 'hdf_filters_tests', '--', '/verbosity:minimal'], s_config + '-build', 1200)
            # Isolate the huge virtual-grid path with a watchdog before the full suite.
            p_memory = p_config / 'large-grid.txt'
            f_start = time.perf_counter()
            i_peak = 0
            with (p_config / 'large-grid.stdout.txt').open('wb') as o_out, (p_config / 'large-grid.stderr.txt').open('wb') as o_err:
                o_process = subprocess.Popen([str(p_root / 'build/output' / s_config / 'hdf_explorer_drop_tests.exe'), 'analysisGridSizingIsBoundedOnLargeModels', '-o', str(p_memory) + ',txt'], cwd=p_root, env=d_environment, stdout=o_out, stderr=o_err)
                o_parent = psutil.Process(o_process.pid)
                try:
                    while o_process.poll() is None:
                        try:
                            i_rss = o_parent.memory_info().rss
                            i_peak = max(i_peak, i_rss)
                        except psutil.NoSuchProcess:
                            break
                        assert i_rss < 768 * 1024 * 1024, ('Large-grid RSS exceeded 768 MiB', i_rss)
                        assert time.perf_counter() - f_start < 30, 'Large-grid test exceeded 30 seconds'
                        time.sleep(0.025)
                    assert o_process.wait(timeout=5) == 0, p_memory
                finally:
                    if o_process.poll() is None:
                        o_process.kill()
                        o_process.wait(timeout=5)
                    (p_config / 'large-grid-memory.json').write_text(json.dumps({'peak_rss': i_peak, 'seconds': time.perf_counter() - f_start, 'limit_bytes': 768 * 1024 * 1024}, indent=2), encoding='utf-8')
            logging.info('PASS %s isolated 400-million-cell range selection; peak RSS %.1f MiB', s_config, i_peak / 1024 / 1024)
            d_counts = {}
            for s_test, i_expected in [('hdf_explorer_drop_tests', 89), ('hovmoller_tests', 20), ('hdf_filters_tests', 6)]:
                p_log = p_config / (s_test + '.txt')
                run([p_root / 'build/output' / s_config / (s_test + '.exe'), '-o', str(p_log) + ',txt'], s_config + '-' + s_test)
                s_text = p_log.read_text(encoding='utf-8')
                o_match = re.search(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped', s_text)
                assert o_match and tuple(map(int, o_match.groups())) == (i_expected, 0, 0), s_text[-3000:]
                d_counts[s_test] = i_expected
            d_environment.pop('HDFSTUDIO_HOVMOLLER_EXPORT', None)
            run([p_ctest, '--test-dir', 'build/vs2022', '-C', s_config, '--output-on-failure'], s_config + '-ctest')
            run([sys.executable, p_root / 'tests/26NOV22_v01_hovmoller_exports.py', '--folder', p_config], s_config + '-section-exports', 120)
            p_desktop_root = p_root / 'logs/S4-GridAnalysis'
            l_before = set(p_desktop_root.glob('desktop-' + s_config + '-*'))
            p_system = Path(os.environ.get('SystemRoot', r'C:\Windows'))
            run([p_system / 'System32/WindowsPowerShell/v1.0/powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', p_root / 'tests/26NOV22_v01_grid_analysis_desktop.ps1', '-Configuration', s_config], s_config + '-grid-desktop', 120)
            l_new = set(p_desktop_root.glob('desktop-' + s_config + '-*')) - l_before
            assert len(l_new) == 1
            p_desktop = l_new.pop()
            d_summary = json.loads((p_desktop / 'summary.json').read_text(encoding='utf-8-sig'))
            assert d_summary['status'] == 'PASS' and d_summary['checks'] == 22
            for s_mode in ['decoded', 'stored', 'both']:
                l_rows = (p_desktop / (s_mode + '.tsv')).read_text(encoding='utf-8').splitlines()
                a_actual = np.array([[float(s_cell) for s_cell in s_row.split('\t')] for s_row in l_rows])
                a_expected = np.array([[row * 100 + (column // 2 if s_mode == 'both' else column) * 0.125 for column in range(1, 5)] for row in range(1, 4)])
                np.testing.assert_array_equal(a_actual, a_expected)
            (p_desktop / 'clipboard-reconciliation.json').write_text(json.dumps({'status': 'PASS', 'modes': 3, 'values': 36}, indent=2), encoding='utf-8')
            d_report['configurations'].append({'configuration': s_config, 'tests': d_counts, 'binaries': {s_name: digest(p_root / 'build/output' / s_config / (s_name + '.exe')) for s_name in ['HDFStudio', 'hdf_cli', 'hdf_explorer_drop_tests', 'hovmoller_tests', 'hdf_filters_tests']}})
            save()
        run([sys.executable, p_root / 'tests/reconcile_time_grid.py'], 'time-grid-reconcile', 120)
        for s_name, s_hash in d_sources.items():
            assert digest(p_root / s_name) == s_hash, s_name
        for s_name, s_hash in d_samples.items():
            assert digest(Path(s_name)) == s_hash, s_name
        d_report['status'] = 'PASS'
        save()
        logging.info('PASS %s', p_folder)
    except Exception:
        d_report['status'] = 'FAIL'
        save()
        logging.exception('Grid verification failed; evidence preserved')
        raise


if __name__ == '__main__':
    main()
