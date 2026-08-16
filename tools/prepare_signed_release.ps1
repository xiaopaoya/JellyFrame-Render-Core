[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')]
    [string]$Version,

    [string]$GpgHome = (Join-Path $env:LOCALAPPDATA 'JellyFrame\release-signing\gnupg'),

    [switch]$PushTag
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$status = & git -C $repositoryRoot status --porcelain=v1
if ($status) {
    throw 'Refusing to tag a dirty checkout. Commit, stash, or remove all changes first.'
}

$configuredKey = (& git -C $repositoryRoot config --local --get user.signingkey).Trim()
if ([string]::IsNullOrWhiteSpace($configuredKey)) {
    throw 'No repository-local signing key is configured. Run initialize_release_signing.ps1 first.'
}

$configuredGpg = (& git -C $repositoryRoot config --local --get gpg.program).Trim()
if ([string]::IsNullOrWhiteSpace($configuredGpg) -or
        -not (Test-Path -LiteralPath $configuredGpg -PathType Leaf)) {
    throw 'Repository-local gpg.program is missing or unavailable. Run initialize_release_signing.ps1 again.'
}

$resolvedHome = [System.IO.Path]::GetFullPath($GpgHome)
if (-not (Test-Path -LiteralPath $resolvedHome -PathType Container)) {
    throw "Local release keyring is missing: $resolvedHome"
}

$secretKeys = & $configuredGpg --homedir $resolvedHome --with-colons --list-secret-keys $configuredKey
if ($LASTEXITCODE -ne 0 -or -not ($secretKeys | Where-Object { $_.StartsWith('sec:') })) {
    throw "Configured signing key is not available in the local keyring: $configuredKey"
}

$versionFile = Join-Path $repositoryRoot 'cmake\render_core_version.cmake'
$versionMatch = [regex]::Match((Get-Content -LiteralPath $versionFile -Raw),
    'JELLYFRAME_RENDER_CORE_PACKAGE_VERSION\s+"([0-9]+\.[0-9]+\.[0-9]+)"')
if (-not $versionMatch.Success -or $versionMatch.Groups[1].Value -ne $Version) {
    throw "Requested tag version $Version does not match cmake/render_core_version.cmake."
}

$tagName = "v$Version"
& git -C $repositoryRoot show-ref --verify --quiet "refs/tags/$tagName"
if ($LASTEXITCODE -eq 0) {
    throw "Tag already exists locally: $tagName"
}

$previousGpgHome = $env:GNUPGHOME
try {
    $env:GNUPGHOME = $resolvedHome
    & git -C $repositoryRoot tag -s $tagName -m "JellyFrame Render Core $Version"
    if ($LASTEXITCODE -ne 0) {
        throw "Signed tag creation failed with exit code $LASTEXITCODE."
    }
    & git -C $repositoryRoot tag -v $tagName
    if ($LASTEXITCODE -ne 0) {
        throw "Signed tag verification failed with exit code $LASTEXITCODE."
    }
} finally {
    if ($null -eq $previousGpgHome) {
        Remove-Item Env:GNUPGHOME -ErrorAction SilentlyContinue
    } else {
        $env:GNUPGHOME = $previousGpgHome
    }
}

$releaseDirectory = Join-Path $repositoryRoot "build\release\$tagName"
if (Test-Path -LiteralPath $releaseDirectory) {
    throw "Release output already exists: $releaseDirectory. Inspect it instead of overwriting release evidence."
}

python (Join-Path $repositoryRoot 'tools\package_render_core_source.py') --output-dir $releaseDirectory
if ($LASTEXITCODE -ne 0) {
    throw "Source archive creation failed with exit code $LASTEXITCODE."
}

Write-Host "Verified signed tag: $tagName"
Write-Host "Release archive directory: $releaseDirectory"
Write-Host 'Review the archive and its .sha256 sidecar before any push or GitHub Release creation.'

if ($PushTag) {
    & git -C $repositoryRoot push origin $tagName
    if ($LASTEXITCODE -ne 0) {
        throw "Tag push failed with exit code $LASTEXITCODE. The local signed tag remains available for retry."
    }
    Write-Host "Pushed signed tag: $tagName"
}
