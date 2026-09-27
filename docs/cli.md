# CLI — everything starts with !vex_

```text
!vex_run <file.vx|file.vxb>
!vex_check <file.vx> [-v]      (-v disassembles)
!vex_build <file.vx> [out.vxb] (VXB2; VXB1 still loads)
!vex_repl                      (session replays; @ attach works)
!vex_version
!vex_help

!vex_new <Project>             scaffold with main.vx
!vex_add <Operator>            install + deps
!vex_remove <Operator>         refuse while needed
!vex_update <Operator>
!vex_list
!vex_info <Operator>
!vex_search <term>
!vex_operator_new <Operator>   C template + manifest + example
!vex_operator_build [dir]      compile entry DLL (needs gcc)
!vex_operator_install <dir>    install explicit directory
!vex_registry_add <dir|http>   add an operator source
!vex_registry_list
!vex_registry_remove <dir|http>

!vex_fmt <file.vx> [-w]         normalize indentation
!vex_lint <file.vx>             static grumbles (0 clean / 1 warns / 2 broken)
!vex_test <file.vx>             run & test_* tools, wet PASS
!vex_debug <file.vx>            step the real VM (s/r/g/v/d/q)
!vex_lsp                       stdio language server (JSON-RPC)
```

Works as `vexel.exe !vex_run main.vx` in plain Windows CMD
(`!` is literal there; keep delayed expansion off in .bat files
that forward these, and mind LF-vs-CRLF: batch needs CRLF).

Typical session:

```text
vexel.exe !vex_operator_build operators/VexGUI
vexel.exe !vex_add VexGUI
vexel.exe !vex_run examples/gui_hello.vx
```
