# publish_release.ps1 - one-click closed-source release to GitHub.
#
# Builds the clean Release x64 d3d12.dll + Run.exe locally, packages the
# shippable files into a zip, and publishes a GitHub Release tagged
# v<version> on the dedicated public release repository:
#
#     https://github.com/StarBobis/PBRelease
#
# Only the packaged zip ever leaves this machine - the source code stays
# private. The old "push a tag and let GitHub Actions build from source"
# flow is gone, because that required the source repo to be reachable by
# the release audience.
#
# Version rule: when -Version is not given, only the last (patch) number is
# bumped, e.g. 0.1.2 -> 0.1.3, based on the newest vX.Y.Z release tag on
# the StarBobis/PBRelease repository.
#
# Authentication: a GitHub personal access token with the "repo" scope is
# required. Provide it via -Token or the GITHUB_TOKEN / GH_TOKEN
# environment variables. Create one at https://github.com/settings/tokens
#
# Usage:
#   .\publish_release.ps1                    # build + auto-bump the patch version
#   .\publish_release.ps1 -Version 0.2.0     # publish an explicit version
#   .\publish_release.ps1 -SkipBuild         # reuse the existing x64\Release artifacts
#   .\publish_release.ps1 -Token <pat>       # pass the token explicitly

param(
    [string]$Version = "",
    [string]$Token = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$root = Split-Path -Parent $PSCommandPath

# The public repository that hosts the binary releases. Keep this in sync
# with the URL at the top of this file.
$releaseRepo = 'StarBobis/PBRelease'
$releaseRepoUrl = "https://github.com/$releaseRepo"

# Built-in fallback token so publishing works without any shell setup.
# Priority: -Token parameter > GITHUB_TOKEN / GH_TOKEN env vars > this value.
# NOTE: this file is tracked by git. The source repo is private; if it ever
# goes public or gets shared, rotate this token first.
$builtinToken = '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'

# ----------------------------------------------------------------------------
# Auth
# ----------------------------------------------------------------------------
if (-not $Token) { $Token = $env:GITHUB_TOKEN }
if (-not $Token) { $Token = $env:GH_TOKEN }
if (-not $Token) { $Token = $builtinToken }
if (-not $Token) {
    Write-Host "ERROR: no GitHub token found." -ForegroundColor Red
    Write-Host "       Create a PAT with the 'repo' scope at https://github.com/settings/tokens" -ForegroundColor Yellow
    Write-Host "       and set it via `$env:GITHUB_TOKEN (or pass -Token)." -ForegroundColor Yellow
    exit 1
}

$apiHeaders = @{
    'Authorization'        = "Bearer $Token"
    'Accept'               = 'application/vnd.github+json'
    'X-GitHub-Api-Version' = '2022-11-28'
    'User-Agent'           = 'publish_release.ps1'
}

function Invoke-GitHubApi {
    param(
        [string]$Method,
        [string]$Uri,
        [object]$Body = $null,
        [string]$ContentType = 'application/json'
    )
    try {
        $params = @{
            Method      = $Method
            Uri         = $Uri
            Headers     = $apiHeaders
            ContentType = $ContentType
        }
        if ($null -ne $Body) {
            if ($Body -is [byte[]]) {
                $params.Body = $Body
            } else {
                $params.Body = ($Body | ConvertTo-Json -Depth 10)
            }
        }
        return Invoke-RestMethod @params
    } catch {
        Write-Host "ERROR: GitHub API call failed: $Method $Uri" -ForegroundColor Red
        Write-Host "       $($_.Exception.Message)" -ForegroundColor Red
        if ($_.ErrorDetails.Message) {
            Write-Host "       $($_.ErrorDetails.Message)" -ForegroundColor Red
        }
        exit 1
    }
}

# ----------------------------------------------------------------------------
# Working tree sanity checks (the release ships whatever is built locally,
# including uncommitted changes, so warn loudly about those)
# ----------------------------------------------------------------------------
$branch = (git -C $root rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne 'main') {
    Write-Host "ERROR: releases must be published from main (current branch: $branch)." -ForegroundColor Red
    exit 1
}

git -C $root fetch origin main --quiet
$remote = (git -C $root rev-parse origin/main).Trim()
$base = (git -C $root merge-base HEAD origin/main).Trim()
if ($remote -ne $base) {
    Write-Host "ERROR: local main is behind origin/main. Run 'git pull' first." -ForegroundColor Red
    exit 1
}

$dirty = git -C $root status --porcelain
if ($dirty) {
    Write-Host "WARNING: uncommitted changes detected - they WILL be built into this release:" -ForegroundColor Yellow
    Write-Host $dirty -ForegroundColor Yellow
    Write-Host "Commit them first if the release should match a pushed commit." -ForegroundColor Yellow
}

# ----------------------------------------------------------------------------
# Resolve the version to publish
# ----------------------------------------------------------------------------
function Get-LatestReleaseVersion {
    # Returns the newest vX.Y.Z release tag on the release repo, or $null.
    $releases = Invoke-GitHubApi -Method Get -Uri "https://api.github.com/repos/$releaseRepo/releases?per_page=100"
    $versions = @()
    foreach ($rel in $releases) {
        if ($rel.tag_name -match '^v(\d+)\.(\d+)\.(\d+)$') {
            $versions += [version]"$($Matches[1]).$($Matches[2]).$($Matches[3])"
        }
    }
    if ($versions.Count -eq 0) {
        return $null
    }
    return ($versions | Sort-Object | Select-Object -Last 1)
}

if ($Version) {
    $newVersion = $Version.Trim().TrimStart('v')
} else {
    $latest = Get-LatestReleaseVersion
    if (-not $latest) {
        Write-Host "ERROR: no v* releases found on $releaseRepo to bump from. Pass -Version explicitly for the first release (e.g. -Version 0.1.0)." -ForegroundColor Red
        exit 1
    }
    $newVersion = "{0}.{1}.{2}" -f $latest.Major, $latest.Minor, ($latest.Build + 1)
    Write-Host "Auto-bumped patch version: v$latest -> v$newVersion" -ForegroundColor Cyan
}
if ($newVersion -notmatch '^\d+\.\d+\.\d+$') {
    Write-Host "ERROR: version '$newVersion' must look like 1.2.3" -ForegroundColor Red
    exit 1
}
$tag = "v$newVersion"

# Refuse to overwrite an existing tag on the release repo.
$existingRef = $null
try {
    $existingRef = Invoke-RestMethod -Method Get -Headers $apiHeaders `
        -Uri "https://api.github.com/repos/$releaseRepo/git/ref/tags/$tag"
} catch {
    if ($_.Exception.Response.StatusCode.value__ -ne 404) {
        Write-Host "ERROR: could not check existing tags on $releaseRepo." -ForegroundColor Red
        Write-Host "       $($_.Exception.Message)" -ForegroundColor Red
        if ($_.ErrorDetails.Message) {
            Write-Host "       $($_.ErrorDetails.Message)" -ForegroundColor Red
        }
        exit 1
    }
}
if ($existingRef) {
    Write-Host "ERROR: tag $tag already exists on $releaseRepo. Pick a new version." -ForegroundColor Red
    exit 1
}

# Warn (not fail) when CMakeLists.txt version drifts from the release tag:
# the DLL reports the CMake project version.
$cmakeLine = Select-String -Path "$root\CMakeLists.txt" -Pattern 'VERSION\s+(\d+\.\d+\.\d+)' | Select-Object -First 1
if ($cmakeLine) {
    $cmakeVersion = $cmakeLine.Matches[0].Groups[1].Value
    if ($cmakeVersion -ne $newVersion) {
        Write-Host "WARNING: CMakeLists.txt project version is $cmakeVersion but the release tag is $tag. The DLL will report $cmakeVersion." -ForegroundColor Yellow
    }
}

# ----------------------------------------------------------------------------
# Build + package
# ----------------------------------------------------------------------------
if (-not $SkipBuild) {
    Write-Host "===== Local Release x64 build =====" -ForegroundColor Cyan
    & "$root\build_release_x64.ps1"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED: local Release build, aborting release." -ForegroundColor Red
        exit $LASTEXITCODE
    }
} else {
    Write-Host "SkipBuild: reusing existing artifacts under x64\Release." -ForegroundColor Yellow
}

$outDir = "$root\x64\Release"
$stage = "$root\build\release-stage"
$zipName = "ProjectBunny-$tag-d3d12-x64.zip"
$zipPath = "$root\build\$zipName"

New-Item -ItemType Directory -Path $stage -Force | Out-Null
Get-ChildItem $stage -File -ErrorAction SilentlyContinue | Remove-Item -Force

$shipFiles = @(
    @{ Src = "$outDir\d3d12.dll"; Dst = 'd3d12.dll' },
    @{ Src = "$outDir\Run.exe"; Dst = 'Run.exe' },
    @{ Src = "$root\LICENSE.txt"; Dst = 'LICENSE.txt' },
    @{ Src = "$root\config\d3dx.ini"; Dst = 'd3dx.ini' }
)
foreach ($f in $shipFiles) {
    if (-not (Test-Path $f.Src)) {
        Write-Host "ERROR: expected file not found: $($f.Src)" -ForegroundColor Red
        Write-Host "       Build first (drop -SkipBuild) or restore the missing file." -ForegroundColor Yellow
        exit 1
    }
    Copy-Item $f.Src (Join-Path $stage $f.Dst) -Force
}

Compress-Archive -Path "$stage\*" -DestinationPath $zipPath -CompressionLevel Optimal -Force
Write-Host "Packaged: $zipPath" -ForegroundColor Green

# ----------------------------------------------------------------------------
# Create the GitHub Release and upload the zip
# ----------------------------------------------------------------------------
Write-Host "===== Publishing $tag to $releaseRepoUrl =====" -ForegroundColor Cyan

$release = Invoke-GitHubApi -Method Post `
    -Uri "https://api.github.com/repos/$releaseRepo/releases" `
    -Body @{
        tag_name   = $tag
        name       = "ProjectBunny $tag"
        body       = "ProjectBunny $tag - d3d12 x64 binary release."
        draft      = $false
        prerelease = $false
    }

$uploadUri = ($release.upload_url -replace '\{\?name,label\}$', '') + "?name=$zipName"
$zipBytes = [System.IO.File]::ReadAllBytes($zipPath)
Invoke-GitHubApi -Method Post -Uri $uploadUri -Body $zipBytes -ContentType 'application/zip' | Out-Null

Write-Host "Release published: $($release.html_url)" -ForegroundColor Green
