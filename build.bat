@echo off
REM build.bat - configure + build vexel.exe and VexGUI.dll with MinGW gcc
setlocal
cd /d "%~dp0"

set CC=
where gcc >nul 2>&1
if not errorlevel 1 goto have_cc
if exist "C:\mingw64\mingw64\bin\gcc.exe" set "CC=C:\mingw64\mingw64\bin\gcc.exe"
if defined CC set "PATH=C:\mingw64\mingw64\bin;%PATH%"
:have_cc

set CMAKE_EXE=cmake
where cmake >nul 2>&1
if errorlevel 1 set "CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe"

if exist build\CMakeCache.txt (
    findstr /c:"MinGW Makefiles" build\CMakeCache.txt >nul 2>&1
    if errorlevel 1 rmdir /s /q build
)
if not exist build mkdir build

if defined CC (
    "%CMAKE_EXE%" -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="%CC%"
) else (
    "%CMAKE_EXE%" -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
)
if errorlevel 1 (
    echo cmake configure failed
    exit /b 1
)
"%CMAKE_EXE%" --build build
if errorlevel 1 (
    echo build failed
    exit /b 1
)
copy /y build\vexel.exe vexel.exe >nul
echo.
echo built: vexel.exe + operators\VexGUI\VexGUI.dll
vexel.exe !vex_version
endlocal
