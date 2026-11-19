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
.PARAMETER EngineMode
  EXPERIMENTAL, UNVERIFIED. Builds the app in ENGINE MODE: the real engine (okay_engine_uwp, ~300 engine sources: a LONG compile) is linked and the app runs
  the game in package\Data. With -MinimalTest the test export then also contains a tiny scene (a cube and a camera). Without it the app only draws the triangle.
  The owner's command for this: powershell -ExecutionPolicy Bypass -File scripts\build-xbox-uwp.ps1 -SourceRoot . -ExportDir .\xbox-engine -MinimalTest -EngineMode -SkipInstall
.PARAMETER Http
  Engine mode only. How downloads are made: 'windows-web-http' (default, Windows.Web.Http, does https) or 'own' (our own HTTP client over Winsock; PLAIN http:// only,
  no TLS of our own yet). Both UNVERIFIED.
.PARAMETER OwnCrypto
  Engine mode only, EXPERIMENT. Turns packet encryption on through our own okay::crypto (OKAY_XBOX_OWN_CRYPTO=ON). It is NOT independently reviewed: for experiments only,
  never ship it. A release build refuses this switch (scripts/check_release_own_stack.sh). Off by default (the build has no libsodium and then no packet encryption).
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
    [switch]$EngineMode,
    [ValidateSet('windows-web-http', 'own')][string]$Http = 'windows-web-http',
    [switch]$OwnCrypto,
    [switch]$SkipInstall,
    [switch]$SkipSign,
    [string]$Subject = ''
)
$ErrorActionPreference = 'Stop'

if ($env:OS -ne 'Windows_NT') { Write-Host 'FAILED: this script builds the Xbox app with Visual Studio and only runs on Windows. WHAT TO DO NEXT: run it on the Windows PC.' -ForegroundColor Red; exit 1 }
# Paths first. -ExportDir may not exist yet (Resolve-Path would fail on it), so it is made absolute by hand.
if (-not (Test-Path -LiteralPath $SourceRoot)) { Write-Host "FAILED: -SourceRoot '$SourceRoot' does not exist." -ForegroundColor Red; exit 1 }
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
if (-not (Test-Path -LiteralPath (Join-Path $SourceRoot 'platform\xbox_uwp\app\CMakeLists.txt'))) {
    Write-Host "FAILED: '$SourceRoot' is not the OkaySpace source folder (platform\xbox_uwp\app\CMakeLists.txt is missing)." -ForegroundColor Red
    Write-Host 'WHAT TO DO NEXT: point -SourceRoot at the folder that contains platform\xbox_uwp. In the Windows release that is the xbox-src folder next to OkayEngine.exe; in a git checkout it is the top folder.' -ForegroundColor Cyan
    exit 1
}
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
$script:StepNo = 0
$script:Total = 9
# "What to do next" for each step, printed when that step fails (scripts/check_xbox_uwp.py checks that every step 1..Total has one).
$script:Hints = @{
    1 = 'Install what the MISSING lines above say (Visual Studio Installer > Modify > "Universal Windows Platform development"), then run the SAME command again. To re-check only the tools: powershell -ExecutionPolicy Bypass -File scripts\xbox-check-sdk.ps1'
    2 = 'Check -SourceRoot is the OkaySpace source folder (it has platform\xbox_uwp) and -ExportDir is writable. Without -MinimalTest the folder must already hold package\AppxManifest.xml from the editor (File > Build Settings > Xbox). If cl.exe failed, open a "x64 Native Tools Command Prompt" and run cl to see whether the C++ compiler works at all.'
    3 = 'cl.exe could not compile our package tool. Send the last lines above and build.log. Nothing on the Xbox side is involved yet.'
    4 = 'CMake could not set up the app. Most often the "C++ Universal Windows Platform tools" component is missing or CMake picked the wrong Visual Studio. Run xbox-check-sdk.ps1 and send its output together with build.log.'
    5 = 'The app did not compile. Send build.log (the first line with the word error is the one that matters). Engine mode (-EngineMode) is the most likely to fail: try the same command WITHOUT -EngineMode first to see whether the basic triangle app builds.'
    6 = 'The build finished but the files are not where the script looks. Send build.log and the list from: dir <ExportDir>\build\app -Recurse -Filter OkaySpaceXbox.exe'
    7 = 'The shader compile failed. Check that fxc.exe exists under C:\Program Files (x86)\Windows Kits\10\bin\<version>\x64 (it comes with the Windows SDK) and send build.log.'
    8 = 'Our packer refused the folder. The message above names the file. If it says a file name is not allowed, rename that file in <ExportDir>\package. Send build.log if unsure.'
    9 = 'Signing failed. Open PowerShell as a normal user (not a service account), check that "dir Cert:\CurrentUser\My" works, and that signtool.exe exists (xbox-check-sdk.ps1). You can still use the unsigned file with -SkipSign to test the packer only.'
}

