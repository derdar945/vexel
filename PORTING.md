# Porting Vexel off Windows

Vexel builds and runs on Windows today. The Core language, compiler
and VM are portable C17; the platform locks are listed here so a
porter knows exactly what to replace. No port is claimed until it
is built and `tests\run_tests.bat` (or its sh twin) passes on it.

## Windows-only spots

| Area | File | Uses | Portable swap |
|------|------|------|---------------|
| Operator loader | `src/oploader.c` | LoadLibrary/GetProcAddress/FreeLibrary | dlopen/dlsym/dlclose |
| Operator manager | `src/opman.c` | FindFirstFile, CreateDirectory, rmdir, WinINet downloads | dirent/mkdir + libcurl or sockets |
| Registry fetch | `src/opman.c` | `wininet.h` | same as above |
| VexGUI backend | `operators/VexGUI` | Win32 windows/controls | Cocoa/Qt-agnostic redraw — new backend |
| VexFS / VexExec | `operators/VexFS`, `VexExec` | Win32 file/process APIs | POSIX open/fork/exec |
| VexNet | `operators/VexNet` | Winsock | BSD sockets (near-mechanical) |
| VexGame | `operators/VexGame` | Win32 + GDI DIB | any blitter |
| Tests/E2E | `tests/**` | `.bat`, PowerShell, Win32 GUI calls | sh + platform harness |

## Already portable

`lexer.c parser.c compiler.c vm.c runtime.c fmt.c lint.c vtest.c
lsp.c dbg.c`, operators `VexRand`-style pure-C code, the `.vx`
benches/tests/examples, the VS Code extension (Node side).

## CMake

On non-Windows, operator DLL targets and `wininet`/`ws2_32` links
must be skipped or retargeted — see the `if (WIN32)` guards in
`CMakeLists.txt`. The `vexel` binary itself should configure cleanly.
