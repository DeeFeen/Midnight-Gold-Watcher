@echo off
setlocal
cd /d "%~dp0"

:: ---------------------------------------------------------------
:: Build GX Monitor (MSVC, no external dependencies)
:: ---------------------------------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :no_vs

for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not exist "%VSDIR%" goto :no_msvc

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
if errorlevel 1 goto :no_vcvars

if not exist build mkdir build

rc /nologo /fo"build\\app.res" res\app.rc
if errorlevel 1 goto :failed

cl /nologo /std:c++17 /O2 /EHsc /W3 /utf-8 ^
   /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /D_CRT_SECURE_NO_WARNINGS ^
   /I"res" ^
   /Fo"build\\" /Fd"build\\GXMon.pdb" ^
   src\main.cpp src\ui.cpp src\watch.cpp src\app.cpp ^
   /link /OUT:"GXMonitor.exe" ^
   user32.lib gdi32.lib shell32.lib shlwapi.lib comctl32.lib comdlg32.lib ^
   dwmapi.lib ole32.lib advapi32.lib build\app.res /SUBSYSTEM:WINDOWS
if errorlevel 1 goto :failed

echo.
echo Build OK: GXMonitor.exe
goto :eof

:no_vs
echo ERROR: Visual Studio Installer not found. Install 'Desktop development with C++'.
exit /b 1

:no_msvc
echo ERROR: MSVC C++ x64 toolset not found.
exit /b 1

:no_vcvars
echo ERROR: vcvars64.bat failed.
exit /b 1

:failed
echo.
echo BUILD FAILED
exit /b 1