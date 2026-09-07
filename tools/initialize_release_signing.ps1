[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^.+<[^<>]+>$')]
    [string]$Identity,

    [ValidatePattern('^[1-9][0-9]*[dwmy]$')]
    [string]$ExpiresIn = '2y',

    [string]$GpgHome = (Join-Path $env:LOCALAPPDATA 'JellyFrame\release-signing\gnupg'),

    [string]$GpgExecutable = 'C:\Program Files\GnuPG\bin\gpg.exe'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-SecretFingerprint {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$GpgHome,
        [Parameter(Mandatory = $true)][string]$Selector
    )

    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = & $Executable --homedir $GpgHome --with-colons --list-secret-keys $Selector 2>$null
        if ($LASTEXITCODE -ne 0) {
            return $null
        }
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    foreach ($line in $lines) {
        if ($line.StartsWith('fpr:')) {
            return $line.Split(':')[9]
        }
    }
    return $null
}

if (-not (Test-Path -LiteralPath $GpgExecutable -PathType Leaf)) {
    throw "GnuPG executable was not found: $GpgExecutable. Install Gpg4win first."
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$resolvedHome = [System.IO.Path]::GetFullPath($GpgHome)
$localAppDataRoot = [System.IO.Path]::GetFullPath($env:LOCALAPPDATA)
if (-not $resolvedHome.StartsWith($localAppDataRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Release signing home must remain below LOCALAPPDATA; do not place private keys in the repository.'
}

New-Item -ItemType Directory -Force -Path $resolvedHome | Out-Null
$existingFingerprint = Get-SecretFingerprint -Executable $GpgExecutable -GpgHome $resolvedHome -Selector $Identity
if ($null -ne $existingFingerprint) {
    throw "A signing key already exists for this identity: $existingFingerprint. Refusing to create a second key."
}

Write-Host 'GnuPG will now open its local pinentry dialog.'
Write-Host 'Choose a strong passphrase. It is never read by this script and must not be stored in the repository.'
& $GpgExecutable --homedir $resolvedHome --quick-generate-key $Identity ed25519 sign $ExpiresIn
if ($LASTEXITCODE -ne 0) {
    throw "GnuPG key generation failed with exit code $LASTEXITCODE."
}

$fingerprint = Get-SecretFingerprint -Executable $GpgExecutable -GpgHome $resolvedHome -Selector $Identity
if ([string]::IsNullOrWhiteSpace($fingerprint)) {
    throw 'GnuPG completed without exposing a local secret-key fingerprint.'
}

$publicKeyPath = Join-Path $resolvedHome 'jellyframe-release-signing-public-key.asc'
& $GpgExecutable --homedir $resolvedHome --armor --export $fingerprint |
    Set-Content -LiteralPath $publicKeyPath -Encoding ascii
if ($LASTEXITCODE -ne 0) {
    throw "GnuPG public-key export failed with exit code $LASTEXITCODE."
}

# PowerShell receives GnuPG armor as individual output lines. Keeping their
# terminators is required for the base64 payload and checksum to remain valid.
$exportedKey = & $GpgExecutable --with-colons --import-options show-only --import $publicKeyPath 2>$null
if ($LASTEXITCODE -ne 0) {
    throw 'GnuPG rejected the exported armored public key.'
}
$exportedFingerprint = $null
foreach ($line in $exportedKey) {
    if ($line.StartsWith('fpr:')) {
        $exportedFingerprint = $line.Split(':')[9]
        break
    }
}
if ($exportedFingerprint -ne $fingerprint) {
    throw 'The exported armored public key does not match the newly created signing key.'
}

& git -C $repositoryRoot config --local gpg.program $GpgExecutable
& git -C $repositoryRoot config --local user.signingkey $fingerprint
& git -C $repositoryRoot config --local tag.gpgsign true

Write-Host "Created local release signing key: $fingerprint"
Write-Host "Public key for GitHub upload: $publicKeyPath"
Write-Host "Private key material remains only under: $resolvedHome"
Write-Host 'Back up the GnuPG revocation certificate from this home separately and offline.'
