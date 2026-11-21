"""🔬 只用解析夹具核验 CLI 的类型/路径/导出错误恢复。

输入：netCDF4-python 创建的小型 Q/A 等价夹具；产品 build/output/<config>/hdf_cli.exe。
输出：logs/S4-FullRegression-20261121/edges/<config>/，不读写业务源文件。
默认 Release；--config Debug 切配置。源 signed/unsigned 整数文本精确比对，double 平面逐值精确。
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

import netCDF4
import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", choices=["Debug", "Release"], default="Release")
    options = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    folder = root / "logs/S4-FullRegression-20261121/edges" / options.config
    folder.mkdir(parents=True, exist_ok=True)
    cli = root / "build/output" / options.config / "hdf_cli.exe"
    # Construct a Unicode path independently of the shell's active codepage.
    fixture = folder / ("fixture-" + chr(0x4E2D) + chr(0x6587) + ".nc")
    stored_values = np.array([[-3, 0, 5], [8, 13, 21]], dtype=np.int32)
    # This installed netCDF4-python passes UTF-8 bytes to an ANSI-path netCDF-C build.
    # Create with an ASCII name, then rename with Python's Unicode filesystem API, so the
    # test exercises the product's Unicode handling instead of the fixture generator's ABI.
    ascii_fixture = folder / "fixture-ascii.nc"
    with netCDF4.Dataset(ascii_fixture, "w", format="NETCDF4") as dataset:
        group = dataset.createGroup("nested")
        group.createDimension("y", 2)
        group.createDimension("x", 3)
        numeric = group.createVariable("field", "i4", ("y", "x"))
        numeric.set_auto_maskandscale(False)
        numeric[:] = stored_values
        numeric.scale_factor = 0.5
        numeric.add_offset = -2.0
        group.createVariable("scalar", "i8")[...] = np.int64(-5000000001)
        signed = group.createVariable("signed64", "i8", ("y", "x"))
        signed[:] = np.array([[-9223372036854775807, -5000000000, 0], [7, 9007199254740993, 9223372036854775807]], dtype=np.int64)
        unsigned = group.createVariable("unsigned64", "u8", ("y", "x"))
        unsigned[:] = np.array([[0, 4294967296, 9007199254740993], [7, 18446744073709551614, 100]], dtype=np.uint64)
        group.createVariable("strings", str, ("y", "x"))
        compound_type = dataset.createCompoundType(np.dtype([("u", "i4"), ("v", "f8")]), "pair")
        group.createVariable("compound", compound_type, ("y", "x"))
        vlen_type = dataset.createVLType(np.int32, "ragged")
        group.createVariable("vlen", vlen_type, ("y", "x"))
        enum_type = dataset.createEnumType(np.uint8, "status", {"clear": 0, "cloud": 1})
        group.createVariable("enum", enum_type, ("y", "x"))
    if fixture.exists():
        fixture.unlink()
    ascii_fixture.rename(fixture)
    records = []

    def run(name, arguments, expected):
        child = subprocess.run([str(cli), *[str(arg) for arg in arguments]], capture_output=True, timeout=30)
        (folder / (name + ".stdout.txt")).write_bytes(child.stdout)
        (folder / (name + ".stderr.txt")).write_bytes(child.stderr)
        record = {"case": name, "arguments": [str(arg) for arg in arguments], "expected_exit": expected, "actual_exit": child.returncode, "pass": child.returncode == expected}
        records.append(record)
        (folder / "results.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
        assert record["pass"], record
        return child.stdout.decode("utf-8", errors="replace")

    run("nested-probe", ["probe", fixture], 0)
    run("nested-properties", ["props", fixture, "/nested/field"], 0)
    output = folder / "decoded.csv"
    if output.exists():
        output.unlink()
    run("nested-plane", ["csv", fixture, "/nested/field", output], 0)
    assert np.array_equal(np.loadtxt(output, delimiter=","), stored_values * 0.5 - 2)
    for name, expected in [
        ("signed64", "-9223372036854775807,-5000000000,0\n7,9007199254740993,9223372036854775807\n"),
        ("unsigned64", "0,4294967296,9007199254740993\n7,18446744073709551614,100\n"),
        ("scalar", "-5000000001\n"),
    ]:
        text = run("exact-" + name, ["export", fixture, "/nested/" + name], 0)
        assert text.replace("\r\n", "\n") == expected, (name, text)
    # User-defined and variable string numeric exports must fail explicitly, without an empty file.
    for kind in ["strings", "compound", "vlen", "enum"]:
        target = folder / ("unsupported-" + kind + ".txt")
        if target.exists():
            target.unlink()
        run("unsupported-" + kind, ["export", fixture, "/nested/" + kind, target], 3)
        assert not target.exists()
    sentinel = folder / "keep.csv"
    sentinel.write_bytes(b"KEEP")
    run("csv-overwrite", ["csv", fixture, "/nested/field", sentinel], 4)
    assert sentinel.read_bytes() == b"KEEP"
    run("text-overwrite", ["export", fixture, "/nested/field", sentinel], 4)
    assert sentinel.read_bytes() == b"KEEP"
    run("missing-output-directory", ["csv", fixture, "/nested/field", folder / "absent" / "out.csv"], 4)
    run("scalar-not-plane", ["csv", fixture, "/nested/scalar", folder / "scalar.csv"], 3)
    damaged = folder / "truncated.nc"
    damaged.write_bytes(fixture.read_bytes()[:64])
    run("truncated", ["info", damaged], 3)
    plain = folder / "unknown.nc"
    plain.write_bytes(b"unknown input")
    run("unknown-probe", ["probe", plain], 3)
    # Failure must not poison a later independent request.
    run("recovery", ["props", fixture, "/nested/field"], 0)
    print(f"{options.config}: {len(records)} command cases passed; exact nested numeric and int64 text assertions passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
