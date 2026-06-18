@echo off
REM Build OkaySpaceGameEngine natively on Windows (needs CMake + a C++ compiler,
REM e.g. Visual Studio Build Tools or MinGW).
setlocal
cd /d "%~dp0"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :error
cmake --build build --config Release
if errorlevel 1 goto :error

echo.
echo Build complete. Launch the demo with:  build\bin\sandbox.exe
goto :eof

:error
echo.
echo Build failed. Make sure CMake and a C++ compiler are installed and on PATH.
exit /b 1
