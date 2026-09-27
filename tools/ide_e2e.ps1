param([string]$Exe, [string]$WorkDir)
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class H {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string t);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr SendI(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr SendS(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  public static System.Collections.Generic.List<IntPtr> kids(IntPtr hw, string cls) {
    var r = new System.Collections.Generic.List<IntPtr>();
    IntPtr ch = GetWindow(hw, 5);
    while (ch != IntPtr.Zero) {
      var cn = new StringBuilder(64); GetClassNameW(ch, cn, 64);
      if (cn.ToString() == cls) r.Add(ch);
      ch = GetWindow(ch, 2);
    }
    return r;
  }
  public static string text(IntPtr h) {
    var tx = new StringBuilder(8192); GetWindowTextW(h, tx, 8192); return tx.ToString();
  }
}
"@

$env:Path = ([IO.Path]::GetDirectoryName($Exe) + ";") + $env:Path
$p = Start-Process -FilePath $Exe -ArgumentList "!vex_run main.vx" -WorkingDirectory $WorkDir -PassThru
Write-Output "pid=$($p.Id)"
$hw = [IntPtr]::Zero
for ($i = 0; $i -lt 40 -and $hw -eq [IntPtr]::Zero; $i++) {
  Start-Sleep -Milliseconds 250
  $hw = [H]::FindWindowW("VexGUIWindow", "VexEd")
}
Write-Output "hwnd=$hw alive=$(-not $p.HasExited)"
if ($hw -eq [IntPtr]::Zero) { exit 2 }
$lb = [H]::kids($hw, "ListBox")[0]
$n = [H]::SendI($lb, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero)
Write-Output "rows=$n"
$idx = [H]::SendS($lb, 0x018F, [IntPtr]::Zero, "hello_test.vx")
Write-Output "find=$idx"
[H]::SendI($lb, 0x0186, [IntPtr]$idx.ToInt32(), [IntPtr]::Zero) | Out-Null
$ed = [H]::kids($hw, "Edit")
$st = [H]::kids($hw, "Static")
$btns = [H]::kids($hw, "Button")
$open = $null; $run = $null
foreach ($b in $btns) {
  $t = [H]::text($b)
  if ($t -eq "Open") { $open = $b }
  if ($t -eq "Run") { $run = $b }
}
Write-Output "open=$open run=$run"
[H]::SendI($open, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
Start-Sleep -Seconds 1
Write-Output "edit0=[$([H]::text($ed[0]))]"
Write-Output "edit1=[$([H]::text($ed[1]))]"
Write-Output "st2=[$([H]::text($st[2]))]"
[H]::SendI($run, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
Start-Sleep -Seconds 4
Write-Output "run-edit0=[$([H]::text($ed[0]))]"
Write-Output "run-edit1=[$([H]::text($ed[1]))]"
[H]::PostMessageW($hw, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
$p.WaitForExit(8000) | Out-Null
Write-Output "exited=$($p.HasExited)"
Write-Output done
