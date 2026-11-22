# Real desktop acceptance of Bitshuffle HDF5, including Mask, grid, histogram and section.
# Inputs: HDFSTUDIO_BITSHUFFLE_SAMPLE and HDFSTUDIO_BITSHUFFLE_EXPECTED (independent JSON).
# Output: logs/S4-HdfBitshuffle/desktop-<label>-<time> with screenshots, PNG and clipboard.
param([ValidateSet('Debug','Release')][string]$Configuration='Release',[string]$Executable='',[string]$RunLabel='')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Windows.Forms,System.Drawing
Add-Type -TypeDefinition @"
using System;using System.Runtime.InteropServices;using System.Text;
public class FilterMouse {
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]public static extern void mouse_event(uint f,uint x,uint y,uint d,IntPtr e);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int c);
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern void keybd_event(byte v,byte s,uint f,IntPtr e);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",SetLastError=true)]public static extern IntPtr OpenInputDesktop(uint f,bool inherit,uint access);
 [DllImport("user32.dll")]public static extern bool CloseDesktop(IntPtr h);
 public delegate bool EnumChildProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")]public static extern bool EnumChildWindows(IntPtr h,EnumChildProc callback,IntPtr p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetClassName(IntPtr h,StringBuilder s,int c);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr c,StringBuilder s);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect r);
 public struct Rect{public int Left,Top,Right,Bottom;}
 public static IntPtr FileEdit(IntPtr dialog,int left,int top,int right,int bottom){IntPtr found=IntPtr.Zero;EnumChildWindows(dialog,(h,p)=>{var name=new StringBuilder(128);GetClassName(h,name,128);Rect r;GetWindowRect(h,out r);if(name.ToString()=="Edit"&&r.Left>=left&&r.Top>=top&&r.Right<=right&&r.Bottom<=bottom){found=h;return false;}return true;},IntPtr.Zero);return found;}
 public static string WindowText(IntPtr h){var s=new StringBuilder(32768);SendMessage(h,0x000D,(IntPtr)s.Capacity,s);return s.ToString();}
 public static void Front(IntPtr h){ShowWindow(h,3);keybd_event(0x12,0,0,IntPtr.Zero);keybd_event(0x12,0,2,IntPtr.Zero);SetForegroundWindow(h);}
 public static void Click(int x,int y,bool right){SetCursorPos(x,y);mouse_event(right?8u:2u,0,0,0,IntPtr.Zero);mouse_event(right?16u:4u,0,0,0,IntPtr.Zero);}
}
"@
$root=Split-Path $PSScriptRoot -Parent
if(-not $Executable){$Executable=Join-Path $root "build/output/$Configuration/HDFStudio.exe"}
if(-not $RunLabel){$RunLabel=$Configuration}
$sample=$env:HDFSTUDIO_BITSHUFFLE_SAMPLE
if(-not $sample -or -not(Test-Path -LiteralPath $sample)){throw 'Set HDFSTUDIO_BITSHUFFLE_SAMPLE'}
$expected=Get-Content -Raw -Encoding UTF8 $env:HDFSTUDIO_BITSHUFFLE_EXPECTED|ConvertFrom-Json
$before=(Get-FileHash -LiteralPath $sample -Algorithm SHA256).Hash
$log=Join-Path $root ('logs/S4-HdfBitshuffle/desktop-'+$RunLabel+'-'+(Get-Date -Format yyyyMMdd-HHmmss))
New-Item -ItemType Directory -Force $log|Out-Null
$uia=[Windows.Automation.AutomationElement];$checks=@();$process=$null;$window=$null;$completed=$false
function Controls($container){$container.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)}
function FindName($container,$name){,@($container.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new($uia::NameProperty,$name)))}
function ById($name){Controls $window|Where-Object {$_.Current.AutomationId.EndsWith('.'+$name) -or $_.Current.AutomationId -eq $name}|Select-Object -Last 1}
function Check($name,$pass){$script:checks+=[pscustomobject]@{check=$name;pass=[bool]$pass};"$(Get-Date -Format s) $name : $pass";if(-not $pass){throw $name}}
function Click($element,$right=$false){if(-not $element){throw 'UI element missing'};$r=$element.Current.BoundingRectangle;if($r.IsEmpty -or $element.Current.IsOffscreen){throw ('Hidden UI: '+$element.Current.Name)};$pattern=$null;if(-not $right -and $element.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$pattern)){$pattern.Invoke()}else{[FilterMouse]::Click([int]($r.X+$r.Width/2),[int]($r.Y+$r.Height/2),$right)};Start-Sleep -Milliseconds 200}
function Shot($name){$r=[Windows.Forms.SystemInformation]::VirtualScreen;$b=[Drawing.Bitmap]::new($r.Width,$r.Height);$g=[Drawing.Graphics]::FromImage($b);try{$g.CopyFromScreen($r.X,$r.Y,0,0,$b.Size);$b.Save((Join-Path $log ($name+'.png')))}finally{$g.Dispose();$b.Dispose()}}
function Context($name){
 [Windows.Forms.SendKeys]::SendWait('{ESC}');Click (FindName $window 'precipitation')[0];Click (FindName $window 'precipitation')[0] $true
 $item=$null;$menuClock=[Diagnostics.Stopwatch]::StartNew()
 do{$item=Controls $uia::RootElement|Where-Object {$_.Current.ProcessId-eq $process.Id -and $_.Current.ControlType-eq [Windows.Automation.ControlType]::MenuItem -and $_.Current.Name.StartsWith($name) -and -not $_.Current.IsOffscreen}|Select-Object -Last 1;if($item){break};Start-Sleep -Milliseconds 150}while($menuClock.ElapsedMilliseconds-lt 5000)
 if(-not $item){throw ('Context menu item missing: '+$name)}
 # Native modal save dialogs can keep UIA Invoke blocked until the dialog closes.
 if($name.StartsWith('Export')){$r=$item.Current.BoundingRectangle;[FilterMouse]::Click([int]($r.X+$r.Width/2),[int]($r.Y+$r.Height/2),$false);Start-Sleep -Milliseconds 200}else{Click $item;[Windows.Forms.SendKeys]::SendWait('{ESC}');Start-Sleep -Milliseconds 200}
}
function SetText($element,$text){$element.SetFocus();$pattern=$element.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern);$pattern.SetValue($text);Check ('Input '+$element.Current.Name) ($pattern.Current.Value-eq $text);[Windows.Forms.SendKeys]::SendWait('{TAB}');Start-Sleep -Milliseconds 400}
function Legend {return (ById 'rasterLegend').Current.Name}
function SaveImage($filename){
 Context 'Export As Image'
 $dialog=$null;$clock=[Diagnostics.Stopwatch]::StartNew()
 while(-not $dialog -and $clock.ElapsedMilliseconds-lt 6000){$all=@($uia::RootElement.FindAll([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id)));$dialog=$all|Where-Object {$_.Current.Name-eq 'Export As Image'}|Select-Object -First 1;if(-not $dialog){$dialog=Controls $window|Where-Object {$_.Current.Name-eq 'Export As Image' -and $_.Current.ControlType-eq [Windows.Automation.ControlType]::Window}|Select-Object -First 1};if(-not $dialog){Start-Sleep -Milliseconds 100}}
 if(-not $dialog){throw 'Native save dialog missing'}
 $hostControl=$null;$clock.Restart()
 while(-not $hostControl -and $clock.ElapsedMilliseconds-lt 5000){$hostControl=Controls $dialog|Where-Object {$_.Current.AutomationId-eq 'FileNameControlHost'}|Select-Object -First 1;if(-not $hostControl){Start-Sleep -Milliseconds 100}}
 if(-not $hostControl){throw 'Native filename host missing'}
 $r=$hostControl.Current.BoundingRectangle;$edit=[FilterMouse]::FileEdit([IntPtr]$dialog.Current.NativeWindowHandle,[int]$r.Left,[int]$r.Top,[int]$r.Right,[int]$r.Bottom)
 if($edit-eq [IntPtr]::Zero){throw 'Native filename Edit missing'}
 $actual='';for($attempt=0;$attempt-lt 3 -and $actual-ne $filename;$attempt++){Click $hostControl;[Windows.Forms.Clipboard]::SetText($filename);[Windows.Forms.SendKeys]::SendWait('^a{BACKSPACE}');[Windows.Forms.SendKeys]::SendWait('^v');Start-Sleep -Milliseconds 350;$actual=[FilterMouse]::WindowText($edit)}
 Check 'Export destination matches' ($actual-eq $filename)
 $save=[FilterMouse]::GetDlgItem([IntPtr]$dialog.Current.NativeWindowHandle,1);$null=[FilterMouse]::PostMessage($save,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
 $clock.Restart();while(-not(Test-Path -LiteralPath $filename) -and $clock.ElapsedMilliseconds-lt 10000){Start-Sleep -Milliseconds 100}
 Check ('Export '+[IO.Path]::GetFileName($filename)) (Test-Path -LiteralPath $filename)
 $image=$null;$clock.Restart();while(-not $image -and $clock.ElapsedMilliseconds-lt 5000){try{$image=[Drawing.Image]::FromFile($filename)}catch{Start-Sleep -Milliseconds 100}}
 if(-not $image){throw 'PNG not completely saved'};try{Check 'PNG full resolution' ($image.Width-eq 1401 -and $image.Height-eq 1201)}finally{$image.Dispose()}
}
try{
 $desktop=[FilterMouse]::OpenInputDesktop(0,$false,0x0100);if($desktop-eq [IntPtr]::Zero){throw ('Interactive desktop unavailable: '+[Runtime.InteropServices.Marshal]::GetLastWin32Error())};$null=[FilterMouse]::CloseDesktop($desktop)
 $env:QT_LOGGING_TO_CONSOLE='1'
 $process=Start-Process $Executable -ArgumentList ('"'+$sample+'"') -PassThru -RedirectStandardError (Join-Path $log 'qt.log');$handle=$process.Handle
 $clock=[Diagnostics.Stopwatch]::StartNew()
 do{$condition=[Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id);$windows=@($uia::RootElement.FindAll([Windows.Automation.TreeScope]::Children,$condition));$window=@($windows|Where-Object {(FindName $_ 'precipitation').Count-gt 0})|Select-Object -First 1;if($window){break};Start-Sleep -Milliseconds 200}while($clock.ElapsedMilliseconds-lt 20000)
 Check 'HDF5 dataset loaded' ($null-ne $window)
 [FilterMouse]::Front([IntPtr]$window.Current.NativeWindowHandle);Start-Sleep -Milliseconds 400
 Context 'Image View';Start-Sleep -Milliseconds 600
 [Windows.Forms.SendKeys]::SendWait('{ESC}')
 $imageWindow=Controls $window|Where-Object {$_.Current.ControlType-eq [Windows.Automation.ControlType]::Window -and $_.Current.Name-eq '/precipitation - Image'}|Select-Object -First 1
 $imageBounds=$imageWindow.Current.BoundingRectangle
 [FilterMouse]::Click([int]($imageBounds.X+200),[int]($imageBounds.Y+15),$false);Start-Sleep -Milliseconds 80;[FilterMouse]::Click([int]($imageBounds.X+200),[int]($imageBounds.Y+15),$false);Start-Sleep -Milliseconds 400
 Check 'Image canvas visible' ((FindName $window 'Raster image').Count-gt 0)
 Check 'Decoded full-plane range' ((Legend).Contains('1401 x 1201') -and (Legend).Contains('Min 0   Max 9999'))
 Shot 'image-all-desktop';SaveImage (Join-Path $log 'image-all.png')
 SetText (FindName $window 'Mask start')[0] '9999'
 Check 'Mask count matches independent reference' ((Legend).Contains(('(decoded): '+$expected.masked+', white')))
 Check 'Masked range matches independent reference' ((Legend).Contains(('Min '+$expected.min+'   Max '+$expected.max)))
 Shot 'image-masked-desktop';SaveImage (Join-Path $log 'image-masked.png')
 Context 'Data Grid (decoded';Start-Sleep -Milliseconds 500
 $table=(FindName $window 'Data grid cells')[0];Check 'Large decoded grid loaded' ($null-ne $table)
 $viewport=Controls $window|Where-Object {$_.Current.AutomationId.EndsWith('gridTable.qt_scrollarea_viewport')}|Select-Object -Last 1
 $tableBounds=$viewport.Current.BoundingRectangle;[FilterMouse]::Click([int]($tableBounds.X+15),[int]($tableBounds.Y+12),$false)
 $clock.Restart();do{$selected=(FindName $window 'Selected: 1 cells').Count-gt 0;if($selected){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
 Check 'Single cell selected' $selected
 Click (FindName $window 'Copy selection')[0];Start-Sleep -Milliseconds 300
 $copied=[Windows.Forms.Clipboard]::GetText();[IO.File]::WriteAllText((Join-Path $log 'first-cell.tsv'),$copied,[Text.UTF8Encoding]::new($false));Check 'Decoded first value copied' ($copied-eq $expected.first)
 Shot 'grid'
 Context 'Histogram';Start-Sleep -Milliseconds 500
 Check 'Histogram finite count' ((ById 'histogramSummary').Current.Name.Contains('1682601'))
 Shot 'histogram'
 Context 'Space-Time Section (Hovm';$clock.Restart()
 do{$status=ById 'hovmollerStatus';if($status -and $status.Current.Name-eq 'Ready'){break};Start-Sleep -Milliseconds 150}while($clock.ElapsedMilliseconds-lt 15000)
 Check 'Independent section Ready' ($status -and $status.Current.Name-eq 'Ready')
 Check 'Independent section canvas' ((FindName $window 'Space-Time Section image').Count-gt 0);Shot 'section'
 Check 'Source unchanged' ((Get-FileHash -LiteralPath $sample -Algorithm SHA256).Hash-eq $before)
 $null=[FilterMouse]::PostMessage([IntPtr]$window.Current.NativeWindowHandle,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
 Check 'Normal process exit' ($process.WaitForExit(10000));$process.Refresh();Check 'Exit code zero' ($process.ExitCode-eq 0)
 $completed=$true
}catch{
 try{Shot 'failure'}catch{}
 if($window){try{Controls $window|ForEach-Object {"$($_.Current.AutomationId) | $($_.Current.Name) | $($_.Current.ControlType.ProgrammaticName) | $($_.Current.BoundingRectangle)"}|Out-File (Join-Path $log 'controls.txt') -Encoding UTF8}catch{}}
 Get-Process HDFStudio,hdf_cli -ErrorAction SilentlyContinue|Select-Object Id,ProcessName,Responding,Path|ConvertTo-Json|Set-Content (Join-Path $log 'processes.json') -Encoding UTF8
 throw
}finally{
 $checks|ConvertTo-Json|Set-Content (Join-Path $log 'results.json') -Encoding UTF8
 [pscustomobject]@{status=$(if($completed){'PASS'}else{'FAIL'});checks=$checks.Count;sample_sha256=$before;executable=$Executable}|ConvertTo-Json|Set-Content (Join-Path $log 'summary.json') -Encoding UTF8
 if($process -and -not $process.HasExited){$null=$process.CloseMainWindow();if(-not $process.WaitForExit(3000)){Stop-Process -Id $process.Id}}
}
