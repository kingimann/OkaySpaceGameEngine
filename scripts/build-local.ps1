<#
.SYNOPSIS
  Builds the OkaySpace programs on your own Windows PC (editor, launcher, player, sandbox, tests).
.DESCRIPTION
  UNVERIFIED: written on a Linux machine; it has not been run on Windows. It only calls CMake the way the
  CMake options in CMakeLists.txt describe, so a failure here is most likely a missing tool.
  scripts\build.bat already builds the small sandbox demo; this script adds the editor, launcher and player
  and lets you pick one.

  You need: CMake 3.20+ and a C++17 compiler (Visual Studio 2022 Build Tools, or MinGW with Ninja). No third-party
  library: the window, input and audio are our own (okay::platform).

  Programs end up in <BuildDir>\bin (OkayEngine.exe = editor, OkaySpace.exe = launcher, OkaySpacePlayer.exe = player).

  The same recipe is in C++ as `okay engine build` (tools/okay-cli/Engine.cpp), which needs no PowerShell and finds
  the tools itself. This script stays to build okay itself on a PC that has no okay yet. The two must build
  the same way: tests/test_okay_cli.cpp checks that this file still has the CMake options and targets okay uses, so
  change both together.
.PARAMETER Target
  What to build: All (default), Editor, Launcher, Player, Sandbox or Tests.
.PARAMETER Config
  Release (default) or Debug.
.PARAMETER BuildDir
  Where the build files and programs go. Default: build-local (inside the repository, git-ignored by build*).
.PARAMETER Generator
  Auto (default: Ninja if it is on PATH, else Visual Studio), or any CMake generator name.
.PARAMETER Jobs
  Parallel compile jobs. Default: half of your logical processors, at least 2.
.PARAMETER Clean
  Delete the build folder first (only the folder named by -BuildDir, only if it is inside the repository).
.PARAMETER Run
  Start the editor (Editor target) or launcher (Launcher target) when the build finishes.
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\build-local.ps1 -Target Editor
#>
[CmdletBinding()]
param(
    [ValidateSet('All', 'Editor', 'Launcher', 'Player', 'Sandbox', 'Tests')]
    [string]$Target = 'All',
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release',
    [string]$BuildDir = 'build-local',
    [string]$Generator = 'Auto',
    [int]$Jobs = 0,
    [switch]$Clean,
    [switch]$Run
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Fail([string]$msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { Fail 'CMake was not found on PATH. Install CMake 3.20 or newer.' }

$buildPath = [System.IO.Path]::GetFullPath((Join-Path $root $BuildDir))
if (-not $buildPath.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase) -or $buildPath -eq $root) {
    Fail "BuildDir must be a folder inside the repository (got $buildPath)."
}
if ($Clean -and (Test-Path $buildPath)) {
    Write-Host "Removing $buildPath"
    Remove-Item -Recurse -Force $buildPath
}

if ($Jobs -le 0) { $Jobs = [Math]::Max(2, [int]([Environment]::ProcessorCount / 2)) }

# Which CMake options and targets each choice needs.
$editor   = ($Target -in 'All', 'Editor')
$launcher = ($Target -in 'All', 'Launcher')
$player   = ($Target -in 'All', 'Player')
$sandbox  = ($Target -in 'All', 'Sandbox')
$tests    = ($Target -eq 'Tests')
function OnOff($b) { if ($b) { 'ON' } else { 'OFF' } }

$cfgArgs = @(
    '-S', $root, '-B', $buildPath,
    "-DOKAY_BUILD_EDITOR=$(OnOff $editor)",
    "-DOKAY_BUILD_LAUNCHER=$(OnOff $launcher)",
    "-DOKAY_BUILD_PLAYER=$(OnOff $player)",
    "-DOKAY_BUILD_SANDBOX=$(OnOff $sandbox)",
    "-DOKAY_BUILD_TESTS=$(OnOff $tests)"
)

$useNinja = $false
if ($Generator -eq 'Auto') {
    if (Get-Command ninja -ErrorAction SilentlyContinue) { $cfgArgs += @('-G', 'Ninja'); $useNinja = $true }
    # else: CMake picks the newest Visual Studio it finds.
} else {
    $cfgArgs += @('-G', $Generator)
    $useNinja = ($Generator -like 'Ninja*')
}
if ($useNinja -or $Generator -like 'Unix*') { $cfgArgs += "-DCMAKE_BUILD_TYPE=$Config" }

Write-Host "Configuring ($Target, $Config) in $buildPath"
& cmake @cfgArgs
if ($LASTEXITCODE -ne 0) { Fail 'CMake configure failed. The first error above is the one to read.' }

$build = @('--build', $buildPath, '--config', $Config, '--parallel', "$Jobs")
$targets = @()
if ($editor)   { $targets += 'okay-editor' }
if ($launcher) { $targets += 'okayspace-launcher' }
if ($player)   { $targets += 'okay-player' }
if ($sandbox)  { $targets += 'sandbox' }
if ($targets.Count -gt 0) { $build += @('--target') + $targets }

Write-Host "Building with $Jobs jobs"
& cmake @build
if ($LASTEXITCODE -ne 0) { Fail 'The build failed. The first error above is the one to read.' }

if ($tests) {
    Write-Host 'Running the tests'
    & ctest --test-dir $buildPath -C $Config --output-on-failure -j $Jobs
    if ($LASTEXITCODE -ne 0) { Fail 'Some tests failed.' }
}

$bin = Join-Path $buildPath 'bin'
if (-not (Test-Path (Join-Path $bin '*.exe')) -and (Test-Path (Join-Path $bin $Config))) { $bin = Join-Path $bin $Config }
Write-Host "Build finished. Programs are in $bin" -ForegroundColor Green
Get-ChildItem -Path $bin -Filter *.exe -ErrorAction SilentlyContinue | ForEach-Object { Write-Host "  $($_.Name)" }

if ($Run) {
    $exe = if ($editor -and -not $launcher) { 'OkayEngine.exe' } elseif ($launcher) { 'OkaySpace.exe' } else { $null }
    if ($exe -and (Test-Path (Join-Path $bin $exe))) { Start-Process (Join-Path $bin $exe) }
    else { Write-Host '-Run: nothing to start for this target.' }
}
