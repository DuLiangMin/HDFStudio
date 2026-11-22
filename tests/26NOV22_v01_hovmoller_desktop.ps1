# Real desktop section acceptance. Read-only HDFSTUDIO_FORECAST_ANCHOR; outputs logs/S4-Hovmoller.
# Run: powershell -NoProfile -ExecutionPolicy Bypass -File tests/26NOV22_v01_hovmoller_desktop.ps1
param([ValidateSet('Debug','Release')][string]$Configuration='Release',[string]$Executable='',[string]$RunLabel='')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Windows.Forms,System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;using System.Drawing;using System.Runtime.InteropServices;using System.Text;
public class SectionMouse {
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]public static extern void mouse_event(uint f,uint x,uint y,uint d,IntPtr e);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int c);
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern void keybd_event(byte v,byte s,uint f,IntPtr e);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 public delegate bool EnumChildProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")]public static extern bool EnumChildWindows(IntPtr h,EnumChildProc callback,IntPtr p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetClassName(IntPtr h,StringBuilder text,int count);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,StringBuilder text,int count);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr SendMessage(IntPtr h,uint message,IntPtr count,StringBuilder text);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect r);
 public struct Rect{public int Left,Top,Right,Bottom;}
 public static IntPtr FileEdit(IntPtr dialog,int left,int top,int right,int bottom){IntPtr found=IntPtr.Zero;EnumChildWindows(dialog,(h,p)=>{var name=new StringBuilder(128);GetClassName(h,name,128);Rect r;GetWindowRect(h,out r);if(name.ToString()=="Edit"&&r.Left>=left&&r.Top>=top&&r.Right<=right&&r.Bottom<=bottom){found=h;return false;}return true;},IntPtr.Zero);return found;}
 public static string WindowText(IntPtr h){var text=new StringBuilder(32768);SendMessage(h,0x000D,(IntPtr)text.Capacity,text);return text.ToString();}
 public static void Front(IntPtr h){ShowWindow(h,3);keybd_event(0x12,0,0,IntPtr.Zero);keybd_event(0x12,0,2,IntPtr.Zero);SetForegroundWindow(h);}
 public static void Click(int x,int y,bool right){SetCursorPos(x,y);mouse_event(right?8u:2u,0,0,0,IntPtr.Zero);mouse_event(right?16u:4u,0,0,0,IntPtr.Zero);}
 public static Bitmap Capture(Rectangle r){var b=new Bitmap(r.Width,r.Height);using(var g=Graphics.FromImage(b))g.CopyFromScreen(r.X,r.Y,0,0,b.Size);return b;}
}
"@
$root=Split-Path $PSScriptRoot -Parent
if(-not $Executable){$Executable=Join-Path $root "build/output/$Configuration/HDFStudio.exe"}
if(-not $RunLabel){$RunLabel=$Configuration}
$source=$env:HDFSTUDIO_FORECAST_ANCHOR
if(-not $source -or -not(Test-Path -LiteralPath $source)){throw 'Set HDFSTUDIO_FORECAST_ANCHOR'}
$variable=$env:HDFSTUDIO_FORECAST_VARIABLE;if(-not $variable){$variable='TMP_2maboveground'}
$before=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$log=Join-Path $root ('logs/S4-Hovmoller/desktop-'+$RunLabel+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss'));New-Item -ItemType Directory -Force $log|Out-Null
$uia=[Windows.Automation.AutomationElement];$checks=@();$process=$null;$window=$null;$completed=$false
function FindName($container,$name){,@($container.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new($uia::NameProperty,$name)))}
function Controls($container){$container.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)}
function ById($name){Controls $window|Where-Object {$_.Current.AutomationId.EndsWith('.'+$name) -or $_.Current.AutomationId -eq $name}|Select-Object -Last 1}
function Check($name,$passed){$script:checks+=[pscustomobject]@{check=$name;pass=[bool]$passed};"$(Get-Date -Format s) $name : $passed";if(-not $passed){throw $name}}
function Click($element,$right=$false){if(-not $element){throw 'UI element missing'};$r=$element.Current.BoundingRectangle;if($r.IsEmpty -or $element.Current.IsOffscreen){throw ('Hidden UI: '+$element.Current.Name)};$invoke=$null;if(-not $right -and $element.Current.ControlType -in @([Windows.Automation.ControlType]::Button,[Windows.Automation.ControlType]::MenuItem) -and $element.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$invoke)){$invoke.Invoke()}else{[SectionMouse]::Click([int]($r.X+$r.Width/2),[int]($r.Y+$r.Height/2),$right)};Start-Sleep -Milliseconds 200}
function SetText($element,$text){Click $element;[Windows.Forms.SendKeys]::SendWait('^a{BACKSPACE}');if($text){[Windows.Forms.Clipboard]::SetText($text);[Windows.Forms.SendKeys]::SendWait('^v')};[Windows.Forms.SendKeys]::SendWait('{TAB}');Start-Sleep -Milliseconds 250}
function Shot($name){$bitmap=[SectionMouse]::Capture([Windows.Forms.SystemInformation]::VirtualScreen);try{$bitmap.Save((Join-Path $log ($name+'.png')))}finally{$bitmap.Dispose()}}
function Text($element){try{return $element.GetCurrentPattern([Windows.Automation.TextPattern]::Pattern).DocumentRange.GetText(-1)}catch{return $element.Current.Name}}
function Summary {return Text (ById 'hovmollerSummary')}
function Ready {
 $clock=[Diagnostics.Stopwatch]::StartNew()
 while($clock.ElapsedMilliseconds -lt 15000){$status=ById 'hovmollerStatus';$text=if($status){Text $status}else{''};if($text.Trim() -eq 'Ready'){return};if($text.StartsWith('Error')){throw $text};Start-Sleep -Milliseconds 100}
  throw ('Section did not become Ready; last status: '+$text+'; canvas count: '+(FindName $window 'Space-Time Section image').Count)
}
function SaveExport($name,$filename){
  Click (ById $name)
  $dialog=$null;$clock=[Diagnostics.Stopwatch]::StartNew()
  while(-not $dialog -and $clock.ElapsedMilliseconds -lt 5000){
   $all=@($uia::RootElement.FindAll([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id)))
   $dialog=$all|Where-Object {$_.Current.Name -like 'Export section*'}|Select-Object -First 1
   if(-not $dialog){$dialog=Controls $window|Where-Object {$_.Current.Name -like 'Export section*' -and $_.Current.ControlType -eq [Windows.Automation.ControlType]::Window}|Select-Object -First 1}
   if(-not $dialog){Start-Sleep -Milliseconds 100}
  }
 if(-not $dialog){throw 'Native section save dialog missing'}
 $hostControl=Controls $dialog|Where-Object {$_.Current.AutomationId -eq 'FileNameControlHost'}|Select-Object -First 1
 if(-not $hostControl){throw 'Native filename host missing'}
 $bounds=$hostControl.Current.BoundingRectangle;$editHandle=[SectionMouse]::FileEdit([IntPtr]$dialog.Current.NativeWindowHandle,[int]$bounds.Left,[int]$bounds.Top,[int]$bounds.Right,[int]$bounds.Bottom)
 if($editHandle -eq [IntPtr]::Zero){throw 'Native filename Edit HWND missing'}
 $actual='';for($attempt=0;$attempt -lt 3 -and $actual -ne $filename;$attempt++){Click $hostControl;[Windows.Forms.Clipboard]::SetText($filename);[Windows.Forms.SendKeys]::SendWait('^a{BACKSPACE}');Start-Sleep -Milliseconds 100;[Windows.Forms.SendKeys]::SendWait('^v');Start-Sleep -Milliseconds 350;$actual=[SectionMouse]::WindowText($editHandle)}
 if($actual -ne $filename){throw ('Native output path mismatch: '+$actual)}
 $save=[SectionMouse]::GetDlgItem([IntPtr]$dialog.Current.NativeWindowHandle,1);if($save -eq [IntPtr]::Zero){throw 'Native IDOK missing'};$null=[SectionMouse]::PostMessage($save,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
 $clock=[Diagnostics.Stopwatch]::StartNew();while(-not(Test-Path -LiteralPath $filename) -and $clock.ElapsedMilliseconds -lt 15000){Start-Sleep -Milliseconds 100};Check ('Export '+[IO.Path]::GetFileName($filename)) (Test-Path -LiteralPath $filename)
}
try{
 $env:QT_LOGGING_TO_CONSOLE='1';$env:HDFSTUDIO_LOG_DIR=Join-Path $root 'logs'
 $process=Start-Process $Executable -ArgumentList ('"'+$source+'"') -RedirectStandardError (Join-Path $log 'qt.log') -PassThru;$handle=$process.Handle;Start-Sleep -Seconds 5
 $condition=[Windows.Automation.AndCondition]::new([Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id),[Windows.Automation.PropertyCondition]::new($uia::NameProperty,'HDFStudio'))
 $window=$uia::RootElement.FindFirst([Windows.Automation.TreeScope]::Children,$condition);Check 'Forecast loaded' ($window -and (FindName $window $variable).Count -eq 1)
 [SectionMouse]::Front([IntPtr]$window.Current.NativeWindowHandle);Start-Sleep -Milliseconds 300
 Click (FindName $window $variable)[0] $true
  $menu=Controls $uia::RootElement|Where-Object {$_.Current.ProcessId -eq $process.Id -and $_.Current.ControlType -eq [Windows.Automation.ControlType]::MenuItem -and $_.Current.Name.StartsWith('Space-Time Section (Hovm') -and -not $_.Current.IsOffscreen}|Select-Object -Last 1
  Check 'Independent section context entry' ($null -ne $menu -and $menu.Current.IsEnabled)
  $invoke=$null
  if($menu.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$invoke)){$invoke.Invoke()}else{Click $menu}
  Ready
  [Windows.Forms.SendKeys]::SendWait('{ESC}')
 Check 'Independent canvas' ((FindName $window 'Space-Time Section image').Count -eq 1)
 Check 'Default Lat-Time axes and dimensions' ((Summary).Contains('shape=(190,480)') -and (Summary).Contains('index0=0'))
 Check 'Both geographic presets enabled' ((ById 'hovmollerLatTime').Current.IsEnabled -and (ById 'hovmollerLonTime').Current.IsEnabled)
 if($env:HDFSTUDIO_PYTHON -eq 'nonexistent-hovmoller-python.exe'){$clock=[Diagnostics.Stopwatch]::StartNew();while((Text (ById 'hovmollerDateStatus')) -notlike '*unavailable*' -and $clock.ElapsedMilliseconds -lt 7000){Start-Sleep -Milliseconds 100};Check 'No Python numeric fallback remains Ready' ((Text (ById 'hovmollerDateStatus')) -like '*unavailable*' -and (Text (ById 'hovmollerStatus')) -eq 'Ready')}
 Shot 'lat-time-desktop'
 SaveExport 'hovmollerExportPng' (Join-Path $log 'lat-time.png');SaveExport 'hovmollerExportCsv' (Join-Path $log 'lat-time.csv')
 Click (ById 'hovmollerLonTime');Ready;Check 'Lon-Time dimensions' ((Summary).Contains('shape=(480,384)'))
 SaveExport 'hovmollerExportPng' (Join-Path $log 'lon-time.png');SaveExport 'hovmollerExportCsv' (Join-Path $log 'lon-time.csv')
 Click (ById 'hovmollerSwapAxes');Ready;Check 'Swap dimensions' ((Summary).Contains('shape=(384,480)'))
 Click (ById 'hovmollerFlipH');Click (ById 'hovmollerFlipV');SaveExport 'hovmollerExportPng' (Join-Path $log 'swapped-flips.png');SaveExport 'hovmollerExportCsv' (Join-Path $log 'swapped-flips.csv')
 SetText (ById 'hovmollerMaskStart') '220';SetText (ById 'hovmollerMaskEnd') '230';Click (ById 'hovmollerApply');Ready;Check 'Mask count nonzero' ((Summary) -match 'masked=[1-9][0-9]*')
 SaveExport 'hovmollerExportPng' (Join-Path $log 'masked.png');SaveExport 'hovmollerExportCsv' (Join-Path $log 'masked.csv')
 SetText (ById 'hovmollerMaskStart') 'invalid';Click (ById 'hovmollerApply');Check 'Invalid Mask disables export' (-not (ById 'hovmollerExportPng').Current.IsEnabled)
 SetText (ById 'hovmollerMaskStart') '';SetText (ById 'hovmollerMaskEnd') '';Click (ById 'hovmollerApply');Ready;Check 'Mask recovery' ((ById 'hovmollerExportPng').Current.IsEnabled)
 $index=Controls $window|Where-Object {$_.Current.AutomationId -like '*.hovmollerFixedIndex_*'}|Select-Object -First 1
 SetText $index '1';Ready;Check 'Manual fixed index updates' ((Summary).Contains('index0=1'))
 SetText (FindName $window 'Frame interval (ms)')[0] '1000'
 $play=(FindName $window 'Play / pause layers')[0]
 if(-not $play){$play=Controls $window|Where-Object {$_.Current.AutomationId.EndsWith('.actionLayerPlay')}|Select-Object -First 1}
 Click $play;Start-Sleep -Milliseconds 2300;Click $play;Ready;$paused=Summary;Start-Sleep -Milliseconds 1200;Check 'Fixed axis playback and pause' ((Summary) -eq $paused -and $paused -match 'index0=([2-9]|[1-9][0-9]+)')
 Shot 'played-section'
 Check 'Source SHA256 unchanged' ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -eq $before)
 $null=[SectionMouse]::PostMessage([IntPtr]$window.Current.NativeWindowHandle,0x0010,[IntPtr]::Zero,[IntPtr]::Zero);Check 'Normal shutdown' ($process.WaitForExit(10000) -and $process.ExitCode -eq 0)
 $completed=$true
}catch{
 try{Shot 'failure'}catch{}
 if($window){Controls $window|ForEach-Object {"$($_.Current.AutomationId) | $($_.Current.Name) | $($_.Current.ControlType.ProgrammaticName) | $($_.Current.BoundingRectangle)"}|Out-File (Join-Path $log 'controls.txt') -Encoding UTF8}
 Get-Process|Where-Object ProcessName -match 'HDFStudio|hdf_cli'|Select-Object Id,ProcessName,Responding,Path|Out-File (Join-Path $log 'processes.txt');throw
}finally{
 $checks|ConvertTo-Json|Set-Content (Join-Path $log 'results.json') -Encoding UTF8
 [pscustomobject]@{executable=$Executable;source=$source;variable=$variable;checks=$checks.Count;all_passed=($completed -and $checks.Count -gt 0 -and @($checks|Where-Object {-not $_.pass}).Count -eq 0)}|ConvertTo-Json|Set-Content (Join-Path $log 'summary.json') -Encoding UTF8
 if($process -and -not $process.HasExited){$null=$process.CloseMainWindow();if(-not $process.WaitForExit(3000)){Stop-Process -Id $process.Id}}
}
