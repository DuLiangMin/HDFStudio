"""🖱️ G3e三样本×双配置真实桌面播放/暂停/整数lag/关闭验证。

输入：两业务源环境变量，最近PASS的G3e自动化；可用HDFSTUDIO_GRIB_PLAYBACK_GATE指定。
输出：该闸门desktop-all-review.json、每次独立desktop-run-时间/日志与原始桌面目录。
命令：C:\\Python312\\python.exe tests/26NOV23_v01_grib_playback_desktop_verify.py
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
    if os.environ.get('HDFSTUDIO_GRIB_PLAYBACK_GATE'):
        p_gate = Path(os.environ['HDFSTUDIO_GRIB_PLAYBACK_GATE'])
    else:
        l_gates = sorted(p_item.parent for p_item in (p_root / 'logs/S4-GribIntegration').glob('G3e-playback-*/result.json') if json.loads(p_item.read_text(encoding='utf-8'))['status'] == 'PASS')
        assert l_gates, 'Complete G3e automatic tests first'
        p_gate = l_gates[-1]
    d_gate = json.loads((p_gate / 'result.json').read_text(encoding='utf-8'))
    assert d_gate['status'] == 'PASS' and len(d_gate['configurations']) == 2
    p_attempt = p_gate / ('desktop-run-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    p_attempt.mkdir()
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s', handlers=[logging.StreamHandler(), logging.FileHandler(p_attempt / 'verify.log', encoding='utf-8')])
    d_result = {'status': 'RUNNING', 'records': []}
    d_env = os.environ.copy()
    try:
        for s_config in ['Release', 'Debug']:
            s_hash = hashlib.sha256((p_root / 'build/output' / s_config / 'HDFStudio.exe').read_bytes()).hexdigest()
            assert s_hash == d_gate['configurations'][s_config]['binary_sha256']
            for s_sample, s_switch, s_suffix in [('Fixture', '', 'playback'), ('Grib1', '-ExternalGrib1', 'playback-grib1'), ('Grib2', '-ExternalGrib2', 'playback-grib2')]:
                if s_switch:
                    d_env['HDFSTUDIO_GRIB_PLAYBACK_REFERENCE'] = str(p_gate / s_config / ('playback-grib' + ('1' if s_sample == 'Grib1' else '2') + '.json'))
                s_pattern = 'desktop-' + s_suffix + '-' + s_config + '-*'
                l_before = set((p_root / 'logs/S4-GribIntegration').glob(s_pattern))
                l_command = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'tests/26NOV23_v01_grib_browser_desktop.ps1', '-Configuration', s_config, '-Playback']
                if s_switch:
                    l_command.append(s_switch)
                logging.info('Start %s %s desktop', s_config, s_sample)
                o_run = subprocess.run(l_command, cwd=p_root, env=d_env, capture_output=True, timeout=180)
                s_stem = s_config + '-' + s_sample
                (p_attempt / (s_stem + '.stdout.txt')).write_bytes(o_run.stdout)
                (p_attempt / (s_stem + '.stderr.txt')).write_bytes(o_run.stderr)
                l_new = set((p_root / 'logs/S4-GribIntegration').glob(s_pattern)) - l_before
                assert len(l_new) == 1, l_new
                p_run = next(iter(l_new))
                l_checks = json.loads((p_run / 'checks.json').read_text(encoding='utf-8-sig'))
                d_result['records'].append({'folder': str(p_run), 'passed': sum(bool(d_item['pass']) for d_item in l_checks), 'exit_code': o_run.returncode, 'binary_sha256': s_hash})
                assert o_run.returncode == 0, (s_stem, o_run.returncode, str(p_run))
                assert len(l_checks) >= 18 and all(d_item['pass'] for d_item in l_checks)
                if s_sample != 'Grib2':
                    l_names = [d_item['check'] for d_item in l_checks]
                    assert 'GRIB timer advances an actual layer' in l_names
                    assert 'Pause keeps GRIB indices fixed' in l_names
                logging.info('PASS %s %s: %s checks', s_config, s_sample, len(l_checks))
        for s_name, s_hash in d_gate['sources'].items():
            assert hashlib.sha256((p_root / s_name).read_bytes()).hexdigest() == s_hash, s_name
        for d_sample in d_gate['samples'].values():
            assert hashlib.sha256(Path(d_sample['source']).read_bytes()).hexdigest() == d_sample['after']
        d_result['total_checks'] = sum(d_record['passed'] for d_record in d_result['records'])
        d_result['status'] = 'PASS'
    except Exception:
        d_result['status'] = 'FAIL'
        logging.exception('GRIB playback desktop verification failed')
        raise
    finally:
        s_json = json.dumps(d_result, indent=2)
        (p_attempt / 'result.json').write_text(s_json, encoding='utf-8')
        (p_gate / 'desktop-all-review.json').write_text(s_json, encoding='utf-8')
    logging.info('PASS %s', p_gate)


if __name__ == '__main__':
    main()
