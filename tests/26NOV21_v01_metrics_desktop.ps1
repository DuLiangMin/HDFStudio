# Real-mouse analysis acceptance. Input: read-only HDFSTUDIO_ZOOM_SAMPLE (3-D variable).
# Optional: HDFSTUDIO_COMPARE_TARGET selects the second file; forecast reference values
# are supplied by HDFSTUDIO_EXPECTED_R/RMSE and HDFSTUDIO_EXPECTED_COMMON_RMSE.
# Output: logs/S4-Metrics-Sync/desktop-<configuration>-<timestamp>, screenshots/controls/plugin logs.
# Run: powershell -NoProfile -ExecutionPolicy Bypass -File tests/26NOV21_v01_metrics_desktop.ps1
param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Windows.Forms,System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System; using System.Runtime.InteropServices; using System.Drawing;
public class MetricsMouse {
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint x,uint y,uint d,IntPtr e);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int c);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h,int x,int y,int w,int t,bool repaint);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")] public static extern void keybd_event(byte v,byte s,uint f,IntPtr e);
 public static void Front(IntPtr h) {ShowWindow(h,3);keybd_event(0x12,0,0,IntPtr.Zero);keybd_event(0x12,0,2,IntPtr.Zero);SetForegroundWindow(h);}
 public static void Click(int x,int y,bool right) {SetCursorPos(x,y);mouse_event(right?8u:2u,0,0,0,IntPtr.Zero);mouse_event(right?16u:4u,0,0,0,IntPtr.Zero);}
 public static Bitmap Capture(int x,int y,int w,int h) {var b=new Bitmap(w,h);using(var g=Graphics.FromImage(b))g.CopyFromScreen(x,y,0,0,b.Size);return b;}
}
"@
$root=Split-Path $PSScriptRoot -Parent
$sample=$env:HDFSTUDIO_ZOOM_SAMPLE
$targetSample=$env:HDFSTUDIO_COMPARE_TARGET
$variable=$env:HDFSTUDIO_ZOOM_VARIABLE
if(-not $variable){$variable='PRECT'}
if(-not $sample -or -not(Test-Path -LiteralPath $sample)){throw 'Set HDFSTUDIO_ZOOM_SAMPLE'}
$before=(Get-FileHash -LiteralPath $sample -Algorithm SHA256).Hash
$targetBefore=if($targetSample){(Get-FileHash -LiteralPath $targetSample -Algorithm SHA256).Hash}else{''}
$log=Join-Path $root ('logs/S4-Metrics-Sync/desktop-'+$Configuration+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $log | Out-Null
$uia=[System.Windows.Automation.AutomationElement]
$results=@()
function FindName($container,$name){,@($container.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.PropertyCondition]::new($uia::NameProperty,$name)))}
function FindId($container,$id){$container.FindFirst([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.PropertyCondition]::new($uia::AutomationIdProperty,$id))}
function Names($container){@($container.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition)|ForEach-Object {$_.Current.Name})}
function Check($name,$pass){$script:results+=[pscustomobject]@{check=$name;pass=[bool]$pass};"$(Get-Date -Format s) $name : $pass";if(-not $pass){throw $name}}
function Click($element,$right=$false){
 if(-not $element){throw 'Missing UI element'}
 $r=$element.Current.BoundingRectangle
 if($r.IsEmpty -or $element.Current.IsOffscreen){throw "Hidden UI element: $($element.Current.Name)"}
 [MetricsMouse]::Click([int]($r.X+$r.Width/2),[int]($r.Y+$r.Height/2),$right)
 Start-Sleep -Milliseconds 200
}
function SetText($element,$text){Click $element;[Windows.Forms.SendKeys]::SendWait('^a{BACKSPACE}');[Windows.Forms.SendKeys]::SendWait($text);[Windows.Forms.SendKeys]::SendWait('{TAB}');Start-Sleep -Milliseconds 500}
function Shot($name){$r=[Windows.Forms.SystemInformation]::VirtualScreen;$b=[MetricsMouse]::Capture($r.X,$r.Y,$r.Width,$r.Height);try{$b.Save((Join-Path $log ($name+'.png')))}finally{$b.Dispose()}}
function MetricsText {
 $element=FindId $script:window 'compareMetricsText'
 if(-not $element){$element=(FindName $script:window 'Comparison metrics')[0]}
 try{return $element.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value}catch{return $element.GetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern).DocumentRange.GetText(-1)}
}
$process=$null
try {
 $env:QT_DEBUG_PLUGINS='1';$env:QT_LOGGING_TO_CONSOLE='1'
 $openArguments="`"$sample`""
 if($targetSample){$openArguments+=" `"$targetSample`""}
 $process=Start-Process (Join-Path $root "build/output/$Configuration/HDFStudio.exe") -ArgumentList $openArguments -RedirectStandardError (Join-Path $log 'qt-plugin.log') -PassThru
 $processHandle=$process.Handle
 Start-Sleep -Seconds 7
 $condition=[System.Windows.Automation.AndCondition]::new([System.Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id),[System.Windows.Automation.PropertyCondition]::new($uia::NameProperty,'HDFStudio'))
 $script:window=$uia::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children,$condition)
 Check 'Main window ready' ($null -ne $window -and (FindName $window $variable).Count -gt 0)
 [MetricsMouse]::Front([IntPtr]$window.Current.NativeWindowHandle);Start-Sleep -Milliseconds 500
 for($i=0;$i -lt 2;$i++){$items=FindName $window $variable;$index=if($targetSample){$i}else{0};Click $items[$index] $true;Click (FindName $uia::RootElement 'Image View')[0];Start-Sleep -Milliseconds 700}
 $compareAction=(FindName $window 'Compare & Metrics')|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Button -and -not $_.Current.IsOffscreen}|Select-Object -First 1
 if(-not $compareAction){Click (FindName $window 'Window')[0];$compareAction=(FindName $uia::RootElement 'Compare & Metrics')[0]}
 if(-not $compareAction){$compareAction=$uia::RootElement.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition)|Where-Object {$_.Current.ControlType-eq [System.Windows.Automation.ControlType]::MenuItem -and $_.Current.Name -match '^Compare.*Metrics$'}|Select-Object -First 1}
 Click $compareAction;Start-Sleep -Milliseconds 600
 Check 'Analysis dock visible' ((FindName $window 'Anchor image').Count -gt 0)
 $text=MetricsText
 if($targetSample){Check 'Forecast pair matches independent reference' ($text.Contains('Pearson r: '+$env:HDFSTUDIO_EXPECTED_R) -and $text.Contains('RMSE: '+$env:HDFSTUDIO_EXPECTED_RMSE))}
 else{Check 'Identical images have r=1 RMSE=0' ($text.Contains('Pearson r: 1') -and $text.Contains('RMSE: 0'))}
  Check 'Entropy is an independent image, not a comparison metric' (-not $text.Contains('Entropy'))
 Shot 'identical'
 SetText (FindName $window 'Lag offset')[0] '2'
 SetText (FindName $window 'Anchor layer index')[0] '3'
 $text=MetricsText;Check 'Lag +2 aligns index 3 to 5' ($text.Contains('Anchor[3]') -and $text.Contains('Target[5]') -and $text.Contains('lag +2'))
 Click (FindName $window 'Residual map')[0];Start-Sleep -Milliseconds 500
 Check 'Residual map visible' ((FindName $window 'Residual image').Count -gt 0 -or (FindName $uia::RootElement 'Residual image').Count -gt 0)
 $residualWindow=$uia::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children,[System.Windows.Automation.PropertyCondition]::new($uia::NameProperty,'Residual map: Target - Anchor'))
 if($residualWindow){$r=$window.Current.BoundingRectangle;[MetricsMouse]::MoveWindow([IntPtr]$residualWindow.Current.NativeWindowHandle,[int]($r.X+20),[int]($r.Y+100),650,500,$true)|Out-Null;Start-Sleep -Milliseconds 300}
 Shot 'lag-residual'
 Click (FindName $window 'Copy metrics')[0]
 $clip=[Windows.Forms.Clipboard]::GetText();Check 'Copied aligned metrics' ($clip.Contains('Anchor[3]') -and $clip.Contains('RMSE:'))
 SetText (FindName $window 'Lag offset')[0] '10000'
 $text=MetricsText;Check 'Out of range invalidates metrics' ($text.Contains('Out of range') -and $text.Contains('RMSE: N/A'))
 Shot 'out-of-range'
 SetText (FindName $window 'Lag offset')[0] '0'
 $text=MetricsText;Check 'Recovered valid layer' ($text.Contains('Status: Ready') -and -not $text.Contains('RMSE: N/A'))
 if($targetSample){
  SetText (FindName $window 'Lag offset')[0] '-1';SetText (FindName $window 'Anchor layer index')[0] '1'
  $text=MetricsText;Check 'Verified common timestamp pair matches NumPy' ($text.Contains('Anchor[1]') -and $text.Contains('Target[0]') -and $text.Contains('RMSE: '+$env:HDFSTUDIO_EXPECTED_COMMON_RMSE))
  Shot 'forecast-common-time'
  SetText (FindName $window 'Lag offset')[0] '0'
 }
 $slider=(FindName $window 'Anchor layer slider')[0];Click $slider;[Windows.Forms.SendKeys]::SendWait('{HOME}{RIGHT 2}');Start-Sleep -Milliseconds 600
 $text=MetricsText;Check 'Slider drives both layers' ($text.Contains('Anchor[2]') -and $text.Contains('Target[2]'))
  $csv=Join-Path $log 'comparison.csv'
 Click (FindName $window 'Export comparison CSV')[0]
 $saveDialog=$null
 for($attempt=0;$attempt -lt 40 -and -not $saveDialog;$attempt++){
  $candidates=FindName $window 'Export comparison CSV'
  foreach($candidate in $candidates){if($candidate.Current.ControlType -eq [System.Windows.Automation.ControlType]::Window){$saveDialog=$candidate;break}}
  if(-not $saveDialog){Start-Sleep -Milliseconds 100}
 }
 if(-not $saveDialog){throw 'CSV save dialog did not become ready'}
 $filenameHost=FindId $saveDialog 'FileNameControlHost'
 $filename=if($filenameHost){FindId $filenameHost '1001'}else{FindId $saveDialog '1001'}
 Click $filename
 [Windows.Forms.Clipboard]::SetText($csv)
 [Windows.Forms.SendKeys]::SendWait('^a{BACKSPACE}^v');Start-Sleep -Milliseconds 300
 try{$filenameText=$filename.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value}catch{$filenameText=$filename.Current.Name}
 Check 'Save dialog contains complete CSV path' ($filenameText -eq $csv)
 # ID 1 also occurs on directory list items; resolve the native direct-child IDOK.
 $saveButtonHandle=[MetricsMouse]::GetDlgItem([IntPtr]$saveDialog.Current.NativeWindowHandle,1)
 if($saveButtonHandle -eq [IntPtr]::Zero){throw 'Native save button not found'}
 $saveButton=$uia::FromHandle($saveButtonHandle)
 "$(Get-Date -Format s) Native save button: $($saveButton.Current.Name), $($saveButton.Current.BoundingRectangle)"
 Click $saveButton
 for($attempt=0;$attempt -lt 50 -and -not (Test-Path -LiteralPath $csv);$attempt++){Start-Sleep -Milliseconds 100}
 Check 'CSV exported with identity and metrics' ((Test-Path $csv) -and ([IO.File]::ReadAllText($csv).Contains('anchor_revision')) -and ([IO.File]::ReadAllText($csv).Contains('rmse')))
 Shot 'final'
 Check 'Input sample unchanged' ((Get-FileHash -LiteralPath $sample -Algorithm SHA256).Hash -eq $before)
 if($targetSample){Check 'Target forecast unchanged' ((Get-FileHash -LiteralPath $targetSample -Algorithm SHA256).Hash -eq $targetBefore)}
 # Reacquire the ready main-window handle, never close an old splash HWND.
 $window=$uia::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children,$condition)
 Click (FindName $window 'File')[0];Click (FindName $uia::RootElement 'Exit')[0]
 Check 'Normal exit' ($process.WaitForExit(10000) -and $process.ExitCode -eq 0)
} catch {
 try{Shot 'failure'}catch{}
 if($window){$window.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition)|ForEach-Object {"$($_.Current.AutomationId) | $($_.Current.Name) | $($_.Current.ControlType.ProgrammaticName) | $($_.Current.BoundingRectangle)"}|Out-File (Join-Path $log 'controls.txt') -Encoding UTF8}
 Get-Process | Where-Object ProcessName -match 'HDFStudio|hdf_explorer' | Select-Object Id,ProcessName,Responding,Path | Out-File (Join-Path $log 'processes.txt')
 throw
} finally {
 $results | ConvertTo-Json | Set-Content (Join-Path $log 'results.json') -Encoding UTF8
 if($process -and -not $process.HasExited){$null=$process.CloseMainWindow();if(-not $process.WaitForExit(3000)){Stop-Process -Id $process.Id}}
}
