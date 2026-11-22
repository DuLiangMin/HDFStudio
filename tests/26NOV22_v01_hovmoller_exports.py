"""🧮 独立核查剖面CSV/PNG；缺省logs/S4-Hovmoller/P2-business-Debug。

输入：--folder内同名CSV/PNG，CSV metadata中的只读源数据；不修改样本。
输出：同目录export-reconciliation.json与时间戳日志。
例：python tests/26NOV22_v01_hovmoller_exports.py --folder logs/S4-Hovmoller/P2-business-Debug
"""
import argparse
import csv
import datetime
import hashlib
import importlib.util
import json
import logging
import struct
import zlib
from pathlib import Path

import netCDF4
import numpy as np


def png_metadata(path):
    data=path.read_bytes();position=8
    while position<len(data):
        length=struct.unpack('>I',data[position:position+4])[0]
        kind=data[position+4:position+8];payload=data[position+8:position+8+length];position+=12+length
        if kind==b'zTXt':
            key,remainder=payload.split(b'\0',1)
            if key==b'HovmollerMetadata':return json.loads(zlib.decompress(remainder[1:]))
        if kind==b'tEXt':
            key,text=payload.split(b'\0',1)
            if key==b'HovmollerMetadata':return json.loads(text)
    raise AssertionError('PNG HovmollerMetadata absent')


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--folder', type=Path, default=root/'logs/S4-Hovmoller/P2-business-Debug')
    folder = parser.parse_args().folder.resolve()
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s', handlers=[logging.StreamHandler(), logging.FileHandler(folder/'exports.log', encoding='utf-8')])
    module_spec = importlib.util.spec_from_file_location('png_reader', root/'tests/26NOV21_v01_full_regression.py')
    png_reader = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(png_reader)
    records = []
    for path in folder.glob('*.csv'):
        if not path.with_suffix('.png').is_file():
            continue
        with path.open(encoding='utf-8') as stream:
            if stream.readline().strip() != '# hovmoller_schema=1':
                continue  # 同目录允许保留原图、残差、熵图等旧功能导出。
            metadata = json.loads(stream.readline().split('=',1)[1])
            table = list(csv.DictReader(stream))
        source = Path(metadata['file'])
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        spec = metadata['spec']
        row_axis, col_axis = spec['row_axis'], spec['col_axis']
        indices = [int(value) for value in spec['fixed_indices']]
        with netCDF4.Dataset(source) as dataset:
            variable = dataset[metadata['variable']]
            variable.set_auto_maskandscale(False)
            selection = [slice(None) if axis in [row_axis,col_axis] else indices[axis] for axis in range(variable.ndim)]
            stored = np.asarray(variable[tuple(selection)])
            expected = stored.astype(np.float64)
            missing = ~np.isfinite(expected)
            for name in ['_FillValue','missing_value']:
                if name in variable.ncattrs():
                    missing |= stored == variable.getncattr(name)
            expected = expected * getattr(variable,'scale_factor',1) + getattr(variable,'add_offset',0)
            missing |= ~np.isfinite(expected)
            expected[missing] = np.nan
            if row_axis > col_axis:
                expected = expected.T
                missing = missing.T
            mask = metadata['mask']
            masked = np.zeros(expected.shape,dtype=bool)
            if mask is not None:
                masked = np.isfinite(expected) & (expected >= (-np.inf if mask['start'] is None else mask['start'])) & (expected <= (np.inf if mask['end'] is None else mask['end']))
                expected[masked] = np.nan
            for name,axis in [('row_coord',row_axis),('col_coord',col_axis)]:
                coordinate = metadata['axes'][axis]['coordinate_path']
                if coordinate:
                    values = np.ma.filled(dataset[coordinate][:],np.nan)
                    actual = np.array([float(row[name]) for row in table]).reshape(expected.shape)
                    wanted = np.broadcast_to(values[:,None] if name=='row_coord' else values[None,:],expected.shape)
                    np.testing.assert_array_equal(actual,wanted)
        actual = np.array([float(row['value']) for row in table]).reshape(expected.shape)
        np.testing.assert_allclose(actual, expected, rtol=1e-12, atol=0, equal_nan=True)
        states = np.array([row['state'] for row in table]).reshape(expected.shape)
        np.testing.assert_array_equal(states, np.where(missing,'missing',np.where(masked,'masked','valid')))
        mapping = metadata['color_mapping']
        registry = json.loads((root/'resources/colormaps/manifest.json').read_text(encoding='utf-8'))
        # Registry JSON filenames encode ':' as '_'; palette contents still verify the identity.
        palettes = [json.loads(file.read_text(encoding='utf-8')) for file in (root/'resources/colormaps/palettes').glob('*.json')]
        palette = next(item for item in palettes if item['id']==mapping['palette'])
        colors = np.array(palette['colors'])[:,:3]
        positions = np.array(palette['positions'])
        rgb = np.full((*expected.shape,3),110,dtype=np.uint8)
        finite = np.isfinite(expected)
        minimum,maximum = mapping['minimum'],mapping['maximum']
        if finite.any():
            if maximum == minimum:
                fractions = np.full(finite.sum(),.5)
            elif mapping['normalization'] == 1:
                center = mapping['center']
                values = expected[finite]
                fractions = np.where(values<=center,.5*(values-minimum)/(center-minimum),.5+.5*(values-center)/(maximum-center))
            else:
                fractions = (expected[finite]-minimum)/(maximum-minimum)
            fractions = np.clip(fractions,0,1)
            if mapping['reverse']:
                fractions = 1-fractions
            for channel in range(3):
                rgb[:,:,channel][finite] = np.floor(np.clip(np.interp(fractions,positions,colors[:,channel]),0,1)*255+.5).astype(np.uint8)
        rgb[masked] = 255
        if metadata['flip_h']:
            rgb = rgb[:,::-1]
        if metadata['flip_v']:
            rgb = rgb[::-1]
        np.testing.assert_array_equal(png_reader.png_rgb(path.with_suffix('.png')),rgb)
        assert png_metadata(path.with_suffix('.png'))==metadata,'PNG/CSV snapshot metadata differs'
        assert hashlib.sha256(source.read_bytes()).hexdigest() == digest
        records.append({'file':path.name,'shape':list(expected.shape),'values':expected.size,'rgb_pixels':expected.size,'source_sha256':digest,'status':'PASS'})
        logging.info('PASS %s: %d values and RGB pixels',path.name,expected.size)
    assert records, 'No paired CSV/PNG files'
    (folder/'export-reconciliation.json').write_text(json.dumps({'status':'PASS','records':records,'checked_at':datetime.datetime.now().isoformat()},indent=2),encoding='utf-8')


if __name__ == '__main__':
    main()
