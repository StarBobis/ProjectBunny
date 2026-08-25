# publish_release.ps1 - one-click release to GitHub.
#
# Resolves the next release version (latest v* tag on origin, patch bumped by
# one), creates and pushes the v<version> tag, which triggers the
# "Release d3d12" GitHub Actions workflow (clean Release x64 d3d12.dll ->
# zip -> GitHub Release tagged v<version>). Then watches the workflow run
# until it finishes and prints the release URL.
#
# Version rule: when -Version is not given, only the last (patch) number is
# bumped, e.g. 0.1.2 -> 0.1.3, based on the newest vX.Y.Z tag on origin.
#
# Usage:
#   .\publish_release.ps1                    # auto-bump the patch version
#   .\publish_release.ps1 -Version 0.2.0     # publish an explicit version
#   .\publish_release.ps1 -VerifyLocalBuild  # run a local Release build first
#   .\publish_release.ps1 -NoWait            # push and exit without watching

param(
    [string]$Version = "",
    [switch]$VerifyLocalBuild,
    [switch]$NoWait
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSCommandPath

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

# Warn (not fail) when uncommitted changes exist: the release is built from
# the tag, so anything left uncommitted will NOT be part of the release.
$dirty = git -C $root status --porcelain
if ($dirty) {
    Write-Host "WARNING: uncommitted changes detected - they will NOT be included in this release:" -ForegroundColor Yellow
    Write-Host $dirty -ForegroundColor Yellow
    Write-Host "Commit and push them first if they should ship in this release." -ForegroundColor Yellow
}

function Get-LatestReleaseVersion {
    # Returns the newest vX.Y.Z tag on origin as a [version], or $null if none.
    $tags = git -C $root ls-remote --tags origin "refs/tags/v*" 2>$null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: cannot query origin tags (network issue?). Pass -Version explicitly." -ForegroundColor Red
        exit 1
    }
    $versions = @()
    foreach ($line in $tags) {
        if ($line -match 'refs/tags/v(\d+)\.(\d+)\.(\d+)$') {
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
        Write-Host "ERROR: no v* tags found on origin to bump from. Pass -Version explicitly for the first release (e.g. -Version 0.1.0)." -ForegroundColor Red
        exit 1
    }
    $newVersion = "{0}.{1}.{2}" -f $latest.Major, $latest.Minor, ($latest.Build + 1)
    Write-Host "Auto-bumped patch version: v$latest -> v$newVersion" -ForegroundColor Cyan
}
if ($newVersion -notmatch '^\d+\.\d+\.\d+$') {
    Write-Host "ERROR: version '$newVersion' must look like 1.2.3" -ForegroundColor Red
    exit 1
}

$existing = git -C $root ls-remote --tags origin "refs/tags/v$newVersion"
if ($existing) {
    Write-Host "ERROR: tag v$newVersion already exists on origin. Pick a new version." -ForegroundColor Red
    exit 1
}

if ($VerifyLocalBuild) {
    Write-Host "===== Local Release build verification =====" -ForegroundColor Cyan
    & "$root\build_release_x64.ps1"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED: local build verification, aborting release." -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

Write-Host "===== Publishing v$newVersion =====" -ForegroundColor Cyan
git -C $root tag "v$newVersion"
if ($LASTEXITCODE -ne 0) {
    Write-Host "FAILED: git tag" -ForegroundColor Red
    exit $LASTEXITCODE
}
git -C $root push origin "v$newVersion"
if ($LASTEXITCODE -ne 0) {
    Write-Host "FAILED: git push tag. Rolling back local tag..." -ForegroundColor Red
    git -C $root tag -d "v$newVersion" 2>$null
    exit $LASTEXITCODE
}
$headSha = (git -C $root rev-parse "v$newVersion").Trim()

$slug = $null
$remoteUrl = (git -C $root remote get-url origin).Trim()
if ($remoteUrl -match 'github\.com[:/](?<owner>[^/]+)/(?<repo>[^/]+?)(\.git)?$') {
    $slug = "$($Matches.owner)/$($Matches.repo)"
}
$releaseUrl = "https://github.com/$slug/releases/tag/v$newVersion"

if ($NoWait -or -not $slug) {
    Write-Host "Pushed tag v$newVersion. Release will appear at: $releaseUrl" -ForegroundColor Green
    exit 0
}

# Watch the workflow run triggered by this tag push. Unauthenticated GitHub
# API access is rate limited (~60 req/hour), so keep the poll interval modest.
$headers = @{ 'User-Agent' = 'publish_release.ps1' }
$deadline = (Get-Date).AddMinutes(25)
$run = $null
Write-Host "Waiting for the Release d3d12 workflow run..." -ForegroundColor Cyan
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 15
    try {
        $runs = Invoke-RestMethod -Uri "https://api.github.com/repos/$slug/actions/runs?per_page=10" -Headers $headers
        $run = $runs.workflow_runs | Where-Object { $_.head_sha -eq $headSha } | Select-Object -First 1
        if ($run) { break }
    } catch { }
}
if (-not $run) {
    Write-Host "Could not find the workflow run via the API. Check: https://github.com/$slug/actions" -ForegroundColor Yellow
    exit 0
}

while ((Get-Date) -lt $deadline) {
    $run = Invoke-RestMethod -Uri "https://api.github.com/repos/$slug/actions/runs/$($run.id)" -Headers $headers
    if ($run.status -eq 'completed') { break }
    Write-Host "  status: $($run.status) ..."
    Start-Sleep -Seconds 30
}

if ($run.conclusion -eq 'success') {
    Write-Host "Release published: $releaseUrl" -ForegroundColor Green
    exit 0
}
Write-Host "Workflow finished with $($run.status)/$($run.conclusion). Logs: $($run.html_url)" -ForegroundColor Red
exit 1
