@echo off
REM Vexel test suite: core, CLI, operators, ABI, VexGUI.
setlocal
cd /d "%~dp0\.."
REM known local toolchains so !vex_operator_build is testable
set PATH=C:\mingw64\mingw64\bin;C:\mingw64-posix\bin;%PATH%
set VX=%CD%\vexel.exe
if not exist "%VX%" set VX=%CD%\build\vexel.exe
if not exist "%VX%" (
    echo vexel.exe not found, run build.bat first
    exit /b 1
)
set PASS=0
set FAIL=0

echo ==============================
echo  VEXEL TEST SUITE
echo ==============================
echo.

call :section_core
call :section_cli
call :section_operators
call :section_gui
call :section_selfhost

echo.
echo ==============================
echo  Core tests:       %CORE%
echo  CLI tests:        %CLI%
echo  Operator tests:   %OPS%
echo  VexGUI tests:     %GUI%
echo  Selfhost tests:   %SELF%
echo ==============================
echo.
if "%FAIL%"=="0" (
    echo ALL TESTS PASSED
    exit /b 0
) else (
    echo %FAIL% TESTS FAILED
    exit /b 1
)

:ok
set /a PASS+=1
echo ok %~1
exit /b 0

:expect_rc
if "%ERRORLEVEL%"=="%~1" ( call :ok "%~2" ) else ( call :bad "%~2 (rc=%ERRORLEVEL%, want %~1)" & set CLI=FAIL )
exit /b 0

:bad
set /a FAIL+=1
echo FAIL %~1
exit /b 0

REM ---------- core ----------
:section_core
set CORE=PASS
for %%F in (tests\core\t_*.vx) do (
    "%VX%" !vex_run "%%F" > "%TEMP%\vex_got.txt" 2>&1
    fc "%TEMP%\vex_got.txt" "tests\core\%%~nF.expected" >nul 2>&1
    if errorlevel 1 (
        call :bad "%%F"
        set CORE=FAIL
    ) else (
        call :ok "%%F"
    )
)
for %%F in (tests\core\e_*.vx) do (
    "%VX%" !vex_check "%%F" >nul 2>&1
    if errorlevel 1 (
        call :ok "%%F (fails as expected)"
    ) else (
        call :bad "%%F (should not compile)"
        set CORE=FAIL
    )
)
exit /b 0

REM ---------- CLI ----------
:section_cli
set CLI=PASS
"%VX%" !vex_version >nul 2>&1
if errorlevel 1 ( call :bad "!vex_version" & set CLI=FAIL ) else ( call :ok "!vex_version" )
"%VX%" !vex_help | findstr /c:"!vex_run" >nul 2>&1
if errorlevel 1 ( call :bad "!vex_help" & set CLI=FAIL ) else ( call :ok "!vex_help" )
rmdir /s /q "%TEMP%\vexproj" >nul 2>&1
mkdir "%TEMP%\vexproj"
cd "%TEMP%\vexproj"
"%VX%" !vex_new TestProject >nul 2>&1
if errorlevel 1 ( call :bad "!vex_new" & set CLI=FAIL ) else ( call :ok "!vex_new" )
if not exist TestProject\vexel.json ( call :bad "vexel.json scaffold" & set CLI=FAIL ) else ( call :ok "vexel.json scaffold" )
"%VX%" !vex_run TestProject\main.vx > "%TEMP%\vex_got.txt" 2>&1
findstr /c:"Hello from TestProject" "%TEMP%\vex_got.txt" >nul 2>&1
if errorlevel 1 ( call :bad "new project runs" & set CLI=FAIL ) else ( call :ok "new project runs" )
"%VX%" !vex_check TestProject\main.vx >nul 2>&1
if errorlevel 1 ( call :bad "!vex_check" & set CLI=FAIL ) else ( call :ok "!vex_check" )
"%VX%" !vex_build TestProject\main.vx TestProject\main.vxb >nul 2>&1
if errorlevel 1 ( call :bad "!vex_build" & set CLI=FAIL ) else ( call :ok "!vex_build" )
"%VX%" !vex_run TestProject\main.vxb > "%TEMP%\vex_got2.txt" 2>&1
fc "%TEMP%\vex_got.txt" "%TEMP%\vex_got2.txt" >nul 2>&1
if errorlevel 1 ( call :bad ".vxb roundtrip" & set CLI=FAIL ) else ( call :ok ".vxb roundtrip" )
cd /d "%~dp0\.."
"%VX%" !vex_fmt examples\fact.vx > "%TEMP%\vex_fmt.txt" 2>&1
if errorlevel 1 ( call :bad "!vex_fmt runs" & set CLI=FAIL ) else ( call :ok "!vex_fmt runs" )
"%VX%" !vex_run "%TEMP%\vex_fmt.txt" >nul 2>&1
if errorlevel 1 ( call :bad "formatted still runs" & set CLI=FAIL ) else ( call :ok "formatted still runs" )
"%VX%" !vex_lint tests\core\lint_demo.vx >nul 2>&1
call :expect_rc 1 "!vex_lint sees warnings"
"%VX%" !vex_lint examples\hello.vx >nul 2>&1
if errorlevel 1 ( call :bad "!vex_lint clean file" & set CLI=FAIL ) else ( call :ok "!vex_lint clean file" )
"%VX%" !vex_test tests\core\t_testdemo.vx | findstr /c:"3 passed, 1 failed" >nul 2>&1
if errorlevel 1 ( call :bad "!vex_test report" & set CLI=FAIL ) else ( call :ok "!vex_test report" )
rmdir /s /q "%TEMP%\vexproj" >nul 2>&1
exit /b 0

