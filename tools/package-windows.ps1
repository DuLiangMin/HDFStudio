# Builds the two GitHub release assets from an already built and tested Release output.
#   build/package/HDFStudio-<version>-win64.zip   portable Windows program (no installer)
#   build/package/HDFStudio-<version>-src.zip     source snapshot of the given git commit (git archive)
# Inputs: build/output/Release (run tests/run-stage4.ps1 first), ext/*/ licence files,
#         the VS 2022 VC143 CRT redistributable folder, Qt LGPL-3.0 text (-QtLicense).
# Usage (defaults are enough after a Release build):
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/package-windows.ps1
#   powershell ... -File tools/package-windows.ps1 -SourceRef github-main
param(
  [string]$SourceRef = 'HEAD',
  [string]$QtLicense = "$env:TEMP\opencode\lgpl-3.0.txt",
  [string]$Git = 'C:\Program Files\Git\cmd\git.exe'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$version = ([regex]'project\(HDFStudio VERSION ([0-9.]+)').Match((Get-Content "$root\CMakeLists.txt" -Raw)).Groups[1].Value
if(-not $version) { throw 'Version not found in CMakeLists.txt' }
$release = "$root\build\output\Release"
$outDir = "$root\build\package"
$name = "HDFStudio-$version-win64"
$stage = "$outDir\$name"
if(Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force "$stage\licenses" | Out-Null

# Program files: the application and only the Qt parts it loads. Test binaries and the unused
# network stack (Qt6Network, tls, networkinformation, tuio touch) are not shipped.
$files = 'HDFStudio.exe','hdf_cli.exe','netcdf.dll','Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','D3Dcompiler_47.dll'
foreach($f in $files) { Copy-Item "$release\$f" $stage }
foreach($d in 'platforms','styles','imageformats') { Copy-Item "$release\$d" "$stage\$d" -Recurse }

# App-local VC++ runtime so the zip runs without installing vc_redist.
$crt = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT' -Directory |
  Sort-Object FullName -Descending | Select-Object -First 1
foreach($f in 'msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll') { Copy-Item "$($crt.FullName)\$f" $stage }

# Documents and licences.
Copy-Item "$root\LICENSE" "$stage\LICENSE.txt"
Copy-Item "$root\README.md","$root\THIRD_PARTY_NOTICES.md","$root\Changelog.md" $stage
if(-not (Test-Path $QtLicense)) { throw "Qt LGPL-3.0 text missing: $QtLicense" }
Copy-Item $QtLicense "$stage\licenses\Qt-LGPL-3.0.txt"
Copy-Item "$root\ext\netcdf-c-4.9.3\COPYRIGHT" "$stage\licenses\netCDF-C-COPYRIGHT.txt"
Copy-Item "$root\ext\hdf5-1.8.20\COPYING" "$stage\licenses\HDF5-COPYING.txt"
Copy-Item "$root\ext\zlib-1.3.1\LICENSE" "$stage\licenses\zlib-LICENSE.txt"

$zip = "$outDir\$name.zip"
if(Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $stage -DestinationPath $zip

$src = "$outDir\HDFStudio-$version-src.zip"
& $Git -C $root archive --format=zip --prefix="HDFStudio-$version/" -o $src $SourceRef
if($LASTEXITCODE -ne 0) { throw 'git archive failed' }

foreach($z in $zip, $src) {
  $item = Get-Item $z
  '{0}  {1:N1} MB  SHA256 {2}' -f $item.Name, ($item.Length / 1MB), (Get-FileHash $z -Algorithm SHA256).Hash
}
