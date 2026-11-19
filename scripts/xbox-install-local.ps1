<#
.SYNOPSIS
  EXPERIMENTAL, UNVERIFIED (never run on Windows). Runs ELEVATED (the editor starts it through the normal UAC prompt, once): trusts the
  test certificate (public .cer) for packages and installs the signed .appx on this PC. Writes INSTALL: OK or INSTALL: FAILED (...)
  to -ResultFile, which the editor reads. Needs no password.
#>
param(
    [Parameter(Mandatory = $true)][string]$Cer,
    [Parameter(Mandatory = $true)][string]$Appx,
    [Parameter(Mandatory = $true)][string]$ResultFile
)
$ErrorActionPreference = 'Stop'
try {
    Import-Certificate -FilePath $Cer -CertStoreLocation 'Cert:\LocalMachine\TrustedPeople' | Out-Null
    Add-AppxPackage -Path $Appx
    Set-Content -Path $ResultFile -Value 'INSTALL: OK' -Encoding ascii
} catch {
    Set-Content -Path $ResultFile -Value ("INSTALL: FAILED (" + $_.Exception.Message + ")") -Encoding ascii
    exit 1
}
