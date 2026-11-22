"""📊 独立进程记录剖面QtTest的GUI/CLI峰值内存及退出后子进程回收。

输入：build/output/Debug（可选--configuration）、可选HDFSTUDIO_FORECAST_ANCHOR。
输出：logs/S4-Hovmoller/memory-时间/result.json和带时间戳日志。
例：python tests/26NOV22_v01_hovmoller_memory.py
"""
from pathlib import Path
import argparse
import datetime
import json
import logging
import os
import subprocess
import time

import psutil


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration',choices=['Debug','Release'],default='Debug')
    configuration=parser.parse_args().configuration
    root=Path(__file__).resolve().parents[1]
    folder=root/'logs/S4-Hovmoller'/('memory-'+configuration+'-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    folder.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO,format='%(asctime)s %(message)s',handlers=[logging.StreamHandler(),logging.FileHandler(folder/'run.log',encoding='utf-8')])
    env=os.environ.copy();env.update(QT_QPA_PLATFORM='windows',HDFSTUDIO_TEST_LOG_DIR=str(folder),HDFSTUDIO_LOG_DIR=str(root/'logs'))
    arguments=[str(root/'build/output'/configuration/'hovmoller_tests.exe'),'heartbeatAndCancel','viewCloseLifecycle','businessSamples','-o',str(folder/'unit.txt')+',txt']
    peak_parent=peak_children=peak_total=0;seen_children=set();samples=0
    with (folder/'stdout.txt').open('wb') as out,(folder/'stderr.txt').open('wb') as err:
        process=subprocess.Popen(arguments,env=env,cwd=root,stdout=out,stderr=err)
        parent=psutil.Process(process.pid)
        start=time.perf_counter()
        while process.poll() is None:
            if time.perf_counter()-start>120:
                process.kill();raise TimeoutError('Memory test process exceeded 120 seconds')
            try:
                parent_bytes=parent.memory_info().rss
                child_bytes=0
                for child in parent.children(recursive=True):
                    try:
                        child_bytes+=child.memory_info().rss
                        seen_children.add((child.pid,child.create_time()))
                    except psutil.NoSuchProcess:
                        pass
                peak_parent=max(peak_parent,parent_bytes);peak_children=max(peak_children,child_bytes);peak_total=max(peak_total,parent_bytes+child_bytes);samples+=1
            except psutil.NoSuchProcess:
                break
            time.sleep(.025)
        code=process.wait(timeout=5)
    assert code==0,(code,folder)
    remaining=[]
    for pid,created in seen_children:
        try:
            child=psutil.Process(pid)
            if child.create_time()==created and child.is_running():remaining.append(pid)
        except psutil.NoSuchProcess:
            pass
    assert not remaining,remaining
    result={'status':'PASS','configuration':configuration,'exit':code,'seconds':time.perf_counter()-start,'samples':samples,'gui_peak_rss':peak_parent,'children_peak_rss':peak_children,'combined_sampled_peak_rss':peak_total,'child_processes_seen':len(seen_children),'surviving_children':remaining,'scope':'sampled RSS includes Qt/runtime/library overhead; not a hard allocator bound'}
    (folder/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    logging.info('PASS %s',result)


if __name__=='__main__':
    main()
