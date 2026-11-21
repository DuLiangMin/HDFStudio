# Real-mouse zoom checks on a read-only NetCDF image, measuring colored image bounds.
# Input: HDFSTUDIO_ZOOM_SAMPLE; HDFSTUDIO_ZOOM_VARIABLE defaults to PRECT.
# Output: logs/S4-UI-Zoom/desktop-*.png and desktop-results-<configuration>.json.
# Run: powershell -NoProfile -ExecutionPolicy Bypass -File tests/desktop_zoom.ps1
param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Windows.Forms,System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System; using System.Runtime.InteropServices; using System.Drawing;
public class ZoomMouse {
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint x,uint y,uint d,IntPtr e);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int c);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern void keybd_event(byte v,byte s,uint f,IntPtr e);
 public static void Front(IntPtr h) {ShowWindow(h,3);keybd_event(0x12,0,0,IntPtr.Zero);keybd_event(0x12,0,2,IntPtr.Zero);SetForegroundWindow(h);}
 public static void Click(int x,int y,bool right) {SetCursorPos(x,y);mouse_event(right?8u:2u,0,0,0,IntPtr.Zero);mouse_event(right?16u:4u,0,0,0,IntPtr.Zero);}
 public static Rectangle ColoredBounds(int x,int y,int width,int height) {
  using(var image=new Bitmap(width,height)) {
   using(var graphics=Graphics.FromImage(image)) graphics.CopyFromScreen(x,y,0,0,image.Size);
   int left=width,top=height,right=-1,bottom=-1;
   for(int row=3;row<height-3;row++) for(int column=3;column<width-3;column++) {
    Color color=image.GetPixel(column,row);
    int largest=Math.Max(color.R,Math.Max(color.G,color.B)),smallest=Math.Min(color.R,Math.Min(color.G,color.B));
    if(largest-smallest<25) continue;
    left=Math.Min(left,column);right=Math.Max(right,column);top=Math.Min(top,row);bottom=Math.Max(bottom,row);
   }
   if(right<left||bottom<top) throw new Exception("No colored raster pixels in canvas capture");
   return new Rectangle(left,top,right-left+1,bottom-top+1);
  }
 }
}
"@
$root=Split-Path $PSScriptRoot -Parent
$sample=$env:HDFSTUDIO_ZOOM_SAMPLE
$variable=$env:HDFSTUDIO_ZOOM_VARIABLE
if(-not $variable){$variable='PRECT'}
if(-not $sample -or -not (Test-Path -LiteralPath $sample)){throw 'Set HDFSTUDIO_ZOOM_SAMPLE to a read-only NetCDF sample.'}
$beforeHash=(Get-FileHash -LiteralPath $sample -Algorithm SHA256).Hash
$log=Join-Path $root 'logs/S4-UI-Zoom'
New-Item -ItemType Directory -Force $log | Out-Null
$uia=[System.Windows.Automation.AutomationElement]
$results=@()
function FindName($container,$name){,@($container.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.PropertyCondition]::new($uia::NameProperty,$name)))}
function Check($name,$pass){
 $script:results+=[pscustomobject]@{check=$name;pass=[bool]$pass}
 "$(Get-Date -Format s) $name : $pass"
 if(-not $pass){throw $name}
}
function Click($element,$right=$false){
 if(-not $element){throw 'UI element not found'}
 $rect=$element.Current.BoundingRectangle
 if($rect.IsEmpty -or $element.Current.IsOffscreen){throw "UI element hidden: $($element.Current.Name)"}
 [ZoomMouse]::Click([int]($rect.X+$rect.Width/2),[int]($rect.Y+$rect.Height/2),$right)
 Start-Sleep -Milliseconds 150
}
function Shot($name){
 $bounds=[Windows.Forms.SystemInformation]::VirtualScreen
 $bitmap=[Drawing.Bitmap]::new($bounds.Width,$bounds.Height);$graphics=[Drawing.Graphics]::FromImage($bitmap)
 try{$graphics.CopyFromScreen($bounds.Location,[Drawing.Point]::Empty,$bounds.Size);$bitmap.Save((Join-Path $log "desktop-$name-$Configuration.png"))}
 finally{$graphics.Dispose();$bitmap.Dispose()}
}
function ImageBounds($canvas){
 $rect=$canvas.Current.BoundingRectangle
 [ZoomMouse]::ColoredBounds([int]$rect.X,[int]$rect.Y,[int]$rect.Width,[int]$rect.Height)
}
$process=$null
try {
 $process=Start-Process (Join-Path $root "build/output/$Configuration/HDFStudio.exe") -ArgumentList "`"$sample`"" -PassThru
 Start-Sleep -Seconds 8
 $condition=[System.Windows.Automation.AndCondition]::new([System.Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id),[System.Windows.Automation.PropertyCondition]::new($uia::NameProperty,'HDFStudio'))
 $window=$uia::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children,$condition)
 Check 'Main window found' ($null -ne $window)
 [ZoomMouse]::Front([IntPtr]$window.Current.NativeWindowHandle);Start-Sleep -Seconds 1
 Click (FindName $window $variable)[0] $true
 Click (FindName $uia::RootElement 'Image View')[0];Start-Sleep -Seconds 1
 $in=(FindName $window 'Zoom in')[0];$out=(FindName $window 'Zoom out')[0];$canvas=(FindName $window 'Raster image')[0]
 Check 'Zoom in visible' ($null -ne $in -and -not $in.Current.IsOffscreen)
 Check 'Zoom out visible' ($null -ne $out -and -not $out.Current.IsOffscreen)
 Check 'Canvas found' ($null -ne $canvas)
 Click (FindName $window 'Fit')[0]
 Click $out;Click $out
 $base=ImageBounds $canvas;Shot 'base'
 Click $in
 $large=ImageBounds $canvas;Shot 'in'
 Check 'Zoom in width 1.25x' ([Math]::Abs($large.Width/$base.Width-1.25) -lt .04)
 Check 'Zoom in height 1.25x' ([Math]::Abs($large.Height/$base.Height-1.25) -lt .04)
 Click $out
 $restored=ImageBounds $canvas;Shot 'out'
 Check 'Zoom out restores image size' ([Math]::Abs($restored.Width-$base.Width) -le 2 -and [Math]::Abs($restored.Height-$base.Height) -le 2)
 Click (FindName $window 'Flip H')[0]
 for($iteration=1;$iteration -le 5;$iteration++) {Click $in;Click $out}
 $flipped=ImageBounds $canvas
 Check 'Five flipped zoom cycles restore image size' ([Math]::Abs($flipped.Width-$base.Width) -le 2 -and [Math]::Abs($flipped.Height-$base.Height) -le 2)
 Check 'Input sample unchanged' ((Get-FileHash -LiteralPath $sample -Algorithm SHA256).Hash -eq $beforeHash)
 Shot 'flipped'
 [pscustomobject]@{base=$base;zoom_in=$large;restored=$restored;flipped=$flipped;sample_sha256=$beforeHash} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $log "desktop-geometry-$Configuration.json") -Encoding UTF8
} catch {
 try{Shot 'failure'}catch{$_ | Out-File (Join-Path $log "desktop-capture-error-$Configuration.txt")}
 Get-Process | Where-Object ProcessName -match 'HDFStudio|hdf_explorer' | Select-Object ProcessName,Id,Path,Responding | Out-File (Join-Path $log "desktop-processes-$Configuration.txt")
 Get-ChildItem (Join-Path $root "build/output/$Configuration") -Recurse -File | Select-Object FullName,Length | Out-File (Join-Path $log "desktop-deployment-$Configuration.txt")
 throw
} finally {
 $results | ConvertTo-Json | Set-Content (Join-Path $log "desktop-results-$Configuration.json") -Encoding UTF8
 if($process -and -not $process.HasExited){$null=$process.CloseMainWindow();if(-not $process.WaitForExit(3000)){Stop-Process -Id $process.Id}}
}
