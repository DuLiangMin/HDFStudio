# Real mouse acceptance: an independent 2D entropy image, layers, Mask and native PNG/CSV export.
# Input: read-only HDFSTUDIO_FORECAST_ANCHOR, HDFSTUDIO_FORECAST_VARIABLE (optional).
# Output: logs/S4-TextureEntropy/desktop-<label>-<timestamp>; screenshots and actual exports.
# Run: powershell -NoProfile -ExecutionPolicy Bypass -File tests/26NOV21_v01_texture_entropy_desktop.ps1
param([ValidateSet('Debug','Release')][string]$Configuration='Release',[string]$Executable='',[string]$RunLabel='')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Windows.Forms,System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;using System.Drawing;using System.Runtime.InteropServices;
public class TextureMouse {
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint x,uint y,uint d,IntPtr e);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int c);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern void keybd_event(byte v,byte s,uint f,IntPtr e);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint msg,IntPtr w,IntPtr l);
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
$log=Join-Path $root ('logs/S4-TextureEntropy/desktop-'+$RunLabel+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $log|Out-Null
$version=([regex]'project\(HDFStudio VERSION ([0-9.]+)').Match([IO.File]::ReadAllText((Join-Path $root 'CMakeLists.txt'))).Groups[1].Value
$uia=[System.Windows.Automation.AutomationElement];$checks=@();$process=$null;$window=$null
function FindName($container,$name){,@($container.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.PropertyCondition]::new($uia::NameProperty,$name)))}
function Controls($container){$container.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition)}
function Check($name,$value){$script:checks+=[pscustomobject]@{check=$name;pass=[bool]$value};"$(Get-Date -Format s) $name : $value";if(-not $value){throw $name}}
function Click($element,$right=$false){if(-not $element){throw 'UI element missing'};$r=$element.Current.BoundingRectangle;if($r.IsEmpty -or $element.Current.IsOffscreen){throw ('Hidden UI: '+$element.Current.Name)};[TextureMouse]::Click([int]($r.X+$r.Width/2),[int]($r.Y+$r.Height/2),$right);Start-Sleep -Milliseconds 200}
function SetText($element,$text){
 if(-not $element){throw 'Text input missing'}
 $element.SetFocus();$value=$null;$numeric=$null
 if($element.TryGetCurrentPattern([Windows.Automation.RangeValuePattern]::Pattern,[ref]$numeric)){
  $numeric.SetValue([double]$text)
  if($numeric.Current.Value -ne [double]$text){throw ('Numeric input read-back mismatch: '+$element.Current.Name)}
 }elseif($element.TryGetCurrentPattern([Windows.Automation.ValuePattern]::Pattern,[ref]$value)){
  $value.SetValue($text)
  if($value.Current.Value -ne $text){throw ('Text input read-back mismatch: '+$element.Current.Name)}
 }else{
  [Windows.Forms.SendKeys]::SendWait('^a{BACKSPACE}')
  if($text){[Windows.Forms.Clipboard]::SetText($text);[Windows.Forms.SendKeys]::SendWait('^v')}
 }
 [Windows.Forms.SendKeys]::SendWait('{TAB}');Start-Sleep -Milliseconds 350
}
function Shot($name){$bitmap=[TextureMouse]::Capture([Windows.Forms.SystemInformation]::VirtualScreen);try{$bitmap.Save((Join-Path $log ($name+'.png')))}finally{$bitmap.Dispose()}}
function Summary {
 $labels=Controls $window|Where-Object {$_.Current.AutomationId.EndsWith('.textureEntropySummary')}
 return ($labels|Where-Object {-not $_.Current.IsOffscreen}|Select-Object -Last 1).Current.Name
}
function MainAction($name){$item=FindName $window $name|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Button}|Select-Object -First 1;Click $item}
function SaveExport($action,$filename){
 $button=FindName $window $action|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Button -and -not $_.Current.IsOffscreen}|Select-Object -Last 1
 Click $button;Start-Sleep -Milliseconds 700
 $dialogs=@($uia::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children,[System.Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id)))
 $dialog=$dialogs|Where-Object {$_.Current.Name -like 'Export texture entropy*'}|Select-Object -First 1
 if(-not $dialog){$dialog=Controls $window|Where-Object {$_.Current.Name -like 'Export texture entropy*' -and $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Window}|Select-Object -First 1}
 if(-not $dialog){throw 'Native export dialog not found'}
 $file_host=Controls $dialog|Where-Object {$_.Current.AutomationId -eq 'FileNameControlHost'}|Select-Object -First 1
 if(-not $file_host){throw 'Native FileNameControlHost missing'}
 $edit=Controls $file_host|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Edit}|Select-Object -First 1
 if(-not $edit){$edit=Controls $file_host|Where-Object {$_.Current.AutomationId -eq '1001'}|Select-Object -First 1}
 if(-not $edit){throw 'Native file name edit missing'}
 $actual=''
 for($attempt=0;$attempt -lt 3 -and $actual -ne $filename;$attempt++){
  Click $edit;Start-Sleep -Milliseconds 250
  [Windows.Forms.Clipboard]::SetText($filename)
  [Windows.Forms.SendKeys]::SendWait('^a{BACKSPACE}');Start-Sleep -Milliseconds 150
  [Windows.Forms.SendKeys]::SendWait('^v');Start-Sleep -Milliseconds 500
  try{$actual=$edit.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value}catch{$actual=$edit.Current.Name}
 }
 if($actual -ne $filename){throw ('Native output path mismatch: '+$actual)}
 $save=[TextureMouse]::GetDlgItem([IntPtr]$dialog.Current.NativeWindowHandle,1)
 if($save -eq [IntPtr]::Zero){throw 'Native Save IDOK not found'}
 $null=[TextureMouse]::PostMessage($save,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
 $deadline=[Diagnostics.Stopwatch]::StartNew()
 while(-not(Test-Path -LiteralPath $filename) -and $deadline.ElapsedMilliseconds -lt 20000){Start-Sleep -Milliseconds 100}
 Start-Sleep -Milliseconds 250
 Check ($action+' created '+[IO.Path]::GetFileName($filename)) (Test-Path -LiteralPath $filename)
}
try {
 $env:QT_DEBUG_PLUGINS='1';$env:QT_LOGGING_TO_CONSOLE='1'
 $process=Start-Process $Executable -ArgumentList ('"'+$source+'"') -RedirectStandardError (Join-Path $log 'qt-plugin.log') -PassThru
 $processHandle=$process.Handle;Start-Sleep -Seconds 6
 $condition=[System.Windows.Automation.AndCondition]::new([System.Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id),[System.Windows.Automation.PropertyCondition]::new($uia::NameProperty,'HDFStudio'))
 $window=$uia::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children,$condition)
 Check 'Forecast variable loaded' ($window -and (FindName $window $variable).Count -eq 1)
 [TextureMouse]::Front([IntPtr]$window.Current.NativeWindowHandle);Start-Sleep -Milliseconds 400
 Click (FindName $window $variable)[0] $true
 Check 'Independent texture image context entry' ((FindName $uia::RootElement 'Texture Entropy Image').Count -gt 0)
 Click (FindName $uia::RootElement 'Texture Entropy Image')[0];Start-Sleep -Milliseconds 800
 Check 'Actual two dimensional entropy canvas' ((FindName $window 'Texture entropy 2D image').Count -eq 1)
 Check 'No source Image View or comparison required' ((FindName $window 'Raster image').Count -eq 0 -and (FindName $window 'Comparison metrics').Count -eq 0)
 Check '2D shape and entropy block units displayed' ((Summary).Contains('190 x 384 pixels') -and (Summary).Contains('48 x 96') -and (Summary).Contains('0..4 bits'))
 $bars=FindName $window 'Colorbar';Check 'Visible bits colorbar' ($bars.Count -gt 0 -and -not $bars[0].Current.IsOffscreen)
 Shot 'independent-layer0-desktop'
 SaveExport 'Export PNG' (Join-Path $log 'independent-layer0.png')
 SaveExport 'Export CSV' (Join-Path $log 'independent-layer0.csv')
 MainAction 'Next Layer';Start-Sleep -Milliseconds 400
 Check 'Next Layer updates entropy image' ((Summary).Contains('layer [1]'))
 SaveExport 'Export PNG' (Join-Path $log 'independent-layer1.png')
 SaveExport 'Export CSV' (Join-Path $log 'independent-layer1.csv')
 SetText (FindName $window 'Texture mask start')[0] '270'
 SetText (FindName $window 'Texture mask end')[0] '280'
 Click (FindName $window 'Apply Mask')[0]
 Check 'Mask excluded count is nonzero' ((Summary) -match 'excluded: [1-9][0-9]*\.')
 SaveExport 'Export PNG' (Join-Path $log 'masked-layer1.png')
 SaveExport 'Export CSV' (Join-Path $log 'masked-layer1.csv')
 SetText (FindName $window 'Texture mask start')[0] 'invalid'
 Click (FindName $window 'Apply Mask')[0]
 Check 'Invalid Mask clears image and disables export' ((Summary).Contains('Invalid Mask') -and -not (FindName $window 'Export PNG')[0].Current.IsEnabled)
 SetText (FindName $window 'Texture mask start')[0] ''
 SetText (FindName $window 'Texture mask end')[0] ''
 Click (FindName $window 'Apply Mask')[0]
 Check 'Valid plane recovers after invalid Mask' ((Summary).Contains('layer [1]'))
 SetText (FindName $window 'Frame interval (ms)')[0] '1000'
 Click (FindName $window 'Play / pause layers')[0];Start-Sleep -Milliseconds 2200
 Click (FindName $window 'Play / pause layers')[0]
 $played=Summary
 Check 'Independent entropy image plays layers' ($played -match 'layer \[([2-9]|[1-9][0-9]+)\]')
 Start-Sleep -Milliseconds 1100
 Check 'Paused entropy image holds its layer' ((Summary) -eq $played)
 Shot 'recovered-layer1'
 Click (FindName $window 'View')[0]
 Check 'Top-level View includes texture image' ((FindName $uia::RootElement 'Texture Entropy Image').Count -gt 0)
 [Windows.Forms.SendKeys]::SendWait('{ESC}')
 Click (FindName $window $variable)[0] $true;Click (FindName $uia::RootElement 'Image View')[0];Start-Sleep -Milliseconds 600
 Click (FindName $window 'View')[0];Click (FindName $uia::RootElement 'Texture Entropy Image')[0];Start-Sleep -Milliseconds 700
 Check 'Image-derived entropy follows source' ((FindName $window 'Following Image View: layer and Mask').Count -eq 1)
 MainAction 'Next Layer';Start-Sleep -Milliseconds 400
 Check 'Derived entropy advances source layer' ((Summary).Contains('layer [1]'))
 Shot 'source-following'
 Click (FindName $window 'Help')[0];Click (FindName $uia::RootElement 'About')[0]
 Check ('About '+$version) (@(Controls $window|Where-Object {$_.Current.Name.Contains('HDFStudio '+$version)}).Count -gt 0)
 [Windows.Forms.SendKeys]::SendWait('{ESC}')
 Check 'Read-only forecast hash unchanged' ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -eq $before)
 Click (FindName $window 'File')[0];Click (FindName $uia::RootElement 'Exit')[0]
 Check 'Normal exit' ($process.WaitForExit(10000) -and $process.ExitCode -eq 0)
} catch {
 try{Shot 'failure'}catch{}
 if($window){Controls $window|ForEach-Object {"$($_.Current.AutomationId) | $($_.Current.Name) | $($_.Current.ControlType.ProgrammaticName) | $($_.Current.BoundingRectangle)"}|Out-File (Join-Path $log 'controls.txt') -Encoding UTF8}
 Get-Process|Where-Object ProcessName -match 'HDFStudio|hdf_explorer'|Select-Object Id,ProcessName,Responding,Path|Out-File (Join-Path $log 'processes.txt')
 throw
} finally {
 $checks|ConvertTo-Json|Set-Content (Join-Path $log 'results.json') -Encoding UTF8
 [pscustomobject]@{executable=$Executable;source=$source;variable=$variable;configuration=$Configuration;label=$RunLabel;checks=$checks.Count;all_passed=($checks.Count -gt 0 -and @($checks|Where-Object {-not $_.pass}).Count -eq 0)}|ConvertTo-Json|Set-Content (Join-Path $log 'summary.json') -Encoding UTF8
 if($process -and -not $process.HasExited){$null=$process.CloseMainWindow();if(-not $process.WaitForExit(3000)){Stop-Process -Id $process.Id}}
}
