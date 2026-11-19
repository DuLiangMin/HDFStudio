# Real-mouse desktop acceptance of the main-window Layer toolbar (S4-UI-LayerToolbar).
# Opens a NetCDF file with a 4-D variable ttr(time, lev, lat, lon) in Release HDFStudio, then with
# real clicks: Grid -> Next Layer -> axis "lev" -> Last Layer, and Image View -> Next Layer -> Last
# Layer; grid cells and image min/max are compared with values computed by Python netCDF4.
# Screenshots go to HDFStudio/logs/S4-UI-LayerToolbar/.
# Usage: $env:HDFSTUDIO_CLASSIC_SAMPLE='<file.nc>'; powershell -NoProfile -ExecutionPolicy Bypass -File HDFStudio/tests/desktop_layer_toolbar.ps1
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Windows.Forms, System.Drawing
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
public class Mouse5 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  public static void Click(int x, int y, bool right) {
    SetCursorPos(x, y); System.Threading.Thread.Sleep(80);
    uint down = right ? 8u : 2u, up = right ? 16u : 4u;
    mouse_event(down, 0, 0, 0, IntPtr.Zero); mouse_event(up, 0, 0, 0, IntPtr.Zero);
  }
}
"@
$UIA = [System.Windows.Automation.AutomationElement]
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'build\output\Release\HDFStudio.exe'
$file = $env:HDFSTUDIO_CLASSIC_SAMPLE
if (-not $file -or -not (Test-Path -LiteralPath $file)) { 'Set HDFSTUDIO_CLASSIC_SAMPLE to a NetCDF file with a 4-D variable ttr'; exit 2 }
$log = Join-Path $root 'logs\S4-UI-LayerToolbar'
New-Item -ItemType Directory -Force $log | Out-Null
$python = if ($env:HDFSTUDIO_PYTHON) { $env:HDFSTUDIO_PYTHON } else { 'python' }

# expected values from netCDF4: grid cells [0:5,0:2] and image min/max of three planes
$probe = Join-Path $log 'expected.py'
@'
import json, sys, netCDF4 as nc, numpy as np
with nc.Dataset(sys.argv[1]) as d:
    v = d["ttr"]
    out = {}
    for key, (t, l) in {"t0l0": (0, 0), "t1l0": (1, 0), "t1lLast": (1, v.shape[1] - 1)}.items():
        plane = np.asarray(v[t, l, :, :])
        out[key] = {"cells": ["%g" % x for x in plane[0:5, 0:2].ravel()],
                    "min": "%.9g" % plane.min(), "max": "%.9g" % plane.max()}
    out["times"] = v.shape[0]; out["levs"] = v.shape[1]
    print(json.dumps(out))
'@ | Set-Content $probe -Encoding ASCII
$expected = (& $python $probe $file) | ConvertFrom-Json
# cells that occur in one plane only, so the check proves which plane is shown
function UniqueCells($key) {
  $others = @()
  foreach ($k in 't0l0','t1l0','t1lLast') { if ($k -ne $key) { $others += $expected.$k.cells } }
  @($expected.$key.cells | Where-Object { $others -notcontains $_ } | Select-Object -First 3)
}

function Shot($name) {
  $v = [System.Windows.Forms.SystemInformation]::VirtualScreen
  $bmp = New-Object System.Drawing.Bitmap $v.Width, $v.Height
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($v.Left, $v.Top, 0, 0, $bmp.Size); $bmp.Save("$log\$name.png"); $g.Dispose(); $bmp.Dispose()
}
function ByName($scope, $name) {
  ,@($scope.FindAll([System.Windows.Automation.TreeScope]::Descendants, (New-Object System.Windows.Automation.PropertyCondition($UIA::NameProperty, $name))))
}
function ClickElement($element, $right) {
  $r = $element.Current.BoundingRectangle
  [Mouse5]::Click([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2), $right)
}
function Shows($cells) { foreach ($c in $cells) { if ((ByName $win $c).Count -eq 0) { return $false } }; return $true }
function LayerLabel() { @($win.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition) | ForEach-Object { $_.Current.Name } | Where-Object { $_ -match '^(Layer \d+ of \d+|No layers)$' }) | Select-Object -First 1 }
# the main toolbar's combos are on the same row as the Open button
function MainCombos() {
  $open = (ByName $win 'Open')[0].Current.BoundingRectangle
  $all = @($win.FindAll([System.Windows.Automation.TreeScope]::Descendants, (New-Object System.Windows.Automation.PropertyCondition($UIA::ControlTypeProperty, [System.Windows.Automation.ControlType]::ComboBox))))
  @($all | Where-Object { [Math]::Abs(($_.Current.BoundingRectangle.Y + $_.Current.BoundingRectangle.Height / 2) - ($open.Y + $open.Height / 2)) -lt 12 } | Sort-Object { $_.Current.BoundingRectangle.X })
}
function PickAxis($text) {
  ClickElement (MainCombos)[0] $false; Start-Sleep -Milliseconds 700
  ClickElement (ByName $UIA::RootElement $text)[0] $false; Start-Sleep -Seconds 1
}
$results = @()
function Check($what, $ok) { $script:results += [pscustomobject]@{ check = $what; pass = [bool]$ok }; "{0,-62} {1}" -f $what, $(if ($ok) { 'PASS' } else { 'FAIL' }) }

