# Real-mouse desktop acceptance of a NetCDF-3 file (S4-UI-Classic).
# Opens the sample in Release HDFStudio, checks the tree, Image View, Grid (values and the Next layer
# button against Python netCDF4) and the Properties dialog; screenshots go to HDFStudio/logs/S4-UI-Classic/.
# Usage: $env:HDFSTUDIO_CLASSIC_SAMPLE='<file.nc>'; powershell -NoProfile -ExecutionPolicy Bypass -File HDFStudio/tests/desktop_classic_netcdf.ps1
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Windows.Forms, System.Drawing
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
public class Mouse4 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  public static void Click(int x, int y, bool right) {
    SetCursorPos(x, y); System.Threading.Thread.Sleep(80);
    uint down = right ? 8u : 2u, up = right ? 16u : 4u;
    mouse_event(down, 0, 0, 0, IntPtr.Zero); mouse_event(up, 0, 0, 0, IntPtr.Zero);
  }
  public static void DoubleClick(int x, int y) { Click(x, y, false); System.Threading.Thread.Sleep(60); Click(x, y, false); }
}
"@
$UIA = [System.Windows.Automation.AutomationElement]
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'build\output\Release\HDFStudio.exe'
# the NetCDF-3 sample is not part of the repository; give its path in HDFSTUDIO_CLASSIC_SAMPLE
$file = $env:HDFSTUDIO_CLASSIC_SAMPLE
if (-not $file -or -not (Test-Path -LiteralPath $file)) { 'Set HDFSTUDIO_CLASSIC_SAMPLE to a NetCDF-3 file with a 4-D variable ttr'; exit 2 }
$log = Join-Path $root 'logs\S4-UI-Classic'
New-Item -ItemType Directory -Force $log | Out-Null
function Shot($name) {
  $v = [System.Windows.Forms.SystemInformation]::VirtualScreen
  $bmp = New-Object System.Drawing.Bitmap $v.Width, $v.Height
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($v.Left, $v.Top, 0, 0, $bmp.Size); $bmp.Save("$log\$name.png"); $g.Dispose(); $bmp.Dispose()
}
function ByName($scope, $name) {
  ,@($scope.FindAll([System.Windows.Automation.TreeScope]::Descendants, (New-Object System.Windows.Automation.PropertyCondition($UIA::NameProperty, $name))))
}
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$p = Start-Process $exe -ArgumentList "`"$file`"" -PassThru
Start-Sleep -Seconds 8
$win = $null
for ($i = 0; $i -lt 60 -and -not $win; $i++) {
  Start-Sleep -Milliseconds 500
  $c = New-Object System.Windows.Automation.AndCondition(
    (New-Object System.Windows.Automation.PropertyCondition($UIA::ProcessIdProperty, $p.Id)),
    (New-Object System.Windows.Automation.PropertyCondition($UIA::NameProperty, 'HDFStudio')))
  $win = $UIA::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children, $c)
}
"window found: $([bool]$win)  name=$($win.Current.Name)"
$ttr = $null
for ($i = 0; $i -lt 40 -and -not $ttr; $i++) { Start-Sleep -Milliseconds 500; $f = ByName $win 'ttr'; if ($f.Count -gt 0) { $ttr = $f[0] } }
"tree shows ttr after $($sw.ElapsedMilliseconds) ms: $([bool]$ttr)"
foreach ($n in 'time', 'lev', 'lat', 'lon', 'description', 'creator') { "  tree item '$n': $((ByName $win $n).Count -gt 0)" }
Shot 'desktop-1-tree'

# Image View through the real context menu
$r = $ttr.Current.BoundingRectangle
[Mouse4]::Click([int]($r.X + 20), [int]($r.Y + $r.Height / 2), $true); Start-Sleep -Milliseconds 700
$menu = ByName $UIA::RootElement 'Image View'
"context menu has Image View: $($menu.Count -gt 0)"
$mr = $menu[0].Current.BoundingRectangle
[Mouse4]::Click([int]($mr.X + $mr.Width / 2), [int]($mr.Y + $mr.Height / 2), $false); Start-Sleep -Seconds 3
Shot 'desktop-2-image-view'

# Grid through the real context menu
[Mouse4]::Click([int]($r.X + 20), [int]($r.Y + $r.Height / 2), $true); Start-Sleep -Milliseconds 700
$menu = ByName $UIA::RootElement 'Grid...'
"context menu has Grid...: $($menu.Count -gt 0)"
$mr = $menu[0].Current.BoundingRectangle
$sw.Restart()
[Mouse4]::Click([int]($mr.X + $mr.Width / 2), [int]($mr.Y + $mr.Height / 2), $false); Start-Sleep -Seconds 3
Shot 'desktop-3-grid'
function Cell($text) { (ByName $win $text).Count -gt 0 }
"grid shows python ttr[0,0,0,0:2] -151.138 / -152.608: $((Cell '-151.138') -and (Cell '-152.608'))"

# next time step with the first green arrow of the grid toolbar (real mouse)
$buttons = @(ByName $win 'Next layer')
"grid Next layer buttons: $($buttons.Count)"
$b = ($buttons | Sort-Object { $_.Current.BoundingRectangle.X })[0]
$br = $b.Current.BoundingRectangle
[Mouse4]::Click([int]($br.X + $br.Width / 2), [int]($br.Y + $br.Height / 2), $false); Start-Sleep -Seconds 2
Shot 'desktop-4-grid-next-time'
"after next-time click, grid shows python ttr[1,0,0,0:2] -151.316 / -152.786: $((Cell '-151.316') -and (Cell '-152.786'))"

# Properties from the context menu
[Mouse4]::Click([int]($r.X + 20), [int]($r.Y + $r.Height / 2), $true); Start-Sleep -Milliseconds 700
$menu = ByName $UIA::RootElement 'Properties...'
$mr = $menu[0].Current.BoundingRectangle
[Mouse4]::Click([int]($mr.X + $mr.Width / 2), [int]($mr.Y + $mr.Height / 2), $false); Start-Sleep -Seconds 2
Shot 'desktop-5-properties'
$texts = @($UIA::RootElement.FindAll([System.Windows.Automation.TreeScope]::Descendants, (New-Object System.Windows.Automation.PropertyCondition($UIA::ProcessIdProperty, $p.Id))) | ForEach-Object { $_.Current.Name } | Where-Object { $_ -like '*Dimensions: ttr*' })
"properties dialog shows 'Dimensions: ttr(time, lev, lat, lon)': $($texts.Count -gt 0)"
Stop-Process -Id $p.Id -Force








