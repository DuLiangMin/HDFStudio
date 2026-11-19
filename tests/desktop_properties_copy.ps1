# Real-mouse desktop acceptance of the copyable Properties dialog and the Window menu (S4-UI-PropertiesCopy).
# Opens a NetCDF file with a variable ttr in Release HDFStudio, then with real clicks:
# right-click ttr -> Properties... -> Copy All, and compares the clipboard with `hdf_cli props <file> /ttr`;
# opens a grid and an image view of ttr -> Window -> Cascade -> numbered entry 1 activates the grid.
# Screenshots go to HDFStudio/logs/S4-UI-PropertiesCopy/.
# Usage: $env:HDFSTUDIO_CLASSIC_SAMPLE='<file.nc>'; powershell -NoProfile -ExecutionPolicy Bypass -File HDFStudio/tests/desktop_properties_copy.ps1
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Windows.Forms, System.Drawing
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
public class Mouse6 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  // a released Alt key lets a background process take the foreground (Windows foreground lock)
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
$cli = Join-Path $root 'build\output\Release\hdf_cli.exe'
$file = $env:HDFSTUDIO_CLASSIC_SAMPLE
if (-not $file -or -not (Test-Path -LiteralPath $file)) { 'Set HDFSTUDIO_CLASSIC_SAMPLE to a NetCDF file with a variable ttr'; exit 2 }
$log = Join-Path $root 'logs\S4-UI-PropertiesCopy'
New-Item -ItemType Directory -Force $log | Out-Null

function Shot($name) {
  $v = [System.Windows.Forms.SystemInformation]::VirtualScreen
  $bmp = New-Object System.Drawing.Bitmap $v.Width, $v.Height
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($v.Left, $v.Top, 0, 0, $bmp.Size); $bmp.Save("$log\$name.png"); $g.Dispose(); $bmp.Dispose()
}
function ByName($scope, $name) {
  ,@($scope.FindAll($TreeScope::Descendants, (New-Object System.Windows.Automation.PropertyCondition($UIA::NameProperty, $name))))
}
function NamesLike($scope, $pattern) {
  ,@($scope.FindAll($TreeScope::Descendants, [System.Windows.Automation.Condition]::TrueCondition) | Where-Object { $_.Current.Name -like $pattern })
}
function ClickElement($element, $right) {
  if (-not $element) { Shot 'desktop-error'; throw 'UI element not found; see desktop-error.png' }
  $r = $element.Current.BoundingRectangle
  [Mouse6]::Click([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2), $right)
}
$results = @()
function Check($what, $ok) { $script:results += [pscustomobject]@{ check = $what; pass = [bool]$ok }; "{0,-62} {1}" -f $what, $(if ($ok) { 'PASS' } else { 'FAIL' }) }

$expected = @(& $cli props $file /ttr | Where-Object { $_ -ne '' })
[System.Windows.Forms.Clipboard]::Clear()
$p = Start-Process $exe -ArgumentList "`"$file`"" -PassThru
Start-Sleep -Seconds 8
$c = New-Object System.Windows.Automation.AndCondition(
  (New-Object System.Windows.Automation.PropertyCondition($UIA::ProcessIdProperty, $p.Id)),
  (New-Object System.Windows.Automation.PropertyCondition($UIA::NameProperty, 'HDFStudio')))
$win = $UIA::RootElement.FindFirst($TreeScope::Children, $c)
Check 'main window found' $win
# other windows (Explorer, terminals) must not cover HDFStudio: maximize it and bring it to the front
[Mouse6]::Front([IntPtr]$win.Current.NativeWindowHandle); Start-Sleep -Seconds 2
$ttr = (ByName $win 'ttr')[0]

# --- Properties: right-click -> Properties... -> Copy All ---
ClickElement $ttr $true; Start-Sleep -Milliseconds 700
ClickElement (ByName $UIA::RootElement 'Properties...')[0] $false; Start-Sleep -Seconds 2
$dialog = (NamesLike $UIA::RootElement 'Properties - ttr')[0]
Check "dialog 'Properties - ttr' opened" $dialog
Shot 'desktop-1-properties'
ClickElement (ByName $dialog 'Copy All')[0] $false; Start-Sleep -Seconds 1
$clip = [System.Windows.Forms.Clipboard]::GetText() -split "`r?`n"
Check "clipboard line 2 'File: <sample path>'" ($clip[1] -eq "File: $file")
Check "clipboard line 3 'Object: /ttr'" ($clip[2] -eq 'Object: /ttr')
$missing = @($expected | Where-Object { $clip -notcontains $_ })
Check "clipboard has all $($expected.Count) hdf_cli props lines (missing $($missing.Count))" ($missing.Count -eq 0 -and $expected.Count -gt 5)
Check "status 'Copied $($clip.Count) lines to the clipboard.'" ((ByName $dialog "Copied $($clip.Count) lines to the clipboard.").Count -eq 1)
Shot 'desktop-2-copied'
ClickElement (ByName $dialog 'Close')[0] $false; Start-Sleep -Seconds 1

# --- Window menu ---
ClickElement $ttr $true; Start-Sleep -Milliseconds 700
ClickElement (ByName $UIA::RootElement 'Grid...')[0] $false; Start-Sleep -Seconds 3
ClickElement $ttr $true; Start-Sleep -Milliseconds 700
ClickElement (ByName $UIA::RootElement 'Image View')[0] $false; Start-Sleep -Seconds 3
ClickElement (ByName $win 'Window')[0] $false; Start-Sleep -Milliseconds 800
$entries = NamesLike $UIA::RootElement '[12] *'
Check 'Window menu lists Cascade, Tile and 2 numbered views' ((ByName $UIA::RootElement 'Cascade').Count -gt 0 -and (ByName $UIA::RootElement 'Tile').Count -gt 0 -and $entries.Count -ge 2)
Shot 'desktop-3-window-menu'
ClickElement (ByName $UIA::RootElement 'Cascade')[0] $false; Start-Sleep -Seconds 1
Shot 'desktop-4-cascade'
ClickElement (ByName $win 'Window')[0] $false; Start-Sleep -Milliseconds 800
$first = (NamesLike $UIA::RootElement '1 *')[0]
$firstTitle = $first.Current.Name.Substring(2)
ClickElement $first $false; Start-Sleep -Seconds 1
ClickElement (ByName $win 'Window')[0] $false; Start-Sleep -Milliseconds 800
$entry = (NamesLike $UIA::RootElement '1 *')[0]
$checked = $false
try { $checked = $entry.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern).Current.ToggleState -eq 'On' } catch { }
Check "entry 1 '$firstTitle' is checked after choosing it" $checked
[System.Windows.Forms.SendKeys]::SendWait('{ESC}')
Shot 'desktop-5-entry1'
Stop-Process -Id $p.Id -Force
$results | ConvertTo-Json | Set-Content (Join-Path $log 'desktop-results.json') -Encoding UTF8
$failed = @($results | Where-Object { -not $_.pass }).Count
"desktop checks: $($results.Count - $failed) passed, $failed failed"
exit $failed
