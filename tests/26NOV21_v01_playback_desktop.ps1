# Real mouse playback acceptance: single source, linked target Next/Play, pause and residual.
# Input: read-only HDFSTUDIO_FORECAST_ANCHOR/TARGET; variable defaults to TMP_2maboveground.
# Output: logs/S4-Playback/desktop-<configuration>-<timestamp>; screenshots, results, controls.
# Run: powershell -NoProfile -ExecutionPolicy Bypass -File tests/26NOV21_v01_playback_desktop.ps1
# Optional -Executable <portable exe> -RunLabel Portable checks the packaged program.
param([ValidateSet('Debug','Release')][string]$Configuration='Release',[string]$Executable='',[string]$RunLabel='')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Windows.Forms,System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;using System.Drawing;using System.Runtime.InteropServices;
public class PlaybackMouse {
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint x,uint y,uint d,IntPtr e);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int c);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h,int x,int y,int w,int t,bool repaint);
 [DllImport("user32.dll")] public static extern void keybd_event(byte v,byte s,uint f,IntPtr e);
 public static void Front(IntPtr h){ShowWindow(h,3);keybd_event(0x12,0,0,IntPtr.Zero);keybd_event(0x12,0,2,IntPtr.Zero);SetForegroundWindow(h);}
 public static void Click(int x,int y,bool right){SetCursorPos(x,y);mouse_event(right?8u:2u,0,0,0,IntPtr.Zero);mouse_event(right?16u:4u,0,0,0,IntPtr.Zero);}
 public static Bitmap Capture(Rectangle r){var b=new Bitmap(r.Width,r.Height);using(var g=Graphics.FromImage(b))g.CopyFromScreen(r.X,r.Y,0,0,b.Size);return b;}
}
"@
$root=Split-Path $PSScriptRoot -Parent
$version=([regex]'project\(HDFStudio VERSION ([0-9.]+)').Match([IO.File]::ReadAllText((Join-Path $root 'CMakeLists.txt'))).Groups[1].Value
if(-not $Executable){$Executable=Join-Path $root "build/output/$Configuration/HDFStudio.exe"}
if(-not $RunLabel){$RunLabel=$Configuration}
$sources=@($env:HDFSTUDIO_FORECAST_ANCHOR,$env:HDFSTUDIO_FORECAST_TARGET)
foreach($source in $sources){if(-not $source -or -not(Test-Path -LiteralPath $source)){throw 'Set HDFSTUDIO_FORECAST_ANCHOR/TARGET'}}
$hashes=@($sources|ForEach-Object {(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash})
$variable=$env:HDFSTUDIO_FORECAST_VARIABLE;if(-not $variable){$variable='TMP_2maboveground'}
$log=Join-Path $root ('logs/S4-Playback/desktop-'+$RunLabel+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $log|Out-Null
$uia=[System.Windows.Automation.AutomationElement];$checks=@();$process=$null;$window=$null
function FindName($container,$name){,@($container.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.PropertyCondition]::new($uia::NameProperty,$name)))}
function AllControls($container){$container.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition)}
function Check($name,$value){$script:checks+=[pscustomobject]@{check=$name;pass=[bool]$value};"$(Get-Date -Format s) $name : $value";if(-not $value){throw $name}}
function Click($element,$right=$false){if(-not $element){throw 'UI element missing'};$r=$element.Current.BoundingRectangle;if($r.IsEmpty -or $element.Current.IsOffscreen){throw ('Hidden UI: '+$element.Current.Name)};[PlaybackMouse]::Click([int]($r.X+$r.Width/2),[int]($r.Y+$r.Height/2),$right);Start-Sleep -Milliseconds 180}
function SetText($element,$text){Click $element;[Windows.Forms.SendKeys]::SendWait('^a{BACKSPACE}');[Windows.Forms.SendKeys]::SendWait($text);[Windows.Forms.SendKeys]::SendWait('{TAB}');Start-Sleep -Milliseconds 250}
function Shot($name){$b=[PlaybackMouse]::Capture([Windows.Forms.SystemInformation]::VirtualScreen);try{$b.Save((Join-Path $log ($name+'.png')))}finally{$b.Dispose()}}
function MainLayer { $label=AllControls $window|Where-Object {$_.Current.AutomationId.EndsWith('.layerLabel')}|Select-Object -First 1;if(-not $label){throw 'Layer label missing'};if($label.Current.Name -match '^Layer (\d+) of (\d+)$'){return [int]$Matches[1]-1};throw $label.Current.Name }
function Metrics { $control=(FindName $window 'Comparison metrics')[0];try{return $control.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value}catch{return $control.GetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern).DocumentRange.GetText(-1)} }
function PairIndices { $text=Metrics;if($text -notmatch 'Anchor\[(\d+)\] <-> Target\[(\d+)\]'){throw $text};return @([int]$Matches[1],[int]$Matches[2]) }
function OpenImage($index){$nodes=FindName $window $variable;Click $nodes[$index] $true;Click (FindName $uia::RootElement 'Image View')[0];Start-Sleep -Milliseconds 600}
function ClickLayerAction($name){$items=FindName $window $name;$item=$items|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Button}|Select-Object -First 1;Click $item}
function GreenPixels($element){$r=$element.Current.BoundingRectangle;$b=[PlaybackMouse]::Capture([Drawing.Rectangle]::new([int]$r.X,[int]$r.Y,[int]$r.Width,[int]$r.Height));$count=0;try{for($y=0;$y -lt $b.Height;$y++){for($x=0;$x -lt $b.Width;$x++){$c=$b.GetPixel($x,$y);if($c.G -gt 100 -and $c.G -gt $c.R*1.8 -and $c.G -gt $c.B*1.5){$count++}}}}finally{$b.Dispose()};return $count}
try {
 $env:QT_DEBUG_PLUGINS='1';$env:QT_LOGGING_TO_CONSOLE='1'
 $process=Start-Process $Executable -ArgumentList ('"'+$sources[0]+'" "'+$sources[1]+'"') -RedirectStandardError (Join-Path $log 'qt-plugin.log') -PassThru
 $processHandle=$process.Handle;Start-Sleep -Seconds 6
 $condition=[System.Windows.Automation.AndCondition]::new([System.Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id),[System.Windows.Automation.PropertyCondition]::new($uia::NameProperty,'HDFStudio'))
 $window=$uia::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children,$condition)
 Check 'Main window has both forecast variables' ($window -and (FindName $window $variable).Count -eq 2)
 [PlaybackMouse]::Front([IntPtr]$window.Current.NativeWindowHandle);Start-Sleep -Milliseconds 400
 Check 'Playback disabled before opening an image' (-not (FindName $window 'Play / pause layers')[0].Current.IsEnabled)
 OpenImage 0
  Check 'Visible playback button' ((FindName $window 'Play / pause layers').Count -gt 0)
 Check 'Playback bitmap is visibly green' ((GreenPixels (FindName $window 'Play / pause layers')[0]) -gt 10)
 SetText (FindName $window 'Frame interval (ms)')[0] '1000'
 $before=MainLayer;Click (FindName $window 'Play / pause layers')[0];Start-Sleep -Milliseconds 2200
 Click (FindName $window 'Play / pause layers')[0];$after=MainLayer
 Check 'Single dataset advances automatically' ($after -gt $before)
 Start-Sleep -Milliseconds 1200;Check 'Single dataset pause holds layer' ((MainLayer) -eq $after)
 Shot 'single-paused'
 OpenImage 1
 Click (FindName $window 'Window')[0];Click (FindName $uia::RootElement 'Tile')[0];Start-Sleep -Milliseconds 300
 $entry=(FindName $window 'Compare & Metrics')|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Button}|Select-Object -First 1
 Check 'Visible comparison toolbar entry' ($entry -and -not $entry.Current.IsOffscreen)
 Click $entry;Start-Sleep -Milliseconds 500
 SetText (FindName $window 'Lag offset')[0] '-1';SetText (FindName $window 'Anchor layer index')[0] '1'
 $pair=PairIndices;Check 'Common-time initial pair' ($pair[0] -eq 1 -and $pair[1] -eq 0)
 # The second image remains the active MDI view: this reproduces the reported Next Layer defect.
 ClickLayerAction 'Next Layer';Start-Sleep -Milliseconds 500
 $pair=PairIndices;Check 'Next on active Target advances both' ($pair[0] -eq 2 -and $pair[1] -eq 1)
 ClickLayerAction 'Previous Layer';Start-Sleep -Milliseconds 300
 $pair=PairIndices;Check 'Previous on active Target advances both' ($pair[0] -eq 1 -and $pair[1] -eq 0)
 Click (FindName $window 'Residual map')[0];Start-Sleep -Milliseconds 300
 $residualWindow=(FindName $window 'Residual map: Target - Anchor')|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Window}|Select-Object -First 1
 if(-not $residualWindow){$residualWindow=$uia::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children,[System.Windows.Automation.PropertyCondition]::new($uia::NameProperty,'Residual map: Target - Anchor'))}
 if(-not $residualWindow){throw 'Residual window missing'}
 $r=$window.Current.BoundingRectangle;[PlaybackMouse]::MoveWindow([IntPtr]$residualWindow.Current.NativeWindowHandle,[int]($r.X+20),[int]($r.Y+120),650,500,$true)|Out-Null
 $before=PairIndices;Click (FindName $window 'Play / pause layers')[0];Start-Sleep -Milliseconds 2200
 Click (FindName $window 'Play / pause layers')[0];Start-Sleep -Milliseconds 300
 $after=PairIndices;Check 'Linked playback preserves negative lag' ($after[0] -gt $before[0] -and $after[1] -eq $after[0]-1)
 $text=Metrics;Check 'Playback metrics remain valid' ($text.Contains('Status: Ready') -and -not $text.Contains('RMSE: N/A'))
 Check 'Entropy is an independent image, not a comparison metric' (-not $text.Contains('Entropy'))
 $summary=AllControls $residualWindow|Where-Object {$_.Current.AutomationId.EndsWith('.residualSummary')}|Select-Object -First 1
 Check 'Residual tracks current played pair' ($summary -and $summary.Current.Name.Contains("Anchor[$($after[0])] <-> Target[$($after[1])]"))
 Start-Sleep -Milliseconds 1200;$paused=PairIndices;Check 'Linked pause holds both layers' ($paused[0] -eq $after[0] -and $paused[1] -eq $after[1])
 Shot 'joint-paused';$text|Set-Content (Join-Path $log 'final-metrics.txt') -Encoding UTF8
 SetText (FindName $window 'Lag offset')[0] '10000'
 $text=Metrics;Check 'Out-of-range clears live metrics' ($text.Contains('Out of range') -and $text.Contains('RMSE: N/A'))
 SetText (FindName $window 'Lag offset')[0] '-1'
 $text=Metrics;Check 'Returning to valid lag restores metrics' ($text.Contains('Status: Ready') -and -not $text.Contains('RMSE: N/A'))
 Click (FindName $window 'Help')[0];Click (FindName $uia::RootElement 'About')[0]
 $about=AllControls $window|Where-Object {$_.Current.Name.Contains('HDFStudio '+$version)}|Select-Object -First 1
 Check ('About identifies complete version '+$version) ($null -ne $about)
 [Windows.Forms.SendKeys]::SendWait('{ESC}');Start-Sleep -Milliseconds 150
 for($index=0;$index -lt 2;$index++){Check ('Read-only forecast '+$index) ((Get-FileHash -LiteralPath $sources[$index] -Algorithm SHA256).Hash -eq $hashes[$index])}
 $window=$uia::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children,$condition)
 Click (FindName $window 'File')[0];Click (FindName $uia::RootElement 'Exit')[0]
 Check 'Normal exit' ($process.WaitForExit(10000) -and $process.ExitCode -eq 0)
} catch {
 try{Shot 'failure'}catch{}
 if($window){AllControls $window|ForEach-Object {"$($_.Current.AutomationId) | $($_.Current.Name) | $($_.Current.ControlType.ProgrammaticName) | $($_.Current.BoundingRectangle)"}|Out-File (Join-Path $log 'controls.txt') -Encoding UTF8}
 Get-Process|Where-Object ProcessName -match 'HDFStudio|hdf_explorer'|Select-Object Id,ProcessName,Responding,Path|Out-File (Join-Path $log 'processes.txt')
 throw
} finally {
 $checks|ConvertTo-Json|Set-Content (Join-Path $log 'results.json') -Encoding UTF8
 [pscustomobject]@{executable=$Executable;configuration=$Configuration;label=$RunLabel;checks=$checks.Count;all_passed=($checks.Count -gt 0 -and @($checks|Where-Object {-not $_.pass}).Count -eq 0)}|ConvertTo-Json|Set-Content (Join-Path $log 'summary.json') -Encoding UTF8
 if($process -and -not $process.HasExited){$null=$process.CloseMainWindow();if(-not $process.WaitForExit(3000)){Stop-Process -Id $process.Id}}
}
