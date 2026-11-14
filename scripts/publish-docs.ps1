param(
    [string]$Repository = "StarBobis/ProjectBunny",
    [string]$Ref = "main"
)

$ErrorActionPreference = "Stop"
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    throw "GitHub CLI is required. Install gh and run gh auth login first."
}

& gh workflow run pages.yml --repo $Repository --ref $Ref
if ($LASTEXITCODE -ne 0) { throw "Could not dispatch the documentation deployment." }

Write-Host "Deployment requested for remote ref '$Ref'. Local files are not uploaded."
Write-Host "Follow deployment: https://github.com/$Repository/actions/workflows/pages.yml"
