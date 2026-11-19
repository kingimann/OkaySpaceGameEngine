<#
.SYNOPSIS
  EXPERIMENTAL, UNVERIFIED (never run on Windows). Makes a test certificate and signs an Xbox UWP .appx with Microsoft's SignTool.
  Called by the editor's one-click Xbox build (platform/xbox_uwp/XboxBuildPlan.hpp). Prints facts the editor reads:
  SIGNER: ..., CERT: <public .cer path>, THUMBPRINT: <sha1>. It never prints a password or key (there is none: the private key
  stays in the current user's certificate store).
#>
param(
    [Parameter(Mandatory = $true)][string]$Appx,
    [Parameter(Mandatory = $true)][string]$Subject,
    [Parameter(Mandatory = $true)][string]$SignTool,
    [Parameter(Mandatory = $true)][string]$OutDir
)
$ErrorActionPreference = 'Stop'
try {
    Write-Output 'SIGNER: PowerShell New-SelfSignedCertificate + Microsoft SignTool'
    if (-not (Test-Path $Appx)) { throw "package not found: $Appx" }
    if (-not (Test-Path $SignTool)) { throw "SignTool not found: $SignTool" }
    # the subject must equal the manifest Publisher exactly
    $cert = New-SelfSignedCertificate -Type Custom -Subject $Subject -KeyUsage DigitalSignature -FriendlyName 'OkaySpace Xbox test certificate' `
        -CertStoreLocation 'Cert:\CurrentUser\My' -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
    $cer = Join-Path $OutDir 'OkaySpaceXbox-test.cer'
    Export-Certificate -Cert $cert -FilePath $cer | Out-Null   # PUBLIC part only
    & $SignTool sign /fd SHA256 /sha1 $cert.Thumbprint $Appx
    if ($LASTEXITCODE -ne 0) { throw "SignTool failed with exit code $LASTEXITCODE" }
    Write-Output "CERT: $cer"
    Write-Output "THUMBPRINT: $($cert.Thumbprint)"
    exit 0
} catch {
    Write-Output "error: $($_.Exception.Message)"
    exit 1
}
