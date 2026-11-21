# Capture a real HDFStudio temperature/texture playback and encode a README GIF offline.
# Input: HDFSTUDIO_DEMO_SAMPLE (read-only); optional HDFSTUDIO_DEMO_VARIABLE, default TMP_2maboveground.
# Output: docs/demo-analysis.gif, docs/screenshot-main.png; raw frames/logs in logs/S4-TextureEntropy/demo-*.
# Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/26NOV21_v01_capture_demo.ps1
param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Windows.Forms,System.Drawing,PresentationCore,WindowsBase
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;using System.Drawing;using System.Runtime.InteropServices;
public class DemoCapture {
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint x,uint y,uint d,IntPtr e);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int c);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern void keybd_event(byte v,byte s,uint f,IntPtr e);
 public static void Front(IntPtr h){ShowWindow(h,3);keybd_event(0x12,0,0,IntPtr.Zero);keybd_event(0x12,0,2,IntPtr.Zero);SetForegroundWindow(h);}
 public static void Click(int x,int y,bool right){SetCursorPos(x,y);mouse_event(right?8u:2u,0,0,0,IntPtr.Zero);mouse_event(right?16u:4u,0,0,0,IntPtr.Zero);}
 public static Bitmap Capture(Rectangle r){var b=new Bitmap(r.Width,r.Height);using(var g=Graphics.FromImage(b))g.CopyFromScreen(r.X,r.Y,0,0,b.Size);return b;}
}
"@
$root=Split-Path $PSScriptRoot -Parent
$source=$env:HDFSTUDIO_DEMO_SAMPLE
if(-not $source -or -not(Test-Path -LiteralPath $source)){throw 'Set HDFSTUDIO_DEMO_SAMPLE'}
$variable=$env:HDFSTUDIO_DEMO_VARIABLE;if(-not $variable){$variable='TMP_2maboveground'}
$before=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$log=Join-Path $root ('logs/S4-TextureEntropy/demo-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $log|Out-Null
$uia=[System.Windows.Automation.AutomationElement];$process=$null;$window=$null;$frames=@()
function FindName($container,$name){,@($container.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.PropertyCondition]::new($uia::NameProperty,$name)))}
function AllControls($container){$container.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition)}
function Click($element,$right=$false){if(-not $element){throw 'Missing demo UI element'};$r=$element.Current.BoundingRectangle;if($r.IsEmpty -or $element.Current.IsOffscreen){throw ('Hidden demo control: '+$element.Current.Name)};[DemoCapture]::Click([int]($r.X+$r.Width/2),[int]($r.Y+$r.Height/2),$right);Start-Sleep -Milliseconds 200}
function SetText($element,$text){Click $element;[Windows.Forms.Clipboard]::SetText($text);[Windows.Forms.SendKeys]::SendWait('^a{BACKSPACE}');Start-Sleep -Milliseconds 100;[Windows.Forms.SendKeys]::SendWait('^v{TAB}');Start-Sleep -Milliseconds 300}
function LayerIndex {
 $label=AllControls $window|Where-Object {$_.Current.AutomationId.EndsWith('.layerLabel')}|Select-Object -First 1
 if($label.Current.Name -match '^Layer (\d+) of (\d+)$'){return [int]$Matches[1]-1}
 throw 'Layer label missing'
}
function Summary {return (AllControls $window|Where-Object {$_.Current.AutomationId.EndsWith('.textureEntropySummary')}|Select-Object -First 1).Current.Name}
function Capture($filename){
 $r=$window.Current.BoundingRectangle
 $info=((FindName $window 'File Info')|Where-Object {-not $_.Current.IsOffscreen}|Select-Object -First 1).Current.BoundingRectangle
 $height=if(-not $info.IsEmpty -and $info.Y -gt $r.Y+500){[int]($info.Y-$r.Y-5)}else{[int]($r.Height-30)}
 $bitmap=[DemoCapture]::Capture([Drawing.Rectangle]::new([int]$r.X,[int]$r.Y,[int]$r.Width,$height))
 try{$bitmap.Save($filename,[Drawing.Imaging.ImageFormat]::Png)}finally{$bitmap.Dispose()}
}
try {
 $env:QT_LOGGING_TO_CONSOLE='1'
 $process=Start-Process (Join-Path $root "build/output/$Configuration/HDFStudio.exe") -ArgumentList ('"'+$source+'"') -RedirectStandardError (Join-Path $log 'qt-plugin.log') -PassThru
 $processHandle=$process.Handle;Start-Sleep -Seconds 7
 $condition=[System.Windows.Automation.AndCondition]::new([System.Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id),[System.Windows.Automation.PropertyCondition]::new($uia::NameProperty,'HDFStudio'))
 $window=$uia::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children,$condition)
 if(-not $window -or (FindName $window $variable).Count -ne 1){throw 'Demo dataset not ready'}
 [DemoCapture]::Front([IntPtr]$window.Current.NativeWindowHandle);Start-Sleep -Milliseconds 500
 Click (FindName $window $variable)[0] $true;Click (FindName $uia::RootElement 'Image View')[0];Start-Sleep -Milliseconds 700
 $combo=(FindName $window 'CMaps')|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::ComboBox -and -not $_.Current.IsOffscreen}|Select-Object -First 1
 Click $combo;[Windows.Forms.SendKeys]::SendWait('{HOME}{ENTER}')
 for($index=0;$index -lt 25;$index++){
  $palette=$combo.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value
  if($palette -eq 'Thermal'){break}
  Click $combo;[Windows.Forms.SendKeys]::SendWait('{DOWN}{ENTER}');Start-Sleep -Milliseconds 120
 }
 if($palette -ne 'Thermal'){throw ('Thermal palette not selected: '+$palette)}
 Click (FindName $window 'View')[0];Click (FindName $uia::RootElement 'Texture Entropy Image')[0];Start-Sleep -Milliseconds 700
 Click (FindName $window 'Window')[0];Click (FindName $uia::RootElement 'Tile')[0];Start-Sleep -Milliseconds 700
 if((FindName $window 'Following Image View: layer and Mask').Count -ne 1){throw 'Demo texture is not following the source'}
 $fit=(FindName $window 'Fit')|Where-Object {-not $_.Current.IsOffscreen}
 foreach($item in $fit){Click $item}
 SetText (FindName $window 'Frame interval (ms)')[0] '800'
 $mainshot=Join-Path $log 'main.png';Capture $mainshot
 Click (FindName $window 'Play / pause layers')[0]
 [DemoCapture]::SetCursorPos(30,45)|Out-Null
 for($index=0;$index -lt 20;$index++){
  Start-Sleep -Milliseconds 550
  $layer=LayerIndex
  $filename=Join-Path $log ('frame-'+$index.ToString('00')+'.png');Capture $filename
  $frames+=[pscustomobject]@{file=$filename;layer=$layer}
 }
 Click (FindName $window 'Play / pause layers')[0]
 if(@($frames.layer|Select-Object -Unique).Count -lt 8){throw 'Demo did not animate enough distinct layers'}
 # Encode captured frames with the Windows WPF GIF encoder; no Python imaging packages required.
 $encoder=[Windows.Media.Imaging.GifBitmapEncoder]::new()
 foreach($frame in $frames){
  $image=[Windows.Media.Imaging.BitmapImage]::new();$image.BeginInit()
  $image.CacheOption=[Windows.Media.Imaging.BitmapCacheOption]::OnLoad
  $image.UriSource=[Uri]::new($frame.file);$image.DecodePixelWidth=1440;$image.EndInit();$image.Freeze()
  $metadata=[Windows.Media.Imaging.BitmapMetadata]::new('gif');$metadata.SetQuery('/grctlext/Delay',[uint16]65)
  $encoder.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($image,$null,$metadata,$null))
 }
 $gif=Join-Path $log 'demo-analysis.gif';$stream=[IO.File]::Open($gif,[IO.FileMode]::CreateNew)
 try{$encoder.Save($stream)}finally{$stream.Dispose()}
 # Loop forever using the standard GIF application extension, preserving every encoded frame.
 $bytes=[IO.File]::ReadAllBytes($gif)
 $start=13;if(($bytes[10] -band 128) -ne 0){$start+=3*[int][Math]::Pow(2,($bytes[10] -band 7)+1)}
 [byte[]]$loop=0x21,0xff,0x0b,0x4e,0x45,0x54,0x53,0x43,0x41,0x50,0x45,0x32,0x2e,0x30,0x03,0x01,0x00,0x00,0x00
 $joined=[byte[]]::new($bytes.Length+$loop.Length)
 [Array]::Copy($bytes,0,$joined,0,$start);[Array]::Copy($loop,0,$joined,$start,$loop.Length);[Array]::Copy($bytes,$start,$joined,$start+$loop.Length,$bytes.Length-$start)
 [IO.File]::WriteAllBytes($gif,$joined)
 Copy-Item $gif (Join-Path $root 'docs/demo-analysis.gif')
 $image=[Windows.Media.Imaging.BitmapImage]::new();$image.BeginInit();$image.CacheOption=[Windows.Media.Imaging.BitmapCacheOption]::OnLoad;$image.UriSource=[Uri]::new($mainshot);$image.DecodePixelWidth=1280;$image.EndInit()
 $pngEncoder=[Windows.Media.Imaging.PngBitmapEncoder]::new();$pngEncoder.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($image))
 $stream=[IO.File]::Open((Join-Path $root 'docs/screenshot-main.png'),[IO.FileMode]::Create)
 try{$pngEncoder.Save($stream)}finally{$stream.Dispose()}
 if((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $before){throw 'Demo input changed'}
 Click (FindName $window 'File')[0];Click (FindName $uia::RootElement 'Exit')[0]
 if(-not $process.WaitForExit(10000) -or $process.ExitCode -ne 0){throw 'Demo did not exit normally'}
 [pscustomobject]@{status='PASS';source=$source;source_sha256=$before;variable=$variable;palette=$palette;frames=$frames;gif=(Join-Path $root 'docs/demo-analysis.gif')}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $log 'result.json') -Encoding UTF8
 Write-Output ('PASS real capture: '+$log)
} catch {
 if($window){try{Capture (Join-Path $log 'failure.png')}catch{};AllControls $window|ForEach-Object {"$($_.Current.AutomationId) | $($_.Current.Name) | $($_.Current.BoundingRectangle)"}|Out-File (Join-Path $log 'controls.txt') -Encoding UTF8}
 Get-Process|Where-Object ProcessName -match 'HDFStudio'|Select-Object Id,Path,Responding|Out-File (Join-Path $log 'processes.txt')
 throw
} finally {
 if($process -and -not $process.HasExited){$null=$process.CloseMainWindow();if(-not $process.WaitForExit(3000)){Stop-Process -Id $process.Id}}
}
