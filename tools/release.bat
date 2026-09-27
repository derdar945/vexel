@echo off
REM tools\release.bat — assemble the Windows release layout (see README Releases).
setlocal
cd /d "%~dp0\.."
set OUT=release\Vexel
rmdir /s /q release >nul 2>&1
mkdir "%OUT%\Windows"
mkdir "%OUT%\Windows\examples"
mkdir "%OUT%\Windows\docs"
mkdir "%OUT%\Windows\operators\VexGUI"

copy /y vexel.exe "%OUT%\Windows\" || exit /b 1
copy /y operators\VexGUI\VexGUI.dll "%OUT%\Windows\operators\VexGUI\" || exit /b 1
copy /y operators\VexGUI\operator.vxop "%OUT%\Windows\operators\VexGUI\" || exit /b 1
xcopy /e /i /y examples "%OUT%\Windows\examples" >nul || exit /b 1
xcopy /e /i /y docs "%OUT%\Windows\docs" >nul || exit /b 1
copy /y README.md "%OUT%\Windows\" >nul
copy /y VEXEL_DESIGN.md "%OUT%\Windows\" >nul
copy /y LICENSE "%OUT%\Windows\" >nul
echo release ready: %OUT%\Windows
endlocal
