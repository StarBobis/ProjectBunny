# build_release_x64.ps1 - quick Release x64 build
param(
    [string]$GameId = ""
)

$root = Split-Path -Parent $PSCommandPath
# Keep the wrapper thin and let build-cmake resolve the selected post-build
# target from build_config.json when -GameId is not supplied explicitly.
& "$root\scripts\build-cmake.ps1" -Configuration Release -Platform x64 -GameId $GameId
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
