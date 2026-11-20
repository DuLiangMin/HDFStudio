# Real-mouse desktop acceptance of the Image View Mask Range (S4-UI-MaskDrop).
# Opens a NetCDF file in Release HDFStudio, right-click <variable> -> Image View, types the Mask Range
# start (decoded value) and leaves the end empty (+inf), then compares the legend with Python netCDF4
# (auto mask-and-scale decoding): masked count and min/max of the remaining values.
# Screenshots go to HDFStudio/logs/S4-UI-MaskDrop/.
# Usage:
#   $env:HDFSTUDIO_MASK_SAMPLE='<file.nc>'; $env:HDFSTUDIO_MASK_VARIABLE='precipitation'; $env:HDFSTUDIO_MASK_START='9999'
#   powershell -NoProfile -ExecutionPolicy Bypass -File HDFStudio/tests/desktop_mask_range.ps1
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Windows.Forms, System.Drawing
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
public class Mouse11 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  public static void Front(IntPtr h) {
    ShowWindow(h, 3); keybd_event(0x12, 0, 0, IntPtr.Zero); keybd_event(0x12, 0, 2, IntPtr.Zero); SetForegroundWindow(h);
  }
  public static void Click(int x, int y, bool right) {
    SetCursorPos(x, y); System.Threading.Thread.Sleep(80);
    uint down = right ? 8u : 2u, up = right ? 16u : 4u;
    mouse_event(down, 0, 0, 0, IntPtr.Zero); mouse_event(up, 0, 0, 0, IntPtr.Zero);
  }
}
"@
$UIA = [System.Windows.Automation.AutomationElement]
$TreeScope = [System.Windows.Automation.TreeScope]
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'build\output\Release\HDFStudio.exe'
$file = $env:HDFSTUDIO_MASK_SAMPLE
$variable = if ($env:HDFSTUDIO_MASK_VARIABLE) { $env:HDFSTUDIO_MASK_VARIABLE } else { 'precipitation' }
$start = if ($env:HDFSTUDIO_MASK_START) { $env:HDFSTUDIO_MASK_START } else { '9999' }
if (-not $file -or -not (Test-Path -LiteralPath $file)) { 'Set HDFSTUDIO_MASK_SAMPLE to a NetCDF file'; exit 2 }
$log = Join-Path $root 'logs\S4-UI-MaskDrop'
New-Item -ItemType Directory -Force $log | Out-Null
$python = if ($env:HDFSTUDIO_PYTHON) { $env:HDFSTUDIO_PYTHON } else { 'python' }

# expected values: netCDF4 decodes with scale_factor/add_offset (auto mask-and-scale), first plane
$probe = Join-Path $log 'expected.py'
@'
import json, sys, numpy as np, netCDF4 as nc
with nc.Dataset(sys.argv[1]) as d:
    v = d[sys.argv[2]]
    plane = np.ma.filled(np.ma.asarray(v[(0,) * (v.ndim - 2)]).astype(float), np.nan)
    start = float(sys.argv[3])
    finite = np.isfinite(plane)
    masked = finite & (plane >= start)
    rest = plane[finite & ~masked]
    print(json.dumps({"masked": int(masked.sum()), "min": "%.9g" % rest.min(), "max": "%.9g" % rest.max()}))
'@ | Set-Content $probe -Encoding ASCII
$expected = (& $python $probe $file $variable $start) | ConvertFrom-Json

function Shot($name) {
  $v = [System.Windows.Forms.SystemInformation]::VirtualScreen
  $bmp = New-Object System.Drawing.Bitmap $v.Width, $v.Height
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($v.Left, $v.Top, 0, 0, $bmp.Size); $bmp.Save("$log\$name.png"); $g.Dispose(); $bmp.Dispose()
}
function ByName($scope, $name) {
  ,@($scope.FindAll($TreeScope::Descendants, (New-Object System.Windows.Automation.PropertyCondition($UIA::NameProperty, $name))))
}
function ClickElement($element, $right) {
  if (-not $element) { Shot 'desktop-error'; throw 'UI element not found; see desktop-error.png' }
  $r = $element.Current.BoundingRectangle
  [Mouse11]::Click([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2), $right)
}
function LegendText() {
  @($win.FindAll($TreeScope::Descendants, [System.Windows.Automation.Condition]::TrueCondition) |
    ForEach-Object { $_.Current.Name } | Where-Object { $_ -like '*Missing: grey*' }) | Select-Object -First 1
}
$results = @()
function Check($what, $ok) { $script:results += [pscustomobject]@{ check = $what; pass = [bool]$ok }; "{0,-70} {1}" -f $what, $(if ($ok) { 'PASS' } else { 'FAIL' }) }

$p = Start-Process $exe -ArgumentList "`"$file`"" -PassThru
Start-Sleep -Seconds 8
$c = New-Object System.Windows.Automation.AndCondition(
  (New-Object System.Windows.Automation.PropertyCondition($UIA::ProcessIdProperty, $p.Id)),
  (New-Object System.Windows.Automation.PropertyCondition($UIA::NameProperty, 'HDFStudio')))
$win = $UIA::RootElement.FindFirst($TreeScope::Children, $c)
Check 'main window found' $win
[Mouse11]::Front([IntPtr]$win.Current.NativeWindowHandle); Start-Sleep -Seconds 2

ClickElement (ByName $win $variable)[0] $true; Start-Sleep -Milliseconds 800
ClickElement (ByName $UIA::RootElement 'Image View')[0] $false; Start-Sleep -Seconds 4
$before = LegendText
Check "legend before mask shows the sentinel as Max (decoded): '$before'" ($before -like '*Max 9999*')
Shot 'desktop-1-before-mask'

# the Mask Range start field (accessible name "Mask start")
$startEdit = (ByName $win 'Mask start')[0]
Check "Mask start field found (accessible name 'Mask start')" $startEdit
ClickElement $startEdit $false; Start-Sleep -Milliseconds 300
[System.Windows.Forms.SendKeys]::SendWait($start)
[System.Windows.Forms.SendKeys]::SendWait('{ENTER}')
Start-Sleep -Seconds 3
$after = LegendText
"legend after: $after"
# plain substring tests: -like would read "[...]" as a wildcard character set
Check "masked count = python $($expected.masked)" ($after -and $after.Contains("Masked [$start, +inf] (decoded): $($expected.masked), white"))
Check "remaining Min $($expected.min) Max $($expected.max) = python" ($after -and $after.Contains("Min $($expected.min)   Max $($expected.max)"))
Shot 'desktop-2-after-mask'
Stop-Process -Id $p.Id -Force
$results | ConvertTo-Json | Set-Content (Join-Path $log 'desktop-results.json') -Encoding UTF8
$failed = @($results | Where-Object { -not $_.pass }).Count
"desktop checks: $($results.Count - $failed) passed, $failed failed"
exit $failed
