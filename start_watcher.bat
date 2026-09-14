@echo off
setlocal enabledelayedexpansion
title GX - Gold Export
cd /d "%~dp0"

:: Nastaveni maleho a cisteho terminaloveho okna
mode con: cols=36 lines=5
cls

:: Overeni, zda je k dispozici funkcni Python 3
set "PY_CMD="

python -c "import sys; assert sys.version_info >= (3, 6)" >nul 2>nul
if %errorlevel% equ 0 (
    set "PY_CMD=python"
    goto :run
)

py -3 -c "import sys; assert sys.version_info >= (3, 6)" >nul 2>nul
if %errorlevel% equ 0 (
    set "PY_CMD=py -3"
    goto :run
)

:: Python chybi - instalace Python 3.13 z Microsoft Store
mode con: cols=60 lines=12
cls
echo ==========================================================
echo  Python 3 nebyl nalezen!
echo  Zahajuji instalaci Python 3.13 z Microsoft Store...
echo ==========================================================
echo.

where winget >nul 2>nul
if %errorlevel% equ 0 (
    echo Instaluji Python 3.13 pomoci winget...
    winget install --id 9PNRBTZXMB4Z --source msstore --accept-package-agreements --accept-source-agreements
    if %errorlevel% equ 0 (
        set "PATH=%LOCALAPPDATA%\Microsoft\WindowsApps;%PATH%"
        python -c "import sys; assert sys.version_info >= (3, 6)" >nul 2>nul
        if %errorlevel% equ 0 (
            set "PY_CMD=python"
            goto :ready
        )
    )
)

:: Zalozni zpusob: otevreni stranky v Microsoft Store
echo Oteviram Microsoft Store pro instalaci...
start ms-windows-store://pdp/?productid=9PNRBTZXMB4Z
echo.
echo Po dokonceni instalace v Microsoft Store
echo stisknete libovolnou klavesu pro pokracovani...
pause >nul

set "PATH=%LOCALAPPDATA%\Microsoft\WindowsApps;%PATH%"
python -c "import sys; assert sys.version_info >= (3, 6)" >nul 2>nul
if %errorlevel% equ 0 (
    set "PY_CMD=python"
    goto :ready
)

echo.
echo Python se nepodarilo overit. Spustte prosim tento soubor znovu.
pause
exit /b 1

:ready
mode con: cols=36 lines=5

:run
cls
%PY_CMD% watcher.py --compact
if %errorlevel% neq 0 (
    mode con: cols=60 lines=10
    echo.
    echo Skript watcher.py byl ukoncen.
    pause
)
