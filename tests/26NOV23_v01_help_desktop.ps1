# Verify real Help-menu click, default-browser local URL and About version.
# Input: -Executable (defaults to Release); output: logs/S4-Version-0.14.02/desktop-*.
# Example: powershell -NoProfile -File tests/26NOV23_v01_help_desktop.ps1
param([string]$Executable='', [string]$RunLabel='Release')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Drawing,System.Windows.Forms
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class HelpDesktop {
  [DllImport("user32.dll",SetLastError=true)]public static extern IntPtr OpenInputDesktop(uint flags,bool inherit,uint access);
  [DllImport("user32.dll")]public static extern bool CloseDesktop(IntPtr handle);
  [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr handle);
  [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr handle,int command);
  [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr handle,uint message,IntPtr wparam,IntPtr lparam);
  [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
  [DllImport("user32.dll")]public static extern void mouse_event(uint flags,uint x,uint y,uint data,IntPtr extra);
  [DllImport("user32.dll")]public static extern void keybd_event(byte key,byte scan,uint flags,IntPtr extra);
  public static void Front(IntPtr handle){keybd_event(0x12,0,0,IntPtr.Zero);keybd_event(0x12,0,2,IntPtr.Zero);SetForegroundWindow(handle);}
  public static void Click(int x,int y){SetCursorPos(x,y);mouse_event(2,0,0,0,IntPtr.Zero);System.Threading.Thread.Sleep(40);mouse_event(4,0,0,0,IntPtr.Zero);}
}
'@
$root=Split-Path -Parent $PSScriptRoot
if(-not $Executable){$Executable=Join-Path $root 'build/output/Release/HDFStudio.exe'}
$log=Join-Path $root ('logs/S4-Version-0.14.02/desktop-'+$RunLabel+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $log | Out-Null
$uia=[Windows.Automation.AutomationElement]
$checks=@();$process=$null;$window=$null;$browser=$null;$completed=$false
function Check($name,$pass){$script:checks+=[pscustomobject]@{check=$name;pass=[bool]$pass};Write-Output ("$(Get-Date -Format s) $name : $pass");if(-not $pass){throw $name}}
function Named($container,$name){$container.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new($uia::NameProperty,$name))}
function Shot($name){
 $bounds=[Windows.Forms.SystemInformation]::VirtualScreen
 $image=[Drawing.Bitmap]::new($bounds.Width,$bounds.Height);$graphics=[Drawing.Graphics]::FromImage($image)
 try{$graphics.CopyFromScreen($bounds.X,$bounds.Y,0,0,$image.Size);$image.Save((Join-Path $log ($name+'.png')))}finally{$graphics.Dispose();$image.Dispose()}
}
function OpenHelpMenu {
 [HelpDesktop]::Front([IntPtr]$window.Current.NativeWindowHandle)
 [Windows.Forms.SendKeys]::SendWait('{ESC}')
 $title=Named $window 'Help';if(-not $title){$title=Named $window '&Help'}
 if(-not $title){throw 'Help menu missing'}
 $bounds=$title.Current.BoundingRectangle
 [HelpDesktop]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
 $timer=[Diagnostics.Stopwatch]::StartNew()
 do{
  $items=$uia::RootElement.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.AndCondition]::new([Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id),[Windows.Automation.PropertyCondition]::new($uia::ControlTypeProperty,[Windows.Automation.ControlType]::MenuItem)))
  $help=$null;$about=$null
  foreach($item in $items){if($item.Current.IsOffscreen){continue};$name=$item.Current.Name.Replace('&','');if($name -eq 'Help' -and $item.Current.BoundingRectangle.Height -gt 0){$help=$item};if($name -eq 'About'){$about=$item}}
  if($help -and $about -and $help.Current.BoundingRectangle.Y -gt $title.Current.BoundingRectangle.Y){return @{help=$help;about=$about}}
  Start-Sleep -Milliseconds 100
 }while($timer.ElapsedMilliseconds -lt 5000)
 throw 'Help submenu entries unavailable'
}
try{
 $desktop=[HelpDesktop]::OpenInputDesktop(0,$false,0x0100)
 if($desktop -eq [IntPtr]::Zero){throw ('Interactive desktop unavailable: '+[Runtime.InteropServices.Marshal]::GetLastWin32Error())}
 [HelpDesktop]::CloseDesktop($desktop)|Out-Null
 $env:QT_LOGGING_TO_CONSOLE='1'
 $process=Start-Process -FilePath $Executable -WorkingDirectory $log -PassThru -RedirectStandardOutput (Join-Path $log 'stdout.txt') -RedirectStandardError (Join-Path $log 'qt.txt')
 $handle=$process.Handle;$timer=[Diagnostics.Stopwatch]::StartNew()
 do{
  $candidates=$uia::RootElement.FindAll([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id))
  foreach($candidate in $candidates){if(Named $candidate 'File Info'){$window=$candidate;break}}
  if($window){break};Start-Sleep -Milliseconds 100
 }while($timer.ElapsedMilliseconds -lt 20000)
 Check 'Main window ready' ($null -ne $window)
 [HelpDesktop]::ShowWindow([IntPtr]$window.Current.NativeWindowHandle,3)|Out-Null
 $entries=OpenHelpMenu
 Check 'Help before About' ($entries.help.Current.BoundingRectangle.Y -lt $entries.about.Current.BoundingRectangle.Y)
 Shot 'help-menu'
 $entries.help.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
 $timer.Restart()
 do{
  $candidates=$uia::RootElement.FindAll([Windows.Automation.TreeScope]::Children,[Windows.Automation.Condition]::TrueCondition)
  foreach($candidate in $candidates){if($candidate.Current.Name -like '*HDFStudio Help*' -and $candidate.Current.ProcessId -ne $process.Id){$browser=$candidate;break}}
  if($browser){break};Start-Sleep -Milliseconds 150
 }while($timer.ElapsedMilliseconds -lt 20000)
 Check 'Default browser displays local help' ($null -ne $browser)
 [HelpDesktop]::ShowWindow([IntPtr]$browser.Current.NativeWindowHandle,3)|Out-Null
 [HelpDesktop]::Front([IntPtr]$browser.Current.NativeWindowHandle)
 [Windows.Forms.SendKeys]::SendWait('^l')
 $timer.Restart();$address=''
 do{
  $focused=$uia::FocusedElement;$pattern=$null
  if($focused.TryGetCurrentPattern([Windows.Automation.ValuePattern]::Pattern,[ref]$pattern)){$address=$pattern.Current.Value}
  if($address -like '*doc/help.html*'){break};Start-Sleep -Milliseconds 100
 }while($timer.ElapsedMilliseconds -lt 5000)
 $expected=Join-Path (Split-Path -Parent $Executable) 'doc/help.html'
 Check 'Address is expected file URL' (([Uri]$address).IsFile -and [IO.Path]::GetFullPath(([Uri]$address).LocalPath) -eq [IO.Path]::GetFullPath($expected))
 [Windows.Forms.SendKeys]::SendWait('{ESC}')
 Start-Sleep -Milliseconds 350
 Shot 'help-browser'
 $browser.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)|ForEach-Object {$_.Current.Name}|Set-Content -Encoding UTF8 (Join-Path $log 'browser-controls.txt')
 # Navigate within our new local-help tab and then close only that tab.
 [Windows.Forms.Clipboard]::SetText($address+'#en-ai')
 [Windows.Forms.SendKeys]::SendWait('^l^v{ENTER}')
 Start-Sleep -Milliseconds 400
 Shot 'help-ai-english'
 [Windows.Forms.SendKeys]::SendWait('^w');$browser=$null
 $entries=OpenHelpMenu
 $bounds=$entries.about.Current.BoundingRectangle
 [HelpDesktop]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
 $timer.Restart();$about=$null;$version=$null
 do{
  $items=$uia::RootElement.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id))
  foreach($item in $items){if($item.Current.Name -like '*HDFStudio 0.14.02*'){$version=$item};if($item.Current.Name -eq 'OK'){$about=$item}}
  if($about -and $version){break};Start-Sleep -Milliseconds 100
 }while($timer.ElapsedMilliseconds -lt 5000)
 Check 'About displays 0.14.02' ($null -ne $version)
 Shot 'about-version'
 $about.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
 [HelpDesktop]::PostMessage([IntPtr]$window.Current.NativeWindowHandle,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 Check 'Normal application exit' ($process.WaitForExit(10000))
 $process.Refresh();Check 'Exit code zero' ($process.ExitCode -eq 0)
 $completed=$true
}catch{
 try{Shot 'failure';if($window){$window.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)|ForEach-Object {$_.Current.Name}|Set-Content -Encoding UTF8 (Join-Path $log 'controls.txt')}}catch{}
 $_|Out-String|Set-Content -Encoding UTF8 (Join-Path $log 'failure.txt');throw
}finally{
 [pscustomobject]@{status=$(if($completed){'PASS'}else{'FAIL'});executable=$Executable;checks=$checks;help_url=$address}|ConvertTo-Json -Depth 5|Set-Content -Encoding UTF8 (Join-Path $log 'result.json')
 if(-not $completed -and $process -and -not $process.HasExited){$process.CloseMainWindow()|Out-Null}
}
