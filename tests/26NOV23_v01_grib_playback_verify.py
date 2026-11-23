"""▶️ GRIB同步/播放双配置验证，直接运行。

输入：只读HDFSTUDIO_GRIB1_SAMPLE/HDFSTUDIO_GRIB2_SAMPLE；现有小夹具。
输出：logs/S4-GribIntegration/G3e-playback-时间/构建、专项、相邻回归和源/EXE hash。
命令：C:\\Python312\\python.exe tests/26NOV23_v01_grib_playback_verify.py
可选HDFSTUDIO_GRIB_PLAYBACK_RED=1只执行Release专项，保存先行失败证据。
"""
import datetime
import hashlib
import json
import logging
import os
from pathlib import Path
import subprocess


def main():
    p_root = Path(__file__).resolve().parents[1]
    p_log = p_root / 'logs/S4-GribIntegration' / ('G3e-playback-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    p_log.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s', handlers=[logging.StreamHandler(), logging.FileHandler(p_log / 'verify.log', encoding='utf-8')])
    d_env = os.environ.copy()
    d_env['QT_LOGGING_TO_CONSOLE'] = '1'
    s_cache = (p_root / 'build/vs2022/CMakeCache.txt').read_text(encoding='utf-8-sig')
    s_cmake = next(s_line.split('=', 1)[1] for s_line in s_cache.splitlines() if s_line.startswith('CMAKE_COMMAND:INTERNAL='))
    d_report = {'status': 'RUNNING', 'sources': {}, 'samples': {}, 'configurations': {}}
    for p_file in [p_root / 'CMakeLists.txt', *sorted((p_root / 'src').rglob('*.cpp')), *sorted((p_root / 'src').rglob('*.hpp')), p_root / 'tests/grib_playback_test.cpp', p_root / 'tests/drop_targets_test.cpp', Path(__file__)]:
        d_report['sources'][str(p_file.relative_to(p_root))] = hashlib.sha256(p_file.read_bytes()).hexdigest()
    for i_edition in [1, 2]:
        p_sample = Path(d_env['HDFSTUDIO_GRIB' + str(i_edition) + '_SAMPLE'])
        d_report['samples'][str(i_edition)] = {'source': str(p_sample), 'before': hashlib.sha256(p_sample.read_bytes()).hexdigest()}

    def run(l_command, p_prefix, i_timeout=1200):
        logging.info('Start %s', p_prefix)
        o_run = subprocess.run(list(map(str, l_command)), cwd=p_root, env=d_env, capture_output=True, timeout=i_timeout)
        p_prefix.with_suffix('.stdout.txt').write_bytes(o_run.stdout)
        p_prefix.with_suffix('.stderr.txt').write_bytes(o_run.stderr)
        assert o_run.returncode == 0, (str(p_prefix), o_run.returncode)
        logging.info('PASS %s', p_prefix)

    try:
        l_configs = ['Release'] if d_env.get('HDFSTUDIO_GRIB_PLAYBACK_RED') == '1' else ['Release', 'Debug']
        for s_config in l_configs:
            p_config = p_log / s_config
            p_config.mkdir()
            p_binary = p_root / 'build/output' / s_config
            run([s_cmake, '--build', 'build/vs2022', '--config', s_config, '--target', 'grib_playback_tests', 'hdf_explorer', 'hdf_explorer_drop_tests', '--parallel', '6'], p_config / 'build')
            d_env['HDFSTUDIO_GRIB_EVIDENCE'] = str(p_config)
            run([p_binary / 'grib_playback_tests.exe', '-o', str(p_config / 'playback.txt') + ',txt'], p_config / 'playback', 300)
            assert 'Totals: 9 passed, 0 failed, 0 skipped' in (p_config / 'playback.txt').read_text(encoding='utf-8-sig')
            run([p_binary / 'hdf_explorer_drop_tests.exe', 'workspaceSyncAnchorLagAndOutOfRange', 'linkedLayerToolbarNavigatesAnchorFromEitherImage', 'layerPlaybackSingleAndLinkedPlanes', 'layerPlaybackStopsOnContextChanges', 'layerPlaybackFrameCountBoundaries', '-o', str(p_config / 'adjacent.txt') + ',txt'], p_config / 'adjacent', 240)
            assert 'Totals: 7 passed, 0 failed, 0 skipped' in (p_config / 'adjacent.txt').read_text(encoding='utf-8-sig')
            d_report['configurations'][s_config] = {'special_passed': 9, 'adjacent_passed': 7, 'binary_sha256': hashlib.sha256((p_binary / 'HDFStudio.exe').read_bytes()).hexdigest(), 'external': {str(i_edition): json.loads((p_config / ('playback-grib' + str(i_edition) + '.json')).read_text(encoding='utf-8')) for i_edition in [1, 2]}}
        for s_name, s_hash in d_report['sources'].items():
            assert hashlib.sha256((p_root / s_name).read_bytes()).hexdigest() == s_hash, s_name
        for d_sample in d_report['samples'].values():
            d_sample['after'] = hashlib.sha256(Path(d_sample['source']).read_bytes()).hexdigest()
            assert d_sample['before'] == d_sample['after']
        d_report['status'] = 'PASS'
    except Exception:
        d_report['status'] = 'FAIL'
        logging.exception('GRIB playback verification failed')
        raise
    finally:
        (p_log / 'result.json').write_text(json.dumps(d_report, indent=2), encoding='utf-8')
    logging.info('PASS %s', p_log)


if __name__ == '__main__':
    main()
