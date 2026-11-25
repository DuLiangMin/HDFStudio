# Real Windows mouse/keyboard acceptance of CMaps on a read-only NetCDF sample.
# Input: read-only HDFSTUDIO_CMAP_SAMPLE; HDFSTUDIO_CMAP_VARIABLE defaults to PRECT.
# Output: logs/S4-UI-ColorMap/desktop-*.png and desktop-results.json; no input modification.
# Run: powershell -NoProfile -ExecutionPolicy Bypass -File tests/desktop_colormaps.ps1
param([string]$Configuration='Release')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Windows.Forms,System.Drawing
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
public class ColorMapMouse {
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint x,uint y,uint d,IntPtr e);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int c);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern void keybd_event(byte v,byte s,uint f,IntPtr e);
 public static void Front(IntPtr h) {ShowWindow(h,3);keybd_event(0x12,0,0,IntPtr.Zero);keybd_event(0x12,0,2,IntPtr.Zero);SetForegroundWindow(h);}
 public static void Click(int x,int y,bool right) {SetCursorPos(x,y);mouse_event(right?8u:2u,0,0,0,IntPtr.Zero);mouse_event(right?16u:4u,0,0,0,IntPtr.Zero);}
}
"@
$root=Split-Path $PSScriptRoot -Parent
$sample=$env:HDFSTUDIO_CMAP_SAMPLE
$variable=$env:HDFSTUDIO_CMAP_VARIABLE
if(-not $variable){$variable='PRECT'}
if(-not $sample -or -not (Test-Path -LiteralPath $sample)){throw 'Set HDFSTUDIO_CMAP_SAMPLE to an existing NetCDF sample.'}
$log=Join-Path $root 'logs/S4-UI-ColorMap'
New-Item -ItemType Directory -Force $log | Out-Null
$uia=[System.Windows.Automation.AutomationElement]
$scope=[System.Windows.Automation.TreeScope]::Descendants
$results=@()
function FindName($container,$name){,@($container.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.PropertyCondition]::new($uia::NameProperty,$name)))}
function Shot($name){
 $bounds=[Windows.Forms.SystemInformation]::VirtualScreen
 $bitmap=[Drawing.Bitmap]::new($bounds.Width,$bounds.Height)
 $graphics=[Drawing.Graphics]::FromImage($bitmap)
 try{$graphics.CopyFromScreen($bounds.Location,[Drawing.Point]::Empty,$bounds.Size);$bitmap.Save((Join-Path $log "desktop-$name-$Configuration.png"))}
 finally{$graphics.Dispose();$bitmap.Dispose()}
}
function Click($element,$right=$false){
 if(-not $element){throw 'UI element not found'}
 $rect=$element.Current.BoundingRectangle
 if($rect.IsEmpty -or $element.Current.IsOffscreen){throw "UI element is not visible: $($element.Current.Name)"}
 [ColorMapMouse]::Click([int]($rect.X+$rect.Width/2),[int]($rect.Y+$rect.Height/2),$right)
}
function Check($name,$pass){
 $script:results+=[pscustomobject]@{check=$name;pass=[bool]$pass}
 "$(Get-Date -Format s) $name : $pass"
 if(-not $pass){throw $name}
}
$process=$null
try {
 $process=Start-Process (Join-Path $root "build/output/$Configuration/HDFStudio.exe") -ArgumentList "`"$sample`"" -PassThru
 Start-Sleep -Seconds 8
 $condition=[System.Windows.Automation.AndCondition]::new([System.Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id),[System.Windows.Automation.PropertyCondition]::new($uia::NameProperty,'HDFStudio'))
 $window=$uia::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children,$condition)
 Check 'Main window found' ($null -ne $window)
 [ColorMapMouse]::Front([IntPtr]$window.Current.NativeWindowHandle);Start-Sleep -Seconds 1
 Click (FindName $window $variable)[0] $true;Start-Sleep -Milliseconds 500
 Click (FindName $uia::RootElement 'Image View')[0];Start-Sleep -Seconds 2
 $combo=@((FindName $window 'CMaps') | Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::ComboBox})[0]
 Check 'CMaps selector visible' ($null -ne $combo -and -not $combo.Current.IsOffscreen)
 Check 'Projection action available' ((FindName $window ('Projection'+[char]0x2026)).Count -gt 0)
 Click $combo;[Windows.Forms.SendKeys]::SendWait('{HOME}{DOWN 5}{ENTER}');Start-Sleep -Seconds 2
 Shot 'viridis'
 $value=$combo.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value
 Check 'Standard Viridis selected' ($value -eq 'Viridis')
 Click $combo;[Windows.Forms.SendKeys]::SendWait('{END}{ENTER}');Start-Sleep -Seconds 2
 Shot 'ncl'
 $value=$combo.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value
 Check 'NCL palette selected' ($value.Contains('BlueWhiteOrangeRed'))
 Check 'Colorbar visible' ((FindName $window 'Colorbar').Count -gt 0)
 Click (FindName $window 'Color map options')[0];Start-Sleep -Milliseconds 300
 Click (FindName $uia::RootElement ('Color cards and mapping'+[char]0x2026))[0];Start-Sleep -Milliseconds 600
 Shot 'options'
 Check 'Mapping options opened' ((FindName $uia::RootElement 'Normalization').Count -gt 0)
 [Windows.Forms.SendKeys]::SendWait('{ESC}')
} catch {
 try{Shot 'failure'}catch{$_ | Out-File (Join-Path $log "desktop-capture-error-$Configuration.txt")}
 Get-Process | Where-Object {$_.ProcessName -match 'HDFStudio|hdf_explorer'} | Select-Object ProcessName,Id,Path,Responding | Out-File (Join-Path $log "desktop-processes-$Configuration.txt")
 Get-ChildItem (Join-Path $root "build/output/$Configuration") -Recurse -File | Select-Object FullName,Length | Out-File (Join-Path $log "desktop-deployment-$Configuration.txt")
 throw
} finally {
 $results | ConvertTo-Json | Set-Content (Join-Path $log "desktop-results-$Configuration.json") -Encoding UTF8
 if($process -and -not $process.HasExited){$null=$process.CloseMainWindow();if(-not $process.WaitForExit(3000)){Stop-Process -Id $process.Id}}
}
