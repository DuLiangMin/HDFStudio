"""🌡️ 两份真实预报的时间坐标、双图指标与残差对账。

输入：HDFSTUDIO_FORECAST_ANCHOR/TARGET 两个只读NetCDF文件；
HDFSTUDIO_FORECAST_VARIABLE 默认 TMP_2maboveground；现有Debug/Release hdf_cli。
输出：logs/S4-Metrics-Sync/forecast-<timestamp>，时间/坐标核查、逐案例参考、命令日志与SHA256。
用法：python tests/26NOV21_v01_forecast_metrics.py；不构建、不修改源数据。
"""
import datetime
import hashlib
import json
import logging
import os
from pathlib import Path
import subprocess
import sys
import netCDF4
import numpy as np


def main():
    root = Path(__file__).resolve().parents[1]
    folder = root / "logs/S4-Metrics-Sync" / ("forecast-"+datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    folder.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", handlers=[logging.StreamHandler(sys.stdout),logging.FileHandler(folder / "runner.log",encoding="utf-8")])
    paths = [Path(os.environ[key]) for key in ["HDFSTUDIO_FORECAST_ANCHOR","HDFSTUDIO_FORECAST_TARGET"]]
    variable = os.environ.get("HDFSTUDIO_FORECAST_VARIABLE","TMP_2maboveground")
    hashes = [hashlib.sha256(path.read_bytes()).hexdigest() for path in paths]
    records = []
    with netCDF4.Dataset(paths[0]) as anchor, netCDF4.Dataset(paths[1]) as target:
        a_var,b_var = anchor[variable],target[variable]
        assert a_var.ndim == b_var.ndim == 3 and a_var.shape[-2:] == b_var.shape[-2:]
        assert a_var.units == b_var.units == "K"
        for dimension_a,dimension_b in zip(a_var.dimensions[-2:],b_var.dimensions[-2:]):
            np.testing.assert_array_equal(anchor[dimension_a][:],target[dimension_b][:])
        time_a,time_b = anchor[a_var.dimensions[0]],target[b_var.dimensions[0]]
        dates_a = netCDF4.num2date(time_a[:],time_a.units,getattr(time_a,"calendar","standard"))
        dates_b = netCDF4.num2date(time_b[:],time_b.units,getattr(time_b,"calendar","standard"))
        lookup = {str(date):index for index,date in enumerate(dates_b)}
        matches = [(index,lookup[str(date)]) for index,date in enumerate(dates_a) if str(date) in lookup]
        assert matches, "No common verified timestamps"
        selected = [matches[0],matches[len(matches)//2],matches[-1]]
        selected.extend([(0,0),(3,5)])
        for config in ["Debug","Release"]:
            cli = root / "build/output" / config / "hdf_cli.exe"
            for case,(a_index,b_index) in enumerate(selected):
                a = np.ma.filled(a_var[a_index],np.nan).astype(float)
                b = np.ma.filled(b_var[b_index],np.nan).astype(float)
                finite = np.isfinite(a)&np.isfinite(b)
                av,bv = a[finite],b[finite]
                diff = bv-av
                reference = {"pearson_r":float(np.corrcoef(av,bv)[0,1]),"rmse":float(np.sqrt(np.mean(diff**2))),"mae":float(np.mean(np.abs(diff))),"bias":float(np.mean(diff))}
                command = [str(cli),"corr",str(paths[0]),variable,str(paths[1]),variable,"--anchor-idx",str(a_index),"--target-idx",str(b_index)]
                result = subprocess.run(command,capture_output=True,timeout=60)
                (folder/f"{config}-{case}.out.txt").write_bytes(result.stdout)
                (folder/f"{config}-{case}.err.txt").write_bytes(result.stderr)
                assert result.returncode == 0
                actual = dict(line.split(": ",1) for line in result.stdout.decode().splitlines())
                assert actual["shape"] == f"{a.shape[0]}x{a.shape[1]}"
                assert int(actual["valid_pairs"]) == finite.sum()
                assert int(actual["excluded_pairs"]) == a.size-finite.sum()
                for key,value in reference.items():
                    assert abs(float(actual[key])-value) <= 1e-12*max(1,abs(value)), (config,case,key,actual[key],value)
                for source,index,plane,label in [(paths[0],a_index,a,"anchor"),(paths[1],b_index,b,"target")]:
                    values = plane[np.isfinite(plane)]
                    counts = np.histogram(values,bins=256)[0]
                    probability = counts[counts>0]/values.size
                    entropy = float(-np.sum(probability*np.log2(probability)))
                    process = subprocess.run([str(cli),"entropy",str(source),variable,"256",str(index)],capture_output=True,timeout=60)
                    (folder/f"{config}-{case}-{label}-entropy.txt").write_bytes(process.stdout+process.stderr)
                    assert process.returncode == 0
                    entropy_result = dict(line.split(": ",1) for line in process.stdout.decode().splitlines())
                    assert abs(float(entropy_result["entropy_bits"])-entropy)<=1e-12*max(1,abs(entropy))
                    reference[label+"_entropy_bits"] = entropy
                record={"configuration":config,"anchor_index":a_index,"target_index":b_index,"lag":b_index-a_index,"anchor_time":str(dates_a[a_index]),"target_time":str(dates_b[b_index]),"same_timestamp":str(dates_a[a_index])==str(dates_b[b_index]),"shape":list(a.shape),"valid_pairs":int(finite.sum()),"residual_min":float(diff.min()),"residual_max":float(diff.max()),"reference":reference,"status":"PASS"}
                records.append(record)
                logging.info("%s a[%s] b[%s] same_time=%s r=%.12g RMSE=%.12g PASS",config,a_index,b_index,record["same_timestamp"],reference["pearson_r"],reference["rmse"])
    for path,digest in zip(paths,hashes):
        assert hashlib.sha256(path.read_bytes()).hexdigest()==digest
    (folder/"result.json").write_text(json.dumps({"paths":[str(path) for path in paths],"sha256":hashes,"common_timestamps":len(matches),"cases":records},indent=2),encoding="utf-8")
    logging.info("PASS %s cases with unchanged source hashes: %s",len(records),folder)


if __name__ == "__main__":
    main()
