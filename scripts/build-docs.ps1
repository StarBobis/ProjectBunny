param(
    [string]$BasePath = "/"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$previousBase = $env:VITEPRESS_BASE

if (-not (Get-Command bun -ErrorAction SilentlyContinue)) {
    throw "Bun is required to build the documentation."
}
if ($BasePath -notmatch '^/[^?#\\]*$') {
    throw "BasePath must be an absolute URL path, for example /ProjectBunny/."
}

Push-Location (Join-Path $repoRoot "usage")
try {
    $env:VITEPRESS_BASE = $BasePath.TrimEnd('/') + '/'
    & bun install --frozen-lockfile
    if ($LASTEXITCODE -ne 0) { throw "Documentation dependency installation failed." }
    & bun run docs:build
    if ($LASTEXITCODE -ne 0) { throw "Documentation build failed." }
}
finally {
    $env:VITEPRESS_BASE = $previousBase
    Pop-Location
}
