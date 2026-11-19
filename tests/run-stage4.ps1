# Build and test both Windows configurations; preserve Qt diagnostics on failure.
# Example: powershell -ExecutionPolicy Bypass -File tests/run-stage4.ps1 S4-R08
param([Parameter(Mandatory=$true)][string]$Feature)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build/vs2022'
$report = New-Item -ItemType Directory -Force (Join-Path $root "logs/$Feature")
$reportPath = $report.FullName
$cmake = (Select-String -Path (Join-Path $build 'CMakeCache.txt') -Pattern '^CMAKE_COMMAND:INTERNAL=(.*)$').Matches[0].Groups[1].Value
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_DEBUG_PLUGINS = '1'
$env:HDFSTUDIO_TEST_LOG_DIR = $report.FullName
foreach ($config in 'Debug','Release') {
    Write-Output "$Feature $config"
    & $cmake --build $build --config $config --target hdf_explorer_drop_tests hdf_explorer -- /verbosity:quiet
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    $runtime = Join-Path $root "build/output/$config"
    $env:HDFSTUDIO_TEST_CONFIG = $config
$env:HDFSTUDIO_TEST_LOG_DIR = $reportPath
    & "$runtime/hdf_explorer_drop_tests.exe" -o "$reportPath/test-$config.txt,txt" *> "$reportPath/process-$config.txt"
    $code = $LASTEXITCODE
    Get-Content "$reportPath/test-$config.txt" -Tail 5
    if ($code) {
        Get-Process | Where-Object ProcessName -match 'HDFStudio|hdf_explorer_drop_tests' |
            Select-Object ProcessName,Id,Path,MainWindowTitle,Responding | Out-File "$reportPath/processes-$config.txt"
        Get-ChildItem $runtime -Recurse -File | Select-Object FullName,Length |
            Out-File "$reportPath/deployment-$config.txt"
        try {
            Add-Type -AssemblyName System.Windows.Forms,System.Drawing
            $bounds = [Windows.Forms.SystemInformation]::VirtualScreen
            $bitmap = [Drawing.Bitmap]::new($bounds.Width,$bounds.Height)
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.CopyFromScreen($bounds.Location,[Drawing.Point]::Empty,$bounds.Size)
                $bitmap.Save("$reportPath/desktop-$config.png",[Drawing.Imaging.ImageFormat]::Png)
            } finally { $graphics.Dispose(); $bitmap.Dispose() }
        } catch { $_ | Out-File "$reportPath/screenshot-error-$config.txt" }
        exit $code
    }
    & (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir $build -C $config --output-on-failure
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
