"""🔬 坐标/日历/packing边界夹具与worker独立检查。

输入：build/output/{Debug,Release}/hdf_cli.exe，resources/netcdf_report.py。
输出：logs/S4-Hovmoller/semantics-时间/临时夹具、请求、结果与日志。
用法：python tests/26NOV22_v01_hovmoller_semantics.py；无外部下载或自动安装。
"""
from pathlib import Path
import argparse
import datetime
import json
import logging
import os
import shutil
import struct
import subprocess
import sys

import netCDF4
import numpy as np


def main():
    root = Path(__file__).resolve().parents[1]
    folder = root/'logs/S4-Hovmoller'/('semantics-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    folder.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO,format='%(asctime)s %(message)s',handlers=[logging.StreamHandler(),logging.FileHandler(folder/'run.log',encoding='utf-8')])
    source = folder/'tmp-semantics.nc'
    with netCDF4.Dataset(source,'w') as dataset:
        dataset.createDimension('a',3)
        dataset.createDimension('b',4)
        dataset.createDimension('c',3)  # Same length, different identity.
        for name,dimension,units,standard in [('latitude','a','degrees_north','latitude'),('latitude2','a','degrees_north','latitude'),('fake','c','degrees_north','latitude'),('projection','b','m','projection_x_coordinate'),('rotated','a','degrees','grid_latitude'),('conflict','a','degrees_north','longitude')]:
            variable = dataset.createVariable(name,'f8',(dimension,))
            variable.units = units
            variable.standard_name = standard
            variable[:] = np.arange(variable.size)
        time = dataset.createVariable('custom_time','f8',('a',))
        time.units = 'days since 2000-01-01'
        time.month_lengths = np.full(12,30,dtype=np.int32)
        time[:] = [0,1,2]
        for name,units,values in [('uneven','days since 2000-01-01',[0,1,3]),('repeated','degrees_north',[0,0,2]),('gaps','degrees_north',[0,np.nan,2]),('seam','degrees_east',[359,0,1])]:
            coordinate=dataset.createVariable(name,'f8',('a',));coordinate.units=units;coordinate[:]=values
            variable=dataset.createVariable('field_'+name,'f8',('a','b'));variable.coordinates=name;variable[:]=np.arange(12).reshape(3,4)
        for name,units in [('latitude2d','degrees_north'),('longitude2d','degrees_east')]:
            coordinate=dataset.createVariable(name,'f8',('a','b'));coordinate.units=units;coordinate[:]=np.arange(12).reshape(3,4)
        curved=dataset.createVariable('curved','f8',('a','b'));curved.coordinates='latitude2d longitude2d';curved[:]=np.arange(12).reshape(3,4)
        for name,coordinates in [('ambiguous','latitude latitude2'),('wrong_dimension','fake'),('valid_aux','latitude'),('projected','rotated projection'),('conflicting','conflict'),('custom_calendar','custom_time')]:
            variable = dataset.createVariable(name,'f8',('a','b'))
            variable.coordinates = coordinates
            variable[:] = np.arange(12).reshape(3,4)
        for name in ['unsigned_flag','vector_missing','invalid_scale','zero_scale','negative_scale','wide','nan_fill','valid_range_only']:
            dtype = 'u8' if name=='wide' else 'f8' if name=='nan_fill' else 'i2'
            variable = dataset.createVariable(name,dtype,('a','b'),fill_value=np.nan if name=='nan_fill' else False)
            variable.set_auto_maskandscale(False)
            variable[:] = np.arange(12,dtype=np.float64).reshape(3,4)
            if name=='unsigned_flag': variable._Unsigned = 'true'
            if name=='vector_missing': variable.missing_value = np.array([-99,-98],dtype=np.int16)
            if name=='invalid_scale': variable.scale_factor = np.array([1,2],dtype=float)
            if name=='zero_scale': variable.scale_factor = 0.;variable.add_offset = 17.
            if name=='negative_scale': variable.scale_factor = -.25;variable.add_offset = 10.
            if name=='wide': variable[0,:3] = np.array([2**53,2**53+1,2**53+2],dtype=np.uint64)
            if name=='nan_fill': variable[0,:2] = [np.nan,np.inf]
            if name=='valid_range_only': variable.valid_range = np.array([3,7],dtype=np.int16)
        a = dataset.createVariable('a','f8',('a',))
        a.units='days since 2000-01-01'
        a[:] = [0,1,2]
        group=dataset.createGroup('nested')
        inherited=group.createVariable('inherited','f8',('a','b'))
        inherited[:] = np.arange(12).reshape(3,4)
        group.createDimension('local',3)
        own=group.createVariable('own','f8',('local','b'))
        own.coordinates='/latitude'
        own[:] = np.arange(12).reshape(3,4)
    # The root dimension coordinate a takes precedence; remove it in a separate aux fixture.
    auxiliary = folder/'tmp-aux.nc'
    with netCDF4.Dataset(source) as original, netCDF4.Dataset(auxiliary,'w') as target:
        for name,dimension in original.dimensions.items(): target.createDimension(name,len(dimension))
        for name,variable in original.variables.items():
            if name=='a': continue
            if name in ['unsigned_flag','vector_missing','invalid_scale','zero_scale','negative_scale','wide','nan_fill','valid_range_only']: continue
            out=target.createVariable(name,variable.dtype,variable.dimensions)
            for key in variable.ncattrs(): out.setncattr(key,variable.getncattr(key))
            out[:]=variable[:]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration',choices=['Debug','Release'])
    options=parser.parse_args()
    records=[]
    for configuration in ([options.configuration] if options.configuration else ['Debug','Release']):
        cli=root/'build/output'/configuration/'hdf_cli.exe'
        count=0
        def run(path,variable,expected=0,known_shape=None):
            nonlocal count
            count+=1
            task=folder/f'tmp-{configuration}-{count}'
            task.mkdir()
            if known_shape is None:
                with netCDF4.Dataset(path) as dataset:
                    shape=dataset[variable].shape
            else:
                shape=known_shape
            request={'schema':1,'operation':'read','request_id':str(count),'source_revision':'1','file':str(path),'variable':variable,
                     'source_stamp':{'size_bytes':str(path.stat().st_size),'mtime_utc_ms':str(path.stat().st_mtime_ns//1000000)},'expected_shape':list(map(str,shape)),
                     'spec':{'row_axis':0,'col_axis':1,'fixed_indices':['0']*len(shape)},'coordinate_axes':list(range(len(shape)))}
            (task/'request.json').write_text(json.dumps(request),encoding='utf-8')
            completed=subprocess.run([str(cli),'slice-worker',str(task/'request.json')],capture_output=True,timeout=20)
            result=json.loads((task/'result.json').read_text())
            assert completed.returncode==expected,(configuration,variable,completed.returncode,result)
            plane=None
            if not expected:
                data=(task/'plane.bin').read_bytes()
                header=struct.unpack('<8sIIQQQQ',data[:48])
                assert header[0]==b'HDFSLICE' and header[3:]==(shape[0],shape[1],count,np.prod(shape)*8)
                plane=np.frombuffer(data[48:],dtype='<f8').reshape(shape)
            records.append({'configuration':configuration,'variable':variable,'exit':completed.returncode})
            return result,plane
        result,_=run(auxiliary,'/ambiguous')
        assert result['axes'][0]['association']=='ambiguous'
        result,_=run(auxiliary,'/wrong_dimension')
        assert result['axes'][0]['coordinate_path']==''
        result,_=run(auxiliary,'/valid_aux')
        assert result['axes'][0]['role']==2 and result['axes'][0]['coordinate_path']=='/latitude'
        result,_=run(auxiliary,'/projected')
        assert [axis['role'] for axis in result['axes']]==[0,0]
        result,_=run(auxiliary,'/conflicting')
        assert result['axes'][0]['role']==0 and 'Conflicting' in result['axes'][0]['diagnostic']
        result,_=run(auxiliary,'/custom_calendar')
        assert result['axes'][0]['calendar_source']=='custom' and not result['axes'][0]['date_supported']
        result,_=run(auxiliary,'/curved');assert all(axis['role']==0 and not axis['coordinate_path'] for axis in result['axes'])
        for name,note in [('uneven','nonuniform spacing'),('repeated','repeated coordinates'),('gaps','missing coordinates'),('seam','longitude seam')]:
            result,_=run(auxiliary,'/field_'+name);assert note in result['axes'][0]['diagnostic'],(name,result['axes'])
        result,_=run(source,'/nested/inherited')
        assert result['axes'][0]['coordinate_path']=='/a' and result['axes'][0]['role']==1
        result,_=run(source,'/nested/own')
        assert not result['axes'][0]['coordinate_path']
        for name in ['unsigned_flag','vector_missing','invalid_scale']:
            result,_=run(source,'/'+name,3)
            assert result['error_code'] in ['UnsupportedPacking','InvalidPacking']
        _,plane=run(source,'/zero_scale');np.testing.assert_array_equal(plane,np.full((3,4),17.))
        _,plane=run(source,'/negative_scale');np.testing.assert_array_equal(plane,np.arange(12).reshape(3,4)*-.25+10.)
        result,plane=run(source,'/wide');assert result['precision_warning']
        _,plane=run(source,'/nan_fill');assert np.isnan(plane[0,:2]).all()
        _,plane=run(source,'/valid_range_only');np.testing.assert_array_equal(plane,np.arange(12).reshape(3,4))
        unicode_source=folder/('tmp-'+configuration+'-\u4e2d\u6587.NC')
        shutil.copy2(auxiliary,unicode_source)
        _,plane=run(unicode_source,'/valid_aux',known_shape=(3,4));np.testing.assert_array_equal(plane,np.arange(12).reshape(3,4))
        extensionless=folder/('tmp-'+configuration+'-no-extension');shutil.copy2(auxiliary,extensionless)
        _,plane=run(extensionless,'/valid_aux');np.testing.assert_array_equal(plane,np.arange(12).reshape(3,4))
        alias=folder/('tmp-'+configuration+'-source-alias.csv');os.link(auxiliary,alias)
        before=auxiliary.read_bytes()
        command=[str(cli),'slice',str(auxiliary),'/valid_aux','0','1',str(alias),'0','0']
        completed=subprocess.run(command,capture_output=True,timeout=20)
        assert completed.returncode==4 and auxiliary.read_bytes()==before,'Hard-link source overwrite must be rejected'
        records.append({'configuration':configuration,'source_alias_guard':'PASS'})
        logging.info('PASS %s coordinate identity, conflicts, groups and packing boundaries',configuration)
    for calendar in ['standard','noleap','360_day','unknown_calendar']:
        request={'schema':1,'request_id':'8','units':'days since 2000-01-01','calendar':calendar,'values':[0,29,59,None,.000001]}
        process=subprocess.run([sys.executable,str(root/'resources/netcdf_report.py'),'labels'],input=json.dumps(request),capture_output=True,text=True,timeout=10)
        result=json.loads(process.stdout)
        if calendar=='unknown_calendar':assert result['error']
        else:
            assert result['error'] is None and result['labels'][3] is None
            dates=netCDF4.num2date([0,29,59,.000001],request['units'],calendar=calendar)
            assert result['labels'][2].startswith(dates[2].strftime('%Y-%m-%d'))
        records.append({'calendar':calendar,'status':'PASS'})
    request={'schema':1,'request_id':'9','units':'days since INVALID_EPOCH','calendar':'standard','values':[0]}
    completed=subprocess.run([sys.executable,str(root/'resources/netcdf_report.py'),'labels'],input=json.dumps(request),capture_output=True,text=True,timeout=10)
    assert json.loads(completed.stdout)['error'],'Invalid epoch must retain numeric time'
    records.append({'calendar_boundary':'invalid_epoch','status':'PASS'})
    for mode,arguments in [('summary',[]),('time',['/a']),('dates',['/a']),('cf',[])]:
        completed=subprocess.run([sys.executable,str(root/'resources/netcdf_report.py'),mode,str(source),*arguments],capture_output=True,text=True,timeout=60)
        (folder/(mode+'.stdout.txt')).write_text(completed.stdout,encoding='utf-8')
        (folder/(mode+'.stderr.txt')).write_text(completed.stderr,encoding='utf-8')
        assert completed.returncode==0,(mode,completed.returncode,completed.stderr)
        assert completed.stdout.strip(),mode
        if mode in ['time','dates']:assert '20000101' in completed.stdout
        records.append({'legacy_date_mode':mode,'exit':completed.returncode})
    (folder/'result.json').write_text(json.dumps({'status':'PASS','records':records},indent=2),encoding='utf-8')
    logging.info('PASS %s',folder)


if __name__ == '__main__':
    main()
