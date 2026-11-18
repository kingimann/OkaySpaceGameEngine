<#
.SYNOPSIS
  EXPERIMENTAL, UNVERIFIED. Builds the OkaySpace Xbox (UWP) app, packs it with OUR packer, makes a test
  certificate and signs the package with SignTool.
.DESCRIPTION
  UNVERIFIED AT THE TOP: this script was written on a Linux machine with no Windows, no Visual Studio, no Windows SDK and no
  Xbox. It has never been run. Every step below is from Microsoft's public documentation. Expect to fix things.
  It does NOT claim to build a native console game: the output is a Universal Windows Platform (UWP) app for an Xbox
  One in Developer Mode, and the app shell only draws a test triangle and reads a gamepad (it does not run your game yet).

  Steps: (1) xbox-check-sdk.ps1  (2) build okay-appx (our packer) with cl.exe  (3) build the UWP app with CMake +
  MSBuild for x64 UWP  (4) copy the exe, the two .cso shaders and the VC runtime notes into <ExportDir>\package
  (5) okay-appx pack  (6) test certificate with New-SelfSignedCertificate (public PowerShell cmdlet)  (7) SignTool sign  (8) print
  the Dev Mode install steps.
.PARAMETER MinimalTest
  No editor export needed: builds the small export tool (cl.exe), writes a minimal TEST export into -ExportDir (which may not exist yet)
  and goes on with the normal steps. This is the owner's one command:
    powershell -ExecutionPolicy Bypass -File scripts\build-xbox-uwp.ps1 -SourceRoot . -ExportDir .\xbox-test -MinimalTest -SkipInstall
