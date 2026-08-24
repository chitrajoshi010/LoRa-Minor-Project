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
set ROLE_OPTS=

if /i "%~1"=="node" goto set_node
if /i "%~1"=="relay" goto set_relay
if /i "%~1"=="gateway" goto set_gateway
if "%~1"=="" goto set_gateway
goto usage

:set_node
set TARGET=esp32s3
goto run

:set_relay
set TARGET=esp32s3
set ROLE_OPTS=-D CONFIG_LDSE_ROLE=1
goto run

:set_gateway
set TARGET=esp32
goto run

:usage
echo Usage: build_win.bat [gateway^|relay^|node^]
echo   gateway - esp32, role 0 ^(default^)
echo   relay   - esp32s3, role 1
echo   node    - esp32s3, role 2
exit /b 1

:run
echo ============================================
echo Target: %TARGET%   Role options: %ROLE_OPTS%
echo ============================================

"%IDF_PY_PYTHON%" "%IDF_PATH%\tools\idf.py" set-target %TARGET%
if errorlevel 1 exit /b 1

"%IDF_PY_PYTHON%" "%IDF_PATH%\tools\idf.py" build %ROLE_OPTS%
exit /b %errorlevel%
