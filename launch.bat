@echo off
REM OkaySpace launcher (Windows): update from GitHub, build, and run.
setlocal
cd /d "%~dp0"

set LAUNCHER=build\bin\okayspace-launcher.exe
if not exist "%LAUNCHER%" (
    echo [launch] First run: building the launcher...
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --target okayspace-launcher
)

"%LAUNCHER%" %*
