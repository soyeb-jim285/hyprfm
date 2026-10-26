# Drives a packaged HyprFM on a Windows CI runner and records what it looks
# like: PNG screenshots and short MP4 clips of the animations.
#   showcase.ps1 -Exe app\bin\hyprfm.exe -Out showcase
param([string]$Exe, [string]$Out = 'showcase')

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing, System.Windows.Forms, Microsoft.VisualBasic
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class U32 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int ht, bool repaint);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, int data, UIntPtr extra);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
}
'@

$Out = [IO.Path]::GetFullPath($Out)
$Exe = [IO.Path]::GetFullPath($Exe)
New-Item -ItemType Directory -Force $Out | Out-Null
$screen = [Windows.Forms.Screen]::PrimaryScreen.Bounds
"Screen: $($screen.Width)x$($screen.Height)"

# ── Demo folder ─────────────────────────────────────────────────────────────
$demo = Join-Path $env:USERPROFILE 'HyprFM Demo'
Remove-Item -Recurse -Force $demo -ErrorAction SilentlyContinue
foreach ($d in 'Documents', 'Pictures', 'Projects\hyprfm', 'Projects\website', 'Music', 'Archives') {
    New-Item -ItemType Directory -Force (Join-Path $demo $d) | Out-Null
}
$palette = @(
    @('#1e66f5', '#ea76cb'), @('#40a02b', '#04a5e5'), @('#fe640b', '#d20f39'),
    @('#8839ef', '#209fb5'), @('#df8e1d', '#e64553'), @('#179299', '#7287fd'),
    @('#dc8a78', '#8839ef'), @('#04a5e5', '#40a02b'))
