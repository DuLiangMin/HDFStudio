# Build Debug/Release, run full QtTest/CTest, then repeat each full suite five times.
# Input: existing Stage 4 build and read-only sample paths used by the existing suite.
# Output: logs/S4-UI-Zoom/{test,cycle,summary,diagnostic} files. Stops on first failure.
# Run: powershell -NoProfile -ExecutionPolicy Bypass -File tests/run-zoom-regression.ps1
param([ValidateRange(1,20)][int]$Cycles=5)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$report=Join-Path $root 'logs/S4-UI-Zoom'
New-Item -ItemType Directory -Force $report | Out-Null
& (Join-Path $PSScriptRoot 'run-stage4.ps1') S4-UI-Zoom
if($LASTEXITCODE){exit $LASTEXITCODE}
$env:QT_QPA_PLATFORM='windows'
$env:QT_DEBUG_PLUGINS='1'
$results=@()
foreach($config in 'Debug','Release') {
 $runtime=Join-Path $root "build/output/$config"
 $env:HDFSTUDIO_TEST_CONFIG=$config
 $env:HDFSTUDIO_TEST_LOG_DIR=$report
 for($iteration=1;$iteration -le $Cycles;$iteration++) {
  $started=Get-Date
  $resultFile=Join-Path $report "cycle-$config-$iteration.txt"
  "$(Get-Date -Format s) Full regression $config $iteration/$Cycles"
  & "$runtime/hdf_explorer_drop_tests.exe" -o "$resultFile,txt" *> (Join-Path $report "cycle-process-$config-$iteration.txt")
  $code=$LASTEXITCODE
  $totals=Select-String -Path $resultFile -Pattern '^Totals:' | Select-Object -Last 1 -ExpandProperty Line
  $results+=[pscustomobject]@{configuration=$config;cycle=$iteration;started=$started.ToString('o');exit_code=$code;totals=$totals;seconds=((Get-Date)-$started).TotalSeconds}
  $results | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $report 'cycle-results.json') -Encoding UTF8
  $totals
  if($code) {
   Get-Process | Where-Object ProcessName -match 'HDFStudio|hdf_explorer_drop_tests' | Select-Object ProcessName,Id,Path,Responding | Out-File (Join-Path $report "cycle-processes-$config-$iteration.txt")
   Get-ChildItem $runtime -Recurse -File | Select-Object FullName,Length | Out-File (Join-Path $report "cycle-deployment-$config-$iteration.txt")
   try {
    Add-Type -AssemblyName System.Windows.Forms,System.Drawing
    $bounds=[Windows.Forms.SystemInformation]::VirtualScreen
    $bitmap=[Drawing.Bitmap]::new($bounds.Width,$bounds.Height)
    $graphics=[Drawing.Graphics]::FromImage($bitmap)
    try{$graphics.CopyFromScreen($bounds.Location,[Drawing.Point]::Empty,$bounds.Size);$bitmap.Save((Join-Path $report "cycle-failure-$config-$iteration.png"))}
    finally{$graphics.Dispose();$bitmap.Dispose()}
   } catch {$_ | Out-File (Join-Path $report "cycle-capture-error-$config-$iteration.txt")}
   exit $code
  }
 }
}
"$(Get-Date -Format s) All $($results.Count) complete regression cycles passed."
