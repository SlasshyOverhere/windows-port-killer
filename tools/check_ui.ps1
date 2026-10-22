Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class W2 {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
}
public class W {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("dwmapi.dll")] public static extern int DwmSetWindowAttribute(IntPtr h, int a, ref int v, int s);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int left, top, right, bottom; }
}
'@

Remove-Item -Path 'HKCU:\Software\PortKiller' -Recurse -ErrorAction SilentlyContinue
$p = Start-Process -FilePath 'C:\Users\suman\.t3\worktrees\windows-port-killer\t3-2e8e4a58\build\PortKiller.exe' -PassThru
Start-Sleep -Seconds 3
$p.Refresh()
$h = $p.MainWindowHandle
[void][W]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 700

$r = New-Object W+RECT
[void][W]::GetWindowRect($h, [ref]$r)
$w = $r.right - $r.left; $ht = $r.bottom - $r.top
"window=${w}x${ht} ratio=" + [math]::Round($w / $ht, 2)

$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[void][W2]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc)
$g.Dispose()
$bmp.Save('C:\Users\suman\.t3\worktrees\windows-port-killer\t3-2e8e4a58\tools\ui-shot.png', [System.Drawing.Imaging.ImageFormat]::Png)

$maxSpread = 0; $sum = 0; $n = 0; $white = 0; $dark = 0; $colored = 0
for ($y = 0; $y -lt $ht; $y += 3) {
  for ($x = 0; $x -lt $w; $x += 3) {
    $c = $bmp.GetPixel($x, $y)
    $mx = [math]::Max($c.R, [math]::Max($c.G, $c.B))
    $mn = [math]::Min($c.R, [math]::Min($c.G, $c.B))
    if (($mx - $mn) -gt $maxSpread) { $maxSpread = $mx - $mn }
    if (($mx - $mn) -gt 20) { $colored++ }
    $l = [int](($c.R + $c.G + $c.B) / 3)
    $sum += $l; $n++
    if ($l -gt 200) { $white++ }
    if ($l -lt 40) { $dark++ }
  }
}
"samples=$n maxChannelSpread=$maxSpread colored%=" + [math]::Round(100.0 * $colored / $n, 2) + " meanLuma=" + [math]::Round($sum / $n, 1) + " bright%=" + [math]::Round(100.0 * $white / $n, 1) + " dark%=" + [math]::Round(100.0 * $dark / $n, 1)

# coarse text rendering so the layout can be judged without an image viewer
$ramp = ' .:-=+*#%@'
$cols = 86; $rows = 34
$sb = New-Object System.Text.StringBuilder
for ($ry = 0; $ry -lt $rows; $ry++) {
  for ($rx = 0; $rx -lt $cols; $rx++) {
    $x0 = [int]($rx * $w / $cols); $x1 = [int](($rx + 1) * $w / $cols)
    $y0 = [int]($ry * $ht / $rows); $y1 = [int](($ry + 1) * $ht / $rows)
    $acc = 0; $cnt = 0
    for ($y = $y0; $y -lt $y1; $y += 2) {
      for ($x = $x0; $x -lt $x1; $x += 2) {
        $c = $bmp.GetPixel($x, $y); $acc += ($c.R + $c.G + $c.B) / 3; $cnt++
      }
    }
    $l = if ($cnt) { $acc / $cnt } else { 0 }
    $idx = [int][math]::Floor(($l / 255.0) * ($ramp.Length - 1) * 2.2)
    if ($idx -gt $ramp.Length - 1) { $idx = $ramp.Length - 1 }
    [void]$sb.Append($ramp[$idx])
  }
  [void]$sb.AppendLine()
}
$bmp.Dispose()
$sb.ToString()

$null = $p.CloseMainWindow(); Start-Sleep -Seconds 1; $p.Refresh()
if ($p.HasExited) { 'closed cleanly' } else { $p.Kill(); 'HUNG' }