for ($i = 0; $i -lt $palette.Count; $i++) {
    $bmp = New-Object Drawing.Bitmap 800, 600
    $g = [Drawing.Graphics]::FromImage($bmp)
    $rect = New-Object Drawing.Rectangle 0, 0, 800, 600
    $brush = New-Object Drawing.Drawing2D.LinearGradientBrush $rect,
        ([Drawing.ColorTranslator]::FromHtml($palette[$i][0])),
        ([Drawing.ColorTranslator]::FromHtml($palette[$i][1])), (35.0 * $i)
    $g.FillRectangle($brush, $rect)
    $white = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(90, 255, 255, 255))
    $g.FillEllipse($white, 120 + 40 * $i, 140, 320, 320)
    $g.Dispose()
    $bmp.Save((Join-Path $demo "Pictures\wallpaper-$($i + 1).png"), [Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}
Copy-Item (Join-Path $demo 'Pictures\wallpaper-1.png') (Join-Path $demo 'cover.png')
Set-Content (Join-Path $demo 'README.md') @'
# HyprFM on Windows

A fast, keyboard-driven file manager, now running natively on Windows.

- Grid, Miller and detailed views
- Quick preview with `Space`
- Split panes, tabs, bulk rename
'@
Set-Content (Join-Path $demo 'notes.txt') "Shopping list`r`n- coffee`r`n- milk`r`n- more coffee"
Set-Content (Join-Path $demo 'Projects\hyprfm\main.cpp') "#include <cstdio>`r`n`r`nint main()`r`n{`r`n    std::puts(`"hello`");`r`n}"
Set-Content (Join-Path $demo 'Projects\hyprfm\CMakeLists.txt') 'project(hyprfm CXX)'
Set-Content (Join-Path $demo 'Projects\website\index.html') '<!doctype html><title>Hi</title><h1>Hello</h1>'
Set-Content (Join-Path $demo 'Documents\budget.csv') "month,amount`r`njan,120`r`nfeb,95"
Set-Content (Join-Path $demo 'Documents\letter.txt') 'Dear reader, this is a letter.'
foreach ($n in 1..30) { Set-Content (Join-Path $demo ('Music\track-{0:d2}.txt' -f $n)) "track $n" }
Compress-Archive -Path (Join-Path $demo 'Projects\*') -DestinationPath (Join-Path $demo 'Archives\projects.zip')
# One of every previewable kind, side by side so Quick Preview can step
# through them with Right. Folders sort first, then files by name.
$prev = Join-Path $demo 'Previews'
New-Item -ItemType Directory -Force (Join-Path $prev 'Photos') | Out-Null
Copy-Item (Join-Path $demo 'Pictures\wallpaper-2.png') (Join-Path $prev 'Photos\beach.png')
Copy-Item (Join-Path $demo 'Pictures\wallpaper-4.png') (Join-Path $prev 'Photos\sunset.png')
Copy-Item (Join-Path $demo 'Pictures\wallpaper-3.png') (Join-Path $prev 'photo.png')
Copy-Item (Join-Path $demo 'Documents\budget.csv') (Join-Path $prev 'budget.csv')
Copy-Item (Join-Path $demo 'Projects\hyprfm\main.cpp') (Join-Path $prev 'main.cpp')
Copy-Item (Join-Path $demo 'README.md') (Join-Path $prev 'notes.md')
Copy-Item (Join-Path $demo 'notes.txt') (Join-Path $prev 'shopping.txt')
Compress-Archive -Path (Join-Path $demo 'Projects\*') -DestinationPath (Join-Path $prev 'projects.zip')
& ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc2=size=640x360:rate=30 -t 5 `
    -c:v libx264 -pix_fmt yuv420p (Join-Path $prev 'clip.mp4')
# A one-page PDF by hand: objects, then an xref of their byte offsets.
$text = "0.12 0.4 0.96 rg 72 420 468 200 re f`n" +
        "BT /F1 36 Tf 72 700 Td (HyprFM on Windows) Tj ET`n" +
        "BT /F1 16 Tf 72 660 Td (Rendered by Windows.Data.Pdf, no poppler) Tj ET"
$objs = @(
    '<< /Type /Catalog /Pages 2 0 R >>',
    '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
    '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>',
    "<< /Length $($text.Length) >>`nstream`n$text`nendstream",
    '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>')
$pdf = "%PDF-1.4`n"; $offsets = @()
for ($i = 0; $i -lt $objs.Count; $i++) { $offsets += $pdf.Length; $pdf += "$($i + 1) 0 obj`n$($objs[$i])`nendobj`n" }
$xref = $pdf.Length
$pdf += "xref`n0 $($objs.Count + 1)`n0000000000 65535 f `n"
foreach ($o in $offsets) { $pdf += "{0:d10} 00000 n `n" -f $o }
$pdf += "trailer`n<< /Size $($objs.Count + 1) /Root 1 0 R >>`nstartxref`n$xref`n%%EOF`n"
[IO.File]::WriteAllText((Join-Path $prev 'document.pdf'), $pdf, [Text.Encoding]::ASCII)

# Something in the Recycle Bin for the trash view.
$doomed = Join-Path $demo 'old-draft.txt'
Set-Content $doomed 'deleted on purpose'
[Microsoft.VisualBasic.FileIO.FileSystem]::DeleteFile($doomed, 'OnlyErrorDialogs', 'SendToRecycleBin')

# ── Config: dark theme to start; rewriting it later reloads live ────────────
$cfgDir = Join-Path $env:LOCALAPPDATA 'hyprfm'
New-Item -ItemType Directory -Force $cfgDir | Out-Null
$cfg = Join-Path $cfgDir 'config.toml'
function Set-Theme($name) { Set-Content $cfg "[general]`r`ntheme = `"$name`"`r`n" }
Set-Theme 'catppuccin-mocha'

# ── Helpers ─────────────────────────────────────────────────────────────────
function Shot($name) {
    Start-Sleep -Milliseconds 1000
    $bmp = New-Object Drawing.Bitmap $screen.Width, $screen.Height
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($screen.Location, [Drawing.Point]::Empty, $screen.Size)
    $g.Dispose()
    $bmp.Save((Join-Path $Out "$name.png"), [Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    "shot $name"
}
function Keys($k, $pause = 1000) {
    [U32]::SetForegroundWindow($script:hwnd) | Out-Null
    [Windows.Forms.SendKeys]::SendWait($k)
    Start-Sleep -Milliseconds $pause
}
function Click($x, $y, $down = 0x2, $up = 0x4) {
    [U32]::SetForegroundWindow($script:hwnd) | Out-Null
    [U32]::SetCursorPos($x, $y) | Out-Null
    [U32]::mouse_event($down, 0, 0, 0, [UIntPtr]::Zero)
    [U32]::mouse_event($up, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 800
}
function RightClick($x, $y) { Click $x $y 0x8 0x10 }
# Empty area below the grid: focuses the view and dismisses any popup.
function ClickEmpty { Click $script:cx ($script:screen.Height - 90) }
function Wheel($x, $y, $clicks) {
    [U32]::SetCursorPos($x, $y) | Out-Null
    for ($i = 0; $i -lt [Math]::Abs($clicks); $i++) {
        [U32]::mouse_event(0x800, 0, 0, [Math]::Sign($clicks) * -120, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 120
    }
}
function Start-Clip($name, $seconds) {
    $ffArgs = "-hide_banner -loglevel error -y -f gdigrab -framerate 30 -draw_mouse 1 -i desktop " +
              "-t $seconds -c:v libx264 -preset veryfast -crf 23 -pix_fmt yuv420p `"$(Join-Path $Out "$name.mp4")`""
    $script:clip = Start-Process ffmpeg -ArgumentList $ffArgs -PassThru -WindowStyle Hidden
    Start-Sleep -Milliseconds 800   # gdigrab start-up
}
function Stop-Clip { $script:clip.WaitForExit(30000) | Out-Null; 'clip done' }
# Path bar (Ctrl+L), type, Enter. SendKeys treats +^%~(){}[] as syntax.
function Go($path) { Keys '^l' 500; Keys ($path -replace '([+^%~(){}\[\]])', '{$1}') 200; Keys '{ENTER}' 1200 }

# ── Launch ──────────────────────────────────────────────────────────────────
# The view's translucent surfaces show what is behind the window: the
# runner's console otherwise, the wallpaper after this.
# The runner pops up a "System Properties" paging-file notice at boot that
# takes keyboard focus from HyprFM; close it.
for ($i = 0; $i -lt 10; $i++) {
    $box = [U32]::FindWindow($null, 'System Properties')
    if ($box -eq [IntPtr]::Zero) { break }
    [U32]::PostMessage($box, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null   # WM_CLOSE
    Start-Sleep -Milliseconds 500
}
(New-Object -ComObject Shell.Application).MinimizeAll()
Start-Sleep -Seconds 1
$p = Start-Process $Exe -ArgumentList "`"$demo`"" -PassThru `
    -RedirectStandardError (Join-Path $Out 'hyprfm.stderr.txt')
for ($i = 0; $i -lt 60 -and $p.MainWindowHandle -eq 0; $i++) { Start-Sleep -Milliseconds 500; $p.Refresh() }
if ($p.MainWindowHandle -eq 0) { throw 'HyprFM window never appeared' }
$script:hwnd = $p.MainWindowHandle
[U32]::MoveWindow($hwnd, 0, 0, $screen.Width, $screen.Height, $true) | Out-Null
[U32]::ShowWindow($hwnd, 3) | Out-Null   # SW_MAXIMIZE
Start-Sleep -Seconds 4
$script:cx = [int]($screen.Width * 0.6); $cy = [int]($screen.Height * 0.55)

Shot '01-grid-dark'
ClickEmpty
# The first key after launch can be lost; Ctrl+3 first so a lost key
# shows up as a missing detailed shot, not a Miller shot of the grid.
Keys '^3' 1500; Keys '^3' 500; Shot '02-detailed-view'
Keys '^2' 1500; Shot '03-miller-view'
Keys '^1' 1200; ClickEmpty

Go "$demo\Pictures"; Shot '04-pictures-thumbnails'
Click 282 130; Keys ' ' 1500; Shot '05-quick-preview-image'
Keys '{ESC}' 1000; ClickEmpty

Go $demo
RightClick 282 297; Shot '06-context-menu'
Keys '{ESC}' 1000; Shot '06b-context-menu-after-esc'   # does Esc close it?
ClickEmpty

Keys '^,' 1500; Shot '07-settings'
Keys '{ESC}' 800

Keys '{F3}' 1500; Shot '08-split-view'
Keys '{F3}' 1000

Go 'trash:///'; Shot '09-recycle-bin'

Go "$demo\Archives"; Click 282 130; Keys ' ' 1500; Shot '10-archive-preview'
Keys '{ESC}' 1000; ClickEmpty

# ── Quick Preview of every kind ─────────────────────────────────────────────
$kinds = 'folder', 'csv', 'video', 'pdf', 'code', 'markdown', 'image', 'zip', 'text'
Go $prev; Click 282 130; Keys ' ' 2500
for ($i = 0; $i -lt $kinds.Count; $i++) {
    Shot ('p{0:d2}-preview-{1}' -f ($i + 1), $kinds[$i])
    Keys '{RIGHT}' 2500
}
Keys '{ESC}' 1000; ClickEmpty

# ── Clips ───────────────────────────────────────────────────────────────────
Start-Clip 'clip-0-preview-tour' 30
Click 282 130; Keys ' ' 2500
foreach ($k in $kinds) { Keys '{RIGHT}' 2800 }
Keys '{ESC}' 1000
Stop-Clip
ClickEmpty

Go $demo
Start-Clip 'clip-1-view-switching' 8
Keys '^3' 1500; Keys '^2' 1500; Keys '^1' 1500; Keys '^3' 1500
Stop-Clip

Start-Clip 'clip-2-navigate-and-preview' 10
Go "$demo\Pictures"
Click 282 130; Keys ' ' 1500; Keys '{RIGHT}' 1000; Keys '{RIGHT}' 1000; Keys '{ESC}' 1000
Keys '%{UP}' 1200
Stop-Clip

Start-Clip 'clip-3-settings-open-close' 7
Keys '^,' 2000; Keys '{ESC}' 1500; Keys '^,' 1500; Keys '{ESC}' 1000
Stop-Clip

Go "$demo\Music"; Keys '^3' 1000
Start-Clip 'clip-4-scrolling' 7
Wheel $cx $cy 10; Start-Sleep -Milliseconds 500; Wheel $cx $cy -10
Stop-Clip
Keys '^1' 800

Go $demo
Start-Clip 'clip-5-split-and-sidebar' 8
Keys '{F3}' 1600; Keys '{F9}' 1400; Keys '{F9}' 1400; Keys '{F3}' 1400
Stop-Clip

Start-Clip 'clip-6-theme-switch' 6
Set-Theme 'catppuccin-latte'; Start-Sleep -Seconds 3
Stop-Clip
Shot '11-grid-light'
Go "$demo\Pictures"; Shot '12-pictures-light'

$p | Stop-Process -Force -ErrorAction SilentlyContinue
Get-ChildItem $Out | Format-Table Name, Length
