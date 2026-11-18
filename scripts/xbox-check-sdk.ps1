<#
.SYNOPSIS
  EXPERIMENTAL, UNVERIFIED. Checks that this Windows PC can build the OkaySpace Xbox (UWP) package.
.DESCRIPTION
  The same checks as platform/xbox_uwp/XboxSdkCheck.hpp (the editor's check), written for PowerShell.
  Looks for Visual Studio 2022 or newer (through vswhere, any install), the C++ Universal Windows Platform tools, a Windows 10/11 SDK
  (17763 or newer) with the UWP headers and libraries, and SignTool.exe. MakeAppx.exe is reported but optional:
  our own packer (okay-appx) makes the package.
  Written without a Windows PC: this script has never been run. If it says something is missing that is
  installed, tell us the folder names (docs/xbox_uwp.md, "What to paste back").
.PARAMETER Json
  Print the result as JSON instead of text.
.OUTPUTS
  Exit code 0: everything required was found. 1: something required is missing.
#>
param([switch]$Json)

$ErrorActionPreference = 'Stop'
$pf86 = ${env:ProgramFiles(x86)}
if (-not $pf86) { $pf86 = 'C:\Program Files (x86)' }
$items = New-Object System.Collections.ArrayList
function Add-Item($id, $name, $found, $detail, $install, $required = $true) {
    [void]$items.Add([pscustomobject]@{ id = $id; name = $name; found = [bool]$found; required = $required; detail = $detail; install = $install })
}
function Newest-Version-Loose($names) {
    # MSVC toolset folder names such as 14.38.33130 or 14.44.35207: numeric order, newest first
    $names | Where-Object { $_ -match '^\d+(\.\d+)+$' } | Sort-Object { [version]$_ } -Descending
}
function Newest-Version($names) {
    # "10.0.19041.0" style folder names, numerically newest first (a text sort would put 9 after 10)
    $names | Where-Object { $_ -match '^\d+(\.\d+){3}$' } | Sort-Object { [version]$_ } -Descending
}

# 1-2. Visual Studio (2022 or newer; any install vswhere lists) and its UWP C++ toolset (an MSVC folder with lib\x64\store).
# The toolset folder is discovered, never assumed (14.38, 14.44 ... any name); versions are compared as numbers.
# Component ids are from Microsoft's component directory as remembered (UNVERIFIED); the folders are what decide.
$vcComponent = 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
$vswhere = Join-Path $pf86 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = $null; $vsVersion = $null; $vsName = $null; $vsMajor = 0; $toolset = $null
$cands = @()
if (Test-Path $vswhere) {
    $paths = @(& $vswhere -products * -sort -property installationPath)
    $vers = @(& $vswhere -products * -sort -property installationVersion)
    $names = @(& $vswhere -products * -sort -property displayName)
    for ($i = 0; $i -lt $paths.Count; $i++) {
        $p = $paths[$i]; $msvc = Join-Path $p 'VC\Tools\MSVC'
        if (-not $p -or -not (Test-Path $msvc)) { continue }
        $ts = $null
        foreach ($v in (Newest-Version-Loose (Get-ChildItem $msvc -Directory | ForEach-Object Name))) {
            if (Test-Path (Join-Path $msvc "$v\lib\x64\store")) { $ts = $v; break }   # an ARM64-only UWP install has lib\arm64\store, not this
        }
        $ver = if ($vers.Count -eq $paths.Count) { $vers[$i] } else { '' }
        $cands += [pscustomobject]@{ path = $p; version = $ver; name = $(if ($names.Count -eq $paths.Count) { $names[$i] } else { '' }); toolset = $ts; order = $i }
    }
}
# prefer an install with a UWP toolset, then the newest installationVersion (numeric), then vswhere's order
$best = $cands | Sort-Object @{ e = { [bool]$_.toolset }; Descending = $true }, @{ e = { if ($_.version -match '^\d+(\.\d+)*$') { [version]$_.version } else { [version]'0.0' } }; Descending = $true }, order | Select-Object -First 1
if ($best) { $vsPath = $best.path; $vsVersion = $best.version; $vsName = $best.name; $toolset = $best.toolset; if ($vsVersion -match '^(\d+)') { $vsMajor = [int]$Matches[1] } }
$generators = @{ 15 = 'Visual Studio 15 2017'; 16 = 'Visual Studio 16 2019'; 17 = 'Visual Studio 17 2022'; 18 = 'Visual Studio 18 2026' }
$generator = if ($generators.ContainsKey($vsMajor)) { $generators[$vsMajor] } else { '' }
$vsInstall = 'Install Visual Studio 2022 or newer (the free Community edition is enough) from the Visual Studio downloads page on visualstudio.microsoft.com.'
if (-not (Test-Path $vswhere)) { $vsInstall = 'vswhere.exe was not found (it installs with Visual Studio 2017 or newer). ' + $vsInstall }
Add-Item 'vs' 'Visual Studio 2022 or newer' ($null -ne $vsPath) $(if ($vsPath) { "$vsName $vsVersion ($vsPath)" } else { $null }) $vsInstall
$uwpInstall = 'Visual Studio 2022: Visual Studio Installer > Modify > the workload "Universal Windows Platform development" (with "C++ (v143) Universal Windows Platform tools" ticked). ' +
    'Newer Visual Studio (no such workload): Modify > Individual components > "C++ Universal Windows Platform support for ... build tools" (the one for your newest MSVC build tools).'
Add-Item 'uwp-toolset' 'C++ Universal Windows Platform tools' ($null -ne $toolset) $(if ($toolset) { "MSVC $toolset" } else { $null }) $uwpInstall

# 3-4. Windows SDK 10.0.17763.0 or newer, and its UWP headers and libraries
$kits = Join-Path $pf86 'Windows Kits\10'
$minSdk = [version]'10.0.17763.0'
$anySdk = $null; $goodSdk = $null
if (Test-Path (Join-Path $kits 'Include')) {
    foreach ($v in (Newest-Version (Get-ChildItem (Join-Path $kits 'Include') -Directory | ForEach-Object Name))) {
        if ([version]$v -lt $minSdk) { continue }
        if (-not $anySdk) { $anySdk = $v }
        $inc = Join-Path $kits "Include\$v"; $lib = Join-Path $kits "Lib\$v\um\x64"
        if ((Test-Path "$inc\winrt") -and (Test-Path "$inc\cppwinrt") -and (Test-Path "$inc\um") -and (Test-Path "$inc\ucrt") -and
            (Test-Path "$lib\WindowsApp.lib") -and (Test-Path "$lib\d3d11.lib")) { $goodSdk = $v; break }
    }
}
Add-Item 'sdk' 'Windows 10/11 SDK (17763 or newer)' ($null -ne $anySdk) $anySdk 'In the Visual Studio Installer tick a Windows 10 SDK or Windows 11 SDK (10.0.17763.0 or newer) under the Universal Windows Platform workload, or install the standalone Windows SDK from the Windows SDK page on developer.microsoft.com.'
Add-Item 'sdk-uwp' 'Windows SDK UWP headers and libraries' ($null -ne $goodSdk) $goodSdk 'Same step as the Windows SDK: the UWP part (C++/WinRT headers, WindowsApp.lib, d3d11.lib) installs with the SDK and the Universal Windows Platform workload.'

# 5-6. tools in the chosen SDK
function Find-Tool($exe) {
    $dirs = @()
    if ($goodSdk) { $dirs += (Join-Path $kits "bin\$goodSdk\x64") }
    $dirs += (Join-Path $kits 'bin\x64')
    foreach ($d in $dirs) { $p = Join-Path $d $exe; if (Test-Path $p) { return $p } }
    return $null
}
$mk = Find-Tool 'makeappx.exe'; $st = Find-Tool 'signtool.exe'
Add-Item 'makeappx' 'MakeAppx.exe (optional)' ($null -ne $mk) $mk 'Optional: our own packer (okay-appx) makes the package. MakeAppx is only used to cross-check it. Part of the Windows SDK ("Windows SDK Signing Tools for Desktop Apps").' $false
Add-Item 'signtool' 'SignTool.exe' ($null -ne $st) $st 'Part of the Windows SDK ("Windows SDK Signing Tools for Desktop Apps"). Install the Windows SDK as above.'

$missing = @($items | Where-Object { $_.required -and -not $_.found })
$available = ($missing.Count -eq 0)
if ($Json) {
    [pscustomobject]@{ available = $available; visualStudioPath = $vsPath; visualStudioVersion = $vsVersion; visualStudioMajor = $vsMajor; generator = $generator; toolsetVersion = $toolset; sdkVersion = $goodSdk; sdkRoot = $kits; signTool = $st; makeAppx = $mk; items = $items } | ConvertTo-Json -Depth 4
} else {
    foreach ($i in $items) {
        $tag = if ($i.found) { 'FOUND  ' } elseif ($i.required) { 'MISSING' } else { 'missing (optional)' }
        Write-Host ("{0,-7} {1}  {2}" -f $tag, $i.name, $i.detail)
        if (-not $i.found -and $i.required) { Write-Host ("        how to install: " + $i.install) }
    }
    Write-Host ''
    if ($available) { Write-Host 'All required tools were found. This only means the files exist: nothing has been built or run (experimental, unverified).' }
    else { Write-Host ('Xbox export is unavailable on this PC. Missing: ' + (($missing | ForEach-Object name) -join '; ')) }
}
if ($available) { exit 0 } else { exit 1 }
