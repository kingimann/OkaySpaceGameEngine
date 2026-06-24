@echo off
REM Upload the game in content\ to Steam (App 1172560, Depot 1172561) via SteamPipe.
REM
REM   1. Export your game from the editor (File ^> Build Game) into  steam\content\
REM   2. set STEAM_USER=your-builder-account
REM      upload.bat
REM      (first run prompts for password + Steam Guard; steamcmd caches it after)
REM   3. By default the build uploads but is NOT live - set it live on the
REM      Steamworks "Builds" page. To auto-publish, set "setlive" "default" in
REM      app_build_1172560.vdf.
REM
REM Needs steamcmd on PATH: https://developer.valvesoftware.com/wiki/SteamCMD
setlocal
cd /d "%~dp0"

if "%STEAM_USER%"=="" (
    echo error: set STEAM_USER to your Steamworks builder account first, e.g.  set STEAM_USER=me
    exit /b 1
)

steamcmd +login %STEAM_USER% +run_app_build "%cd%\app_build_1172560.vdf" +quit
echo Done. If setlive was empty, set the build live on the Steamworks Builds page.
