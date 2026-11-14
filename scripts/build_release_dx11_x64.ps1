# Build only the DX11 Release DLL, without deployment or game launch.
[CmdletBinding()]
param(
    [string]$BuildDirectory = '',
    [ValidateRange(1, 128)]
    [int]$Parallel = 6
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $projectRoot 'build\dx11-release-x64'
} elseif (-not [System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $projectRoot $BuildDirectory
}
$BuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer / vswhere was not found.'
}
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $installation) {
    throw 'Visual Studio with the C++ x64 build tools was not found.'
}
$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) {
    throw "Missing MSVC environment script: $vcvars"
}

# Import the compiler environment into this process; paths remain quoted for cmd.
$environmentCommand = 'call "{0}" > nul && set' -f $vcvars
$compilerEnvironment = & $env:ComSpec /d /c $environmentCommand
if ($LASTEXITCODE -ne 0) { throw 'Failed to initialize the MSVC x64 environment.' }
foreach ($line in $compilerEnvironment) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}

$cmake = (Get-Command cmake -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$ninja = (Get-Command ninja -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
& $cmake -S $projectRoot -B $BuildDirectory -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" `
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DDX12_RELEASE_TIMING=OFF
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed ($LASTEXITCODE)." }
& $cmake --build $BuildDirectory --target DirectX11 --parallel $Parallel
if ($LASTEXITCODE -ne 0) { throw "DX11 build failed ($LASTEXITCODE)." }

$dll = Join-Path $BuildDirectory 'src\DirectX11\d3d11.dll'
if (-not (Test-Path -LiteralPath $dll)) { throw "Build output was not found: $dll" }
Write-Host "Built: $dll"
