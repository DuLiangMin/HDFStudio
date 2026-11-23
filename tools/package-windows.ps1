# Builds the two GitHub release assets from an already built and tested Release output.
#   build/package/HDFStudio-<version>-win64.zip   portable Windows program (no installer)
#   build/package/HDFStudio-<version>-src.zip     filtered public source snapshot (git archive)
# Inputs: build/output/Release (run tests/run-stage4.ps1 first), ext/*/ licence files,
#         the VS 2022 VC143 CRT redistributable folder, Qt LGPL-3.0 text (-QtLicense).
# Usage (defaults are enough after a Release build):
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/package-windows.ps1
#   powershell ... -File tools/package-windows.ps1 -SourceRef github-main
param(
  [string]$SourceRef = 'github-main',
  [string]$QtLicense = "$env:TEMP\opencode\lgpl-3.0.txt",
  [string]$Git = 'C:\Program Files\Git\cmd\git.exe',
  [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$version = ([regex]'project\(HDFStudio VERSION ([0-9.]+)').Match((Get-Content "$root\CMakeLists.txt" -Raw -Encoding UTF8)).Groups[1].Value
if(-not $version) { throw 'Version not found in CMakeLists.txt' }
$release = "$root\build\output\Release"
$outDir = if($OutputDirectory) { [IO.Path]::GetFullPath((Join-Path $root $OutputDirectory)) } else { "$root\build\package" }
$name = "HDFStudio-$version-win64"
$stage = "$outDir\$name"
foreach($existing in $stage,"$outDir\$name.zip","$outDir\HDFStudio-$version-src.zip") {
  if(Test-Path $existing) { throw "Output already exists; choose a new OutputDirectory: $existing" }
}
# Require the reviewed public tree: never silently archive local vendor sources.
$sourceFiles = & $Git -C $root -c core.quotepath=false ls-tree -r --name-only $SourceRef
if($LASTEXITCODE -ne 0) { throw 'SourceRef cannot be read' }
$excludes = Get-Content "$root\tools\public-source-excludes.txt" -Encoding UTF8 | Where-Object { $_ -and -not $_.StartsWith('#') }
foreach($file in $sourceFiles) {
  foreach($pattern in $excludes) { if($file -like $pattern) { throw "SourceRef still contains third-party code: $file" } }
  if($file -match '^(ext|build|logs)/') { throw "SourceRef contains local-only files: $file" }
}
New-Item -ItemType Directory -Force "$stage\licenses" | Out-Null

# Program files: the application and only the Qt parts it loads. Test binaries and the unused
# network stack (Qt6Network, tls, networkinformation, tuio touch) are not shipped.
$files = 'HDFStudio.exe','hdf_cli.exe','netcdf.dll','Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','D3Dcompiler_47.dll'
foreach($f in $files) { Copy-Item "$release\$f" $stage }
foreach($d in 'platforms','styles','imageformats') { Copy-Item "$release\$d" "$stage\$d" -Recurse }
# Definitions and samples are embedded in eccodes_memfs.dll; keep all pinned decoder DLLs.
$gribManifest = Get-Content "$root\licenses\eccodes\native-manifest.json" -Raw -Encoding UTF8 | ConvertFrom-Json
foreach($entry in $gribManifest.files) {
  if(-not $entry.file.StartsWith('bin/')) { continue }
  $dll = Join-Path $release (Split-Path -Leaf $entry.file)
  if((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) { throw "GRIB runtime hash mismatch: $dll" }
  Copy-Item -LiteralPath $dll -Destination $stage
}

# App-local VC++ runtime so the zip runs without installing vc_redist.
$crt = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT' -Directory |
  Sort-Object FullName -Descending | Select-Object -First 1
foreach($f in 'msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll') { Copy-Item "$($crt.FullName)\$f" $stage }

# Documents and licences.
Copy-Item "$root\LICENSE" "$stage\LICENSE.txt"
Copy-Item "$root\LICENSE" "$stage\LICENSE"
Copy-Item "$root\README.md","$root\README_CN.md","$root\THIRD_PARTY_NOTICES.md","$root\Changelog.md" $stage
Copy-Item "$root\docs" "$stage\docs" -Recurse
Copy-Item "$root\doc" "$stage\doc" -Recurse
if(-not (Test-Path $QtLicense)) { throw "Qt LGPL-3.0 text missing: $QtLicense" }
Copy-Item $QtLicense "$stage\licenses\Qt-LGPL-3.0.txt"
Copy-Item "$root\ext\netcdf-c-4.9.3\COPYRIGHT" "$stage\licenses\netCDF-C-COPYRIGHT.txt"
Copy-Item "$root\ext\hdf5-1.8.20\COPYING" "$stage\licenses\HDF5-COPYING.txt"
Copy-Item "$root\ext\zlib-1.3.1\LICENSE" "$stage\licenses\zlib-LICENSE.txt"
Copy-Item "$root\licenses\colormaps" "$stage\licenses\colormaps" -Recurse
Copy-Item "$root\licenses\eccodes" "$stage\licenses\eccodes" -Recurse
New-Item -ItemType Directory -Force "$stage\licenses\hdf-filters" | Out-Null
Copy-Item "$root\third_party\hdf_filters\bitshuffle\LICENSE" "$stage\licenses\hdf-filters\Bitshuffle-MIT.txt"
Copy-Item "$root\third_party\hdf_filters\lz4\LICENSE" "$stage\licenses\hdf-filters\LZ4-BSD-2-Clause.txt"
Copy-Item "$root\third_party\hdf_filters\zstd\LICENSE" "$stage\licenses\hdf-filters\Zstandard-BSD-3-Clause.txt"
# The binary already embeds these assets; ship provenance beside it for inspection.
New-Item -ItemType Directory -Force "$stage\resources" | Out-Null
Copy-Item "$root\resources\colormaps" "$stage\resources\colormaps" -Recurse
# Upstream Python palette copies are development provenance, not runtime data.
$cmapCopies = "$stage\resources\colormaps\source\cmap"
if(Test-Path $cmapCopies) { Remove-Item -LiteralPath $cmapCopies -Recurse -Force }

$zip = "$outDir\$name.zip"
Compress-Archive -Path $stage -DestinationPath $zip

$src = "$outDir\HDFStudio-$version-src.zip"
& $Git -C $root archive --format=zip --prefix="HDFStudio-$version/" -o $src $SourceRef
if($LASTEXITCODE -ne 0) { throw 'git archive failed' }

foreach($z in $zip, $src) {
  $item = Get-Item $z
  '{0}  {1:N1} MB  SHA256 {2}' -f $item.Name, ($item.Length / 1MB), (Get-FileHash $z -Algorithm SHA256).Hash
}
