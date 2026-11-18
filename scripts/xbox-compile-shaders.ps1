<#
.SYNOPSIS
  EXPERIMENTAL, UNVERIFIED (written on Linux, never run on Windows). Compiles the engine's Direct3D 11 HLSL to .cso files at BUILD time.
.DESCRIPTION
  A UWP app may not call D3DCompile at run time, so every entry point of the engine's HLSL (engine\include\okay\Render\D3D11Hlsl.hpp) is
  compiled here with fxc.exe from the Windows SDK and written next to a shaders.manifest (name, profile, size, hash, and the hash of the
  HLSL text). The app (D3D11Renderer built with OKAY_XBOX_UWP) loads them from <package>\Shaders and refuses a bundle made from a
  different HLSL text. Written from Microsoft's fxc documentation: `fxc /nologo /T <profile> /E <entry> /Fo <file> <hlsl>`.
  Steps: build okay-shaders (cl.exe), `okay-shaders dump`, fxc once per entry listed in entries.txt, `okay-shaders manifest`, `okay-shaders verify`.
.PARAMETER SourceRoot  The OkaySpace source folder.
.PARAMETER OutDir      Where okay3d_*.cso, okay3d.hlsl and shaders.manifest are written (the package's Shaders folder).
.PARAMETER Fxc         Path to fxc.exe. Default: looked up in the newest Windows Kits\10\bin\<version>\x64 folder.
.PARAMETER VsPath      Visual Studio folder (for vcvars64.bat). Default: from xbox-check-sdk.ps1.
#>
param(
    [Parameter(Mandatory = $true)][string]$SourceRoot,
    [Parameter(Mandatory = $true)][string]$OutDir,
    [string]$Fxc = '',
    [string]$VsPath = '',
    [string]$WorkDir = ''
)
$ErrorActionPreference = 'Stop'
$SourceRoot = (Resolve-Path $SourceRoot).Path
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
if (-not $WorkDir) { $WorkDir = Join-Path $OutDir '..\shader-build' }
New-Item -ItemType Directory -Force $WorkDir | Out-Null
$WorkDir = (Resolve-Path $WorkDir).Path
Write-Host 'UNVERIFIED: this script has never been run on Windows.' -ForegroundColor Yellow

if (-not $Fxc) {
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $cand = Get-ChildItem $kits -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^10\.' } | Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName 'x64\fxc.exe' } | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $cand) { throw 'fxc.exe not found in the Windows SDK (install a Windows 10/11 SDK). Pass -Fxc <path> to point at it.' }
    $Fxc = $cand
}
if (-not $VsPath) {
    $j = & (Join-Path $SourceRoot 'scripts\xbox-check-sdk.ps1') -Json | ConvertFrom-Json
    $VsPath = $j.visualStudioPath
}
# our small helper (no engine link): dumps the HLSL text and writes/validates the manifest
$tool = Join-Path $WorkDir 'okay-shaders.exe'
$vcvars = Join-Path $VsPath 'VC\Auxiliary\Build\vcvars64.bat'
$src = Join-Path $SourceRoot 'platform\xbox_uwp\tool\okay_shaders.cpp'
cmd /c "`"$vcvars`" && cl /nologo /std:c++17 /EHsc /O2 /I `"$SourceRoot\engine\include`" `"$src`" /Fe:`"$tool`" /Fo:`"$WorkDir\\`""
if (-not (Test-Path $tool)) { throw 'Could not build okay-shaders.' }

& $tool dump $OutDir
if ($LASTEXITCODE -ne 0) { throw 'okay-shaders dump failed.' }
$hlsl = Join-Path $OutDir 'okay3d.hlsl'
$failed = @()
foreach ($line in Get-Content (Join-Path $OutDir 'entries.txt')) {
    $p = $line -split ' '
    $entry = $p[0]; $profile = $p[1]; $required = ($p[2] -eq 'required')
    $cso = Join-Path $OutDir ("okay3d_{0}.cso" -f $entry)
    if (Test-Path $cso) { Remove-Item $cso -Force }
    & $Fxc /nologo /T $profile /E $entry /Fo $cso $hlsl
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $cso)) {
        if ($required) { throw "fxc failed for the required shader $entry." }
        Write-Host "warning: fxc failed for the optional shader $entry (that engine feature will be off)" -ForegroundColor Yellow
        $failed += $entry
    }
}
& $tool manifest $OutDir
if ($LASTEXITCODE -ne 0) { throw 'okay-shaders manifest failed.' }
& $tool verify $OutDir
if ($LASTEXITCODE -ne 0) { throw 'okay-shaders verify failed.' }
Write-Host ("Shaders ready in {0} ({1} optional ones failed)." -f $OutDir, $failed.Count) -ForegroundColor Green
