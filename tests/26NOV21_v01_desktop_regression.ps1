# Real mouse regression: zoom geometry, flips, color cards, Reverse, Mask, layers, About, close/exit.
# Input: HDFSTUDIO_ZOOM_SAMPLE (read-only); HDFSTUDIO_ZOOM_VARIABLE defaults to PRECT.
# Output: logs/S4-FullRegression-20261121/desktop/<configuration>/*.png and results.json.
# Run: powershell -NoProfile -ExecutionPolicy Bypass -File tests/26NOV21_v01_desktop_regression.ps1
param([ValidateSet('Debug','Release')][string]$Configuration='Release',[string]$LogDirectory='')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Windows.Forms,System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System; using System.Runtime.InteropServices; using System.Drawing;
public class FullRegressionMouse {
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint x,uint y,uint d,IntPtr e);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int c);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern void keybd_event(byte v,byte s,uint f,IntPtr e);
 public static void Front(IntPtr h) {ShowWindow(h,3);keybd_event(0x12,0,0,IntPtr.Zero);keybd_event(0x12,0,2,IntPtr.Zero);SetForegroundWindow(h);}
 public static void Click(int x,int y,bool right) {SetCursorPos(x,y);mouse_event(right?8u:2u,0,0,0,IntPtr.Zero);mouse_event(right?16u:4u,0,0,0,IntPtr.Zero);}
 public static Rectangle ColoredBounds(int x,int y,int width,int height) {
  using(var image=Capture(x,y,width,height)) {
   int left=width,top=height,right=-1,bottom=-1;
   for(int row=3;row<height-3;row++) for(int col=3;col<width-3;col++) {
    Color color=image.GetPixel(col,row);
    if(Math.Max(color.R,Math.Max(color.G,color.B))-Math.Min(color.R,Math.Min(color.G,color.B))<25) continue;
    left=Math.Min(left,col);right=Math.Max(right,col);top=Math.Min(top,row);bottom=Math.Max(bottom,row);
   }
   if(right<left) throw new Exception("No colored pixels in canvas");
   return new Rectangle(left,top,right-left+1,bottom-top+1);
  }
 }
 public static Bitmap Capture(int x,int y,int w,int h) {var image=new Bitmap(w,h);using(var g=Graphics.FromImage(image))g.CopyFromScreen(x,y,0,0,image.Size);return image;}
 public static bool Same(Bitmap a,Bitmap b) {if(a.Size!=b.Size)return false;for(int y=0;y<a.Height;y++)for(int x=0;x<a.Width;x++)if(a.GetPixel(x,y)!=b.GetPixel(x,y))return false;return true;}
 public static bool Flipped(Bitmap a,Bitmap b) {a.RotateFlip(RotateFlipType.RotateNoneFlipX);return Same(a,b);}
}
"@
$root=Split-Path $PSScriptRoot -Parent
$version=([regex]'project\(HDFStudio VERSION ([0-9.]+)').Match([IO.File]::ReadAllText((Join-Path $root 'CMakeLists.txt'))).Groups[1].Value
$sample=$env:HDFSTUDIO_ZOOM_SAMPLE
$variable=$env:HDFSTUDIO_ZOOM_VARIABLE
if(-not $variable){$variable='PRECT'}
if(-not $sample -or -not(Test-Path -LiteralPath $sample)){throw 'Set HDFSTUDIO_ZOOM_SAMPLE to a read-only NetCDF sample'}
$before=(Get-FileHash -LiteralPath $sample -Algorithm SHA256).Hash
$log=if($LogDirectory){[IO.Path]::GetFullPath((Join-Path $root $LogDirectory))}else{Join-Path $root "logs/S4-FullRegression-20261121/desktop/$Configuration"}
New-Item -ItemType Directory -Force $log | Out-Null
$uia=[System.Windows.Automation.AutomationElement]
$results=@()
function FindName($container,$name){,@($container.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.PropertyCondition]::new($uia::NameProperty,$name)))}
function Names($container){@($container.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition) | ForEach-Object {$_.Current.Name})}
function Check($name,$pass){$script:results+=[pscustomobject]@{check=$name;pass=[bool]$pass};"$(Get-Date -Format s) $name : $pass";if(-not $pass){throw $name}}
function Click($element,$right=$false){
 if(-not $element){throw 'UI element missing'}
 $r=$element.Current.BoundingRectangle
 if($r.IsEmpty -or $element.Current.IsOffscreen){throw "UI element hidden: $($element.Current.Name)"}
 [FullRegressionMouse]::Click([int]($r.X+$r.Width/2),[int]($r.Y+$r.Height/2),$right)
 Start-Sleep -Milliseconds 180
}
function Shot($name){$r=[Windows.Forms.SystemInformation]::VirtualScreen;$b=[FullRegressionMouse]::Capture($r.X,$r.Y,$r.Width,$r.Height);try{$b.Save((Join-Path $log "$name.png"))}finally{$b.Dispose()}}
function CaptureElement($element){$r=$element.Current.BoundingRectangle;[FullRegressionMouse]::Capture([int]$r.X,[int]$r.Y,[int]$r.Width,[int]$r.Height)}
function Bounds($element){$r=$element.Current.BoundingRectangle;[FullRegressionMouse]::ColoredBounds([int]$r.X,[int]$r.Y,[int]$r.Width,[int]$r.Height)}
function SetText($element,$text){Click $element;[Windows.Forms.SendKeys]::SendWait('^a{BACKSPACE}');if($text){[Windows.Forms.SendKeys]::SendWait($text)};[Windows.Forms.SendKeys]::SendWait('{TAB}');Start-Sleep -Milliseconds 350}
$process=$null
try {
 $env:QT_DEBUG_PLUGINS='1'
 $env:QT_LOGGING_TO_CONSOLE='1'
 $process=Start-Process (Join-Path $root "build/output/$Configuration/HDFStudio.exe") -ArgumentList "`"$sample`"" -RedirectStandardError (Join-Path $log 'qt-plugin.log') -PassThru
 $processHandle=$process.Handle
 Start-Sleep -Seconds 8
 $condition=[System.Windows.Automation.AndCondition]::new([System.Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id),[System.Windows.Automation.PropertyCondition]::new($uia::NameProperty,'HDFStudio'))
 $window=$uia::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children,$condition)
 Check 'Main window visible' ($null -ne $window)
 [FullRegressionMouse]::Front([IntPtr]$window.Current.NativeWindowHandle);Start-Sleep -Seconds 1
 Click (FindName $window $variable)[0] $true;Click (FindName $uia::RootElement 'Image View')[0];Start-Sleep -Seconds 2
 # Maximize the MDI image with actual title-bar clicks so both Mask fields are outside overflow.
 $imageWindow=(FindName $window ('/'+$variable+' - Image'))[0]
 if($imageWindow){$r=$imageWindow.Current.BoundingRectangle;[FullRegressionMouse]::Click([int]($r.X+$r.Width/2),[int]($r.Y+13),$false);[FullRegressionMouse]::Click([int]($r.X+$r.Width/2),[int]($r.Y+13),$false);Start-Sleep -Milliseconds 400}
 $canvas=(FindName $window 'Raster image')[0];Check 'Raster canvas visible' ($null -ne $canvas -and -not $canvas.Current.IsOffscreen)
 Click (FindName $window 'Fit')[0];Click (FindName $window 'Zoom out')[0];Click (FindName $window 'Zoom out')[0]
 $base=Bounds $canvas;Shot 'zoom-base';Click (FindName $window 'Zoom in')[0];$large=Bounds $canvas;Shot 'zoom-in'
 Check 'Zoom 1.25x geometry' ([Math]::Abs($large.Width/$base.Width-1.25)-lt .04 -and [Math]::Abs($large.Height/$base.Height-1.25)-lt .04)
 Click (FindName $window 'Zoom out')[0];$restored=Bounds $canvas
 Check 'Zoom out inverse geometry' ([Math]::Abs($restored.Width-$base.Width)-le 2 -and [Math]::Abs($restored.Height-$base.Height)-le 2)
 Click (FindName $window 'Flip H')[0];for($i=0;$i-lt 5;$i++){Click (FindName $window 'Zoom in')[0];Click (FindName $window 'Zoom out')[0]}
 $flipped=Bounds $canvas;Check 'Flip and 5 zoom cycles preserve bounds' ([Math]::Abs($flipped.Width-$base.Width)-le 2 -and [Math]::Abs($flipped.Height-$base.Height)-le 2)
 Shot 'flipped';Click (FindName $window 'Reset')[0]
 $combo=@((FindName $window 'CMaps')|Where-Object {$_.Current.ControlType-eq [System.Windows.Automation.ControlType]::ComboBox})[0]
 Check 'Projs action retained' ((FindName $window 'Projs').Count-gt 0)
 Click $combo;[Windows.Forms.SendKeys]::SendWait('{HOME}{DOWN 5}{ENTER}');Start-Sleep -Milliseconds 400
 Check 'Viridis standard selected' ($combo.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value-eq 'Viridis')
 $original=CaptureElement $canvas;Shot 'viridis'
 $reverse=(FindName $window 'Reverse')[0];Click $reverse
 $toggle=$reverse.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern).Current.ToggleState
 Check 'Reverse checkbox toggled' ($toggle-eq [System.Windows.Automation.ToggleState]::On)
 $reversed=CaptureElement $canvas;Check 'Reverse changes actual pixels' (-not [FullRegressionMouse]::Same($original,$reversed));Shot 'reverse'
 Click $reverse;$again=CaptureElement $canvas;Check 'Reverse roundtrip restores exact pixels' ([FullRegressionMouse]::Same($original,$again));$original.Dispose();$reversed.Dispose();$again.Dispose()
 Check 'Colorbar visible' ((FindName $window 'Colorbar').Count-gt 0)
 $legendBefore=@(Names $window|Where-Object {$_.StartsWith('147 x 115') -or $_.Contains('Missing: grey')})
 SetText (FindName $window 'Mask start')[0] '0';SetText (FindName $window 'Mask end')[0] '0'
 Check 'Mask count displayed' (@(Names $window|Where-Object {$_.Contains('Masked [0, 0] (decoded):')}).Count-gt 0);Shot 'mask'
 SetText (FindName $window 'Mask start')[0] '';SetText (FindName $window 'Mask end')[0] ''
 Check 'Mask cleared' (@(Names $window|Where-Object {$_.Contains('Masked [')}).Count-eq 0)
 Click (FindName $window 'Next Layer')[0];Check 'Next layer state' ((FindName $window 'Layer 2 of 60').Count-gt 0);Shot 'next-layer'
 Click (FindName $window 'First Layer')[0];Check 'First layer state restored' ((FindName $window 'Layer 1 of 60').Count-gt 0)
 Click $combo;[Windows.Forms.SendKeys]::SendWait('{END}{ENTER}');Check 'NCL palette selected' ($combo.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value.Contains('BlueWhiteOrangeRed'));Shot 'ncl'
 Click (FindName $window 'Color map options')[0];Click (FindName $uia::RootElement ('Color cards and mapping'+[char]0x2026))[0]
 Check 'Mapping options opened' ((FindName $uia::RootElement 'Normalization').Count-gt 0);Shot 'mapping';[Windows.Forms.SendKeys]::SendWait('{ESC}')
 Click (FindName $window 'Help')[0];Click (FindName $uia::RootElement 'About')[0];Start-Sleep -Milliseconds 350
 $processWindows=@($uia::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children,[System.Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id)))
 $all=@();foreach($w in $processWindows){$all+=Names $w}
  Check ('About version '+$version) (@($all|Where-Object {$_.Contains('HDFStudio '+$version)}).Count-gt 0);Shot 'about';[Windows.Forms.SendKeys]::SendWait('{ESC}')
 Click (FindName $window 'File')[0];Click (FindName $uia::RootElement 'Close File')[0];Start-Sleep -Milliseconds 500
 Check 'File closed removes dataset' ((FindName $window $variable).Count-eq 0)
 Check 'File closed removes raster view' ((FindName $window 'Raster image').Count-eq 0);Shot 'closed'
 Check 'Read-only input hash unchanged' ((Get-FileHash -LiteralPath $sample -Algorithm SHA256).Hash-eq $before)
 $null=$process.CloseMainWindow();Check 'Normal exit within 5 seconds' ($process.WaitForExit(5000));$process.Refresh();$exitCode=$process.ExitCode
 [pscustomobject]@{pid=$process.Id;exit_code=$exitCode;has_exited=$process.HasExited} | ConvertTo-Json | Set-Content (Join-Path $log 'exit.json') -Encoding UTF8
 Check 'Exit code zero' ($null-ne $exitCode -and $exitCode-eq 0)
 [pscustomobject]@{base=$base;zoom_in=$large;restored=$restored;flipped=$flipped;sample=$sample;sha256=$before} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $log 'geometry.json') -Encoding UTF8
}catch{
 try{Shot 'failure'}catch{$_|Out-File (Join-Path $log 'capture-error.txt')}
 Get-Process HDFStudio -ErrorAction SilentlyContinue | Select-Object Id,Path,Responding,WorkingSet64,PrivateMemorySize64 | ConvertTo-Json | Out-File (Join-Path $log 'processes.json')
 Get-ChildItem (Join-Path $root "build/output/$Configuration") -Recurse -File | Select-Object FullName,Length | ConvertTo-Json | Out-File (Join-Path $log 'deployment.json')
 throw
}finally{
 $results|ConvertTo-Json|Set-Content (Join-Path $log 'results.json') -Encoding UTF8
 if($process -and -not $process.HasExited){$null=$process.CloseMainWindow();if(-not $process.WaitForExit(3000)){Stop-Process -Id $process.Id}}
}
