@echo off
setlocal

set IDF_ROOT=C:\Espressif
set IDF_PATH=%IDF_ROOT%\frameworks\esp-idf-v5.5.5
set IDF_TOOLS_PATH=%IDF_ROOT%

set IDF_EXPORT=%IDF_PATH%\export.bat
if not exist "%IDF_EXPORT%" (
    echo ERROR: ESP-IDF export.bat not found at %IDF_EXPORT%
    exit /b 1
)

pushd "%~dp0"
if errorlevel 1 (
    echo ERROR: Could not enter project directory. UNC path unsupported.
    exit /b 1
)

call "%IDF_EXPORT%"
if errorlevel 1 (
    echo ERROR: ESP-IDF environment setup failed.
    exit /b 1
)

rem export.bat sets IDF_PYTHON_ENV_PATH to the venv it activated. Invoke idf.py
rem via that venv's python.exe explicitly, rather than relying on PATH order,
rem so idf.py always runs with the venv that has "click" etc. installed
rem (avoids "No module named 'click'" when a bare/base interpreter or a second
rem installed venv wins the PATH race).
set "IDF_PY_PYTHON=python"
if not "%IDF_PYTHON_ENV_PATH%"=="" (
    if exist "%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" (
        set "IDF_PY_PYTHON=%IDF_PYTHON_ENV_PATH%\Scripts\python.exe"
    )
)

set TARGET=esp32
set ROLE=0

if /i "%~1"=="node" goto set_node
if /i "%~1"=="relay" goto set_relay
if /i "%~1"=="gateway" goto set_gateway
if "%~1"=="" goto set_gateway
goto usage

:set_node
set TARGET=esp32s3
set ROLE=2
goto run

:set_relay
set TARGET=esp32s3
set ROLE=1
goto run

:set_gateway
set TARGET=esp32
set ROLE=0
goto run

:usage
echo Usage: build_win.bat [gateway^|relay^|node^]
echo   gateway - esp32, role 0 ^(default^)
echo   relay   - esp32s3, role 1
echo   node    - esp32s3, role 2
exit /b 1

:run
echo ============================================
echo Target: %TARGET%   Role: %ROLE%
echo ============================================

rem "idf.py -D CONFIG_LDSE_ROLE=N build/set-target" does NOT reliably
rem override the role: ESP-IDF applies the top-level sdkconfig.defaults.
rem <target> file (which pins CONFIG_LDSE_ROLE=0 for esp32 / 2 for
rem esp32s3) as a base layer, then layers SDKCONFIG_DEFAULTS
rem (sdkconfig.defaults + sdkconfig.defaults.local) on top of it, and
rem -D command-line overrides lose to that merge. Concretely: relay
rem (esp32s3, role 1) diverges from the esp32s3 target default (role 2,
rem the same as node), so "build_win.bat relay" after a "node" build
rem previously produced a NODE binary with no error - this is what
rem silently got flashed to the relay board. Also, once sdkconfig
rem exists on disk with a role saved in it, that persisted value wins
rem over everything on the next configure too.
rem
rem Fix: force the intended role via a managed line in
rem sdkconfig.defaults.local (already part of SDKCONFIG_DEFAULTS and
rem git-ignored - see that file's header), which DOES win over
rem sdkconfig.defaults.<target>, and always delete the generated
rem sdkconfig first so the merge is re-applied instead of reusing a
rem stale persisted value.
set "LOCAL_DEFAULTS=%~dp0sdkconfig.defaults.local"
powershell -NoProfile -Command ^
    "$p = '%LOCAL_DEFAULTS%';" ^
    "$lines = if (Test-Path $p) { Get-Content $p | Where-Object { $_ -notmatch '^CONFIG_LDSE_ROLE=' } } else { @() };" ^
    "$lines + \"CONFIG_LDSE_ROLE=%ROLE%\" | Set-Content -Encoding utf8 $p"
if errorlevel 1 (
    echo ERROR: Failed to write CONFIG_LDSE_ROLE=%ROLE% into sdkconfig.defaults.local
    exit /b 1
)

if exist "%~dp0sdkconfig" del /f /q "%~dp0sdkconfig"

"%IDF_PY_PYTHON%" "%IDF_PATH%\tools\idf.py" set-target %TARGET%
if errorlevel 1 exit /b 1

"%IDF_PY_PYTHON%" "%IDF_PATH%\tools\idf.py" build
exit /b %errorlevel%
