"""🧪 时空剖面双配置本地编译/单元测试/CTest执行器。

输入：build/vs2022/CMakeCache.txt；业务样本仅由HDFSTUDIO_*环境变量提供。
输出：logs/S4-Hovmoller/full-时间/{配置}/日志、实际退出码、输入/二进制哈希。
用法：python tests/26NOV22_v01_hovmoller_verify.py（缺省clean双配置）；--incremental用于开发。
不安装、不推送、不发版；真实桌面单独顺序运行。
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import logging
import os
import re
import subprocess
import sys
import time


def digest(path):
    checksum=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):
            checksum.update(block)
    return checksum.hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--incremental',action='store_true')
    parser.add_argument('--configuration',choices=['Debug','Release'])
    options=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    folder=root/'logs/S4-Hovmoller'/('full-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    folder.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO,format='%(asctime)s %(message)s',handlers=[logging.StreamHandler(sys.stdout),logging.FileHandler(folder/'run.log',encoding='utf-8')])
    cache=(root/'build/vs2022/CMakeCache.txt').read_text()
    cmake=next(line.split('=',1)[1] for line in cache.splitlines() if line.startswith('CMAKE_COMMAND:INTERNAL='))
    ctest=Path(cmake).with_name('ctest.exe')
    environment=os.environ.copy()
    environment.update(QT_QPA_PLATFORM='windows',QT_LOGGING_TO_CONSOLE='1',HDFSTUDIO_LOG_DIR=str(root/'logs'),HDFSTUDIO_PYTHON=environment.get('HDFSTUDIO_PYTHON',sys.executable))
    required=['HDFSTUDIO_ZOOM_SAMPLE','HDFSTUDIO_CLASSIC_SAMPLE','HDFSTUDIO_PACKED_SAMPLE','HDFSTUDIO_FORECAST_ANCHOR','HDFSTUDIO_FORECAST_TARGET']
    for key in required:
        assert environment.get(key) and Path(environment[key]).is_file(),f'Set {key} to an existing read-only sample'
    hashes={str(Path(environment[key]).resolve()):digest(Path(environment[key])) for key in required}
    inputs=[root/'CMakeLists.txt',*sorted((root/'src').rglob('*.cpp')),*sorted((root/'src').rglob('*.hpp')),root/'resources/netcdf_report.py',root/'tests/hovmoller_test.cpp',root/'tests/drop_targets_test.cpp']
    source_hashes={str(path.relative_to(root)):digest(path) for path in inputs}
    report={'status':'RUNNING','clean':not options.incremental,'sources':source_hashes,'samples':hashes,'commands':[],'configurations':[]}
    def save():
        (folder/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    def run(arguments,label,timeout=1800):
        logging.info('Start %s',label)
        started=time.perf_counter()
        result=subprocess.run(list(map(str,arguments)),cwd=root,env=environment,capture_output=True,timeout=timeout)
        (folder/(label+'.stdout.txt')).write_bytes(result.stdout)
        (folder/(label+'.stderr.txt')).write_bytes(result.stderr)
        report['commands'].append({'arguments':list(map(str,arguments)),'label':label,'exit':result.returncode,'seconds':time.perf_counter()-started})
        save()
        assert result.returncode==0,(label,result.returncode,str(folder))
        logging.info('PASS %s in %.2fs',label,time.perf_counter()-started)
        return result
    save()
    try:
        for configuration in ([options.configuration] if options.configuration else ['Debug','Release']):
            target=folder/configuration;target.mkdir()
            environment.update(HDFSTUDIO_TEST_CONFIG=configuration,HDFSTUDIO_TEST_LOG_DIR=str(target),HDFSTUDIO_HOVMOLLER_EXPORT=str(target))
            arguments=[cmake,'--build','build/vs2022','--config',configuration]
            if not options.incremental:arguments+=['--clean-first']
            arguments+=['--target','hdf_explorer','hdf_cli','hovmoller_tests','hdf_explorer_drop_tests','--','/verbosity:minimal']
            run(arguments,configuration+'-build')
            counts={}
            for name in ['hovmoller_tests','hdf_explorer_drop_tests']:
                run([root/'build/output'/configuration/(name+'.exe'),'-o',str(target/(name+'.txt'))+',txt'],configuration+'-'+name,300)
                text=(target/(name+'.txt')).read_text(encoding='utf-8')
                match=re.search(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped',text)
                assert match and int(match[2])==int(match[3])==0,(configuration,name,match.groups() if match else text[-2000:])
                counts[name]=int(match[1])
                for warning in ['QProcess: Destroyed','QTemporaryDir: Unable','parent must be in parent hierarchy','Called object is not of the correct type']:
                    if name=='hovmoller_tests':assert warning not in text,(configuration,warning)
            environment.pop('HDFSTUDIO_HOVMOLLER_EXPORT',None)
            run([ctest,'--test-dir','build/vs2022','-C',configuration,'--output-on-failure'],configuration+'-ctest',360)
            run([sys.executable,root/'tests/26NOV22_v01_hovmoller_exports.py','--folder',target],configuration+'-exports',120)
            report['configurations'].append({'configuration':configuration,'tests':counts,'binaries':{name:digest(root/'build/output'/configuration/(name+'.exe')) for name in ['HDFStudio','hdf_cli','hovmoller_tests','hdf_explorer_drop_tests']}})
            save()
        for path,expected in source_hashes.items():assert digest(root/path)==expected,path
        for path,expected in hashes.items():assert digest(Path(path))==expected,path
        report['status']='PASS';save();logging.info('PASS %s',folder)
    except Exception:
        report['status']='FAIL';save();logging.exception('Verification failed; evidence preserved');raise


if __name__=='__main__':
    main()
