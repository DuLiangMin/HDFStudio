# Focused real desktop acceptance for the GRIB tree/properties gate.
# Inputs: build/output/<Configuration>/HDFStudio.exe, read-only testdata/grib/mini_cube.grb2.
# Output: logs/S4-GribIntegration/desktop-<stage>-<Configuration>-<timestamp>/.
# Usage: powershell -NoProfile -File tests/26NOV23_v01_grib_browser_desktop.ps1 [-Grid] [-Image | -Histogram | -Texture | -Profile | -Compare | -Playback] [-ExternalGrib1 | -ExternalGrib2]
param([ValidateSet('Debug','Release')][string]$Configuration='Release',[switch]$Grid,[switch]$Image,[switch]$Histogram,[switch]$Texture,[switch]$Profile,[switch]$Compare,[switch]$Playback,[switch]$ExternalGrib1,[switch]$ExternalGrib2)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Drawing,System.Windows.Forms
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Text;
using System.Runtime.InteropServices;
public static class GribBrowserMouse {
  [DllImport("user32.dll",SetLastError=true)]public static extern IntPtr OpenInputDesktop(uint flags,bool inherit,uint access);
  [DllImport("user32.dll")]public static extern bool CloseDesktop(IntPtr handle);
  [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr handle);
  [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")]public static extern bool BringWindowToTop(IntPtr handle);
  [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr handle,out uint processId);
  [DllImport("kernel32.dll")]public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")]public static extern bool AttachThreadInput(uint thread,uint target,bool attach);
  [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr handle,int command);
  [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
  [DllImport("user32.dll")]public static extern void mouse_event(uint flags,uint x,uint y,uint data,IntPtr extra);
  [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr handle,uint message,IntPtr wparam,IntPtr lparam);
  [DllImport("user32.dll")]public static extern void keybd_event(byte key,byte scan,uint flags,IntPtr extra);
  [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr handle,int id);
  public delegate bool ChildCallback(IntPtr handle,IntPtr parameter);
  [DllImport("user32.dll")]public static extern bool EnumChildWindows(IntPtr handle,ChildCallback callback,IntPtr parameter);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetClassName(IntPtr handle,StringBuilder text,int count);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr SendMessage(IntPtr handle,uint message,IntPtr count,StringBuilder text);
  [DllImport("user32.dll",EntryPoint="SendMessageW")]public static extern IntPtr SendNativeMessage(IntPtr handle,uint message,IntPtr wparam,IntPtr lparam);
  [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr handle,out NativeRect rectangle);
  public struct NativeRect{public int Left,Top,Right,Bottom;}
  public static IntPtr FileEdit(IntPtr dialog,int left,int top,int right,int bottom){
    IntPtr found=IntPtr.Zero;
    EnumChildWindows(dialog,(handle,parameter)=>{var name=new StringBuilder(128);GetClassName(handle,name,128);NativeRect rectangle;GetWindowRect(handle,out rectangle);if(name.ToString()=="Edit"&&rectangle.Left>=left&&rectangle.Top>=top&&rectangle.Right<=right&&rectangle.Bottom<=bottom){found=handle;return false;}return true;},IntPtr.Zero);
    return found;
  }
  public static string ReadText(IntPtr handle){var text=new StringBuilder(32768);SendMessage(handle,0x000D,(IntPtr)text.Capacity,text);return text.ToString();}
  public static void Front(IntPtr handle){
    uint processId;uint foregroundThread=GetWindowThreadProcessId(GetForegroundWindow(),out processId);
    uint currentThread=GetCurrentThreadId();bool attached=false;
    try{
      if(foregroundThread!=0&&foregroundThread!=currentThread)attached=AttachThreadInput(currentThread,foregroundThread,true);
      BringWindowToTop(handle);SetForegroundWindow(handle);
    }finally{if(attached)AttachThreadInput(currentThread,foregroundThread,false);}
  }
  public static void Click(int x,int y){SetCursorPos(x,y);mouse_event(2,0,0,0,IntPtr.Zero);System.Threading.Thread.Sleep(50);mouse_event(4,0,0,0,IntPtr.Zero);}
  public static void RightClick(int x,int y){SetCursorPos(x,y);mouse_event(8,0,0,0,IntPtr.Zero);mouse_event(16,0,0,0,IntPtr.Zero);}
  public static int ColoredPixels(int x,int y,int width,int height){
    using(var image=new Bitmap(width,height))using(var graphics=Graphics.FromImage(image)){
      graphics.CopyFromScreen(x,y,0,0,image.Size);int count=0;
      for(int row=5;row<height-5;row+=3)for(int column=5;column<width-5;column+=3){
        var color=image.GetPixel(column,row);
        if(Math.Max(color.R,Math.Max(color.G,color.B))-Math.Min(color.R,Math.Min(color.G,color.B))>25)count++;
      }return count;
    }
  }
}
'@
$root=Split-Path -Parent $PSScriptRoot
$source=Join-Path $root 'testdata/grib/mini_cube.grb2'
if($ExternalGrib1){$source=$env:HDFSTUDIO_GRIB1_SAMPLE;if(-not $source-or -not(Test-Path -LiteralPath $source)){throw 'Set HDFSTUDIO_GRIB1_SAMPLE for external acceptance'}}
if($ExternalGrib2){$source=$env:HDFSTUDIO_GRIB2_SAMPLE;if(-not $source-or -not(Test-Path -LiteralPath $source)){throw 'Set HDFSTUDIO_GRIB2_SAMPLE for external acceptance'}}
if($ExternalGrib1-and $ExternalGrib2){throw 'Select one external sample per desktop run'}
if(([int]$Image.IsPresent+[int]$Histogram.IsPresent+[int]$Texture.IsPresent+[int]$Profile.IsPresent+[int]$Compare.IsPresent+[int]$Playback.IsPresent)-gt 1){throw 'Select one analysis gate per desktop run'}
$executable=Join-Path $root "build/output/$Configuration/HDFStudio.exe"
$external=$ExternalGrib1-or $ExternalGrib2
$stage=if($Image){if($ExternalGrib1){'image-grib1'}elseif($ExternalGrib2){'image-grib2'}else{'image'}}elseif($ExternalGrib1){'grib1'}elseif($ExternalGrib2){'grib2'}elseif($Grid){'grid'}else{'browser'}
if($Histogram){$stage=if($ExternalGrib1){'histogram-grib1'}elseif($ExternalGrib2){'histogram-grib2'}else{'histogram'}}
if($Texture){$stage=if($ExternalGrib1){'texture-grib1'}elseif($ExternalGrib2){'texture-grib2'}else{'texture'}}
if($Profile){$stage=if($ExternalGrib1){'profile-grib1'}elseif($ExternalGrib2){'profile-grib2'}else{'profile'}}
if($Compare){$stage=if($ExternalGrib1){'compare-grib1'}elseif($ExternalGrib2){'compare-grib2'}else{'compare'}}
if($Playback){$stage=if($ExternalGrib1){'playback-grib1'}elseif($ExternalGrib2){'playback-grib2'}else{'playback'}}
$log=Join-Path $root ('logs/S4-GribIntegration/desktop-'+$stage+'-'+$Configuration+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $log | Out-Null
$before=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$uia=[Windows.Automation.AutomationElement]
$checks=@();$process=$null;$window=$null;$completed=$false
function Controls($container){$container.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)}
function ProcessMenuItems(){
  $processCondition=[Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id)
  $menuCondition=[Windows.Automation.PropertyCondition]::new($uia::ControlTypeProperty,[Windows.Automation.ControlType]::MenuItem)
  $containers=$uia::RootElement.FindAll([Windows.Automation.TreeScope]::Children,$processCondition)
  foreach($container in $containers){try{$container.FindAll([Windows.Automation.TreeScope]::Descendants,$menuCondition)}catch [Windows.Automation.ElementNotAvailableException]{continue}}
}
function Named($container,$prefix){
  $exact=$container.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new($uia::NameProperty,$prefix))
  if($exact){return $exact}
  Controls $container | Where-Object {$_.Current.Name.StartsWith($prefix)} | Select-Object -First 1
}
function Check($name,$passed){$script:checks+=[pscustomobject]@{check=$name;pass=[bool]$passed};Write-Output ("$(Get-Date -Format s) $name : $passed");if(-not $passed){throw $name}}
function Expand($element){
  if(-not $element){throw 'Tree item missing'}
  $name=$element.Current.Name
  $pattern=$element.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern)
  if($pattern.Current.ExpandCollapseState-ne [Windows.Automation.ExpandCollapseState]::Expanded){
    $bounds=$element.Current.BoundingRectangle
    [GribBrowserMouse]::Click([int]($bounds.Left-10),[int]($bounds.Top+$bounds.Height/2))
  }
  $clock=[Diagnostics.Stopwatch]::StartNew()
  $expanded=$false
  do{
    # Qt invalidates its accessibility item proxies as tree rows are inserted.
    $fresh=Named $window $name
    if($fresh){$current=$fresh.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern);$expanded=$current.Current.ExpandCollapseState-eq [Windows.Automation.ExpandCollapseState]::Expanded}
    if($expanded){break};Start-Sleep -Milliseconds 100
  }while($clock.ElapsedMilliseconds-lt 3000)
  Check ('Expanded '+$name) $expanded
}
function Shot($name){$bounds=[Windows.Forms.SystemInformation]::VirtualScreen;$bitmap=[Drawing.Bitmap]::new($bounds.Width,$bounds.Height);$graphics=[Drawing.Graphics]::FromImage($bitmap);try{$graphics.CopyFromScreen($bounds.X,$bounds.Y,0,0,$bitmap.Size);$bitmap.Save((Join-Path $log ($name+'.png')))}finally{$graphics.Dispose();$bitmap.Dispose()}}
function InvokeNamed($name){$element=Named $window $name;if(-not $element){throw ('Control missing: '+$name)};$element.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()}
function SetValue($name,$text){$element=Named $window $name;$element.SetFocus();$pattern=$element.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern);$pattern.SetValue($text);Check ($name+' value readback') ($pattern.Current.Value-eq $text);[Windows.Forms.SendKeys]::SendWait('{TAB}')}
function ReadMetrics($element){
  try{return $element.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value}
  catch{return $element.GetCurrentPattern([Windows.Automation.TextPattern]::Pattern).DocumentRange.GetText(-1)}
}
function ProcessWindow($title){
  $candidates=$uia::RootElement.FindAll([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id))
  $condition=[Windows.Automation.AndCondition]::new([Windows.Automation.PropertyCondition]::new($uia::NameProperty,$title),[Windows.Automation.PropertyCondition]::new($uia::ControlTypeProperty,[Windows.Automation.ControlType]::Window))
  foreach($candidate in $candidates){if($candidate.Current.Name-eq $title){return $candidate};$child=$candidate.FindFirst([Windows.Automation.TreeScope]::Descendants,$condition);if($child){return $child}}
}
function SaveNative($element,$title,$path){
  $bounds=$element.Current.BoundingRectangle
  [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
  $timer=[Diagnostics.Stopwatch]::StartNew();$dialog=$null
  do{$dialog=ProcessWindow $title;if($dialog){break};Start-Sleep -Milliseconds 100}while($timer.ElapsedMilliseconds-lt 5000)
  Check ($title+' dialog visible') ($null-ne $dialog)
  $hostEdit=Controls $dialog|Where-Object {$_.Current.AutomationId-eq 'FileNameControlHost'-or $_.Current.AutomationId-eq '1148'}|Select-Object -First 1
  $bounds=$hostEdit.Current.BoundingRectangle
  $editHandle=[GribBrowserMouse]::FileEdit([IntPtr]$dialog.Current.NativeWindowHandle,[int]$bounds.Left,[int]$bounds.Top,[int]$bounds.Right,[int]$bounds.Bottom)
  Check ($title+' native filename edit found') ($editHandle-ne [IntPtr]::Zero)
  [GribBrowserMouse]::Front([IntPtr]$dialog.Current.NativeWindowHandle)
  [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
  [Windows.Forms.Clipboard]::SetText($path)
  $timer.Restart();do{if([Windows.Forms.Clipboard]::GetText()-eq $path){break};Start-Sleep -Milliseconds 100}while($timer.ElapsedMilliseconds-lt 3000)
  Check ($title+' clipboard path readback') ([Windows.Forms.Clipboard]::GetText()-eq $path)
  # Send selection and paste to the resolved native Edit, independent of SendKeys focus timing.
  [GribBrowserMouse]::SendNativeMessage($editHandle,0x00B1,[IntPtr]::Zero,[IntPtr](-1))|Out-Null
  [GribBrowserMouse]::SendNativeMessage($editHandle,0x0302,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  $timer.Restart();do{if([GribBrowserMouse]::ReadText($editHandle)-eq $path){break};Start-Sleep -Milliseconds 100}while($timer.ElapsedMilliseconds-lt 3000)
  Check ($title+' complete path readback') ([GribBrowserMouse]::ReadText($editHandle)-eq $path)
  $saveHandle=[GribBrowserMouse]::GetDlgItem([IntPtr]$dialog.Current.NativeWindowHandle,1)
  [GribBrowserMouse]::PostMessage($saveHandle,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  $timer.Restart();do{if(Test-Path -LiteralPath $path){break};Start-Sleep -Milliseconds 100}while($timer.ElapsedMilliseconds-lt 5000)
  Check ($title+' saved') (Test-Path -LiteralPath $path)
}
try {
  $desktop=[GribBrowserMouse]::OpenInputDesktop(0,$false,0x0100)
  if($desktop-eq [IntPtr]::Zero){throw ('Interactive desktop unavailable: '+[Runtime.InteropServices.Marshal]::GetLastWin32Error())}
  [GribBrowserMouse]::CloseDesktop($desktop)|Out-Null
  $env:QT_LOGGING_TO_CONSOLE='1'
  $process=Start-Process -FilePath $executable -ArgumentList ('"'+$source+'"') -PassThru -RedirectStandardOutput (Join-Path $log 'stdout.txt') -RedirectStandardError (Join-Path $log 'qt.txt')
  $handle=$process.Handle;$clock=[Diagnostics.Stopwatch]::StartNew()
  do {
    $candidates=$uia::RootElement.FindAll([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id))
    foreach($candidate in $candidates){try{if(Named $candidate 'Variables [virtual arrays]'){$window=$candidate;break}}catch [Windows.Automation.ElementNotAvailableException]{continue}}
    if($window){break};Start-Sleep -Milliseconds 150
  }while($clock.ElapsedMilliseconds-lt 15000)
  Check 'Native GRIB tree loaded' ($null-ne $window)
  [GribBrowserMouse]::ShowWindow([IntPtr]$window.Current.NativeWindowHandle,3)|Out-Null
  [GribBrowserMouse]::SetForegroundWindow([IntPtr]$window.Current.NativeWindowHandle)|Out-Null
  if($external){
    $variables=Named $window 'Variables [virtual arrays]';$bounds=$variables.Current.BoundingRectangle
    [GribBrowserMouse]::Click([int]($bounds.Left-10),[int]($bounds.Top+$bounds.Height/2))
    Start-Sleep -Milliseconds 200
  }
  Expand (Named $window 'Messages [physical order')
  Expand (Named $window 'Messages 0-')
  Expand (Named $window 'Message 0 [')
  $field=Named $window 'Field 0 [sub=0]';Check 'Physical field visible' ($field-and -not $field.Current.IsOffscreen)
  Shot 'tree'
  [GribBrowserMouse]::Front([IntPtr]$window.Current.NativeWindowHandle)
  $field=Named $window 'Field 0 [sub=0]';$field.SetFocus()
  $bounds=$field.Current.BoundingRectangle
  [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
  Start-Sleep -Milliseconds ([Windows.Forms.SystemInformation]::DoubleClickTime+50)
  [GribBrowserMouse]::Front([IntPtr]$window.Current.NativeWindowHandle)
  if([GribBrowserMouse]::GetForegroundWindow()-ne [IntPtr]$window.Current.NativeWindowHandle){throw 'Target window is not foreground before double-click'}
  [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2));Start-Sleep -Milliseconds 60
  [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
  $dialog=$null;$clock.Restart()
  do{$dialog=Named $window 'Properties - Field 0';if($dialog){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
  Check 'Double-click opened Properties' ($null-ne $dialog)
  $copy=Named $dialog 'Copy All';$pattern=$copy.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern);$pattern.Invoke()
  $clock.Restart();$text=''
  do{$text=[Windows.Forms.Clipboard]::GetText();if($text.Contains('Object: /field/0')-and $text.Contains('sub-field: 0; offset: 0; length:')){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 3000)
  [IO.File]::WriteAllText((Join-Path $log 'properties.txt'),$text,[Text.UTF8Encoding]::new($false))
  Check 'Properties preserves field locator' ($text.Contains('Object: /field/0')-and $text.Contains('sub-field: 0; offset: 0; length:'))
  Shot 'properties'
  $close=Named $dialog 'Close';$close.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke();Start-Sleep -Milliseconds 100
  if($Grid){
    $field=Named $window 'Field 0 [sub=0]';$bounds=$field.Current.BoundingRectangle
    [GribBrowserMouse]::RightClick([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    $menu=$null;$clock.Restart()
    do{$menu=Controls $uia::RootElement|Where-Object {$_.Current.ProcessId-eq $process.Id-and $_.Current.ControlType-eq [Windows.Automation.ControlType]::MenuItem-and $_.Current.Name.StartsWith('Data Grid (decoded)')-and -not $_.Current.IsOffscreen}|Select-Object -Last 1;if($menu){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'GRIB Data Grid menu enabled' ($menu-and $menu.Current.IsEnabled)
    $bounds=$menu.Current.BoundingRectangle
    [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    $grid_element=$null;$clock.Restart()
    do{$grid_element=$window.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new($uia::NameProperty,'GRIB data grid cells'));if($grid_element){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 10000)
    Check 'Decoded grid visible' ($null-ne $grid_element)
    [GribBrowserMouse]::Front([IntPtr]$window.Current.NativeWindowHandle)
    $grid_element=Named $window 'GRIB data grid cells'
    $grid_pattern=$grid_element.GetCurrentPattern([Windows.Automation.GridPattern]::Pattern)
    $rows=if($ExternalGrib1){361}elseif($ExternalGrib2){181}else{3};$cols=if($ExternalGrib1){720}elseif($ExternalGrib2){360}else{4}
    Check 'Decoded grid shape' ($grid_pattern.Current.RowCount-eq $rows-and $grid_pattern.Current.ColumnCount-eq $cols)
    $value=$grid_pattern.GetItem(0,0);$bounds=$value.Current.BoundingRectangle
    Write-Output ('Decoded cell bounds: '+$bounds.ToString())
    [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    Start-Sleep -Milliseconds 150
    [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    $clock.Restart();$copy=$null
    do{$copy=Named $window 'Copy selection';if($copy-and $copy.Current.IsEnabled){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 3000)
    Check 'Cell selection enables Copy' ($copy-and $copy.Current.IsEnabled)
    $copy.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke();Start-Sleep -Milliseconds 100
    $copied=[Windows.Forms.Clipboard]::GetText()
    [IO.File]::WriteAllText((Join-Path $log 'selected-value.txt'),$copied,[Text.UTF8Encoding]::new($false))
    if($external){
      $cases_path=if($ExternalGrib1){$env:HDFSTUDIO_GRIB1_CASES}else{$env:HDFSTUDIO_GRIB2_CASES}
      $cases=Get-Content -Raw -Encoding UTF8 $cases_path|ConvertFrom-Json
      $expected=[double]$cases.fields[0].probes[0].value
      $actual=[double]::Parse($copied,[Globalization.CultureInfo]::InvariantCulture)
      Check 'Mouse cell copy matches independent GRIB reference' ($actual-eq $expected)
    }else{Check 'Mouse cell selection copies decoded value' ($copied-eq '50000')}
    Shot 'decoded-grid'
  }
  if($Image){
    $field=Named $window 'Field 0 [sub=0]';$bounds=$field.Current.BoundingRectangle
    [GribBrowserMouse]::RightClick([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    $menu=$null;$clock.Restart()
    do{$menu=Controls $uia::RootElement|Where-Object {$_.Current.ProcessId-eq $process.Id-and $_.Current.ControlType-eq [Windows.Automation.ControlType]::MenuItem-and $_.Current.Name-eq 'Image View'-and -not $_.Current.IsOffscreen}|Select-Object -Last 1;if($menu){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'GRIB Image View menu enabled' ($menu-and $menu.Current.IsEnabled)
    $menu.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    $canvas=$null;$legend=$null;$clock.Restart()
    $shape=if($ExternalGrib1){'720 x 361'}elseif($ExternalGrib2){'360 x 181'}else{'4 x 3'}
    do{$canvas=Named $window 'Raster image';$legend=Named $window $shape;if($canvas-and $legend){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 15000)
    Check 'GRIB image canvas and shape visible' ($canvas-and $legend-and -not $canvas.Current.IsOffscreen)
    $imageWindow=Named $window '/field/0 - Image';$bounds=$imageWindow.Current.BoundingRectangle
    [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+13))
    [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+13))
    Start-Sleep -Milliseconds 200
    $canvas=Named $window 'Raster image'
    [GribBrowserMouse]::Front([IntPtr]$window.Current.NativeWindowHandle)
    $painted=0;$clock.Restart()
    do{$bounds=$canvas.Current.BoundingRectangle;$painted=[GribBrowserMouse]::ColoredPixels([int]$bounds.X,[int]$bounds.Y,[int]$bounds.Width,[int]$bounds.Height);if($painted-gt 50){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'GRIB canvas actually painted' ($painted-gt 50)
    Shot 'image-painted'
    $projs=Named $window 'Projs';Check 'Unverified geographic lookup disabled' ($projs-and -not $projs.Current.IsEnabled)
    $reverse=Named $window 'Reverse';$toggle=$reverse.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern);$old=$toggle.Current.ToggleState;$toggle.Toggle()
    Check 'Reverse colors toggles' ($toggle.Current.ToggleState-ne $old);$toggle.Toggle()
    InvokeNamed 'Flip H';InvokeNamed 'Flip V';InvokeNamed 'Reset'
    SetValue 'Mask start' '0';SetValue 'Mask end' '0'
    $masked=$null;$clock.Restart()
    do{$masked=Controls $window|Where-Object {$_.Current.Name.Contains('Masked [0, 0] (decoded):')}|Select-Object -First 1;if($masked){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Mask uses decoded values' ($null-ne $masked)
    SetValue 'Mask start' '';SetValue 'Mask end' ''
    Shot 'image-restored'
    $field=Named $window 'Field 0 [sub=0]';$bounds=$field.Current.BoundingRectangle
    [GribBrowserMouse]::RightClick([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    $exportMenu=$null;$clock.Restart()
    do{$exportMenu=Controls $uia::RootElement|Where-Object {$_.Current.ProcessId-eq $process.Id-and $_.Current.ControlType-eq [Windows.Automation.ControlType]::MenuItem-and $_.Current.Name.Replace('&','').StartsWith('Export As Image')-and -not $_.Current.IsOffscreen}|Select-Object -Last 1;if($exportMenu){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'GRIB PNG export menu enabled' ($exportMenu-and $exportMenu.Current.IsEnabled)
    $bounds=$exportMenu.Current.BoundingRectangle
    [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    $saveDialog=$null;$clock.Restart()
    do{
      $candidates=$uia::RootElement.FindAll([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id))
      foreach($candidate in $candidates){if($candidate.Current.Name.StartsWith('Export As Image')){$saveDialog=$candidate;break}}
      if(-not $saveDialog){$saveDialog=Controls $window|Where-Object {$_.Current.ControlType-eq [Windows.Automation.ControlType]::Window-and $_.Current.Name.StartsWith('Export As Image')}|Select-Object -First 1}
      if($saveDialog){break};Start-Sleep -Milliseconds 100
    }while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Native PNG save dialog visible' ($null-ne $saveDialog)
    $filenameHost=Controls $saveDialog|Where-Object {$_.Current.AutomationId-eq 'FileNameControlHost'}|Select-Object -First 1
    $bounds=$filenameHost.Current.BoundingRectangle
    $editHandle=[GribBrowserMouse]::FileEdit([IntPtr]$saveDialog.Current.NativeWindowHandle,[int]$bounds.Left,[int]$bounds.Top,[int]$bounds.Right,[int]$bounds.Bottom)
    Check 'Native PNG filename edit found' ($editHandle-ne [IntPtr]::Zero)
    $pngOutput=Join-Path $log 'gui-export.png'
    [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    [Windows.Forms.Clipboard]::SetText($pngOutput);[Windows.Forms.SendKeys]::SendWait('^a{BACKSPACE}');[Windows.Forms.SendKeys]::SendWait('^v')
    $clock.Restart();do{if([GribBrowserMouse]::ReadText($editHandle)-eq $pngOutput){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 3000)
    Check 'Native PNG output path readback' ([GribBrowserMouse]::ReadText($editHandle)-eq $pngOutput)
    $saveHandle=[GribBrowserMouse]::GetDlgItem([IntPtr]$saveDialog.Current.NativeWindowHandle,1)
    [GribBrowserMouse]::PostMessage($saveHandle,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
    $clock.Restart();do{if(Test-Path -LiteralPath $pngOutput){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 10000)
    Check 'GUI PNG export created' (Test-Path -LiteralPath $pngOutput)
    $pngImage=[Drawing.Image]::FromFile($pngOutput)
    try{$expectedWidth=if($ExternalGrib1){720}elseif($ExternalGrib2){360}else{4};$expectedHeight=if($ExternalGrib1){361}elseif($ExternalGrib2){181}else{3};Check 'GUI PNG full source dimensions' ($pngImage.Width-eq $expectedWidth-and $pngImage.Height-eq $expectedHeight)}finally{$pngImage.Dispose()}
    $field=Named $window 'Field 0 [sub=0]';$bounds=$field.Current.BoundingRectangle
    [GribBrowserMouse]::RightClick([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    $closeFile=$null;$clock.Restart()
    do{$closeFile=Controls $uia::RootElement|Where-Object {$_.Current.ProcessId-eq $process.Id-and $_.Current.ControlType-eq [Windows.Automation.ControlType]::MenuItem-and $_.Current.Name.Replace('&','')-eq 'Close File'-and -not $_.Current.IsOffscreen}|Select-Object -First 1;if($closeFile){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Close File menu available' ($null-ne $closeFile)
    $closeFile.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    $clock.Restart();do{if(-not(Named $window 'Raster image')){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Close source removes image' ($null-eq (Named $window 'Raster image'))
    Shot 'image-closed'
  }
  if($Histogram){
    $field=Named $window 'Field 0 [sub=0]';$bounds=$field.Current.BoundingRectangle
    [GribBrowserMouse]::Front([IntPtr]$window.Current.NativeWindowHandle)
    $field.SetFocus()
    [GribBrowserMouse]::RightClick([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    $menu=$null;$clock.Restart()
    do{$menu=ProcessMenuItems|Where-Object {$_.Current.Name-eq 'Histogram...'-and -not $_.Current.IsOffscreen}|Select-Object -Last 1;if($menu){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'GRIB Histogram menu enabled' ($menu-and $menu.Current.IsEnabled)
    $bounds=$menu.Current.BoundingRectangle
    [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    $sampling=$null;$summary=$null;$clock.Restart()
    do{$sampling=Named $window 'Sample 1 in N';$summary=Controls $window|Where-Object {$_.Current.Name.Contains('values sampled at random')-and $_.Current.Name.Contains('GRIB decoded values')}|Select-Object -First 1;if($sampling-and $summary){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 15000)
    Check 'GRIB histogram read and summary visible' ($sampling-and $summary-and -not $sampling.Current.IsOffscreen)
    $histogramWindow=Named $window '/field/0 - Histogram';$bounds=$histogramWindow.Current.BoundingRectangle
    [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+13))
    [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+13))
    [GribBrowserMouse]::Front([IntPtr]$window.Current.NativeWindowHandle)
    $sampling.GetCurrentPattern([Windows.Automation.RangeValuePattern]::Pattern).SetValue(1)
    $total=if($ExternalGrib1){259920}elseif($ExternalGrib2){65160}else{12}
    $summary=$null;$clock.Restart()
    do{$summary=Controls $window|Where-Object {$_.Current.Name.Contains("$total of $total values sampled at random (1 in 1)")}|Select-Object -First 1;if($summary){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Full-plane histogram counts match source field' ($null-ne $summary)
    $text=$summary.Current.Name
    Check 'Histogram reports decoded entropy and missing' ($text.Contains('Whole-plane histogram entropy')-and $text.Contains('NaN among them 0')-and $text.Contains('GRIB decoded values | units:'))
    [IO.File]::WriteAllText((Join-Path $log 'histogram-summary.txt'),$text,[Text.UTF8Encoding]::new($false))
    Shot 'histogram-full'
    $bins=Named $window 'Bins';$bins.GetCurrentPattern([Windows.Automation.RangeValuePattern]::Pattern).SetValue(16)
    $clock.Restart();$rebinned=$null
    do{$rebinned=Controls $window|Where-Object {$_.Current.Name.Contains('bins 16')-and $_.Current.Name.Contains('GRIB decoded values')}|Select-Object -First 1;if($rebinned){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 3000)
    Check 'Histogram bin setting updates cached statistics' ($null-ne $rebinned)
    [GribBrowserMouse]::Front([IntPtr]$window.Current.NativeWindowHandle)
    $field=Named $window 'Field 0 [sub=0]';$bounds=$field.Current.BoundingRectangle
    [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    [GribBrowserMouse]::RightClick([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    $closeFile=$null;$clock.Restart()
    do{$closeFile=ProcessMenuItems|Where-Object {$_.Current.Name.Replace('&','')-eq 'Close File'-and -not $_.Current.IsOffscreen}|Select-Object -First 1;if($closeFile){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Histogram close source menu available' ($null-ne $closeFile)
    $closeFile.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    $clock.Restart();do{if(-not(Named $window 'Sample 1 in N')){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Close source removes histogram' ($null-eq (Named $window 'Sample 1 in N'))
  }
  if($Texture){
    [GribBrowserMouse]::Front([IntPtr]$window.Current.NativeWindowHandle)
    $field=Named $window 'Field 0 [sub=0]';$field.SetFocus();$bounds=$field.Current.BoundingRectangle
    [GribBrowserMouse]::RightClick([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    $menu=$null;$clock.Restart()
    do{$menu=ProcessMenuItems|Where-Object {$_.Current.Name-eq 'Texture Entropy Image'-and -not $_.Current.IsOffscreen}|Select-Object -Last 1;if($menu){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'GRIB Texture Entropy menu enabled' ($menu-and $menu.Current.IsEnabled)
    $bounds=$menu.Current.BoundingRectangle
    [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    $canvas=$null;$summary=$null;$clock.Restart()
    do{$canvas=Named $window 'Texture entropy 2D image';$summary=Controls $window|Where-Object {$_.Current.Name.Contains('2D texture entropy |')-and $_.Current.Name.Contains('GRIB decoded values')}|Select-Object -First 1;if($canvas-and $summary){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 15000)
    Check 'GRIB texture image and summary available' ($canvas-and $summary)
    $imageWindow=Named $window '/field/0 - Texture Entropy Image';$bounds=$imageWindow.Current.BoundingRectangle
    [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+13))
    [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+13))
    [GribBrowserMouse]::Front([IntPtr]$window.Current.NativeWindowHandle)
    $shape=if($ExternalGrib1){'361 x 720'}elseif($ExternalGrib2){'181 x 360'}else{'3 x 4'}
    $total=if($ExternalGrib1){259920}elseif($ExternalGrib2){65160}else{12}
    Check 'Texture dimensions and finite count match source field' ($summary.Current.Name.Contains($shape+' pixels')-and $summary.Current.Name.Contains("Valid samples: $total; excluded: 0"))
    Check 'Texture block semantics and units visible' ($summary.Current.Name.Contains('256 shared gray levels')-and $summary.Current.Name.Contains('0..4 bits')-and $summary.Current.Name.Contains('source units:'))
    InvokeNamed 'Fit'
    $painted=0;$clock.Restart()
    do{$bounds=$canvas.Current.BoundingRectangle;$painted=[GribBrowserMouse]::ColoredPixels([int]$bounds.X,[int]$bounds.Y,[int]$bounds.Width,[int]$bounds.Height);if($painted-gt 50){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Texture canvas actually painted' ($painted-gt 50)
    Shot 'texture-painted'
    SetValue 'Texture mask start' '-1e300';SetValue 'Texture mask end' '1e300'
    $invalid=$null;$clock.Restart()
    do{$invalid=Controls $window|Where-Object {$_.Current.Name.Contains('No finite unmasked samples')}|Select-Object -First 1;if($invalid){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 10000)
    Check 'Texture full Mask clears statistics' ($null-ne $invalid)
    $export=Named $window 'Export CSV';Check 'Texture invalid state disables export' ($export-and -not $export.Current.IsEnabled)
    SetValue 'Texture mask start' '';SetValue 'Texture mask end' ''
    $summary=$null;$clock.Restart()
    do{$summary=Controls $window|Where-Object {$_.Current.Name.Contains("Valid samples: $total; excluded: 0")-and $_.Current.Name.Contains('GRIB decoded values')}|Select-Object -First 1;if($summary){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 15000)
    Check 'Clearing Texture Mask restores source plane' ($null-ne $summary)
    InvokeNamed 'Fit'
    Shot 'texture-restored'
    [GribBrowserMouse]::Front([IntPtr]$window.Current.NativeWindowHandle)
    $field=Named $window 'Field 0 [sub=0]';$field.SetFocus();$bounds=$field.Current.BoundingRectangle
    [GribBrowserMouse]::RightClick([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    $closeFile=$null;$clock.Restart()
    do{$closeFile=ProcessMenuItems|Where-Object {$_.Current.Name.Replace('&','')-eq 'Close File'-and -not $_.Current.IsOffscreen}|Select-Object -First 1;if($closeFile){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Texture close source menu available' ($null-ne $closeFile)
    $closeFile.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    $clock.Restart();do{if(-not(Named $window 'Texture entropy 2D image')){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Close source removes texture image' ($null-eq (Named $window 'Texture entropy 2D image'))
  }
  if($Profile){
    [GribBrowserMouse]::Front([IntPtr]$window.Current.NativeWindowHandle)
    $field=Named $window 'Field 0 [sub=0]';$field.SetFocus();$bounds=$field.Current.BoundingRectangle
    [GribBrowserMouse]::RightClick([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    $menu=$null;$clock.Restart()
    do{$menu=ProcessMenuItems|Where-Object {$_.Current.Name-eq 'Image View'-and -not $_.Current.IsOffscreen}|Select-Object -Last 1;if($menu){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Profile GRIB image menu enabled' ($menu-and $menu.Current.IsEnabled)
    $menu.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    $canvas=$null;$clock.Restart()
    do{$canvas=Named $window 'Raster image';$entropy=Named $window 'Whole-plane histogram entropy (256 bins):';if($canvas-and $entropy-and -not $entropy.Current.Name.Contains('N/A')){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 15000)
    Check 'GRIB gray entropy available' ($canvas-and $entropy-and -not $entropy.Current.Name.Contains('N/A'))
    $imageWindow=Named $window '/field/0 - Image';$bounds=$imageWindow.Current.BoundingRectangle
    [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+13))
    [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+13))
    InvokeNamed 'Fit'
    InvokeNamed 'Profile'
    $canvas=Named $window 'Raster image';$bounds=$canvas.Current.BoundingRectangle
    [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width*.4),[int]($bounds.Y+$bounds.Height*.4))
    [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width*.6),[int]($bounds.Y+$bounds.Height*.6))
    $show=Named $window 'Show Profile'
    Check 'Mouse control points enable Show Profile' ($show-and $show.Current.IsEnabled)
    $show.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    $profileWindow=$null;$summary=$null;$clock.Restart()
    do{
      $candidates=$uia::RootElement.FindAll([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id))
      foreach($candidate in $candidates){
        if($candidate.Current.Name.EndsWith(' - Curve Profile')){$profileWindow=$candidate;break}
        $child=Named $candidate '/field/0 - Curve Profile';if($child){$profileWindow=$child;break}
      }
      if($profileWindow){$summary=Named $profileWindow 'Points:';if($summary){break}}
      Start-Sleep -Milliseconds 100
    }while($clock.ElapsedMilliseconds-lt 5000)
    Check 'GRIB curve profile visible with samples' ($profileWindow-and $summary-and $summary.Current.Name.Contains('finite'))
    $export=Named $profileWindow 'Export CSV'
    Check 'Finite curve enables CSV export' ($export-and $export.Current.IsEnabled)
    [GribBrowserMouse]::Front([IntPtr]$profileWindow.Current.NativeWindowHandle)
    Shot 'profile-painted'
    $bounds=$export.Current.BoundingRectangle
    [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    $saveDialog=$null;$clock.Restart()
    do{
      $candidates=$uia::RootElement.FindAll([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new($uia::ProcessIdProperty,$process.Id))
      foreach($candidate in $candidates){$child=Named $candidate 'Export Profile CSV';if($child){$saveDialog=$child;break};if($candidate.Current.Name-eq 'Export Profile CSV'){$saveDialog=$candidate;break}}
      if($saveDialog){break};Start-Sleep -Milliseconds 100
    }while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Native profile CSV dialog visible' ($null-ne $saveDialog)
    $filenameHost=Controls $saveDialog|Where-Object {$_.Current.AutomationId-eq 'FileNameControlHost'}|Select-Object -First 1
    $bounds=$filenameHost.Current.BoundingRectangle
    $editHandle=[GribBrowserMouse]::FileEdit([IntPtr]$saveDialog.Current.NativeWindowHandle,[int]$bounds.Left,[int]$bounds.Top,[int]$bounds.Right,[int]$bounds.Bottom)
    Check 'Native profile CSV filename edit found' ($editHandle-ne [IntPtr]::Zero)
    $csvOutput=Join-Path $log 'gui-profile.csv'
    [GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    [Windows.Forms.Clipboard]::SetText($csvOutput);[Windows.Forms.SendKeys]::SendWait('^a{BACKSPACE}');[Windows.Forms.SendKeys]::SendWait('^v')
    $clock.Restart();do{if([GribBrowserMouse]::ReadText($editHandle)-eq $csvOutput){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 3000)
    Check 'Native profile CSV path readback' ([GribBrowserMouse]::ReadText($editHandle)-eq $csvOutput)
    $saveHandle=[GribBrowserMouse]::GetDlgItem([IntPtr]$saveDialog.Current.NativeWindowHandle,1)
    [GribBrowserMouse]::PostMessage($saveHandle,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
    $clock.Restart();do{if(Test-Path -LiteralPath $csvOutput){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'GUI profile CSV saved' (Test-Path -LiteralPath $csvOutput)
    $csvLines=[IO.File]::ReadAllLines($csvOutput)
    Check 'GUI profile CSV has values and coordinates' ($csvLines.Count-gt 2-and $csvLines[0]-eq 'index,distance,row,col,value')
    [GribBrowserMouse]::Front([IntPtr]$window.Current.NativeWindowHandle)
    SetValue 'Mask start' '-1e300';SetValue 'Mask end' '1e300'
    $clock.Restart()
    do{$summary=Named $profileWindow 'Points:';if($summary-and $summary.Current.Name.Contains('(0 finite,')){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Full image Mask clears finite curve samples' ($summary-and $summary.Current.Name.Contains('(0 finite,'))
    Check 'Invalid curve disables CSV export' (-not $export.Current.IsEnabled)
    $entropy=Named $window 'Whole-plane histogram entropy (256 bins):'
    Check 'Fully masked gray entropy is unavailable' ($entropy.Current.Name.Contains('N/A'))
    SetValue 'Mask start' '';SetValue 'Mask end' ''
    $clock.Restart()
    do{if($export.Current.IsEnabled){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Clearing Mask restores curve export' $export.Current.IsEnabled
    [GribBrowserMouse]::Front([IntPtr]$window.Current.NativeWindowHandle)
    $field=Named $window 'Field 0 [sub=0]';$field.SetFocus();$bounds=$field.Current.BoundingRectangle
    [GribBrowserMouse]::RightClick([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    $closeFile=$null;$clock.Restart()
    do{$closeFile=ProcessMenuItems|Where-Object {$_.Current.Name.Replace('&','')-eq 'Close File'-and -not $_.Current.IsOffscreen}|Select-Object -First 1;if($closeFile){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Profile source close menu available' ($null-ne $closeFile)
    $closeFile.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    $clock.Restart();do{if(-not(Named $window 'Raster image')){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Closing source removes profile image' ($null-eq (Named $window 'Raster image'))
  }
  if($Playback){
    $referencePath=$env:HDFSTUDIO_GRIB_PLAYBACK_REFERENCE
    if($external){
      if(-not $referencePath){throw 'Set HDFSTUDIO_GRIB_PLAYBACK_REFERENCE to the automatic external playback JSON'}
      $reference=Get-Content -Raw -Encoding UTF8 $referencePath|ConvertFrom-Json
    }else{$reference=[pscustomobject]@{playable=$true;axis=0;title='t [Temperature, K] isobaricInhPa'}}
    $variables=Named $window 'Variables [virtual arrays]';Expand $variables
    $cube=Named $window $reference.title
    Check 'Playback virtual variable visible' ($cube-and -not $cube.Current.IsOffscreen)
    foreach($index in @(1,2)){
      [GribBrowserMouse]::Front([IntPtr]$window.Current.NativeWindowHandle)
      $cube=Named $window $reference.title;$cube.SetFocus();$bounds=$cube.Current.BoundingRectangle
      [GribBrowserMouse]::RightClick([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
      $clock.Restart();$menu=$null
      do{$menu=ProcessMenuItems|Where-Object {$_.Current.Name-eq 'Image View'-and -not $_.Current.IsOffscreen}|Select-Object -Last 1;if($menu){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
      Check ('Playback Image View menu '+$index) ($menu-and $menu.Current.IsEnabled)
      $bounds=$menu.Current.BoundingRectangle;[GribBrowserMouse]::Click([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
      $clock.Restart();$images=@();$painted=0
      do{
        $images=@(Controls $window|Where-Object {$_.Current.Name-eq 'Raster image'})
        if($images.Count-eq $index){$bounds=$images[-1].Current.BoundingRectangle;$painted=[GribBrowserMouse]::ColoredPixels([int]$bounds.X,[int]$bounds.Y,[int]$bounds.Width,[int]$bounds.Height)}
        if($images.Count-eq $index-and $painted-gt 50){break};Start-Sleep -Milliseconds 100
      }while($clock.ElapsedMilliseconds-lt 15000)
      Check ('Playback image actually painted '+$index) ($images.Count-eq $index-and $painted-gt 50)
      Start-Sleep -Milliseconds ([Windows.Forms.SystemInformation]::DoubleClickTime+50)
    }
    $compareAction=Controls $window|Where-Object {$_.Current.Name-eq 'Compare & Metrics'-and $_.Current.ControlType-eq [Windows.Automation.ControlType]::Button-and -not $_.Current.IsOffscreen}|Select-Object -First 1
    $compareAction.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    $clock.Restart();$metrics=''
    do{$metricsControl=Named $window 'Comparison metrics';if($metricsControl){$metrics=ReadMetrics $metricsControl;if($metrics.Contains('Status: Ready')){break}};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 15000)
    Check 'Playback pair starts Ready by index' ($metrics.Contains('Status: Ready')-and $metrics.Contains('Pairing: by index'))
    $play=Named $window 'Play / pause layers'
    if($reference.playable){
      Check 'Multi-frame GRIB enables Play' ($play-and $play.Current.IsEnabled)
      $interval=Named $window 'Frame interval (ms)';$range=$interval.GetCurrentPattern([Windows.Automation.RangeValuePattern]::Pattern);$range.SetValue(10000)
      Check 'Playback interval numeric readback' ($range.Current.Value-eq 10000)
      InvokeNamed 'Play / pause layers'
      $toggle=$play.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern)
      $clock.Restart();do{if($toggle.Current.ToggleState-eq [Windows.Automation.ToggleState]::On){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 3000)
      Check 'GRIB playback starts from toolbar' ($toggle.Current.ToggleState-eq [Windows.Automation.ToggleState]::On)
      InvokeNamed 'Play / pause layers'
      $clock.Restart();do{if($toggle.Current.ToggleState-eq [Windows.Automation.ToggleState]::Off){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 3000)
      Check 'GRIB playback pauses from toolbar' ($toggle.Current.ToggleState-eq [Windows.Automation.ToggleState]::Off)
      SetValue 'Lag offset' '-1';SetValue 'Anchor layer index' '1'
      $clock.Restart()
      do{$metrics=ReadMetrics $metricsControl;if($metrics.Contains('Status: Ready')-and $metrics.Contains('Anchor[1')-and $metrics.Contains('Target[0')){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 15000)
      Check 'GRIB integer lag routes Anchor and Target' ($metrics.Contains('Status: Ready')-and $metrics.Contains('Anchor[1')-and $metrics.Contains('Target[0'))
      SetValue 'Anchor layer index' '0'
      $clock.Restart();do{$metrics=ReadMetrics $metricsControl;if($metrics.Contains('Out of range')-and $metrics.Contains('RMSE: N/A')){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
      Check 'Negative lag invalidates out-of-range target' ($metrics.Contains('Out of range')-and $metrics.Contains('RMSE: N/A'))
      SetValue 'Lag offset' '0'
      $clock.Restart();do{$metrics=ReadMetrics $metricsControl;if($metrics.Contains('Status: Ready')){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 15000)
      Check 'Clearing lag restores available pair' ($metrics.Contains('Status: Ready'))
      $range.SetValue(1000);InvokeNamed 'Play / pause layers'
      $clock.Restart();do{$metrics=ReadMetrics $metricsControl;if($metrics.Contains('Anchor[1')){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 7000)
      Check 'GRIB timer advances an actual layer' ($metrics.Contains('Anchor[1'))
      if($toggle.Current.ToggleState-eq [Windows.Automation.ToggleState]::On){InvokeNamed 'Play / pause layers'}
      $clock.Restart();do{if($toggle.Current.ToggleState-eq [Windows.Automation.ToggleState]::Off){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 3000)
      $metrics=ReadMetrics $metricsControl
      $paused=$metrics.Split("`n")[0];Start-Sleep -Milliseconds 1200
      $metrics=ReadMetrics $metricsControl
      Check 'Pause keeps GRIB indices fixed' ($metrics.Split("`n")[0]-eq $paused)
      Shot 'playback-paused'
      $range.SetValue(10000);InvokeNamed 'Play / pause layers'
    }else{
      Check 'Single-field GRIB disables Play' ($play-and -not $play.Current.IsEnabled)
      Shot 'playback-disabled'
    }
    [GribBrowserMouse]::Front([IntPtr]$window.Current.NativeWindowHandle)
    $cube=Named $window $reference.title;$bounds=$cube.Current.BoundingRectangle
    [GribBrowserMouse]::RightClick([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    $closeFile=$null;$clock.Restart()
    do{$closeFile=ProcessMenuItems|Where-Object {$_.Current.Name.Replace('&','')-eq 'Close File'-and -not $_.Current.IsOffscreen}|Select-Object -First 1;if($closeFile){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Playback source close available' ($null-ne $closeFile)
    $closeFile.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    $clock.Restart();do{if(-not(Named $window 'Raster image')){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Closing playing source removes images' ($null-eq (Named $window 'Raster image'))
    $play=Named $window 'Play / pause layers'
    Check 'Closing playing source stops and disables Play' ($play-and -not $play.Current.IsEnabled-and $play.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Current.ToggleState-eq [Windows.Automation.ToggleState]::Off)
  }
  if($Compare){
    foreach($index in @(1,2)){
      if($index-gt 1){Start-Sleep -Milliseconds ([Windows.Forms.SystemInformation]::DoubleClickTime+50)}
      [GribBrowserMouse]::Front([IntPtr]$window.Current.NativeWindowHandle)
      $field=Named $window 'Field 0 [sub=0]';$field.SetFocus();$bounds=$field.Current.BoundingRectangle
      [GribBrowserMouse]::RightClick([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
      $menu=$null;$clock.Restart()
      do{$menu=ProcessMenuItems|Where-Object {$_.Current.Name-eq 'Image View'-and -not $_.Current.IsOffscreen}|Select-Object -Last 1;if($menu){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
      Check ('Comparison image menu '+$index) ($menu-and $menu.Current.IsEnabled)
      $menuBounds=$menu.Current.BoundingRectangle
      [GribBrowserMouse]::Click([int]($menuBounds.X+$menuBounds.Width/2),[int]($menuBounds.Y+$menuBounds.Height/2))
      $clock.Restart();$images=@()
      do{$images=@(Controls $window|Where-Object {$_.Current.Name-eq 'Raster image'});if($images.Count-eq $index){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 15000)
      Check ('Comparison image ready '+$index) ($images.Count-eq $index)
      $painted=0;$clock.Restart()
      do{
        $canvas=$images[-1];$bounds=$canvas.Current.BoundingRectangle
        $painted=[GribBrowserMouse]::ColoredPixels([int]$bounds.X,[int]$bounds.Y,[int]$bounds.Width,[int]$bounds.Height)
        $entropy=Named $window 'Whole-plane histogram entropy (256 bins):'
        if($painted-gt 50-and $entropy-and -not $entropy.Current.Name.Contains('N/A')){break}
        Start-Sleep -Milliseconds 100
      }while($clock.ElapsedMilliseconds-lt 15000)
      Check ('Comparison image actually painted '+$index) ($painted-gt 50-and $entropy-and -not $entropy.Current.Name.Contains('N/A'))
    }
    $compareAction=Controls $window|Where-Object {$_.Current.Name-eq 'Compare & Metrics'-and $_.Current.ControlType-eq [Windows.Automation.ControlType]::Button-and -not $_.Current.IsOffscreen}|Select-Object -First 1
    Check 'Compare toolbar action available' ($null-ne $compareAction)
    $compareAction.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    $clock.Restart();$metrics=''
    do{$metricsControl=Named $window 'Comparison metrics';if($metricsControl){$metrics=ReadMetrics $metricsControl;if($metrics.Contains('Status: Ready')){break}};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 15000)
    Check 'Identical GRIB planes have zero RMSE' ($metrics.Contains('RMSE: 0')-and $metrics.Contains('Bias (Target - Anchor): 0'))
    Check 'Comparison explicitly uses index pairing' ($metrics.Contains('Pairing: by index'))
    InvokeNamed 'Copy metrics'
    $copied='';$clock.Restart()
    do{$copied=[Windows.Forms.Clipboard]::GetText();if($copied.Contains('Pairing: by index')-and $copied.Contains('RMSE: 0')){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 3000)
    Check 'Copied current comparison metrics' ($copied.Contains('Pairing: by index')-and $copied.Contains('RMSE: 0'))
    [IO.File]::WriteAllText((Join-Path $log 'metrics.txt'),$copied,[Text.UTF8Encoding]::new($false))
    SaveNative (Named $window 'Export comparison CSV') 'Export comparison CSV' (Join-Path $log 'gui-comparison.csv')
    $csv=Import-Csv -LiteralPath (Join-Path $log 'gui-comparison.csv')
    Check 'Comparison CSV records index pairing and identities' ($csv.pairing-eq 'by index'-and $csv.rmse-eq '0'-and $csv.anchor_grid_identity-and $csv.anchor_time_identity)
    InvokeNamed 'Residual map'
    $residualWindow=$null;$clock.Restart()
    do{$residualWindow=ProcessWindow 'Residual map: Target - Anchor';if($residualWindow-and (Named $residualWindow 'Residual image')){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Residual map visible' ($null-ne $residualWindow)
    [GribBrowserMouse]::Front([IntPtr]$residualWindow.Current.NativeWindowHandle)
    Shot 'residual-painted'
    SaveNative (Named $residualWindow 'Export PNG') 'Export residual PNG' (Join-Path $log 'gui-residual.png')
    $bitmap=[Drawing.Bitmap]::FromFile((Join-Path $log 'gui-residual.png'))
    try{$rows=if($ExternalGrib1){361}elseif($ExternalGrib2){181}else{3};$cols=if($ExternalGrib1){720}elseif($ExternalGrib2){360}else{4};Check 'Native residual PNG has source dimensions' ($bitmap.Width-eq $cols-and $bitmap.Height-eq $rows)}finally{$bitmap.Dispose()}
    [GribBrowserMouse]::Front([IntPtr]$window.Current.NativeWindowHandle)
    SetValue 'Mask start' '-1e300';SetValue 'Mask end' '1e300'
    $clock.Restart()
    do{$metrics=ReadMetrics $metricsControl;if($metrics.Contains('RMSE: N/A')){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Full Mask invalidates pair metrics' ($metrics.Contains('RMSE: N/A'))
    Check 'Invalid residual disables PNG export' (-not (Named $residualWindow 'Export PNG').Current.IsEnabled)
    SetValue 'Mask start' '';SetValue 'Mask end' ''
    $clock.Restart();do{if((Named $residualWindow 'Export PNG').Current.IsEnabled){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Clearing Mask restores residual export' (Named $residualWindow 'Export PNG').Current.IsEnabled
    [GribBrowserMouse]::Front([IntPtr]$residualWindow.Current.NativeWindowHandle)
    Shot 'residual-restored'
    [GribBrowserMouse]::Front([IntPtr]$window.Current.NativeWindowHandle)
    $field=Named $window 'Field 0 [sub=0]';$field.SetFocus();$bounds=$field.Current.BoundingRectangle
    [GribBrowserMouse]::RightClick([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
    $closeFile=$null;$clock.Restart()
    do{$closeFile=ProcessMenuItems|Where-Object {$_.Current.Name.Replace('&','')-eq 'Close File'-and -not $_.Current.IsOffscreen}|Select-Object -First 1;if($closeFile){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Comparison close source available' ($null-ne $closeFile)
    $closeFile.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    $clock.Restart();do{if(-not(Named $window 'Raster image')){break};Start-Sleep -Milliseconds 100}while($clock.ElapsedMilliseconds-lt 5000)
    Check 'Closing source removes comparison images' ($null-eq (Named $window 'Raster image'))
    Check 'Closing source clears residual export' (-not (Named $residualWindow 'Export PNG').Current.IsEnabled)
  }
  Check 'Source hash unchanged' ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash-eq $before)
  [GribBrowserMouse]::PostMessage([IntPtr]$window.Current.NativeWindowHandle,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  Check 'Normal process exit' ($process.WaitForExit(10000));$process.Refresh();Check 'Exit code zero' ($process.ExitCode-eq 0)
  $completed=$true
}catch {
  try{Shot 'failure';if($window){Controls $window|ForEach-Object {$_.Current.Name}|Set-Content -Encoding UTF8 (Join-Path $log 'controls.txt')}}catch{}
  $_|Out-String|Set-Content -Encoding UTF8 (Join-Path $log 'failure.txt')
  throw
}finally {
  $checks|ConvertTo-Json -Depth 5|Set-Content -Encoding UTF8 (Join-Path $log 'checks.json')
  if(-not $completed-and $process-and -not $process.HasExited){$process.Kill();$process.WaitForExit()}
}
