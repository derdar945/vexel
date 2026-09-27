# VexExec 1.0.0 — run programs for Vexel (native, Win32)

```text
@ VexExec
> exec # "vexel.exe !vex_check main.vx", 10000
```

`exec/2`: runs the command line, catches merged stdout+stderr as text.
`fail` when it cannot start or the timeout (ms, clamped 100–600000)
runs out (the child is terminated). The child inherits the bench cwd;
`vexel.exe` must resolve via PATH or the current directory.
