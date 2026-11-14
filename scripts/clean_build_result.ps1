param(
    [switch]$WhatIf,
    [switch]$KeepOutput
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectRootFull = [System.IO.Path]::GetFullPath($projectRoot)

function Test-PathInsideProject {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    return $fullPath.StartsWith($projectRootFull + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)
}

function Remove-BuildPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (!(Test-Path -LiteralPath $Path)) {
        return
    }

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if (!(Test-PathInsideProject -Path $fullPath)) {
        throw "Refusing to clean path outside project root: $fullPath"
    }

    if ($WhatIf) {
        Write-Host "[WhatIf] Remove $fullPath"
        return
    }

    Write-Host "Remove $fullPath"
    Remove-Item -LiteralPath $fullPath -Recurse -Force
}

$pathsToClean = @(
    "build",
    "CMakeFiles",
    "CMakeCache.txt",
    "cmake_install.cmake",
    "build-debug-current.log",
    "build-last.log"
)

if (!$KeepOutput) {
    $pathsToClean += @(
        "x64"
    )
}

foreach ($relativePath in $pathsToClean) {
    Remove-BuildPath -Path (Join-Path $projectRootFull $relativePath)
}

Write-Host "Clean complete."
