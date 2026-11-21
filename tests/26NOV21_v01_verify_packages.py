"""📦 验证本轮本地包，绝不上传。

输入：--packages指定的两个当前CMake版本zip，--source-ref（缺省HEAD），HDFSTUDIO_ZOOM_SAMPLE。
输出：--output指定目录下verification.json、日志与派生解压目录。
缺省仍使用build/package-full-regression和logs/S4-FullRegression-20261121/package；
例如 --packages build/package-local-0.12.07 --output logs/S4-Playback/package-0.12.07。
默认运行即可；命令、源包逐文件核验、资源许可严格哈希、PE依赖及纯系统PATH实跑均留证。
"""
import ctypes
import argparse
from ctypes import wintypes
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import zipfile

from importlib.util import module_from_spec, spec_from_file_location


def digest(data):
    return hashlib.sha256(data).hexdigest()


def pe_imports(path):
    data = path.read_bytes()
    assert data[:2] == b"MZ"
    header = struct.unpack_from("<I", data, 60)[0]
    assert data[header:header + 4] == b"PE\0\0"
    machine, section_count = struct.unpack_from("<HH", data, header + 4)
    optional_size = struct.unpack_from("<H", data, header + 20)[0]
    optional = header + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    directories = optional + (112 if magic == 0x20B else 96)
    imports_rva, imports_size = struct.unpack_from("<II", data, directories + 8)
    sections = []
    for index in range(section_count):
        location = optional + optional_size + 40 * index
        size, rva, raw_size, raw = struct.unpack_from("<IIII", data, location + 8)
        sections.append((rva, max(size, raw_size), raw))

    def file_offset(rva):
        for start, size, raw in sections:
            if start <= rva < start + size:
                return raw + rva - start
        raise ValueError(rva)

    imports = []
    if imports_rva:
        location = file_offset(imports_rva)
        while any(data[location:location + 20]):
            name_rva = struct.unpack_from("<I", data, location + 12)[0]
            start = file_offset(name_rva)
            end = data.index(0, start)
            imports.append(data[start:end].decode("ascii"))
            location += 20
    return {"machine": hex(machine), "imports": imports}


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packages", default="build/package-full-regression")
    parser.add_argument("--output", default="logs/S4-FullRegression-20261121/package")
    parser.add_argument("--source-ref", default="HEAD")
    options = parser.parse_args()
    version = re.search(r"project\(HDFStudio VERSION ([0-9.]+)", (root / "CMakeLists.txt").read_text(encoding="utf-8")).group(1)
    folder = root / options.output
    folder.mkdir(parents=True, exist_ok=True)
    packages = root / options.packages
    git = shutil.which("git") or str(Path(os.environ["ProgramFiles"]) / "Git/cmd/git.exe")
    commit = subprocess.check_output([git, "rev-parse", options.source_ref], cwd=root, text=True).strip()
    tracked = subprocess.check_output([git, "ls-tree", "-r", "-z", "--name-only", commit], cwd=root).decode("utf-8").strip("\0").split("\0")
    source_zip = packages / ("HDFStudio-" + version + "-src.zip")
    program_zip = packages / ("HDFStudio-" + version + "-win64.zip")
    report = {"commit": commit, "packages": {str(path.name): {"sha256": digest(path.read_bytes()), "bytes": path.stat().st_size} for path in [source_zip, program_zip]}, "source_files": len(tracked), "commands": [], "status": "FAIL"}
    with zipfile.ZipFile(source_zip) as archive:
        prefix = "HDFStudio-" + version + "/"
        names = [name[len(prefix):] for name in archive.namelist() if not name.endswith("/")]
        assert set(names) == set(tracked)
        forbidden = [name for name in names if any(part in {"logs", "ext", "build", ".git", "decompile_4_learning_v3"} for part in Path(name).parts) or Path(name).name in {"AGENTS.md", "DESIGN_STAGE4.md"} or Path(name).suffix.lower() in {".nc", ".h5", ".exe", ".dll", ".pdb"}]
        assert not forbidden, forbidden
        for name in names:
            expected = subprocess.check_output([git, "show", f"{commit}:{name}"], cwd=root)
            actual = archive.read(prefix + name)
            if name.startswith(("resources/colormaps/", "licenses/")):
                assert actual == expected, name
            else:
                assert actual.replace(b"\r\n", b"\n") == expected.replace(b"\r\n", b"\n"), name
        report["source_all_files_match_commit"] = True
    extract = folder / "extracted"
    extract.mkdir(exist_ok=True)
    with zipfile.ZipFile(program_zip) as archive:
        archive.extractall(extract)
    runtime = extract / ("HDFStudio-" + version + "-win64")
    assert not list(runtime.rglob("*test*.exe"))
    for name in ["HDFStudio.exe", "hdf_cli.exe", "netcdf.dll"]:
        assert (runtime / name).read_bytes() == (root / "build/output/Release" / name).read_bytes()
    for subdirectory in ["resources/colormaps", "licenses/colormaps"]:
        for path in (root / subdirectory).rglob("*"):
            if path.is_file():
                assert (runtime / subdirectory / path.relative_to(root / subdirectory)).read_bytes() == path.read_bytes()
    report["resource_palette_count"] = len(list((runtime / "resources/colormaps/palettes").glob("*.json")))
    report["colormap_license_file_count"] = len(list((runtime / "licenses/colormaps").glob("*")))
    assert report["resource_palette_count"] == 13 and report["colormap_license_file_count"] == 7
    dependencies = {}
    shipped = {path.name.lower() for path in runtime.rglob("*.dll")}
    system = Path(os.environ["SystemRoot"]) / "System32"
    for binary in [*runtime.rglob("*.exe"), *runtime.rglob("*.dll")]:
        details = pe_imports(binary)
        assert details["machine"] == "0x8664"
        missing = [name for name in details["imports"] if name.lower() not in shipped and not (system / name).is_file() and not name.lower().startswith(("api-ms-", "ext-ms-"))]
        assert not missing, (binary, missing)
        dependencies[str(binary.relative_to(runtime))] = details
    report["pe_dependencies"] = dependencies
    environment = os.environ.copy()
    environment["PATH"] = str(system) + ";" + str(system.parent)
    environment["QT_PLUGIN_PATH"] = ""
    environment["QT_QPA_PLATFORM_PLUGIN_PATH"] = ""
    environment["QT_DEBUG_PLUGINS"] = "1"
    environment["QT_LOGGING_TO_CONSOLE"] = "1"
    environment["HDFSTUDIO_PYTHON"] = ""
    report["test_path"] = environment["PATH"]

    def run(arguments, label):
        result = subprocess.run([str(arg) for arg in arguments], capture_output=True, env=environment, cwd=folder, timeout=45)
        (folder / (label + ".stdout.txt")).write_bytes(result.stdout)
        (folder / (label + ".stderr.txt")).write_bytes(result.stderr)
        report["commands"].append({"arguments": [str(arg) for arg in arguments], "exit": result.returncode, "evidence": label})
        assert result.returncode == 0, label
        return result.stdout.decode("utf-8", errors="replace")

    assert version in run([runtime / "HDFStudio.exe", "--version"], "version")
    assert len(run([runtime / "hdf_cli.exe", "cmap"], "cmap").splitlines()) == 18
    sample = Path(os.environ["HDFSTUDIO_ZOOM_SAMPLE"])
    before = digest(sample.read_bytes())
    png = folder / "portable-preview.png"
    for output in [png, Path(str(png) + ".json")]:
        if output.exists():
            output.unlink()
    run([runtime / "hdf_cli.exe", "props", sample, "/PRECT"], "props")
    run([runtime / "HDFStudio.exe", "--preview", "PRECT", "--png", png, sample], "preview")
    spec = spec_from_file_location("regression", root / "tests/26NOV21_v01_full_regression.py")
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    with module.netCDF4.Dataset(sample) as dataset:
        values = module.np.asarray(dataset["PRECT"][0], dtype=float)
    expected, low, high = module.expected_rgb(values)
    assert module.np.array_equal(module.png_rgb(png), expected)
    report["portable_png"] = {"width": expected.shape[1], "height": expected.shape[0], "mismatched_pixels": 0, "min": low, "max": high}
    # Real Windows GUI process on the pure-system PATH: verify main window is visible, close it,
    # and check normal exit. The detailed mouse/geometry checks live in the desktop script.
    user32 = ctypes.WinDLL("user32")
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    with (folder / "portable-gui-plugin.txt").open("wb") as stream:
        child = subprocess.Popen([str(runtime / "HDFStudio.exe"), str(sample)], env=environment, cwd=folder, stdout=stream, stderr=stream)
        handles = []

        @callback_type
        def visit(handle, parameter):
            pid = wintypes.DWORD()
            user32.GetWindowThreadProcessId(handle, ctypes.byref(pid))
            text = ctypes.create_unicode_buffer(512)
            user32.GetWindowTextW(handle, text, 512)
            if pid.value == child.pid and text.value == "HDFStudio" and user32.IsWindowVisible(handle):
                handles.append(handle)
            return True

        deadline = time.monotonic() + 20
        while not handles and time.monotonic() < deadline and child.poll() is None:
            user32.EnumWindows(visit, 0)
            time.sleep(0.2)
        assert handles, "Portable GUI main window did not appear"
        # Verify the file's variable is present in the actual portable GUI, then capture it.
        inspection = r'''
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Windows.Forms,System.Drawing
Add-Type -TypeDefinition @"
using System;using System.Runtime.InteropServices;
public class PortableWindow {
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int c);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern void keybd_event(byte v,byte s,uint f,IntPtr e);
}
"@
Start-Sleep -Seconds 8
$uia=[System.Windows.Automation.AutomationElement]
$condition=[System.Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,__PID__)
$windows=@($uia::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children,$condition))
$names=@()
foreach($window in $windows){$names+=@($window.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition)|ForEach-Object {$_.Current.Name})}
if(-not ($names -contains 'PRECT')){throw 'Portable GUI did not expose the PRECT dataset'}
$names|ConvertTo-Json|Set-Content portable-gui-uia.json -Encoding UTF8
$process=Get-Process -Id __PID__
$handle=$process.MainWindowHandle
if($handle-eq [IntPtr]::Zero){throw 'Portable main window handle missing'}
$null=[PortableWindow]::ShowWindow($handle,3)
[PortableWindow]::keybd_event(0x12,0,0,[IntPtr]::Zero)
[PortableWindow]::keybd_event(0x12,0,2,[IntPtr]::Zero)
$null=[PortableWindow]::SetForegroundWindow($handle)
Start-Sleep -Milliseconds 400
$r=[Windows.Forms.SystemInformation]::VirtualScreen
$b=[Drawing.Bitmap]::new($r.Width,$r.Height)
$g=[Drawing.Graphics]::FromImage($b)
try{$g.CopyFromScreen($r.X,$r.Y,0,0,$b.Size);$b.Save((Join-Path (Get-Location) 'portable-gui.png'))}finally{$g.Dispose();$b.Dispose()}
'''.replace("__PID__", str(child.pid))
        run([system / "WindowsPowerShell/v1.0/powershell.exe", "-NoProfile", "-Command", inspection], "portable-gui-inspect")
        # The initial visible HDFStudio window may have been the startup splash.
        # Reacquire the live main-window HWND after dataset/UIA readiness.
        handles.clear()
        user32.EnumWindows(visit, 0)
        assert handles, "Portable main window disappeared before close"
        assert user32.PostMessageW(handles[0], 0x10, 0, 0), "Portable WM_CLOSE failed"
        assert child.wait(timeout=10) == 0
    report["portable_gui_open_and_exit"] = "PASS"
    assert digest(sample.read_bytes()) == before
    report["input_unchanged"] = before
    report["status"] = "PASS"
    (folder / "verification.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key not in {"pe_dependencies", "commands"}}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
