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

set "PATH=%IDF_ROOT%\tools\idf-python\3.11.2;%IDF_ROOT%\tools\idf-git\2.44.0\cmd;%PATH%"

call "%IDF_EXPORT%"
if errorlevel 1 (
    echo ERROR: ESP-IDF environment setup failed.
    exit /b 1
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

python "%IDF_PATH%\tools\idf.py" set-target %TARGET%
if errorlevel 1 exit /b 1

python "%IDF_PATH%\tools\idf.py" build %ROLE_OPTS%
exit /b %errorlevel%