$p = Start-Process $exe -ArgumentList "`"$file`"" -PassThru
Start-Sleep -Seconds 8
$c = New-Object System.Windows.Automation.AndCondition(
  (New-Object System.Windows.Automation.PropertyCondition($UIA::ProcessIdProperty, $p.Id)),
  (New-Object System.Windows.Automation.PropertyCondition($UIA::NameProperty, 'HDFStudio')))
$win = $UIA::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children, $c)
Check 'main window found' $win
Check "toolbar shows 'No layers' before any view" ((LayerLabel) -eq 'No layers')
$ttr = (ByName $win 'ttr')[0]

# --- Grid ---
ClickElement $ttr $true; Start-Sleep -Milliseconds 700
ClickElement (ByName $UIA::RootElement 'Grid...')[0] $false; Start-Sleep -Seconds 3
Check "grid opens at time 1 of $($expected.times): python t0 lev0 cells" (Shows (UniqueCells 't0l0'))
Check "label 'Layer 1 of $($expected.times)'" ((LayerLabel) -eq "Layer 1 of $($expected.times)")
Shot 'desktop-1-grid-layer1'
ClickElement (ByName $win 'Next Layer')[0] $false; Start-Sleep -Seconds 2
Check 'main toolbar Next Layer -> python t1 lev0 cells' (Shows (UniqueCells 't1l0'))
Check "label 'Layer 2 of $($expected.times)'" ((LayerLabel) -eq "Layer 2 of $($expected.times)")
Shot 'desktop-2-grid-next'
PickAxis "lev ($($expected.levs))"
Check "axis lev selected: label 'Layer 1 of $($expected.levs)'" ((LayerLabel) -eq "Layer 1 of $($expected.levs)")
ClickElement (ByName $win 'Last Layer')[0] $false; Start-Sleep -Seconds 2
Check 'main toolbar Last Layer -> python t1 lev-last cells' (Shows (UniqueCells 't1lLast'))
Check "label 'Layer $($expected.levs) of $($expected.levs)'" ((LayerLabel) -eq "Layer $($expected.levs) of $($expected.levs)")
Shot 'desktop-3-grid-lev-last'

# --- Image View ---
ClickElement $ttr $true; Start-Sleep -Milliseconds 700
ClickElement (ByName $UIA::RootElement 'Image View')[0] $false; Start-Sleep -Seconds 3
Check "new image view starts at 'Layer 1 of $($expected.times)'" ((LayerLabel) -eq "Layer 1 of $($expected.times)")
ClickElement (ByName $win 'Next Layer')[0] $false; Start-Sleep -Seconds 2
$legend = "Min $($expected.t1l0.min)   Max $($expected.t1l0.max)"
Check "image Next Layer -> legend '$legend'" (@($win.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition) | Where-Object { $_.Current.Name -like "*$legend*" }).Count -gt 0)
PickAxis "lev ($($expected.levs))"
ClickElement (ByName $win 'Last Layer')[0] $false; Start-Sleep -Seconds 2
$legend = "Min $($expected.t1lLast.min)   Max $($expected.t1lLast.max)"
Check "image lev Last Layer -> legend '$legend'" (@($win.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition) | Where-Object { $_.Current.Name -like "*$legend*" }).Count -gt 0)
Shot 'desktop-4-image-lev-last'

# --- Layer menu ---
ClickElement (ByName $win 'Layer')[0] $false; Start-Sleep -Milliseconds 700
Check 'Layer menu lists Next/Previous/Last/First/Go to Layer...' ((ByName $UIA::RootElement 'Go to Layer...').Count -gt 0 -and (ByName $UIA::RootElement 'First Layer').Count -gt 0)
Shot 'desktop-5-layer-menu'
[System.Windows.Forms.SendKeys]::SendWait('{ESC}')
Stop-Process -Id $p.Id -Force
$results | ConvertTo-Json | Set-Content (Join-Path $log 'desktop-results.json') -Encoding UTF8
$failed = @($results | Where-Object { -not $_.pass }).Count
"desktop checks: $($results.Count - $failed) passed, $failed failed"
exit $failed