REM ---------- operators + ABI ----------
:section_operators
set OPS=PASS
for %%F in (tests\operators\t_*.vx) do (
    "%VX%" !vex_run "%%F" > "%TEMP%\vex_got.txt" 2>&1
    fc "%TEMP%\vex_got.txt" "tests\operators\%%~nF.expected" >nul 2>&1
    if errorlevel 1 (
        call :bad "%%F"
        set OPS=FAIL
    ) else (
        call :ok "%%F"
    )
)
for %%F in (tests\operators\e_*.vx) do (
    "%VX%" !vex_check "%%F" >nul 2>&1
    if errorlevel 1 (
        call :ok "%%F (fails as expected)"
    ) else (
        call :bad "%%F (should not compile)"
        set OPS=FAIL
    )
)
REM manager sandbox
set VEXEL_OPS=%TEMP%\vexstore
rmdir /s /q "%VEXEL_OPS%" >nul 2>&1
"%VX%" !vex_add VexGUI >nul 2>&1
if errorlevel 1 ( call :bad "!vex_add VexGUI" & set OPS=FAIL ) else ( call :ok "!vex_add VexGUI" )
"%VX%" !vex_list | findstr /c:"VexGUI" >nul 2>&1
if errorlevel 1 ( call :bad "!vex_list" & set OPS=FAIL ) else ( call :ok "!vex_list" )
"%VX%" !vex_info VexGUI | findstr /c:"Provides" >nul 2>&1
if errorlevel 1 ( call :bad "!vex_info" & set OPS=FAIL ) else ( call :ok "!vex_info" )
"%VX%" !vex_search Vex | findstr /c:"VexGUI" >nul 2>&1
if errorlevel 1 ( call :bad "!vex_search" & set OPS=FAIL ) else ( call :ok "!vex_search" )
"%VX%" !vex_add NopeMissing >nul 2>&1
if errorlevel 1 ( call :ok "missing operator fails" ) else ( call :bad "missing operator should fail" & set OPS=FAIL )
call :dep_fixtures
"%VX%" !vex_add NeedDep >nul 2>&1
if errorlevel 1 ( call :ok "missing dependency fails" ) else ( call :bad "missing dependency should fail" & set OPS=FAIL )
"%VX%" !vex_add CycleA >nul 2>&1
if errorlevel 1 ( call :ok "cycle fails" ) else ( call :bad "cycle should fail" & set OPS=FAIL )
cd /d "%~dp0\.."
"%VX%" !vex_remove VexGUI >nul 2>&1
if errorlevel 1 ( call :bad "!vex_remove" & set OPS=FAIL ) else ( call :ok "!vex_remove" )
if exist "%VEXEL_OPS%\VexGUI" ( call :bad "remove cleans store" & set OPS=FAIL ) else ( call :ok "remove cleans store" )
REM custom operator: new, build, install, summon (ABI clone path)
where gcc >nul 2>&1
if errorlevel 1 (
    echo skip custom operator build ^(no gcc^)
) else (
    rmdir /s /q "%TEMP%\vexcustom" >nul 2>&1
    mkdir "%TEMP%\vexcustom"
    cd "%TEMP%\vexcustom"
    "%VX%" !vex_operator_new Demo >nul 2>&1
    if errorlevel 1 ( call :bad "!vex_operator_new" & set OPS=FAIL ) else ( call :ok "!vex_operator_new" )
    "%VX%" !vex_operator_build Demo >nul 2>&1
    if errorlevel 1 ( call :bad "!vex_operator_build" & set OPS=FAIL ) else ( call :ok "!vex_operator_build" )
    "%VX%" !vex_add Demo >nul 2>&1
    if errorlevel 1 ( call :bad "!vex_add Demo" & set OPS=FAIL ) else ( call :ok "!vex_add Demo" )
    (echo @ Demo) > demo.vx
    (echo ^> ping # 41 + 1) >> demo.vx
    "%VX%" !vex_run demo.vx > "%TEMP%\vex_got.txt" 2>&1
    echo 42> "%TEMP%\vex_want.txt"
    fc "%TEMP%\vex_got.txt" "%TEMP%\vex_want.txt" >nul 2>&1
    if errorlevel 1 ( call :bad "custom operator summons" & set OPS=FAIL ) else ( call :ok "custom operator summons" )
    cd /d "%~dp0\.."
    rmdir /s /q "%TEMP%\vexcustom" >nul 2>&1
)
rmdir /s /q "%VEXEL_OPS%" >nul 2>&1
rmdir /s /q "%TEMP%\vexdep" >nul 2>&1
set VEXEL_OPS=
exit /b 0

:dep_fixtures
rmdir /s /q "%TEMP%\vexdep" >nul 2>&1
mkdir "%TEMP%\vexdep\operators\NeedDep"
mkdir "%TEMP%\vexdep\operators\CycleA"
mkdir "%TEMP%\vexdep\operators\CycleB"
mkdir "%TEMP%\vexdep\operators\BadOp"
echo name = NeedDep> "%TEMP%\vexdep\operators\NeedDep\operator.vxop"
echo version = 1.0.0>> "%TEMP%\vexdep\operators\NeedDep\operator.vxop"
echo type = native>> "%TEMP%\vexdep\operators\NeedDep\operator.vxop"
echo vexel_version = 0.1.0>> "%TEMP%\vexdep\operators\NeedDep\operator.vxop"
echo platform = any>> "%TEMP%\vexdep\operators\NeedDep\operator.vxop"
echo entry = NeedDep.dll>> "%TEMP%\vexdep\operators\NeedDep\operator.vxop"
echo provides = ping/1>> "%TEMP%\vexdep\operators\NeedDep\operator.vxop"
echo dependencies = VexNet ^>^= 1.0>> "%TEMP%\vexdep\operators\NeedDep\operator.vxop"
echo name = CycleA> "%TEMP%\vexdep\operators\CycleA\operator.vxop"
echo version = 1.0.0>> "%TEMP%\vexdep\operators\CycleA\operator.vxop"
echo type = native>> "%TEMP%\vexdep\operators\CycleA\operator.vxop"
echo vexel_version = 0.1.0>> "%TEMP%\vexdep\operators\CycleA\operator.vxop"
echo platform = any>> "%TEMP%\vexdep\operators\CycleA\operator.vxop"
echo entry = CycleA.dll>> "%TEMP%\vexdep\operators\CycleA\operator.vxop"
echo provides = ping/1>> "%TEMP%\vexdep\operators\CycleA\operator.vxop"
echo dependencies = CycleB>> "%TEMP%\vexdep\operators\CycleA\operator.vxop"
echo name = CycleB> "%TEMP%\vexdep\operators\CycleB\operator.vxop"
echo version = 1.0.0>> "%TEMP%\vexdep\operators\CycleB\operator.vxop"
echo type = native>> "%TEMP%\vexdep\operators\CycleB\operator.vxop"
echo vexel_version = 0.1.0>> "%TEMP%\vexdep\operators\CycleB\operator.vxop"
echo platform = any>> "%TEMP%\vexdep\operators\CycleB\operator.vxop"
echo entry = CycleB.dll>> "%TEMP%\vexdep\operators\CycleB\operator.vxop"
echo provides = ping/1>> "%TEMP%\vexdep\operators\CycleB\operator.vxop"
echo dependencies = CycleA>> "%TEMP%\vexdep\operators\CycleB\operator.vxop"
echo broken line without equals> "%TEMP%\vexdep\operators\BadOp\operator.vxop"
cd /d "%TEMP%\vexdep"
exit /b 0

REM ---------- VexGUI ----------
:section_gui
set GUI=PASS
"%VX%" !vex_run tests\operators\t_gui_smoke.vx > "%TEMP%\vex_got.txt" 2>&1
fc "%TEMP%\vex_got.txt" "tests\operators\t_gui_smoke.expected" >nul 2>&1
if errorlevel 1 ( call :bad "gui smoke" & set GUI=FAIL ) else ( call :ok "gui smoke" )
powershell -NoProfile -ExecutionPolicy Bypass -File tests\operators\gui_window.ps1 "%VX%" "%CD%\tests\operators\gui_e2e.vx" "VexWindow" > "%TEMP%\vex_gui.txt" 2>&1
if errorlevel 1 (
    call :bad "gui e2e"
    type "%TEMP%\vex_gui.txt"
    set GUI=FAIL
) else (
    call :ok "gui e2e"
)
exit /b 0

REM ---------- selfhost: Vexel lexer written in Vexel ----------
:section_selfhost
set SELF=PASS
copy /y vex\lex_input.vx lex_input.vx >nul 2>&1
"%VX%" !vex_run vex\lex.vx > "%TEMP%\vex_got.txt" 2>&1
if errorlevel 1 ( call :bad "selfhost lex runs" & set SELF=FAIL ) else (
    fc "%TEMP%\vex_got.txt" "tests\selfhost\lex.expected" >nul 2>&1
    if errorlevel 1 ( call :bad "selfhost lex tokens" & set SELF=FAIL ) else ( call :ok "selfhost lex tokens" )
)
del lex_input.vx >nul 2>&1
exit /b 0
