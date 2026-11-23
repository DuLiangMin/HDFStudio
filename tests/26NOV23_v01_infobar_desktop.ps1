# Real desktop check for Window > Hide Infobar and its check mark.
# Input: build/output/<Configuration>/HDFStudio.exe, read-only mini_cube.grb2.
# Output: logs/S4-Infobar/desktop-<Configuration>-<timestamp>/ screenshots/checks/Qt logs.
# Usage: powershell -NoProfile -File tests/26NOV23_v01_infobar_desktop.ps1
param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Drawing,System.Windows.Forms
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class InfobarDesktop {
  [DllImport("user32.dll",SetLastError=true)]public static extern IntPtr OpenInputDesktop(uint flags,bool inherit,uint access);
  [DllImport("user32.dll")]public static extern bool CloseDesktop(IntPtr handle);
  [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr handle,int command);
  [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr handle);
  [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
  [DllImport("user32.dll")]public static extern void mouse_event(uint flags,uint x,uint y,uint data,IntPtr extra);
  [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr handle,uint message,IntPtr wparam,IntPtr lparam);
  [DllImport("user32.dll")]public static extern void keybd_event(byte key,byte scan,uint flags,IntPtr extra);
  public static void Front(IntPtr handle){keybd_event(0x12,0,0,IntPtr.Zero);keybd_event(0x12,0,2,IntPtr.Zero);SetForegroundWindow(handle);}
  public static void Click(int x,int y){SetCursorPos(x,y);mouse_event(2,0,0,0,IntPtr.Zero);System.Threading.Thread.Sleep(50);mouse_event(4,0,0,0,IntPtr.Zero);}
}
'@
$root=Split-Path -Parent $PSScriptRoot
$log=Join-Path $root ('logs/S4-Infobar/desktop-'+$Configuration+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $log|Out-Null
$uia=[Windows.Automation.AutomationElement];$checks=@();$process=$null;$window=$null;$completed=$false
function Check($name,$passed){$script:checks+=[pscustomobject]@{check=$name;pass=[bool]$passed};Write-Output ("$(Get-Date -Format s) $name : $passed");if(-not $passed){throw $name}}
function FindNamed($container,$name){$container.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new($uia::NameProperty,$name))}
function VisibleInfoTabs {
  foreach($name in @('File Info','By NetCDF4','CF Check')){
    $tab=FindNamed $window $name
    if(-not $tab-or $tab.Current.IsOffscreen){return $false}
  }
  return $true
}
function Shot($name){
  $bounds=[Windows.Forms.SystemInformation]::VirtualScreen;$image=[Drawing.Bitmap]::new($bounds.Width,$bounds.Height);$graphics=[Drawing.Graphics]::FromImage($image)
  try{$graphics.CopyFromScreen($bounds.X,$bounds.Y,0,0,$image.Size);$image.Save((Join-Path $log ($name+'.png')))}finally{$graphics.Dispose();$image.Dispose()}
}
function OpenMenu {
  # Reassert the target foreground after automation/console callbacks.
  [InfobarDesktop]::Front([IntPtr]$window.Current.NativeWindowHandle)
  [Windows.Forms.SendKeys]::SendWait('{ESC}')
  $title=FindNamed $window 'Window';if(-not $title){$title=FindNamed $window '&Window'}
  if(-not $title){throw 'Window menu title missing'}
  $bounds=$title.Current.BoundingRectangle
  [InfobarDesktop]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
  $timer=[Diagnostics.Stopwatch]::StartNew()
  do{
    $items=$uia::RootElement.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.AndCondition]::new(
      [Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id),
      [Windows.Automation.PropertyCondition]::new($uia::NameProperty,'Hide Infobar')))
    foreach($item in $items){if(-not $item.Current.IsOffscreen){return $item}}
    Start-Sleep -Milliseconds 100
  }while($timer.ElapsedMilliseconds-lt 5000)
  throw 'Visible Hide Infobar menu item missing'
}
function Checked($entry){
  $pattern=$null
  if($entry.TryGetCurrentPattern([Windows.Automation.TogglePattern]::Pattern,[ref]$pattern)){
    return $pattern.Current.ToggleState-eq [Windows.Automation.ToggleState]::On
  }
  if($entry.TryGetCurrentPattern([Windows.Automation.LegacyIAccessiblePattern]::Pattern,[ref]$pattern)){
    return ($pattern.Current.State-band 0x10)-ne 0
  }
  throw 'Check mark not exposed by accessibility'
}
try{
  $desktop=[InfobarDesktop]::OpenInputDesktop(0,$false,0x0100)
  if($desktop-eq [IntPtr]::Zero){throw ('Interactive desktop unavailable: '+[Runtime.InteropServices.Marshal]::GetLastWin32Error())}
  [InfobarDesktop]::CloseDesktop($desktop)|Out-Null
  $env:QT_LOGGING_TO_CONSOLE='1'
  $source=Join-Path $root 'testdata/grib/mini_cube.grb2';$before=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
  $process=Start-Process -FilePath (Join-Path $root "build/output/$Configuration/HDFStudio.exe") -ArgumentList ('"'+$source+'"') -PassThru -RedirectStandardOutput (Join-Path $log 'stdout.txt') -RedirectStandardError (Join-Path $log 'qt.txt')
  $handle=$process.Handle;$timer=[Diagnostics.Stopwatch]::StartNew()
  do{
    $candidates=$uia::RootElement.FindAll([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id))
    foreach($candidate in $candidates){if(FindNamed $candidate 'Variables [virtual arrays]'){$window=$candidate;break}}
    if($window){break};Start-Sleep -Milliseconds 100
  }while($timer.ElapsedMilliseconds-lt 15000)
  Check 'Main window and sample loaded' ($null-ne $window)
  [InfobarDesktop]::ShowWindow([IntPtr]$window.Current.NativeWindowHandle,3)|Out-Null
  [InfobarDesktop]::SetForegroundWindow([IntPtr]$window.Current.NativeWindowHandle)|Out-Null
  Check 'Information tabs initially visible' (VisibleInfoTabs)
  $entry=OpenMenu;Check 'Hide Infobar initially unchecked' (-not(Checked $entry));Shot 'initial-menu'
  $entry.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
  $timer.Restart();do{if(-not(VisibleInfoTabs)){break};Start-Sleep -Milliseconds 100}while($timer.ElapsedMilliseconds-lt 3000)
  Check 'Click hides all information tabs' (-not(VisibleInfoTabs))
  $entry=OpenMenu;Check 'Reopened menu shows check mark' (Checked $entry);Shot 'hidden-menu'
  $entry.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
  $timer.Restart();do{if(VisibleInfoTabs){break};Start-Sleep -Milliseconds 100}while($timer.ElapsedMilliseconds-lt 3000)
  Check 'Second click restores information tabs' (VisibleInfoTabs)
  $entry=OpenMenu;Check 'Reopened menu removes check mark' (-not(Checked $entry));Shot 'restored-menu'
  [Windows.Forms.SendKeys]::SendWait('{ESC}')
  Check 'Source hash unchanged' ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash-eq $before)
  [InfobarDesktop]::PostMessage([IntPtr]$window.Current.NativeWindowHandle,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  Check 'Normal process exit' ($process.WaitForExit(10000));$process.Refresh();Check 'Exit code zero' ($process.ExitCode-eq 0)
  $completed=$true
}catch{
  try{Shot 'failure';if($window){$window.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)|ForEach-Object {$_.Current.Name}|Set-Content -Encoding UTF8 (Join-Path $log 'controls.txt')}}catch{}
  $_|Out-String|Set-Content -Encoding UTF8 (Join-Path $log 'failure.txt');throw
}finally{
  $checks|ConvertTo-Json -Depth 5|Set-Content -Encoding UTF8 (Join-Path $log 'checks.json')
  if(-not $completed-and $process-and -not $process.HasExited){$process.Kill();$process.WaitForExit()}
}
