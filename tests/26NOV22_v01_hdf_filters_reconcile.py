"""独立核对HDF5存储值、解码值及桌面PNG（仅测试环境需要h5py/hdf5plugin）。

输入：HDFSTUDIO_BITSHUFFLE_SAMPLE/REFERENCE，--folder可指定桌面证据目录。
输出：缺省logs/S4-HdfBitshuffle/independent-时间/expected.json与result.json。
用法：python tests/26NOV22_v01_hdf_filters_reconcile.py [--folder <桌面目录>]。
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import importlib.util
import json
import logging
import os
import numpy as np
import netCDF4


def main():
    p_root = Path(__file__).resolve().parents[1]
    o_parser = argparse.ArgumentParser(description=__doc__)
    o_parser.add_argument('--folder', type=Path)
    o_options = o_parser.parse_args()
    p_output = o_options.folder or p_root / 'logs/S4-HdfBitshuffle' / ('independent-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    p_output.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s', handlers=[logging.StreamHandler(), logging.FileHandler(p_output/'independent.log', encoding='utf-8')])
    p_sample = Path(os.environ['HDFSTUDIO_BITSHUFFLE_SAMPLE'])
    p_reference = Path(os.environ['HDFSTUDIO_BITSHUFFLE_REFERENCE'])
    with netCDF4.Dataset(p_reference) as o_dataset:
        a_reference = np.asarray(o_dataset['precipitation'][0], dtype=np.float64)
    a_mask = a_reference >= 9999
    a_visible = a_reference[~a_mask]
    d_expected = {'masked': int(a_mask.sum()), 'min': format(a_visible.min(), '.9g'), 'max': format(a_visible.max(), '.9g'), 'first': format(a_reference[0,0], '.15g')}
    (p_output/'expected.json').write_text(json.dumps(d_expected, indent=2), encoding='utf-8')
    d_report = {'status': 'RUNNING', 'expected': d_expected, 'samples': [], 'pixels': []}
    if not o_options.folder:
        import h5py
        import hdf5plugin
        for p_hdf in sorted(p_sample.parent.glob('*_H5_i32_1e4_bitzstd3.h5')):
            p_nc = p_hdf.with_name(p_hdf.name.replace('_H5_i32_1e4_bitzstd3.h5','_NC_i32_1e4_zlib4.nc'))
            s_hash = hashlib.sha256(p_hdf.read_bytes()).hexdigest()
            with h5py.File(p_hdf,'r') as o_hdf, netCDF4.Dataset(p_nc) as o_nc:
                o_variable = o_hdf['precipitation'];a_raw = o_variable[:]
                a_decoded = a_raw.astype(np.float64)*o_variable.attrs['scale_factor']
                o_other = o_nc['precipitation'];a_other_decoded = np.asarray(o_other[:])
                o_other.set_auto_maskandscale(False)
                np.testing.assert_array_equal(a_raw,o_other[:])
                np.testing.assert_array_equal(a_decoded,a_other_decoded)
                o_properties = o_variable.id.get_create_plist()
                l_filters = [{'id': o_properties.get_filter(i_index)[0], 'parameters': list(o_properties.get_filter(i_index)[2])} for i_index in range(o_properties.get_nfilters())]
            assert hashlib.sha256(p_hdf.read_bytes()).hexdigest() == s_hash
            d_report['samples'].append({'file':str(p_hdf),'sha256':s_hash,'raw_values':int(a_raw.size),'raw_sha256':hashlib.sha256(a_raw.tobytes()).hexdigest(),'filters':l_filters})
            logging.info('PASS independent stored and decoded values %s: %d',p_hdf.name,a_raw.size)
    else:
        o_spec = importlib.util.spec_from_file_location('png_reader',p_root/'tests/26NOV21_v01_full_regression.py')
        o_png = importlib.util.module_from_spec(o_spec);o_spec.loader.exec_module(o_png)
        # New Image Views retain the original five-point Viridis (legacy) default.
        a_colors = np.asarray([[68,1,84],[59,82,139],[33,145,140],[94,201,98],[253,231,37]],dtype=np.float64)/255
        a_positions = np.linspace(0,1,5)
        for s_kind in ['all','masked']:
            a_values = a_reference.copy();a_excluded = a_mask if s_kind=='masked' else np.zeros(a_values.shape,dtype=bool)
            a_values[a_excluded] = np.nan;a_finite = np.isfinite(a_values)
            a_fraction = (a_values[a_finite]-np.nanmin(a_values))/(np.nanmax(a_values)-np.nanmin(a_values))
            a_rgb = np.full((*a_values.shape,3),110,dtype=np.uint8)
            for i_channel in range(3):
                a_rgb[:,:,i_channel][a_finite] = np.floor(np.clip(np.interp(a_fraction,a_positions,a_colors[:,i_channel]),0,1)*255+.5).astype(np.uint8)
            a_rgb[a_excluded] = 255
            np.testing.assert_array_equal(o_png.png_rgb(p_output/('image-'+s_kind+'.png')),a_rgb)
            d_report['pixels'].append({'image':s_kind,'rgb_pixels':int(a_values.size)})
            logging.info('PASS %s %d RGB pixels',s_kind,a_values.size)
        assert (p_output/'first-cell.tsv').read_text(encoding='utf-8') == d_expected['first']
    d_report['status'] = 'PASS'
    (p_output/'independent-result.json').write_text(json.dumps(d_report,indent=2),encoding='utf-8')
    logging.info('PASS %s',p_output)


if __name__ == '__main__':
    main()
