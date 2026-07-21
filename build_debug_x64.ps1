param(
    [string]$GameId = ""
)

$root = Split-Path -Parent $PSCommandPath
& "$root\scripts\build-cmake.ps1" -Configuration Debug -Platform x64 -GameId $GameId
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
