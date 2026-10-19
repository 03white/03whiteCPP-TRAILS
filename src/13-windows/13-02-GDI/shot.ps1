# Drive the demo deterministically and screenshot just its window.
#   powershell -ExecutionPolicy Bypass -File shot.ps1 -Keys "nihao{F3}" -Out a.png
# Keys uses SendKeys syntax: {F1}..{F6} {ESC} {BS} {UP} {DOWN} etc.
# Keep this file ASCII-only.
param(
  [string]$Keys = "",
  [string]$Out  = "shot.png",
  [int]$Settle  = 600,
  [int]$Pad     = 40,
  [string]$Exe  = "13-02-GDI"   # process name, no .exe
)

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool f);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  public struct R { public int L, T, Rt, B; }

  // Windows refuses cross-process foreground steals unless the calling thread
  // shares an input queue with the current foreground thread. Attach, steal,
  // detach -- the standard workaround.
  public static void ForceForeground(IntPtr h) {
    uint cur = GetWindowThreadProcessId(GetForegroundWindow(), IntPtr.Zero);
    uint me  = GetCurrentThreadId();
    AttachThreadInput(cur, me, true);
    BringWindowToTop(h);
    SetForegroundWindow(h);
    AttachThreadInput(cur, me, false);
  }
}
"@

# PowerShell is DPI-unaware by default, so GetWindowRect on a per-monitor-aware
# window comes back virtualized while CopyFromScreen works in physical pixels.
# Opt in to physical pixels so the two agree.
[W]::SetProcessDPIAware() | Out-Null

$proc = Get-Process $Exe -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { Write-Error "$Exe.exe is not running"; exit 1 }
$h = $proc.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Error "no main window"; exit 1 }

[W]::ShowWindow($h, 9) | Out-Null   # SW_RESTORE
# Park it at a fixed spot so screenshots are reproducible (SWP_NOSIZE|SWP_NOZORDER).
[W]::SetWindowPos($h, [IntPtr]::Zero, 80, 80, 0, 0, 0x0001 -bor 0x0004) | Out-Null
[W]::ForceForeground($h)
Start-Sleep -Milliseconds 500
if ([W]::GetForegroundWindow() -ne $h) {
  Write-Error "could not bring the demo window to the foreground - keys would go elsewhere"
  exit 1
}

if ($Keys -ne "") {
  # One key at a time, so the panel re-lays-out on every keystroke like real typing.
  $i = 0
  while ($i -lt $Keys.Length) {
    $tok = ""
    # ^ + % are modifier prefixes -- they must stay glued to the key that
    # follows, otherwise "^3" would be sent as a bare "^" and then a bare "3".
    while ($i -lt $Keys.Length -and '^+%'.Contains($Keys[$i])) {
      $tok += $Keys[$i]; $i++
    }
    if ($i -lt $Keys.Length -and $Keys[$i] -eq '{') {
      $end = $Keys.IndexOf('}', $i)
      $tok += $Keys.Substring($i, $end - $i + 1)
      $i = $end + 1
    } elseif ($i -lt $Keys.Length) {
      $tok += $Keys[$i]
      $i++
    }
    [System.Windows.Forms.SendKeys]::SendWait($tok)
    Start-Sleep -Milliseconds 70
  }
}
Start-Sleep -Milliseconds $Settle

# Crop to the host window plus padding, so the popup (which can sit outside) is kept.
$r = New-Object W+R
[W]::GetWindowRect($h, [ref]$r) | Out-Null
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$x = [Math]::Max($screen.Left,  $r.L - $Pad)
$y = [Math]::Max($screen.Top,   $r.T - $Pad)
$w = [Math]::Min($screen.Right  - $x, ($r.Rt + $Pad) - $x)
$ht= [Math]::Min($screen.Bottom - $y, ($r.B  + $Pad) - $y)

$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen((New-Object System.Drawing.Point $x, $y), [System.Drawing.Point]::Empty,
                  (New-Object System.Drawing.Size $w, $ht))
$bmp.Save((Join-Path $PSScriptRoot $Out))
$g.Dispose(); $bmp.Dispose()
Write-Output "saved $Out  ($w x $ht)"
