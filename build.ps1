param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64', 'Win32')]
    [string]$Platform = 'x64',

    [string]$Target = 'DirectX12',
    [string]$GameId = '',
    [switch]$NoPostBuild,
    [switch]$CleanConfigure
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSCommandPath

function Find-CMake {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $candidates = @(
        'C:\Program Files\CMake\bin\cmake.exe',
        'C:\Program Files (x86)\CMake\bin\cmake.exe',
        'C:\tools\cmake\cmake-4.3.2-windows-x86_64\bin\cmake.exe'
    )
    foreach ($path in $candidates) {
        if (Test-Path $path) { return $path }
    }

    throw 'CMake not found. Install CMake or add it to PATH.'
}

function Find-VSInstallPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -eq 0 -and $path) { return $path.Trim() }
    }

    $candidates = @(
        'C:\Program Files\Microsoft Visual Studio\2022\Community',
        'C:\Program Files\Microsoft Visual Studio\2022\Professional',
        'C:\Program Files\Microsoft Visual Studio\2022\Enterprise',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
    )
    foreach ($path in $candidates) {
        if (Test-Path $path) { return $path }
    }

    throw 'Visual Studio 2022 C++ Build Tools not found.'
}

function Find-BuiltFile {
    param(
        [string]$BuildDirectory,
        [string]$ConfigurationName,
        [string]$FileName
    )

    $pattern = "*\$ConfigurationName\*"
    $matches = Get-ChildItem -Path $BuildDirectory -Filter $FileName -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like $pattern } |
        Sort-Object LastWriteTime -Descending
    if ($matches) { return $matches[0].FullName }
    return $null
}

function Copy-DX12PostBuild {
    param(
        [string]$OutputDirectory,
        [string]$BuiltDll,
        [string]$SelectedGameId
    )

    $configPath = Join-Path $root 'build_config.json'
    if (-not (Test-Path $configPath)) {
        Write-Host 'Post-build skipped: build_config.json not found.' -ForegroundColor Yellow
        return
    }

    $config = Get-Content -Path $configPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not $SelectedGameId) { $SelectedGameId = [string]$config.GameId }
    if (-not $SelectedGameId) {
        Write-Host 'Post-build skipped: GameId is empty.' -ForegroundColor Yellow
        return
    }

    $game = $config.Games | Where-Object { [string]$_.GameId -eq $SelectedGameId } | Select-Object -First 1
    if (-not $game) {
        Write-Host "Post-build skipped: GameId '$SelectedGameId' not found." -ForegroundColor Yellow
        return
    }

    $dll = $BuiltDll
    if (-not $dll) { $dll = Join-Path $OutputDirectory 'd3d12.dll' }
    if (-not (Test-Path $dll)) {
        Write-Host "Post-build skipped: d3d12.dll not found: $dll" -ForegroundColor Yellow
        return
    }

    $targets = @()
    if ($game.CopyToTargetDirectory -and $game.CopyToTargetDirectoryPath) {
        $targets += [string]$game.CopyToTargetDirectoryPath
    }
    if ($game.PSObject.Properties['ExtraCopyToTargetDirectoryPaths'] -and $game.ExtraCopyToTargetDirectoryPaths) {
        foreach ($item in $game.ExtraCopyToTargetDirectoryPaths) {
            if (-not [string]::IsNullOrWhiteSpace([string]$item)) { $targets += [string]$item }
        }
    }
    $targets = $targets | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique

    foreach ($dir in $targets) {
        if (-not (Test-Path $dir)) {
            Write-Host "Copy skipped, directory not found: $dir" -ForegroundColor Yellow
            continue
        }
        Copy-Item $dll (Join-Path $dir 'd3d12.dll') -Force
        Write-Host "Copied DX12 DLL: $(Join-Path $dir 'd3d12.dll')" -ForegroundColor Gray
    }
}

$cmake = Find-CMake
$cmakeDir = Split-Path -Parent $cmake
if ($env:PATH -notlike "*$cmakeDir*") {
    $env:PATH = "$cmakeDir;$env:PATH"
}

$vsInstall = Find-VSInstallPath
$buildDir = Join-Path $root "build\vs-$Platform-$($Configuration.ToLower())"
$outDir = if ($Platform -eq 'x64') { Join-Path $root "x64\$Configuration" } else { Join-Path $root "x32\$Configuration" }
$arch = if ($Platform -eq 'x64') { 'x64' } else { 'Win32' }

Write-Host "CMake: $cmake" -ForegroundColor Cyan
Write-Host "Visual Studio: $vsInstall" -ForegroundColor Cyan
Write-Host "Build: $Configuration $Platform Target=$Target" -ForegroundColor Cyan

if ($CleanConfigure -and (Test-Path $buildDir)) {
    Remove-Item $buildDir -Recurse -Force
}

& $cmake -S $root -B $buildDir -G 'Visual Studio 17 2022' -A $arch
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$buildArgs = @('--build', $buildDir, '--config', $Configuration)
if ($Target -and $Target -ne 'ALL') {
    $buildArgs += @('--target', $Target)
}
$buildArgs += @('--parallel')
& $cmake @buildArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$builtD3D12 = Find-BuiltFile -BuildDirectory $buildDir -ConfigurationName $Configuration -FileName 'd3d12.dll'
if ($builtD3D12) {
    if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }
    Copy-Item $builtD3D12 (Join-Path $outDir 'd3d12.dll') -Force
    Write-Host "Copied build output: $(Join-Path $outDir 'd3d12.dll')" -ForegroundColor Gray
}

if (-not $NoPostBuild -and $Platform -eq 'x64' -and ($Target -eq 'DirectX12' -or $Target -eq 'ALL')) {
    Copy-DX12PostBuild -OutputDirectory $outDir -BuiltDll $builtD3D12 -SelectedGameId $GameId
}

Write-Host "Build succeeded. Output: $outDir" -ForegroundColor Green