function Log([string]$line) { Add-Content -LiteralPath $script:LogFile -Value $line -Encoding UTF8 }
function Step([int]$n, [string]$text) {
    $script:StepName = "$n/$($script:Total) $text"
    $script:StepNo = $n
    $script:Tail.Clear()
    Write-Host ""
    Write-Host ("[{0}/{1}] {2}" -f $n, $script:Total, $text) -ForegroundColor Cyan
    Log ("`n=== [{0}/{1}] {2}" -f $n, $script:Total, $text)
}
# Runs a program, shows and logs its output line by line, keeps the last lines, and returns its exit code.
# Native programs write progress to stderr: under $ErrorActionPreference = 'Stop' that would throw, so it is relaxed in here only.
function Invoke-Tool([string]$exe, [string[]]$arguments) {
    Log ("> " + $exe + " " + ($arguments -join ' '))
    $script:LastLines = New-Object System.Collections.ArrayList   # every output line of the tool that just ran (Tail keeps only the last 15)
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    try {
        & $exe @arguments 2>&1 | ForEach-Object {
            $l = "$_"
            Write-Host $l
            Log $l
            [void]$script:Tail.Add($l)
            [void]$script:LastLines.Add($l)
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
    # UTF-8 without BOM plus chcp 65001 so a non-ASCII user folder survives; paths with spaces are quoted inside the .bat
    $text = "@echo off`r`nchcp 65001 >nul`r`ncall `"$script:Vcvars`"`r`nif errorlevel 1 exit /b 1`r`ncl /nologo /std:c++17 /EHsc /O2 /W0 $inc $src /Fe:`"$outExe`" /Fo:`"$obj\\`"`r`nexit /b %errorlevel%`r`n"
    [System.IO.File]::WriteAllText($bat, $text, (New-Object System.Text.UTF8Encoding($false)))
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
    $all = $message + ' ' + (($script:Tail | ForEach-Object { "$_" }) -join ' ')
    if ($all -match 'v145') { Write-Host 'Hint: "v145" in the error: the generated project picked a newer platform toolset than the one with the UWP libraries. The script passes -T v143,version=<MSVC folder>; send us build\app\*.vcxproj lines with PlatformToolset.' -ForegroundColor Yellow }
    if ($all -match 'ZW') { Write-Host 'Hint: "ZW" in the error: the project compiled as C++/CX (/ZW). It must be plain C++ with C++/WinRT (uwp_cpp.props sets CompileAsWinRT false).' -ForegroundColor Yellow }
    if ($all -match '(?i)appxmanifest') { Write-Host 'Hint: "appxmanifest" in the error: the MSBuild project wants a Package.appxmanifest (platform/xbox_uwp/app has a minimal one; check it was picked up).' -ForegroundColor Yellow }
    if ($all -match '(?i)VCLibs') { Write-Host 'Hint: "VCLibs": the package depends on Microsoft.VCLibs.140.00; install it first (see the next-steps lines of a successful run or docs/xbox_uwp.md).' -ForegroundColor Yellow }
    if ($script:Hints.ContainsKey($script:StepNo)) { Write-Host ''; Write-Host ('WHAT TO DO NEXT: ' + $script:Hints[$script:StepNo]) -ForegroundColor Cyan }
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
        $code = Compile-Cpp 'export' (@((Join-Path $packSrc 'tool\okay_xbox_export.cpp')) + $img) @($packSrc, (Join-Path $SourceRoot 'engine\include'), (Join-Path $SourceRoot 'engine\src')) $exportExe
        Require-Ok $code 'Compiling okay-xbox-export'
        if (-not (Test-Path -LiteralPath $exportExe)) { throw 'okay-xbox-export.exe was not produced.' }
        $exportArgs = @('--out', $ExportDir)
        if ($EngineMode) { $exportArgs += '--with-scene' }   # a tiny scene (cube + camera) + game.okayconfig for the engine
        Require-Ok (Invoke-Tool $exportExe $exportArgs) 'okay-xbox-export'
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
    # the toolset NAME too (v143 / v145): a newer Visual Studio's default toolset can differ from the MSVC folder that has the UWP libraries
    if ($sdk.toolsetVersion) {
        $tsArg = "version=$($sdk.toolsetVersion)"
        if ($sdk.platformToolset) { $tsArg = "$($sdk.platformToolset),$tsArg" }
        $cmakeArgs += @('-T', $tsArg)
    }
    $cmakeArgs += @('-DCMAKE_SYSTEM_NAME=WindowsStore', '-DCMAKE_SYSTEM_VERSION=10.0')
    if ($EngineMode) {
        # the shell stays C++20 (C++/WinRT), the engine is its own C++17 target (okay_engine_uwp); only plain C++ crosses (app/engine_bridge.hpp)
        $cmakeArgs += @('-DOKAY_XBOX_APP_ENGINE=ON', ('-DOKAY_SOURCE_ROOT=' + ($SourceRoot -replace '\\', '/')), ('-DOKAY_XBOX_HTTP=' + $Http))
        if ($OwnCrypto) {
            $cmakeArgs += '-DOKAY_XBOX_OWN_CRYPTO=ON'
            $w = 'OWN CRYPTO is ON: packet encryption uses our own okay::crypto, which is NOT independently reviewed. For experiments only; do not ship.'
            Write-Host $w -ForegroundColor Yellow; Log $w
        }
        $m = 'ENGINE MODE is ON: the engine (okay_engine_uwp, about 300 sources) is compiled and linked. This is EXPERIMENTAL; the first compile takes a long time and may fail (the engine has never been through Microsoft''s UWP compiler).'
        Write-Host $m -ForegroundColor Yellow; Log $m
    } else { Write-Host 'Engine mode is OFF: the app is the triangle test (add -EngineMode to build the engine in).' }
    Require-Ok (Invoke-Tool $cmakeExe $cmakeArgs) 'CMake (setting up the UWP app)'
    # What the generated project really passes to the compiler: /ZW (C++/CX) and /await must NOT be there, /std:c++20 should be (STL1011 / C++/WinRT).
    $vcx = Get-ChildItem -LiteralPath $appBuild -Filter 'OkaySpaceXbox.vcxproj' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($vcx) {
        $vtext = Get-Content -LiteralPath $vcx.FullName -Raw
        $zw = [regex]::IsMatch($vtext, '/ZW\b') -or [regex]::IsMatch($vtext, '<CompileAsWinRT>\s*true')
        $aw = $vtext.Contains('/await')
        $std = [regex]::Match($vtext, '<LanguageStandard>([^<]*)</LanguageStandard>').Groups[1].Value
        $m = "vcxproj check: /ZW or CompileAsWinRT=true present: $zw; /await present: $aw; LanguageStandard: '$std'"
        Write-Host $m; Log $m
        if ($zw -or $aw) { Write-Host 'WARNING: the project passes /ZW or /await; the C++/WinRT build is likely to fail (see docs/xbox_uwp.md).' -ForegroundColor Yellow }
    } else { Write-Host 'vcxproj check: OkaySpaceXbox.vcxproj not found (is the generator a Visual Studio one?).' }
    Step 5 $(if ($EngineMode) { 'Compiling the UWP app WITH THE ENGINE (can take 10 minutes or much more)' } else { 'Compiling the UWP app (can take a few minutes)' })
    # Only the app target (not ALL_BUILD). MSBuild's AppX validation runs AFTER the exe is linked; if that validation (and nothing else) fails, the exe is
    # still good (our own packer makes the real package), so it is a warning, not a failure.
    $bcode = Invoke-Tool $cmakeExe @('--build', $appBuild, '--config', $Configuration, '--target', 'OkaySpaceXbox')
    if ($bcode -ne 0) {
        $errLines = @($script:LastLines | Where-Object { $_ -match '(?i)\berror\b' })
        $nonAppx = @($errLines | Where-Object { $_ -notmatch 'APPX\d+' })
        $exeBuilt = @(Get-ChildItem -LiteralPath $appBuild -Recurse -Filter 'OkaySpaceXbox.exe' -ErrorAction SilentlyContinue |
                      Where-Object { $_.FullName -match ('[\\/]' + [regex]::Escape($Configuration) + '[\\/]') })
        if ($errLines.Count -gt 0 -and $nonAppx.Count -eq 0 -and $exeBuilt.Count -gt 0) {
            Write-Host 'WARNING: MSBuild reported only AppX packaging validation errors (APPX####), but OkaySpaceXbox.exe was built. Continuing: our own packer makes the package.' -ForegroundColor Yellow
            Log 'WARNING: build exit code non-zero with only APPX#### errors; continuing.'
        } else { throw "The UWP app build failed (exit code $bcode)." }
    }

    # 6. copy the program into the package folder. The manifest's Executable is OkaySpaceXbox.exe.
    Step 6 'Putting the program into the package'
    # Prefer the files of the configuration just built (...\Release\...), newest first; any other match only as a fallback.
    function Pick-Built([string]$name) {
        $all = @(Get-ChildItem -LiteralPath $appBuild -Recurse -Filter $name -ErrorAction SilentlyContinue)
        $cfg = @($all | Where-Object { $_.FullName -match ('[\\/]' + [regex]::Escape($Configuration) + '[\\/]') })
        if ($cfg.Count -gt 0) { $all = $cfg }
        return ($all | Sort-Object LastWriteTime -Descending | Select-Object -First 1)
    }
    $exe = Pick-Built 'OkaySpaceXbox.exe'
    if (-not $exe) { throw 'OkaySpaceXbox.exe was not found after the build.' }
    Copy-Item -LiteralPath $exe.FullName -Destination $pkg -Force
    foreach ($cso in 'tri_vs.cso', 'tri_ps.cso') {
        $f = Pick-Built $cso
        if (-not $f) { throw "Shader $cso was not produced by the build." }
        Copy-Item -LiteralPath $f.FullName -Destination $pkg -Force
    }
    Write-Host "Copied $($exe.Name) and the two triangle shaders."
    if ($EngineMode) {
        $hasData = Test-Path -LiteralPath (Join-Path $pkg 'Data\game.okayconfig')
        Write-Host ("Engine mode: package\Data\game.okayconfig present: {0} (the app runs the triangle test instead when it is missing)." -f $hasData)
        Write-Host ("Engine mode: OkaySpaceXbox.exe is {0:N1} MB." -f ($exe.Length / 1MB))
    }

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
    # Re-use the test certificate of an earlier run (same subject, still valid, private key present) so the PC and the Xbox only have to trust ONE certificate.
    $cert = Get-ChildItem -Path 'Cert:\CurrentUser\My' | Where-Object { $_.Subject -eq $Subject -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date).AddDays(7) } | Sort-Object NotAfter -Descending | Select-Object -First 1
    if ($cert) { Write-Host ("Re-using the test certificate from an earlier run (thumbprint {0})." -f $cert.Thumbprint) }
    else {
        $cert = New-SelfSignedCertificate -Type Custom -Subject $Subject -KeyUsage DigitalSignature -FriendlyName 'OkaySpace Xbox test certificate' `
            -CertStoreLocation 'Cert:\CurrentUser\My' -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
    }
    $cer = Join-Path $ExportDir 'OkaySpaceXbox-test.cer'
    Export-Certificate -Cert $cert -FilePath $cer | Out-Null   # the PUBLIC part only; the private key stays in your certificate store
    Require-Ok (Invoke-Tool $sdk.signTool @('sign', '/fd', 'SHA256', '/sha1', $cert.Thumbprint, $appx)) 'SignTool'   # by thumbprint: no password anywhere

    Write-Host ''
    Write-Host "Done. Signed test package: $appx" -ForegroundColor Green
    Write-Host "Test certificate (public): $cer"
    Write-Host "Full log: $script:LogFile"
    $pfn = $ident.Name
    Write-Host ''
    Write-Host 'A) TO TRY IT ON THIS PC (UNVERIFIED), in an ADMINISTRATOR PowerShell (right-click PowerShell > Run as administrator):' -ForegroundColor Cyan
    $vclibs = $null
    $ext = Join-Path ${env:ProgramFiles(x86)} 'Microsoft SDKs\Windows Kits\10\ExtensionSDKs\Microsoft.VCLibs'
    if (Test-Path -LiteralPath $ext) { $vclibs = Get-ChildItem -LiteralPath $ext -Recurse -Filter 'Microsoft.VCLibs.x64.14.00.appx' -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match 'Retail' } | Select-Object -First 1 }
    Write-Host ("  Get-AppxPackage {0} | Remove-AppxPackage      # only if an older copy is installed (otherwise Windows says 0x80073CFB: same version, different contents)" -f $pfn) -ForegroundColor DarkGray
    Write-Host ("  Import-Certificate -FilePath '{0}' -CertStoreLocation Cert:\LocalMachine\TrustedPeople" -f $cer)
    Write-Host ("  Add-AppxPackage -Path '{0}'" -f $appx)
    Write-Host '  Then start "OkaySpace Test" from the Start menu. A pad: left stick moves the triangle, A centres it, B closes it.'
    Write-Host '  If Add-AppxPackage says a dependency is missing (Microsoft.VCLibs), install the framework first (it can print 0x80073D02 when it is in use: harmless):' -ForegroundColor DarkGray
    if ($vclibs) { Write-Host ("  Add-AppxPackage -Path '{0}'" -f $vclibs.FullName) }
    else { Write-Host '  (the Microsoft.VCLibs.x64.14.00.appx file is under Program Files (x86)\Microsoft SDKs\Windows Kits\10\ExtensionSDKs\Microsoft.VCLibs when Visual Studio installed it)' }
    Write-Host '  If Windows says 0x800B0109 / "certificate chain": the Import-Certificate line was not run as administrator. If it says 0x80073CF3: a dependency or the manifest is wrong (send build.log).'
    Write-Host ''
    Write-Host 'B) FOR THE XBOX (Developer Mode): open https://<the Xbox IP>:11443 in a browser on the PC, Apps > Add, choose the .appx above, add the VCLibs package under dependencies, and the .cer file above if the page offers a certificate field.' -ForegroundColor Cyan
    Write-Host '   Step by step with every screen: docs/try_it/ (the "Xbox UWP next" page) or docs/xbox_uwp.md, "Deploy to an Xbox in Developer Mode".'
    Write-Host ''
    Write-Host 'C) WHERE THE APP WRITES ITS LOG (UNVERIFIED): file okayspace.log in the app''s LocalState\logs folder.' -ForegroundColor Cyan
    Write-Host '   On this PC: %LOCALAPPDATA%\Packages\<the package family name>\LocalState\logs   (type %LOCALAPPDATA%\Packages\ in Explorer and look for a folder starting with com.okayspace.test).'
    Write-Host '   On the Xbox: Device Portal > Apps > this app > its file / "LocalState" browser (or Visual Studio''s Device Portal Files page).'
    exit 0
} catch {
    Fail-With $_.Exception.Message
}