.PARAMETER SkipInstall
  Accepted for convenience. This script has no install step (the editor's one-click build has the optional install); it does nothing here.
.PARAMETER SourceRoot
  The OkaySpace source folder (has platform\xbox_uwp). Needed to build the app and the packer.
.PARAMETER ExportDir
  The folder the editor wrote (File > Build Settings > Xbox One (experimental)). It holds package\AppxManifest.xml.
.PARAMETER Configuration
  Release (default) or Debug.
.PARAMETER SkipSign
  Pack only. The .appx is then UNSIGNED and Windows will not install it.
.PARAMETER Subject
  Certificate subject. Must equal the manifest's Publisher. Default: read from package\AppxManifest.xml.
#>
param(
    [Parameter(Mandatory = $true)][string]$SourceRoot,
    [Parameter(Mandatory = $true)][string]$ExportDir,
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [switch]$MinimalTest,
    [switch]$SkipInstall,
    [switch]$SkipSign,
    [string]$Subject = ''
)
$ErrorActionPreference = 'Stop'

# Paths first. -ExportDir may not exist yet (Resolve-Path would fail on it), so it is made absolute by hand.
if (-not (Test-Path -LiteralPath $SourceRoot)) { Write-Host "FAILED: -SourceRoot '$SourceRoot' does not exist." -ForegroundColor Red; exit 1 }
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
if (-not [System.IO.Path]::IsPathRooted($ExportDir)) { $ExportDir = Join-Path (Get-Location).Path $ExportDir }
$ExportDir = [System.IO.Path]::GetFullPath($ExportDir)
New-Item -ItemType Directory -Force -Path $ExportDir | Out-Null
$pkg = Join-Path $ExportDir 'package'
$manifestPath = Join-Path $pkg 'AppxManifest.xml'
$work = Join-Path $ExportDir 'build'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$script:LogFile = Join-Path $ExportDir 'build.log'
Set-Content -LiteralPath $script:LogFile -Value ("OkaySpace Xbox build log, started " + (Get-Date -Format 's')) -Encoding UTF8
$script:Tail = New-Object System.Collections.ArrayList   # the last output lines of the tool that is running
$script:StepName = 'start'
$script:Total = 9

function Log([string]$line) { Add-Content -LiteralPath $script:LogFile -Value $line -Encoding UTF8 }
function Step([int]$n, [string]$text) {
    $script:StepName = "$n/$($script:Total) $text"
    $script:Tail.Clear()
    Write-Host ""
    Write-Host ("[{0}/{1}] {2}" -f $n, $script:Total, $text) -ForegroundColor Cyan
    Log ("`n=== [{0}/{1}] {2}" -f $n, $script:Total, $text)
}
# Runs a program, shows and logs its output line by line, keeps the last lines, and returns its exit code.
# Native programs write progress to stderr: under $ErrorActionPreference = 'Stop' that would throw, so it is relaxed in here only.
function Invoke-Tool([string]$exe, [string[]]$arguments) {
    Log ("> " + $exe + " " + ($arguments -join ' '))
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    try {
        & $exe @arguments 2>&1 | ForEach-Object {
            $l = "$_"
            Write-Host $l
            Log $l
            [void]$script:Tail.Add($l)
            if ($script:Tail.Count -gt 15) { $script:Tail.RemoveAt(0) }
        }
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $old }
    if ($null -eq $code) { $code = 0 }
    Log ("(exit code $code)")
    return $code
}
function Require-Ok([int]$code, [string]$what) { if ($code -ne 0) { throw "$what failed (exit code $code)." } }
# Compile C++ with cl.exe through a small .bat that first runs the vcvars64.bat of the Visual Studio the SDK check found (any version).
function Compile-Cpp([string]$name, [string[]]$sources, [string[]]$includes, [string]$outExe) {
    $obj = Join-Path $work ("obj-" + $name); New-Item -ItemType Directory -Force -Path $obj | Out-Null
    $bat = Join-Path $work ("cl-" + $name + ".bat")
    $inc = ($includes | ForEach-Object { '/I"' + $_ + '"' }) -join ' '
    $src = ($sources | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $text = "@echo off`r`ncall `"$script:Vcvars`"`r`nif errorlevel 1 exit /b 1`r`ncl /nologo /std:c++17 /EHsc /O2 /W0 $inc $src /Fe:`"$outExe`" /Fo:`"$obj\\`"`r`nexit /b %errorlevel%`r`n"
    Set-Content -LiteralPath $bat -Value $text -Encoding ASCII
    return (Invoke-Tool $bat @())
}

function Fail-With([string]$message) {
    Write-Host ""
    Write-Host ("FAILED in step " + $script:StepName) -ForegroundColor Red
    Write-Host $message -ForegroundColor Red
    if ($script:Tail.Count -gt 0) {
        Write-Host "Last lines of the failing tool's output:" -ForegroundColor Yellow
        foreach ($l in $script:Tail) { Write-Host ("  " + $l) }
    }
    Write-Host ("Full log (paste it to us): " + $script:LogFile) -ForegroundColor Yellow
    Log ("FAILED in step " + $script:StepName + ": " + $message)
    exit 1
}

Write-Host 'UNVERIFIED: this pipeline has never been run on Windows or on an Xbox.' -ForegroundColor Yellow
if ($SkipInstall) { Write-Host '(-SkipInstall: this script has no install step; nothing to skip.)' }
try {
    # 1. tools
    Step 1 'Checking the Windows tools (Visual Studio, UWP C++ tools, Windows SDK, SignTool)'
    $checkScript = Join-Path $SourceRoot 'scripts\xbox-check-sdk.ps1'
    $sdkJson = Join-Path $ExportDir 'sdk-check.json'
    & $checkScript -Json | Out-File -LiteralPath $sdkJson -Encoding utf8
    $checkCode = $LASTEXITCODE
    if ($checkCode -ne 0) {
        & $checkScript | ForEach-Object { Write-Host "$_"; Log "$_"; [void]$script:Tail.Add("$_") }
        throw 'A required tool is missing (see the list above).'
    }
    $sdk = Get-Content -LiteralPath $sdkJson -Raw | ConvertFrom-Json
    $script:Vcvars = Join-Path $sdk.visualStudioPath 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path -LiteralPath $script:Vcvars)) { throw "vcvars64.bat not found in the Visual Studio folder: $script:Vcvars" }
    Write-Host ("Found Visual Studio {0} (generator '{1}'), MSVC {2}, Windows SDK {3}." -f $sdk.visualStudioVersion, $sdk.generator, $sdk.toolsetVersion, $sdk.sdkVersion)

    # 2. the export folder
    $packSrc = Join-Path $SourceRoot 'platform\xbox_uwp'
    if ($MinimalTest) {
        Step 2 'Writing a minimal TEST export (a tiny tool is compiled first)'
        $exportExe = Join-Path $work 'okay-xbox-export.exe'
        $img = 'engine\src\Graphics\Image.cpp', 'engine\src\Core\ImageDecode.cpp', 'engine\src\Core\ImageDecodeJpeg.cpp', 'engine\src\Core\ImageWrite.cpp' | ForEach-Object { Join-Path $SourceRoot $_ }
        $code = Compile-Cpp 'export' (@((Join-Path $packSrc 'tool\okay_xbox_export.cpp')) + $img) @($packSrc, (Join-Path $SourceRoot 'engine\include'), (Join-Path $SourceRoot 'engine\src'), (Join-Path $SourceRoot 'engine\third_party\stb')) $exportExe
        Require-Ok $code 'Compiling okay-xbox-export'
        if (-not (Test-Path -LiteralPath $exportExe)) { throw 'okay-xbox-export.exe was not produced.' }
        Require-Ok (Invoke-Tool $exportExe @('--out', $ExportDir)) 'okay-xbox-export'
    } else {
        Step 2 'Using the export folder'
        if (-not (Test-Path -LiteralPath $manifestPath)) { throw "No package\AppxManifest.xml in $ExportDir. Export from the editor first, or add -MinimalTest." }
        Write-Host "Found $manifestPath"
    }
    if (-not (Test-Path -LiteralPath $manifestPath)) { throw "The export did not produce $manifestPath" }

    # 3. our packer (a plain desktop program: no engine, no UWP)
    Step 3 'Compiling our own package tool (okay-appx)'
    $okayAppx = Join-Path $work 'okay-appx.exe'
    $code = Compile-Cpp 'appx' @((Join-Path $packSrc 'tool\okay_appx.cpp')) @($packSrc, (Join-Path $SourceRoot 'engine\include')) $okayAppx
    Require-Ok $code 'Compiling okay-appx'
    if (-not (Test-Path -LiteralPath $okayAppx)) { throw 'okay-appx.exe was not produced.' }

    # 4-5. the UWP app
    $appBuild = Join-Path $work 'app'
    Step 4 'Setting up the UWP app build (CMake)'
    $cmakeExe = 'cmake'
    $vsCmake = Join-Path $sdk.visualStudioPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (Test-Path -LiteralPath $vsCmake) { $cmakeExe = $vsCmake }
    # generator, install and toolset come from the SDK check (Visual Studio 2022 or newer), not a fixed 2022 / v143
    $cmakeArgs = @('-S', (Join-Path $packSrc 'app'), '-B', $appBuild)
    if ($sdk.generator) { $cmakeArgs += @('-G', $sdk.generator) }
    $cmakeArgs += @('-A', 'x64')
    if ($sdk.visualStudioPath) { $cmakeArgs += "-DCMAKE_GENERATOR_INSTANCE=$($sdk.visualStudioPath)" }
    if ($sdk.toolsetVersion) { $cmakeArgs += @('-T', "version=$($sdk.toolsetVersion)") }
    $cmakeArgs += @('-DCMAKE_SYSTEM_NAME=WindowsStore', '-DCMAKE_SYSTEM_VERSION=10.0')
    Require-Ok (Invoke-Tool $cmakeExe $cmakeArgs) 'CMake (setting up the UWP app)'
    Step 5 'Compiling the UWP app (can take a few minutes)'
    Require-Ok (Invoke-Tool $cmakeExe @('--build', $appBuild, '--config', $Configuration)) 'The UWP app build'

    # 6. copy the program into the package folder. The manifest's Executable is OkaySpaceXbox.exe.
    Step 6 'Putting the program into the package'
    $exe = Get-ChildItem -LiteralPath $appBuild -Recurse -Filter 'OkaySpaceXbox.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $exe) { throw 'OkaySpaceXbox.exe was not found after the build.' }
    Copy-Item -LiteralPath $exe.FullName -Destination $pkg -Force
    foreach ($cso in 'tri_vs.cso', 'tri_ps.cso') {
        $f = Get-ChildItem -LiteralPath $appBuild -Recurse -Filter $cso -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $f) { throw "Shader $cso was not produced by the build." }
        Copy-Item -LiteralPath $f.FullName -Destination $pkg -Force
    }
    Write-Host "Copied $($exe.Name) and the two triangle shaders."

    # 7. the engine's own shaders: compiled to .cso at build time (D3DCompile does not exist in UWP) into package\Shaders
    Step 7 'Compiling the engine shaders (offline, fxc.exe)'
    $shaderArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $SourceRoot 'scripts\xbox-compile-shaders.ps1'),
                    '-SourceRoot', $SourceRoot, '-OutDir', (Join-Path $pkg 'Shaders'), '-VsPath', $sdk.visualStudioPath, '-WorkDir', (Join-Path $work 'shaders'))
    Require-Ok (Invoke-Tool 'powershell.exe' $shaderArgs) 'The shader compile'
    Remove-Item -LiteralPath (Join-Path $pkg 'Shaders\okay3d.hlsl'), (Join-Path $pkg 'Shaders\entries.txt') -Force -ErrorAction SilentlyContinue

    # 8. pack with OUR packer (unsigned) and check it
    Step 8 'Packing the .appx with our own packer and checking it'
    $ident = ([xml](Get-Content -LiteralPath $manifestPath -Raw)).Package.Identity
    $appx = Join-Path $ExportDir ("{0}_{1}_x64.appx" -f $ident.Name, $ident.Version)
    Require-Ok (Invoke-Tool $okayAppx @('pack', $pkg, $appx)) 'okay-appx pack'
    Require-Ok (Invoke-Tool $okayAppx @('verify', $appx)) 'okay-appx verify'
    if ($sdk.makeAppx) {
        Write-Host 'Cross-check: asking Microsoft''s MakeAppx to unpack our package (0 = it accepted the container). Informational only.'
        $x = Join-Path $work 'makeappx-unpack'; if (Test-Path -LiteralPath $x) { Remove-Item -LiteralPath $x -Recurse -Force }
        $mc = Invoke-Tool $sdk.makeAppx @('unpack', '/p', $appx, '/d', $x, '/nv')   # /nv: skip semantic validation; this only proves the container reads
        Write-Host ("MakeAppx unpack exit code: {0}" -f $mc)
    }
    if ($SkipSign) { Write-Host "Done, UNSIGNED: $appx (Windows will not install an unsigned package). Log: $script:LogFile" -ForegroundColor Green; exit 0 }

    # 9. test certificate (a self-signed code-signing certificate; the subject must equal the manifest Publisher), then sign
    Step 9 'Signing with a test certificate (PowerShell + SignTool)'
    if (-not $Subject) { $Subject = $ident.Publisher }
    $cert = New-SelfSignedCertificate -Type Custom -Subject $Subject -KeyUsage DigitalSignature -FriendlyName 'OkaySpace Xbox test certificate' `
        -CertStoreLocation 'Cert:\CurrentUser\My' -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
    $cer = Join-Path $ExportDir 'OkaySpaceXbox-test.cer'
    Export-Certificate -Cert $cert -FilePath $cer | Out-Null   # the PUBLIC part only; the private key stays in your certificate store
    Require-Ok (Invoke-Tool $sdk.signTool @('sign', '/fd', 'SHA256', '/sha1', $cert.Thumbprint, $appx)) 'SignTool'   # by thumbprint: no password anywhere

    Write-Host ''
    Write-Host "Done. Signed test package: $appx" -ForegroundColor Green
    Write-Host "Test certificate (public): $cer"
    Write-Host "Full log: $script:LogFile"
    Write-Host 'Next (docs/xbox_uwp.md, "Deploy to an Xbox in Developer Mode", unverified): trust the .cer on your PC to install there first'
    Write-Host '(Import-Certificate into Cert:\LocalMachine\TrustedPeople, as administrator, then Add-AppxPackage), or open the Windows Device Portal of your console'
    Write-Host '(the address is shown in Dev Home on the Xbox) and use "Add" under Apps to upload the .appx and the VC runtime framework package.'
    exit 0
} catch {
    Fail-With $_.Exception.Message
}
