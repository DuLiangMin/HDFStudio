"""Bitshuffle完整验证：双配置GUI/CLI共用解码、独立数值及PNG对账。

输入：HDFSTUDIO_BITSHUFFLE_SAMPLE/REFERENCE及既有五份HDFSTUDIO_*样本；已配置CMake。
输出：logs/S4-HdfBitshuffle/full-时间/JSON、CSV、PNG；不修改业务文件。
用法：python tests/26NOV22_v01_hdf_filters_verify.py [--numeric-only] [--configuration Release]。
普通模式复用完整表格/旧功能验证器；numeric-only只测已经构建的指定配置。
"""
from pathlib import Path
import argparse
import csv
import datetime
import hashlib
import json
import logging
import os
import subprocess
import sys
import numpy as np
import netCDF4


def digest(p_path):
    return hashlib.sha256(p_path.read_bytes()).hexdigest()


def main():
    o_parser = argparse.ArgumentParser(description=__doc__)
    o_parser.add_argument('--numeric-only', action='store_true')
    o_parser.add_argument('--configuration', choices=['Debug', 'Release'], default='Release')
    o_options = o_parser.parse_args()
    p_root = Path(__file__).resolve().parents[1]
    p_sample = Path(os.environ['HDFSTUDIO_BITSHUFFLE_SAMPLE'])
    p_reference = Path(os.environ['HDFSTUDIO_BITSHUFFLE_REFERENCE'])
    assert p_sample.is_file() and p_reference.is_file()
    p_output = p_root / 'logs/S4-HdfBitshuffle' / ('full-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    p_output.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s', handlers=[logging.StreamHandler(), logging.FileHandler(p_output / 'run.log', encoding='utf-8')])
    l_samples = sorted(p_sample.parent.glob('*_H5_i32_1e4_bitzstd3.h5'))
    assert p_sample in l_samples
    d_hashes = {str(p_file): digest(p_file) for p_file in l_samples}
    d_report = {'status': 'RUNNING', 'samples': d_hashes, 'configurations': [], 'commands': []}
    l_sources = [p_root / 'CMakeLists.txt', *list((p_root/'src').rglob('*.cpp')), *list((p_root/'src').rglob('*.hpp')), *list((p_root/'third_party/hdf_filters').rglob('*')), p_root/'tests/hdf_filters_test.cpp', p_root/'tests/hdf_filters_fixture.hpp', Path(__file__).resolve()]
    d_report['sources'] = {str(p_file.relative_to(p_root)): digest(p_file) for p_file in l_sources if p_file.is_file()}
    def save():
        (p_output/'result.json').write_text(json.dumps(d_report, indent=2), encoding='utf-8')
    def run(l_command, s_label, i_timeout=90):
        logging.info('Start %s', s_label)
        o_run = subprocess.run(list(map(str, l_command)), cwd=p_root, capture_output=True, timeout=i_timeout)
        (p_output/(s_label+'.stdout.txt')).write_bytes(o_run.stdout)
        (p_output/(s_label+'.stderr.txt')).write_bytes(o_run.stderr)
        d_report['commands'].append({'label': s_label, 'returncode': o_run.returncode})
        save()
        assert o_run.returncode == 0, (s_label, o_run.returncode)
    save()
    try:
        if not o_options.numeric_only:
            p_grid = p_root/'logs/S4-GridAnalysis'
            l_before = set(p_grid.glob('full-*/result.json'))
            run([sys.executable, p_root/'tests/26NOV22_v01_grid_analysis_verify.py'], 'full-regression', 2400)
            l_new = set(p_grid.glob('full-*/result.json')) - l_before
            assert len(l_new) == 1
            p_full = l_new.pop()
            d_full = json.loads(p_full.read_text())
            assert d_full['status'] == 'PASS'
            d_report['regression'] = str(p_full)
        for s_config in ([o_options.configuration] if o_options.numeric_only else ['Debug', 'Release']):
            p_cli = p_root/'build/output'/s_config/'hdf_cli.exe'
            p_gui = p_cli.with_name('HDFStudio.exe')
            i_values = 0
            for p_hdf in l_samples:
                p_nc = p_hdf.with_name(p_hdf.name.replace('_H5_i32_1e4_bitzstd3.h5', '_NC_i32_1e4_zlib4.nc'))
                assert p_nc.is_file()
                d_hashes[str(p_nc)] = digest(p_nc)
                p_csv = p_output/(s_config+'-'+p_hdf.stem+'.csv')
                run([p_cli, 'slice', p_hdf, '/precipitation', 1, 2, p_csv, 0, 0, 0], s_config+'-'+p_hdf.stem)
                with p_csv.open(encoding='utf-8') as o_stream:
                    next(o_stream); d_metadata = json.loads(next(o_stream).split('=',1)[1])
                    a_actual = np.fromiter((float(d_row['value']) for d_row in csv.DictReader(o_stream)), dtype=np.float64)
                with netCDF4.Dataset(p_nc) as o_dataset:
                    a_expected = np.ma.filled(o_dataset.variables['precipitation'][0], np.nan)
                assert a_actual.size == a_expected.size
                np.testing.assert_allclose(a_actual.reshape(a_expected.shape), a_expected, rtol=1e-14, atol=1e-14, equal_nan=True)
                i_values += a_actual.size
                logging.info('PASS %s %s %d values', s_config, p_hdf.name, a_actual.size)
            for s_kind, p_input in [('hdf', p_sample), ('reference', p_reference)]:
                for s_mask, l_extra in [('all', [])]:
                    p_png = p_output/(s_config+'-'+s_kind+'-'+s_mask+'.png')
                    run([p_gui, '--preview', 'precipitation', '--png', p_png, *l_extra, p_input], s_config+'-'+s_kind+'-'+s_mask)
            for s_mask in ['all']:
                assert digest(p_output/(s_config+'-hdf-'+s_mask+'.png')) == digest(p_output/(s_config+'-reference-'+s_mask+'.png'))
            d_report['configurations'].append({'configuration': s_config, 'files': len(l_samples), 'values': int(i_values), 'png_equal': True, 'binary_sha256': digest(p_gui)})
        for s_file, s_hash in d_hashes.items():
            assert digest(Path(s_file)) == s_hash, s_file
        for s_file, s_hash in d_report['sources'].items():
            assert digest(p_root/s_file) == s_hash, s_file
        d_report['status'] = 'PASS'
        save()
        logging.info('PASS %s', p_output)
    except Exception:
        d_report['status'] = 'FAIL'
        save()
        logging.exception('HDF filters verification failed')
        raise


if __name__ == '__main__':
    main()
