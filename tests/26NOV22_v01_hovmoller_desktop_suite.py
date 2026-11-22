"""🖱️ 顺序执行新剖面及原显示/指标/熵图/播放真实桌面回归。

输入：当前双配置编译输出，业务样本路径仅由HDFSTUDIO_*环境变量提供。
输出：logs/S4-Hovmoller/desktop-suite-时间/记录实际退出码；各脚本保留自身截图证据。
运行：python tests/26NOV22_v01_hovmoller_desktop_suite.py；完整QtTest结束后单独执行。
"""
from pathlib import Path
import datetime
import json
import logging
import os
import subprocess
import sys
import time


def main():
    root=Path(__file__).resolve().parents[1]
    folder=root/'logs/S4-Hovmoller'/('desktop-suite-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    folder.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO,format='%(asctime)s %(message)s',handlers=[logging.StreamHandler(sys.stdout),logging.FileHandler(folder/'run.log',encoding='utf-8')])
    environment=os.environ.copy();environment.update(QT_QPA_PLATFORM='windows',HDFSTUDIO_PYTHON=environment.get('HDFSTUDIO_PYTHON',sys.executable))
    powershell=Path(environment.get('SystemRoot',r'C:\Windows'))/'System32/WindowsPowerShell/v1.0/powershell.exe'
    records=[];report={'status':'RUNNING','records':records}
    def save():
        (folder/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    def run(arguments,label,timeout=300):
        logging.info('Start %s',label);started=time.perf_counter()
        completed=subprocess.run(list(map(str,arguments)),cwd=root,env=environment,capture_output=True,timeout=timeout)
        (folder/(label+'.stdout.txt')).write_bytes(completed.stdout);(folder/(label+'.stderr.txt')).write_bytes(completed.stderr)
        records.append({'label':label,'arguments':list(map(str,arguments)),'exit':completed.returncode,'seconds':time.perf_counter()-started});save()
        assert completed.returncode==0,(label,completed.returncode,folder)
        logging.info('PASS %s in %.1fs',label,time.perf_counter()-started)
    save()
    try:
        for configuration in ['Debug','Release']:
            scripts=['26NOV22_v01_hovmoller_desktop.ps1']
            for script in scripts:
                arguments=[powershell,'-NoProfile','-ExecutionPolicy','Bypass','-File',root/'tests'/script,'-Configuration',configuration]
                if script=='26NOV21_v01_desktop_regression.ps1':arguments+=['-LogDirectory',folder/('existing-'+configuration)]
                before=set((root/'logs/S4-Hovmoller').glob('desktop-'+configuration+'-*'))
                run(arguments,configuration+'-'+Path(script).stem)
                if script=='26NOV22_v01_hovmoller_desktop.ps1':
                    created=set((root/'logs/S4-Hovmoller').glob('desktop-'+configuration+'-*'))-before
                    assert len(created)==1,created
                    desktop=next(iter(created));summary=json.loads((desktop/'summary.json').read_text(encoding='utf-8-sig'));assert summary['all_passed']
                    run([sys.executable,root/'tests/26NOV22_v01_hovmoller_exports.py','--folder',desktop],configuration+'-desktop-exports',120)
            run([sys.executable,root/'tests/26NOV22_v01_hovmoller_memory.py','--configuration',configuration],configuration+'-memory',180)
        run([sys.executable,root/'tests/26NOV21_v01_texture_entropy_regression.py'],'existing-desktop-regression',1200)
        report['status']='PASS';save();logging.info('PASS %s',folder)
    except Exception:
        report['status']='FAIL';save();logging.exception('Desktop evidence preserved');raise


if __name__=='__main__':
    main()
