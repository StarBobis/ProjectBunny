# Compatibility entry point for the default x64 release build.
param(
    [string]$GameId = ""
)

$root = Split-Path -Parent $PSScriptRoot
& "$root\scripts\build_release_x64.ps1" -GameId $GameId
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
