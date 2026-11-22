"""📦 从当前Release输出组装并独立解压本地便携副本，验证无Python日期回退。

输入：build/output/Release、tools/package-windows.ps1列出的运行依赖与许可。
输出：logs/S4-Hovmoller/portable-时间/中的本地包、解压副本、桌面与RGB证据。
用法：python tests/26NOV22_v01_hovmoller_portable.py；业务样本由HDFSTUDIO_FORECAST_ANCHOR传入。
不制作旧提交的源码包、不修改版本号、不推送或发布。
"""
from pathlib import Path
import argparse
import csv
import datetime
import hashlib
import json
import logging
import os
import re
import shutil
import subprocess
import sys
import zipfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-only',action='store_true',help='Validate isolated runtime with native Qt tests when the interactive desktop is unavailable; real-desktop acceptance remains pending')
    options=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    folder=root/'logs/S4-Hovmoller'/('portable-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    stage=folder/'tmp-stage/HDFStudio-local-hovmoller';stage.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO,format='%(asctime)s %(message)s',handlers=[logging.StreamHandler(),logging.FileHandler(folder/'run.log',encoding='utf-8')])
    release=root/'build/output/Release'
    for name in ['HDFStudio.exe','hdf_cli.exe','netcdf.dll','Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','D3Dcompiler_47.dll']:
        shutil.copy2(release/name,stage/name)
    for name in ['platforms','styles','imageformats']:shutil.copytree(release/name,stage/name)
    visual_studio=Path(os.environ.get('ProgramFiles',r'C:\Program Files'))/'Microsoft Visual Studio/2022'
    runtimes=sorted(visual_studio.glob('*/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT'))
    assert runtimes,'Existing VC143 runtime required'
    for name in ['msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll']:shutil.copy2(runtimes[-1]/name,stage/name)
    for name in ['README.md','README_CN.md','Changelog.md','THIRD_PARTY_NOTICES.md']:shutil.copy2(root/name,stage/name)
    shutil.copy2(root/'LICENSE',stage/'LICENSE.txt')
    shutil.copy2(root/'LICENSE',stage/'LICENSE')
    shutil.copytree(root/'docs',stage/'docs')
    licenses=stage/'licenses';licenses.mkdir()
    qt_licenses=sorted((root/'build').glob('package-local-*/**/licenses/Qt-LGPL-3.0.txt'))
    assert qt_licenses,'Existing Qt LGPL text required'
    qt_license=qt_licenses[-1]
    shutil.copy2(qt_license,licenses/qt_license.name)
    for source,name in [('ext/netcdf-c-4.9.3/COPYRIGHT','netCDF-C-COPYRIGHT.txt'),('ext/hdf5-1.8.20/COPYING','HDF5-COPYING.txt'),('ext/zlib-1.3.1/LICENSE','zlib-LICENSE.txt')]:shutil.copy2(root/source,licenses/name)
    shutil.copytree(root/'licenses/colormaps',licenses/'colormaps')
    shutil.copytree(root/'resources/colormaps',stage/'resources/colormaps')
    package=folder/'HDFStudio-local-hovmoller.zip'
    with zipfile.ZipFile(package,'w',compression=zipfile.ZIP_DEFLATED) as archive:
        for path in stage.rglob('*'):
            if path.is_file():archive.write(path,path.relative_to(stage.parent))
    extracted=folder/'tmp-extracted'
    with zipfile.ZipFile(package) as archive:archive.extractall(extracted)
    portable=extracted/stage.name
    for path in stage.rglob('*'):
        if path.is_file():assert digest(path)==digest(portable/path.relative_to(stage)),path
    environment=os.environ.copy();system=Path(environment.get('SystemRoot',r'C:\Windows'))
    environment.update(PATH=os.pathsep.join(map(str,[system/'System32',system,system/'System32/WindowsPowerShell/v1.0'])),HDFSTUDIO_PYTHON='nonexistent-hovmoller-python.exe',QT_QPA_PLATFORM='windows',QT_LOGGING_TO_CONSOLE='1')
    for key in ['QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','PYTHONPATH','PYTHONHOME']:environment.pop(key,None)
    if options.runtime_only:
        # Keep the QtTest runner outside the program archive, beside an identical runtime copy.
        harness=folder/'tmp-runtime-harness';shutil.copytree(portable,harness)
        for name in ['hovmoller_tests.exe','Qt6Test.dll']:shutil.copy2(release/name,harness/name)
        environment['HDFSTUDIO_HOVMOLLER_EXPORT']=str(folder/'runtime-exports')
        environment['HDFSTUDIO_TEST_LOG_DIR']=str(folder/'runtime-exports')
        arguments=[str(harness/'hovmoller_tests.exe'),'datesAndNumericFallback','mainWindowIntegration','viewLatestAndPlayback','viewCloseLifecycle','businessSamples','-o',str(folder/'runtime-unit.txt')+',txt']
        test_environment=environment.copy()
        # This test always verifies the nonexistent-Python fallback first. Its optional
        # positive half runs only for a nonempty configured Python executable.
        test_environment['HDFSTUDIO_PYTHON']=''
        result=subprocess.run(arguments,cwd=portable,env=test_environment,capture_output=True,timeout=120)
        (folder/'runtime.stdout.txt').write_bytes(result.stdout);(folder/'runtime.stderr.txt').write_bytes(result.stderr)
        assert result.returncode==0,(result.returncode,folder)
        text=(folder/'runtime-unit.txt').read_text(encoding='utf-8')
        assert '7 passed, 0 failed, 0 skipped' in text,text
        check=subprocess.run([sys.executable,str(root/'tests/26NOV22_v01_hovmoller_exports.py'),'--folder',str(folder/'runtime-exports')],cwd=root,capture_output=True,timeout=60)
        (folder/'exports.stdout.txt').write_bytes(check.stdout);(folder/'exports.stderr.txt').write_bytes(check.stderr);assert check.returncode==0
        version=subprocess.run([str(portable/'HDFStudio.exe'),'--version'],cwd=portable,env=environment,capture_output=True,timeout=15)
        expected_version=re.search(r'project\(HDFStudio VERSION ([0-9.]+)',(root/'CMakeLists.txt').read_text(encoding='utf-8')).group(1)
        assert version.returncode==0 and version.stdout.decode('utf-8').strip()=='HDFStudio '+expected_version,(version.returncode,version.stdout,version.stderr)
        cli_records=[]
        for name,row_axis,col_axis in [('lat-time',1,0),('lon-time',0,2)]:
            output=folder/('cli-'+name+'.csv')
            arguments=[str(portable/'hdf_cli.exe'),'slice',environment['HDFSTUDIO_FORECAST_ANCHOR'],'/TMP_2maboveground',str(row_axis),str(col_axis),str(output),'0','0','0']
            completed=subprocess.run(arguments,cwd=portable,env=environment,capture_output=True,timeout=30)
            (folder/('cli-'+name+'.stderr.txt')).write_bytes(completed.stderr)
            assert completed.returncode==0,(name,completed.returncode)
            with output.open(encoding='utf-8') as actual,(folder/'runtime-exports'/(name+'.csv')).open(encoding='utf-8') as expected:
                next(actual);next(actual);next(expected);next(expected)
                actual_rows=list(csv.DictReader(actual));expected_rows=list(csv.DictReader(expected))
                assert actual_rows==expected_rows,name
            cli_records.append({'name':name,'exit':completed.returncode,'values':len(actual_rows)})
        report={'status':'PASS_RUNTIME_ONLY','desktop_status':'PENDING_INTERACTIVE_DESKTOP','executable':str(portable/'HDFStudio.exe'),'archive_sha256':digest(package),'files':{str(path.relative_to(portable)):digest(path) for path in portable.rglob('*') if path.is_file()},'environment':{key:test_environment[key] for key in ['PATH','HDFSTUDIO_PYTHON']},'runtime_exit':result.returncode,'export_exit':check.returncode,'runtime_tests':7,'version':version.stdout.decode('utf-8').strip(),'cli':cli_records}
        (folder/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        logging.info('PASS isolated runtime and exports; interactive desktop remains pending: %s',folder)
        return
    before=set((root/'logs/S4-Hovmoller').glob('desktop-Portable-*'))
    arguments=[str(system/'System32/WindowsPowerShell/v1.0/powershell.exe'),'-NoProfile','-ExecutionPolicy','Bypass','-File',str(root/'tests/26NOV22_v01_hovmoller_desktop.ps1'),'-Configuration','Release','-Executable',str(portable/'HDFStudio.exe'),'-RunLabel','Portable']
    result=subprocess.run(arguments,cwd=root,env=environment,capture_output=True,timeout=240)
    (folder/'desktop.stdout.txt').write_bytes(result.stdout);(folder/'desktop.stderr.txt').write_bytes(result.stderr)
    assert result.returncode==0,(result.returncode,folder)
    desktop=next(iter(set((root/'logs/S4-Hovmoller').glob('desktop-Portable-*'))-before))
    summary=json.loads((desktop/'summary.json').read_text(encoding='utf-8-sig'));assert summary['all_passed']
    check=subprocess.run([sys.executable,str(root/'tests/26NOV22_v01_hovmoller_exports.py'),'--folder',str(desktop)],cwd=root,capture_output=True,timeout=60)
    (folder/'exports.stdout.txt').write_bytes(check.stdout);(folder/'exports.stderr.txt').write_bytes(check.stderr);assert check.returncode==0
    report={'status':'PASS','desktop':str(desktop),'executable':str(portable/'HDFStudio.exe'),'archive_sha256':digest(package),'files':{str(path.relative_to(portable)):digest(path) for path in portable.rglob('*') if path.is_file()},'environment':{key:environment[key] for key in ['PATH','HDFSTUDIO_PYTHON']},'desktop_exit':result.returncode,'export_exit':check.returncode}
    (folder/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    logging.info('PASS %s; %d files; %d desktop checks',folder,len(report['files']),summary['checks'])


if __name__=='__main__':
    main()
