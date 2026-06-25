@echo off
REM Publish the whole OkaySpace application to Steam (App 1172560, Depot 1172561)
REM via SteamPipe, so installed copies auto-update.
REM
REM It uploads the distribution from build-win-dist\ (OkaySpace.exe + SDL2.dll +
REM Tools\). Build that first (on Windows, run cmake to produce the exes and copy
REM them + SDL2.dll into ..\build-win-dist\ in the docs/packaging.md layout), then:
REM
REM   set STEAM_USER=your-builder-account
REM   upload.bat
REM
REM By default the build uploads but is NOT live - set it live on the Steamworks
REM "Builds" page. To auto-publish, set "setlive" "default" in app_build_1172560.vdf.
REM
REM Needs steamcmd on PATH: https://developer.valvesoftware.com/wiki/SteamCMD
setlocal
cd /d "%~dp0"

if "%STEAM_USER%"=="" (
    echo error: set STEAM_USER to your Steamworks builder account first, e.g.  set STEAM_USER=me
    exit /b 1
)
if not exist "..\build-win-dist\OkaySpace.exe" (
    echo error: ..\build-win-dist\OkaySpace.exe not found - build the distribution first.
    exit /b 1
)

steamcmd +login %STEAM_USER% +run_app_build "%cd%\app_build_1172560.vdf" +quit
echo Done. If setlive was empty, set the build live on the Steamworks Builds page.
