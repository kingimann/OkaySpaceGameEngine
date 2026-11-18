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
    [switch]$SkipSign,
    [string]$Subject = ''
)
$ErrorActionPreference = 'Stop'
$SourceRoot = (Resolve-Path $SourceRoot).Path
$ExportDir = (Resolve-Path $ExportDir).Path
$pkg = Join-Path $ExportDir 'package'
$manifestPath = Join-Path $pkg 'AppxManifest.xml'
if (-not (Test-Path $manifestPath)) { throw "No package\AppxManifest.xml in $ExportDir. Export from the editor first." }
Write-Host 'UNVERIFIED: this pipeline has never been run on Windows or on an Xbox.' -ForegroundColor Yellow

# 1. tools
& (Join-Path $SourceRoot 'scripts\xbox-check-sdk.ps1') -Json | Out-File (Join-Path $ExportDir 'sdk-check.json') -Encoding utf8
if ($LASTEXITCODE -ne 0) { & (Join-Path $SourceRoot 'scripts\xbox-check-sdk.ps1'); throw 'A required tool is missing (see above).' }
$sdk = Get-Content (Join-Path $ExportDir 'sdk-check.json') -Raw | ConvertFrom-Json
$work = Join-Path $ExportDir 'build'
New-Item -ItemType Directory -Force $work | Out-Null

# 2. our packer (a plain desktop program: no engine, no UWP). Compiled directly with cl.exe from the VS 2022 developer environment.
$packSrc = Join-Path $SourceRoot 'platform\xbox_uwp'
$okayAppx = Join-Path $work 'okay-appx.exe'
$vcvars = Join-Path $sdk.visualStudioPath 'VC\Auxiliary\Build\vcvars64.bat'
cmd /c "`"$vcvars`" && cl /nologo /std:c++17 /EHsc /O2 /I `"$packSrc`" /I `"$SourceRoot\engine\include`" `"$packSrc\tool\okay_appx.cpp`" /Fe:`"$okayAppx`" /Fo:`"$work\\`""
if (-not (Test-Path $okayAppx)) { throw 'Could not build okay-appx (our packer).' }

# 3. the UWP app
$appBuild = Join-Path $work 'app'
cmake -S (Join-Path $packSrc 'app') -B $appBuild -G 'Visual Studio 17 2022' -A x64 -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0 2>&1 | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'CMake could not configure the UWP app.' }
cmake --build $appBuild --config $Configuration 2>&1 | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'The UWP app did not build.' }
$out = Join-Path $appBuild "out\$Configuration"
if (-not (Test-Path $out)) { $out = Join-Path $appBuild 'out' }

# 4. copy the program into the package folder. The manifest's Executable is OkaySpaceXbox.exe.
Copy-Item (Join-Path $out 'OkaySpaceXbox.exe') $pkg -Force
foreach ($cso in 'tri_vs.cso', 'tri_ps.cso') {
    $f = Get-ChildItem -Path $appBuild -Recurse -Filter $cso | Select-Object -First 1
    if (-not $f) { throw "Shader $cso was not produced by the build." }
    Copy-Item $f.FullName $pkg -Force
}

# 5. pack with OUR packer (unsigned)
$appxName = ([xml](Get-Content $manifestPath)).Package.Identity
$version = $appxName.Version; $id = $appxName.Name
$appx = Join-Path $ExportDir ("{0}_{1}_x64.appx" -f $id, $version)
& $okayAppx pack $pkg $appx
if ($LASTEXITCODE -ne 0) { throw 'okay-appx failed.' }
& $okayAppx verify $appx
if ($LASTEXITCODE -ne 0) { throw 'okay-appx verify failed.' }
if ($sdk.makeAppx) {
    Write-Host 'Cross-check: asking MakeAppx to unpack our package (it reads the block map and content types).' -ForegroundColor Cyan
    $x = Join-Path $work 'makeappx-unpack'; if (Test-Path $x) { Remove-Item $x -Recurse -Force }
    & $sdk.makeAppx unpack /p $appx /d $x /nv   # /nv: skip semantic validation; this only proves the container reads
    Write-Host ("MakeAppx unpack exit code: {0}  (0 = it accepted the container)" -f $LASTEXITCODE)
}
if ($SkipSign) { Write-Host "Done, UNSIGNED: $appx (Windows will not install an unsigned package)."; exit 0 }

# 6. test certificate (a self-signed code-signing certificate; the subject must equal the manifest Publisher)
if (-not $Subject) { $Subject = $appxName.Publisher }
$cert = New-SelfSignedCertificate -Type Custom -Subject $Subject -KeyUsage DigitalSignature -FriendlyName 'OkaySpace Xbox test certificate' `
    -CertStoreLocation 'Cert:\CurrentUser\My' -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
$cer = Join-Path $ExportDir 'OkaySpaceXbox-test.cer'
Export-Certificate -Cert $cert -FilePath $cer | Out-Null   # the PUBLIC part only; the private key stays in your certificate store

# 7. sign by thumbprint (no password on the command line)
& $sdk.signTool sign /fd SHA256 /sha1 $cert.Thumbprint $appx
if ($LASTEXITCODE -ne 0) { throw 'SignTool failed.' }

# 8. what next
Write-Host ''
Write-Host "Signed test package: $appx" -ForegroundColor Green
Write-Host "Test certificate (public): $cer"
Write-Host 'Next (docs/xbox_uwp.md, "Deploy to an Xbox in Developer Mode", unverified): trust the .cer on your PC if you want to install there first'
Write-Host '(Import-Certificate into Cert:\LocalMachine\TrustedPeople, as administrator), then open the Windows Device Portal of your console'
Write-Host '(the address is shown in Dev Home on the Xbox) and use "Add" under Apps to upload the .appx and the VC runtime framework package.'
