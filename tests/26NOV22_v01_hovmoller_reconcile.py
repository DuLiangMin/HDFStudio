"""🔬 任意两轴切片独立对账。缺省读取Debug/Release CLI，临时夹具仅写logs。

输入：环境变量HDFSTUDIO_ZOOM_SAMPLE/FORECAST_ANCHOR/CLASSIC_SAMPLE（可选）。
输出：logs/S4-Hovmoller/numeric-时间/；运行python tests/26NOV22_v01_hovmoller_reconcile.py。
"""
from pathlib import Path
import argparse
import csv
import ctypes
import datetime
import hashlib
import itertools
import json
import logging
import os
import struct
import subprocess

import netCDF4
import numpy as np


def independent_hdf_storage(path, variable, shape):
    """直接调用已安装Python wheel的独立HDF5，不使用产品的库或读取路径。"""
    libraries=Path(netCDF4.__file__).resolve().parent.parent/'netcdf4.libs'
    with os.add_dll_directory(str(libraries)):
        library=ctypes.CDLL(str(next(libraries.glob('hdf5-*.dll'))))
        identifier=ctypes.c_int64
        library.H5Fopen.argtypes=[ctypes.c_char_p,ctypes.c_uint,identifier];library.H5Fopen.restype=identifier
        library.H5Dopen2.argtypes=[identifier,ctypes.c_char_p,identifier];library.H5Dopen2.restype=identifier
        library.H5Dget_type.argtypes=[identifier];library.H5Dget_type.restype=identifier
        library.H5Tget_native_type.argtypes=[identifier,ctypes.c_int];library.H5Tget_native_type.restype=identifier
        library.H5Dread.argtypes=[identifier,identifier,identifier,identifier,identifier,ctypes.c_void_p]
        for name in ['H5Tclose','H5Dclose','H5Fclose']:getattr(library,name).argtypes=[identifier]
        file_id=library.H5Fopen(os.fsencode(path),0,0);assert file_id>=0
        dataset=library.H5Dopen2(file_id,variable.encode(),0);assert dataset>=0
        storage_type=library.H5Dget_type(dataset);native_type=library.H5Tget_native_type(storage_type,0)
        values=np.empty(shape,dtype=np.int16)
        try:assert library.H5Dread(dataset,native_type,0,0,0,values.ctypes.data)==0
        finally:
            library.H5Tclose(native_type);library.H5Tclose(storage_type);library.H5Dclose(dataset);library.H5Fclose(file_id)
        return values


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration',choices=['Debug','Release'])
    options=parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    folder = root / 'logs/S4-Hovmoller' / ('numeric-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    folder.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s', handlers=[logging.StreamHandler(), logging.FileHandler(folder/'run.log', encoding='utf-8')])
    records = []
    sources = []
    shape = (3, 4, 5)
    raw = np.arange(60, dtype=np.int16).reshape(shape)
    raw[1, 2, 3] = -999
    for number, model in enumerate(['NETCDF3_CLASSIC', 'NETCDF3_64BIT_OFFSET', 'NETCDF3_64BIT_DATA', 'NETCDF4_CLASSIC', 'NETCDF4']):
        path = folder / ('tmp-fixture-' + str(number) + '.nc')
        with netCDF4.Dataset(path, 'w', format=model) as dataset:
            for name, length, units in [('time',3,'days since 2001-01-01'),('lat',4,'degrees_north'),('lon',5,'degrees_east')]:
                dataset.createDimension(name,length)
                coordinate = dataset.createVariable(name,'f8',(name,))
                coordinate.units = units
                coordinate[:] = np.arange(length)
            variable = dataset.createVariable('field','i2',('time','lat','lon'),fill_value=-999)
            variable.scale_factor = .125
            variable.add_offset = 17.
            variable.set_auto_maskandscale(False)
            variable[:] = raw
        reference = raw.astype(np.float64)*.125+17.
        reference[raw == -999] = np.nan
        sources.append((path,'/field',reference))
    path = folder/'tmp-plain.h5'
    environment = os.environ.copy()
    environment['QT_QPA_PLATFORM'] = 'windows'
    environment['HDFSTUDIO_HOVMOLLER_FIXTURE'] = str(path)
    subprocess.run([str(root/'build/output/Debug/hovmoller_tests.exe'),'plainHdf'],env=environment,capture_output=True,check=True,timeout=60)
    sources.append((path,'/field',reference))
    for path,variable,_ in sources[3:]:
        np.testing.assert_array_equal(independent_hdf_storage(path,variable,shape),raw)
    logging.info('PASS independent wheel HDF5 storage cross-check (NetCDF4 and user block)')
    for configuration in ([options.configuration] if options.configuration else ['Debug','Release']):
        cli = root/'build/output'/configuration/'hdf_cli.exe'
        assert cli.is_file(), cli
        def run(args, expected=0):
            result = subprocess.run([str(cli),*map(str,args)],capture_output=True,timeout=90)
            assert result.returncode == expected,(args,result.returncode,result.stderr.decode(errors='replace'))
            return result
        for source_index,(path,variable,reference) in enumerate(sources):
            before = hashlib.sha256(path.read_bytes()).hexdigest()
            for row_axis,col_axis in itertools.permutations(range(3),2):
                fixed = [1,2,3]
                fixed[row_axis] = fixed[col_axis] = 0
                output = folder/f'{configuration}-{source_index}-{row_axis}-{col_axis}.csv'
                run(['slice',path,variable,row_axis,col_axis,output,*fixed])
                selected = [slice(None) if axis in [row_axis,col_axis] else fixed[axis] for axis in range(3)]
                expected = reference[tuple(selected)]
                if row_axis > col_axis:
                    expected = expected.T
                with output.open(encoding='utf-8') as stream:
                    assert stream.readline().strip() == '# hovmoller_schema=1'
                    metadata = json.loads(stream.readline().split('=',1)[1])
                    actual = np.array([float(row['value']) for row in csv.DictReader(stream)]).reshape(expected.shape)
                np.testing.assert_array_equal(actual,expected)
                assert metadata['spec']['row_axis'] == row_axis
                run(['slice',path,variable,row_axis,col_axis,output,*fixed],4)
                records.append({'configuration':configuration,'source':source_index,'row_axis':row_axis,'col_axis':col_axis,'values':actual.size})
            assert hashlib.sha256(path.read_bytes()).hexdigest() == before
            if source_index<5:
                legacy_output=folder/f'{configuration}-{source_index}-legacy.csv'
                run(['csv',path,variable,legacy_output,1])
                legacy=np.genfromtxt(legacy_output,delimiter=',',ndmin=2)
                np.testing.assert_array_equal(legacy,reference[1])
                records.append({'configuration':configuration,'source':source_index,'legacy_image_plane_equivalence':True,'values':legacy.size})
        # Worker JSON and binary are independently decoded, not through the C++ loader.
        path,variable,reference = sources[4]
        task = folder / ('tmp-worker-'+configuration)
        task.mkdir()
        request = {'schema':1,'operation':'read','request_id':'7','source_revision':'1','file':str(path),'variable':variable,
                   'source_stamp':{'size_bytes':str(path.stat().st_size),'mtime_utc_ms':str(path.stat().st_mtime_ns//1000000)},
                   'expected_shape':['3','4','5'],'spec':{'row_axis':1,'col_axis':0,'fixed_indices':['0','0','2']},'coordinate_axes':[0,1,2]}
        (task/'request.json').write_text(json.dumps(request),encoding='utf-8')
        run(['slice-worker',task/'request.json'])
        result = json.loads((task/'result.json').read_text())
        assert result['status'] == 'ok'
        binary = (task/'plane.bin').read_bytes()
        assert struct.unpack('<8sIIQQQQ',binary[:48]) == (b'HDFSLICE',1,0,4,3,7,96)
        np.testing.assert_array_equal(np.frombuffer(binary[48:],dtype='<f8').reshape(4,3),reference[:,:,2].T)
        roles = [axis['role'] for axis in result['axes']]
        assert roles == [1,2,3],roles
        four_dimensional=folder/('tmp-four-dimensional-'+configuration+'.nc')
        four_shape=(2,3,4,5)
        time,level,latitude,longitude=np.indices(four_shape)
        four_values=(1000*time+100*level+10*latitude+longitude).astype(np.float64)
        with netCDF4.Dataset(four_dimensional,'w') as dataset:
            for axis,length in enumerate(four_shape):dataset.createDimension('axis'+str(axis),length)
            dataset.createVariable('field','f8',tuple('axis'+str(axis) for axis in range(4)))[:]=four_values
        for row_axis,col_axis in itertools.permutations(range(4),2):
            for position in ['first','middle','last']:
                fixed=[0 if position=='first' else length//2 if position=='middle' else length-1 for length in four_shape]
                fixed[row_axis]=fixed[col_axis]=0
                output=folder/f'{configuration}-four-{row_axis}-{col_axis}-{position}.csv'
                run(['slice',four_dimensional,'/field',row_axis,col_axis,output,*fixed])
                selected=[slice(None) if axis in [row_axis,col_axis] else fixed[axis] for axis in range(4)]
                expected=four_values[tuple(selected)]
                if row_axis>col_axis:expected=expected.T
                with output.open(encoding='utf-8') as stream:
                    next(stream);next(stream);actual=np.array([float(row['value']) for row in csv.DictReader(stream)]).reshape(expected.shape)
                np.testing.assert_array_equal(actual,expected)
                records.append({'configuration':configuration,'fixture':'4D deterministic','row_axis':row_axis,'col_axis':col_axis,'position':position,'values':actual.size})
        for environment,variable in [('HDFSTUDIO_ZOOM_SAMPLE','PRECT'),('HDFSTUDIO_FORECAST_ANCHOR','TMP_2maboveground'),('HDFSTUDIO_CLASSIC_SAMPLE','ttr')]:
            value = os.environ.get(environment)
            if not value:
                continue
            path = Path(value)
            before = hashlib.sha256(path.read_bytes()).hexdigest()
            with netCDF4.Dataset(path,'r') as dataset:
                field = dataset[variable]
                field.set_auto_maskandscale(False)
                base_pairs=[(field.ndim-2,0),(0,field.ndim-1)]
                for row_axis,col_axis,position in [(row,col,position) for pair in base_pairs for row,col in [pair,pair[::-1]] for position in ['first','middle','last']]:
                    fixed = [length//2 for length in field.shape]
                    if position=='first':fixed=[0]*field.ndim
                    if position=='last':fixed=[length-1 for length in field.shape]
                    fixed[row_axis] = fixed[col_axis] = 0
                    select = [slice(None) if axis in [row_axis,col_axis] else fixed[axis] for axis in range(field.ndim)]
                    stored = np.asarray(field[tuple(select)])
                    expected = stored.astype(np.float64)
                    absent = ~np.isfinite(expected)
                    for key in ['_FillValue','missing_value']:
                        if key in field.ncattrs():
                            absent |= stored == field.getncattr(key)
                    expected = expected*getattr(field,'scale_factor',1)+getattr(field,'add_offset',0)
                    expected[absent] = np.nan
                    if row_axis > col_axis:
                        expected = expected.T
                    output = folder/f'{configuration}-{variable}-{row_axis}-{col_axis}-{position}.csv'
                    run(['slice',path,'/'+variable,row_axis,col_axis,output,*fixed])
                    with output.open(encoding='utf-8') as stream:
                        next(stream);next(stream)
                        actual = np.array([float(row['value']) for row in csv.DictReader(stream)]).reshape(expected.shape)
                    np.testing.assert_allclose(actual,expected,rtol=1e-12,atol=0,equal_nan=True)
                    finite=np.isfinite(expected);difference=np.abs(actual[finite]-expected[finite]);nonzero=np.abs(expected[finite])>0
                    records.append({'configuration':configuration,'sample':environment,'row_axis':row_axis,'col_axis':col_axis,'position':position,'fixed_indices':fixed,'values':actual.size,'max_abs':float(difference.max(initial=0)),'max_rel':float((difference[nonzero]/np.abs(expected[finite][nonzero])).max(initial=0))})
            assert hashlib.sha256(path.read_bytes()).hexdigest() == before
        logging.info('PASS %s synthetic, worker and real sample reconciliation',configuration)
    report = {'status':'PASS','records':records,'values':sum(record['values'] for record in records)}
    (folder/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    logging.info('PASS %s values; %s',report['values'],folder)


if __name__ == '__main__':
    main()
