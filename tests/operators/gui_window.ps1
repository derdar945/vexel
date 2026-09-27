# gui_window.ps1 - real VexGUI end-to-end: window, click, text, close.
# usage: powershell -NoProfile -ExecutionPolicy Bypass -File gui_window.ps1 <vexel.exe> <bench.vx> <title>
param([string]$Vexel, [string]$Bench, [string]$Title)
$ErrorActionPreference = "Stop"

$code = @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class Z {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string t);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  public static IntPtr child(IntPtr hw, string cls) {
    IntPtr ch = GetWindow(hw, 5);
    while (ch != IntPtr.Zero) {
      var cn = new StringBuilder(64); GetClassNameW(ch, cn, 64);
      if (cn.ToString() == cls) return ch;
      ch = GetWindow(ch, 2);
    }
    return IntPtr.Zero;
  }
  public static string text(IntPtr h) {
    var tx = new StringBuilder(256); GetWindowTextW(h, tx, 256); return tx.ToString();
  }
}
"@
Add-Type -TypeDefinition $code

function Fail($m) { Write-Output "FAIL $m"; exit 1 }

$out = [IO.Path]::GetTempFileName()
$p = Start-Process -FilePath $Vexel -ArgumentList "!vex_run $Bench" -PassThru -RedirectStandardOutput $out
try {
  $hw = [IntPtr]::Zero
  for ($i = 0; $i -lt 40 -and $hw -eq [IntPtr]::Zero; $i++) {
    Start-Sleep -Milliseconds 250
    $hw = [Z]::FindWindowW("VexGUIWindow", $Title)
  }
  if ($hw -eq [IntPtr]::Zero) { Fail "window not found" }
  Write-Output "ok window"
  $btn = [Z]::child($hw, "Button")
  if ($btn -eq [IntPtr]::Zero) { Fail "button not found" }
  [Z]::SendMessageW($btn, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
  Start-Sleep -Seconds 1
  $st = [Z]::child($hw, "Static")
  $t = [Z]::text($st)
  if ($t -ne "Hello, !") { Fail "static text is [$t]" }
  Write-Output "ok click"
  [Z]::PostMessageW($hw, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
  if (-not $p.WaitForExit(8000)) { Fail "did not exit after close" }
  $lines = @(Get-Content $out)
  if ($lines.Count -lt 3 -or $lines[-1] -ne "bye") { Fail "bad output" }
  Write-Output "ok close"
  Write-Output "GUI E2E PASSED"
  exit 0
} finally {
  try { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } } catch {}
  Remove-Item $out -Force -ErrorAction SilentlyContinue
}
