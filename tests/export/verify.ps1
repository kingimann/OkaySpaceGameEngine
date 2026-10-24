# Windows version of verify.sh: export tests/export/vs_export.okayscene with the
# editor's real Build, run the exported game headless, and check that its visual
# scripts printed exactly expected.txt (including the height of a mesh built from
# a bundled .okaymesh asset).
#
#   powershell -File tests/export/verify.ps1 -Editor path\to\OkayEngine.exe [-Work dir]
# The player runtime (OkaySpacePlayer.exe + SDL2.dll) must sit beside the editor.
param([Parameter(Mandatory = $true)][string]$Editor, [string]$Work = "")
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$Editor = (Resolve-Path $Editor).Path
if (-not $Work) { $Work = Join-Path ([System.IO.Path]::GetTempPath()) ("okay-export-" + [guid]::NewGuid()) }
New-Item -ItemType Directory -Force -Path $Work | Out-Null
$out = Join-Path $Work "game"

# Both exes are GUI programs (no console): wait for each, with its output
# redirected to files.
function Run($exe, $argList, $dir, $name) {
    $p = Start-Process -FilePath $exe -ArgumentList $argList -WorkingDirectory $dir -Wait -PassThru `
        -NoNewWindow -RedirectStandardOutput (Join-Path $Work "$name.out") -RedirectStandardError (Join-Path $Work "$name.err")
    Get-Content (Join-Path $Work "$name.out"), (Join-Path $Work "$name.err") -ErrorAction SilentlyContinue | Out-Host
    return $p.ExitCode
}

# Export from here so the scene's relative .okayvs paths resolve and get embedded.
$rc = Run $Editor @("--build-game", "vs_export.okayscene", "`"$out`"", "--name", "VsExport") $here "build"
if ($rc -ne 0) { throw "export failed (exit $rc)" }
$game = Join-Path $out "VsExport.exe"
if (-not (Test-Path $game)) { throw "export didn't produce $game" }
if (-not (Test-Path (Join-Path $out "SDL2.dll"))) { throw "export didn't copy SDL2.dll beside the game" }

# Run from elsewhere, so the game can only use what the export embedded.
$log = Join-Path $Work "run.log"
$rc = Run $game @("--server", "--run-for", "20", "--status", "0", "--port", "45190", "--log", "`"$log`"") $Work "run"
Get-Content $log | Out-Host
if ($rc -ne 0) { throw "the exported game exited with $rc" }

$expected = @(Get-Content (Join-Path $here "expected.txt"))
$actual = @(Get-Content $log | ForEach-Object { if ($_ -match '(EXPORT .*)$') { $Matches[1].TrimEnd() } })
$bad = $expected.Count -ne $actual.Count
for ($i = 0; $i -lt [Math]::Max($expected.Count, $actual.Count); $i++) {
    $e = if ($i -lt $expected.Count) { $expected[$i] } else { "(nothing)" }
    $a = if ($i -lt $actual.Count) { $actual[$i] } else { "(nothing)" }
    if ($e -ne $a) { $bad = $true; Write-Host "line $($i + 1): expected '$e' but got '$a'" }
}
if ($bad) { throw "the exported game's visual scripts printed something else (see the log above)" }
Write-Host "export check passed ($($actual.Count) lines)"
