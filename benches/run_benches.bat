@echo off
REM benches\run_benches.bat — timing baselines (machine-dependent).
setlocal
cd /d "%~dp0\.."
set VX=%CD%\vexel.exe
if not exist "%VX%" set VX=%CD%\build\vexel.exe
echo --- bench_fib (fib 22, recursion) ---
"%VX%" !vex_run benches\bench_fib.vx
echo --- bench_loop (200k rounds) ---
"%VX%" !vex_run benches\bench_loop.vx
echo --- bench_calls (50k summons) ---
"%VX%" !vex_run benches\bench_calls.vx
endlocal
