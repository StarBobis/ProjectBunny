param(
    [string]$GameId = ""
)

# Release build WITH the DX12 detailed hot-path timing instrumentation
# compiled in (DX12HotPathTiming per-stage breakdown, ApiWall buckets,
# queue execute slices). Use this to profile the DX12 hook overhead in the
# same optimized build you play with, then rebuild with
# build_release_x64.ps1 to go back to the clean Release DLL.
#
# NOTE: this overwrites the deployed d3d12.dll just like a normal Release
# build, and the timing instrumentation itself costs some performance -
# do not benchmark fps with this build, only read the stage breakdown logs.

$root = Split-Path -Parent $PSScriptRoot
& "$root\scripts\build-cmake.ps1" -Configuration Release -Platform x64 -GameId $GameId -ReleaseTiming
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
