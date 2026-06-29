param(
    [ValidateSet('Debug','Release')]
    [string]$Configuration = 'Debug',

    [ValidateSet('x64','Win32')]
    [string]$Platform = 'x64',

    [string]$GameId = ''
)

$root = Split-Path -Parent $PSScriptRoot

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $cmake = "C:\tools\cmake\cmake-4.3.2-windows-x86_64\bin\cmake.exe"
}

$buildDir = "$root\build\cmake-$Platform-$($Configuration.ToLower())"

$cacheFile = "$buildDir\CMakeCache.txt"
if (Test-Path $cacheFile) {
    $cacheContent = Get-Content $cacheFile -Raw -ErrorAction SilentlyContinue
    $cachedSourceDir = if ($cacheContent -match 'CMAKE_HOME_DIRECTORY:INTERNAL=(.+)') { $Matches[1].Trim() } else { '' }
    $currentSourceDir = (Resolve-Path $root).Path.TrimEnd('\')
    $cachedSourceDir = $cachedSourceDir.Replace('/', '\').TrimEnd('\')
    if ($cachedSourceDir -and $cachedSourceDir -ne $currentSourceDir) {
        Write-Host "Stale CMake cache detected (was: $cachedSourceDir, now: $currentSourceDir)" -ForegroundColor Yellow
        Write-Host "Cleaning build directory: $buildDir" -ForegroundColor Yellow
        Remove-Item $buildDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

if (-not (Test-Path $cmake)) {
    Write-Host "ERROR: CMake not found at $cmake" -ForegroundColor Red
    Write-Host "Please install CMake and ensure it is in your system PATH." -ForegroundColor Red
    exit 1
}

$vcvars = $null
$vswherePath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

if (Test-Path $vswherePath) {
    $vsInstallPath = & $vswherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if ($vsInstallPath) {
        $vcvars = if ($Platform -eq 'x64') {
            "$vsInstallPath\VC\Auxiliary\Build\vcvars64.bat"
        } else {
            "$vsInstallPath\VC\Auxiliary\Build\vcvars32.bat"
        }
    }
}

if (-not $vcvars -or -not (Test-Path $vcvars)) {
    $candidateRoots = @(
        "C:\Program Files\Microsoft Visual Studio"
    )
    $found = $false
    foreach ($rootDir in $candidateRoots) {
        if (-not (Test-Path $rootDir)) { continue }
        $versions = Get-ChildItem $rootDir -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending
        foreach ($ver in $versions) {
            $editions = @('Community', 'Professional', 'Enterprise', 'BuildTools')
            foreach ($ed in $editions) {
                $testVcvars = if ($Platform -eq 'x64') {
                    "$($ver.FullName)\$ed\VC\Auxiliary\Build\vcvars64.bat"
                } else {
                    "$($ver.FullName)\$ed\VC\Auxiliary\Build\vcvars32.bat"
                }
                if (Test-Path $testVcvars) {
                    $vcvars = $testVcvars
                    $found = $true
                    break
                }
            }
            if ($found) { break }
        }
        if ($found) { break }
    }
}

if (-not $vcvars -or -not (Test-Path $vcvars)) {
    Write-Host "ERROR: Cannot locate Visual Studio with VC tools (vswhere + fallback scan both failed)." -ForegroundColor Red
    Write-Host "       Install Visual Studio 2022/18 with 'Desktop development with C++' workload." -ForegroundColor Red
    exit 1
}

Write-Host "INFO: Using Visual Studio vcvars: $vcvars" -ForegroundColor Green

$tempDir = $env:TEMP
if (-not $tempDir) {
    $tempDir = [System.IO.Path]::GetTempPath()
}
if (-not $tempDir) {
    $tempDir = $root
}
$tmpScript = Join-Path $tempDir "cmake_build_$PID.bat"
$content = @"
@echo off
call "$vcvars" > nul
if errorlevel 1 exit /b 1
"$cmake" -S "$root" -B "$buildDir" -G "Ninja" -DCMAKE_BUILD_TYPE="$Configuration"
if errorlevel 1 exit /b 1
"$cmake" --build "$buildDir" --target DirectX12 D3DCompiler_46 dxgi DirectX11
"@

Write-Host "===== Configuring CMake ($Configuration|$Platform) =====" -ForegroundColor Cyan
if (-not $tmpScript) {
    Write-Host "ERROR: Cannot determine temp directory for build script." -ForegroundColor Red
    exit 1
}
$content | Set-Content -Path $tmpScript -Encoding ASCII
cmd /c $tmpScript 2>&1
$exitCode = $LASTEXITCODE
Remove-Item $tmpScript -Force -ErrorAction SilentlyContinue
if ($exitCode -ne 0) {
    Write-Host "FAILED: CMake $($Configuration) $($Platform)" -ForegroundColor Red
    exit $exitCode
}

$outDir = if ($Platform -eq 'x64') { "$root\x64\$Configuration" } else { "$root\x32\$Configuration" }
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }

$files = @(
    "src/DirectX11/d3d11.dll",
    "src/DirectX12/d3d12.dll",
    "src/D3DCompiler_46/d3dcompiler_47.dll",
    "src/DirectXGI/dxgi.dll"
)
foreach ($f in $files) {
    $src = "$buildDir/$f"
    $dst = "$outDir/$(Split-Path -Leaf $f)"
    if (Test-Path $src) {
        Copy-Item $src $dst -Force
        Write-Host "  Copied: $dst" -ForegroundColor Gray
    }
}

if ($Platform -eq 'x64') {
    $buildConfigPath = Join-Path $root "build_config.json"
    if (-not (Test-Path $buildConfigPath)) {
        Write-Host "  Warning: build_config.json not found. Skipping DX12 test DLL copy and launch." -ForegroundColor Yellow
        return
    }

    try {
        $buildConfig = Get-Content -Path $buildConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
    } catch {
        Write-Host "  Warning: Could not parse build_config.json. Skipping DX12 test DLL copy and launch." -ForegroundColor Yellow
        Write-Host "  $($_.Exception.Message)" -ForegroundColor Yellow
        return
    }

    $selectedGameId = if ($GameId) { $GameId } else { [string]$buildConfig.GameId }
    if (-not $selectedGameId) {
        Write-Host "  Warning: No GameId specified in build_config.json. Skipping DX12 test DLL copy and launch." -ForegroundColor Yellow
        return
    }

    $gameConfig = $buildConfig.Games | Where-Object { [string]$_.GameId -eq $selectedGameId } | Select-Object -First 1
    if (-not $gameConfig) {
        Write-Host "  Warning: GameId '$selectedGameId' not found in build_config.json. Skipping DX12 test DLL copy and launch." -ForegroundColor Yellow
        return
    }

    $dx12Exe = [string]$gameConfig.TargetExe
    $hasWorkingDirectory = $null -ne $gameConfig.PSObject.Properties["WorkingDirectory"]
    $dx12WorkingDirectory = if ($hasWorkingDirectory) { [string]$gameConfig.WorkingDirectory } else { Split-Path -Parent $dx12Exe }
    $dx12LaunchArguments = [string]$gameConfig.LaunchArguments
    $hasCopyToTargetDirectory = $null -ne $gameConfig.PSObject.Properties["CopyToTargetDirectory"]
    $copyToTargetDirectory = if ($hasCopyToTargetDirectory) { [bool]$gameConfig.CopyToTargetDirectory } else { $true }
    $copyToTargetDirectoryPath = [string]$gameConfig.CopyToTargetDirectoryPath
    $hasExtraCopyTargets = $null -ne $gameConfig.PSObject.Properties["ExtraCopyToTargetDirectoryPaths"]
    $extraCopyTargets = @()
    if ($hasExtraCopyTargets -and $null -ne $gameConfig.ExtraCopyToTargetDirectoryPaths) {
        foreach ($item in $gameConfig.ExtraCopyToTargetDirectoryPaths) {
            $pathText = [string]$item
            if (-not [string]::IsNullOrWhiteSpace($pathText)) {
                $extraCopyTargets += $pathText
            }
        }
    }
    $hasLaunchGame = $null -ne $gameConfig.PSObject.Properties["LaunchGame"]
    $launchGame = if ($hasLaunchGame) { [bool]$gameConfig.LaunchGame } else { $true }

    if (-not $dx12Exe -and -not $copyToTargetDirectoryPath) {
        Write-Host "  Warning: GameId '$selectedGameId' has neither TargetExe nor CopyToTargetDirectoryPath. Skipping DX12 post-build actions." -ForegroundColor Yellow
        return
    }

    if ($dx12Exe -and -not (Test-Path $dx12Exe)) {
        Write-Host "  Skipped DX12 launch: executable not found: $dx12Exe" -ForegroundColor Yellow
        $launchGame = $false
    } elseif (-not $dx12Exe) {
        $launchGame = $false
    }

    if ($dx12WorkingDirectory -and -not (Test-Path $dx12WorkingDirectory)) {
        Write-Host "  Skipped DX12 launch: working directory not found: $dx12WorkingDirectory" -ForegroundColor Yellow
        $launchGame = $false
    }

    if (-not $copyToTargetDirectory -and -not $launchGame) {
        Write-Host "  Skipped DX12 target copy and launch for GameId '$selectedGameId'." -ForegroundColor Gray
        return
    }

    $dx12PrimaryCopyDir = if ($copyToTargetDirectoryPath) { $copyToTargetDirectoryPath } else { Split-Path -Parent $dx12Exe }
    $dx12CopyTargets = @()
    if ($copyToTargetDirectory -and -not [string]::IsNullOrWhiteSpace($dx12PrimaryCopyDir)) {
        $dx12CopyTargets += $dx12PrimaryCopyDir
    }
    $dx12CopyTargets += $extraCopyTargets
    $dx12CopyTargets = $dx12CopyTargets |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Select-Object -Unique
    $dx12Dll = "$outDir\d3d12.dll"
    if ($dx12CopyTargets.Count -gt 0 -and (Test-Path $dx12Dll)) {
        if ($dx12Exe) {
            $dx12ProcessName = [System.IO.Path]::GetFileNameWithoutExtension($dx12Exe)
            $dx12Processes = Get-Process -Name $dx12ProcessName -ErrorAction SilentlyContinue
            if ($dx12Processes) {
                Write-Host "  Stopping running DX12 target before DLL copy..." -ForegroundColor Gray
                $dx12Processes | Stop-Process -Force
                Start-Sleep -Seconds 1
            }
        }

        foreach ($dx12TestDir in $dx12CopyTargets) {
            if (-not (Test-Path $dx12TestDir)) {
                Write-Host "  Skipped DX12 DLL copy: directory not found: $dx12TestDir" -ForegroundColor Yellow
                continue
            }

            $dx12CleanupPaths = @(
                "$dx12TestDir\ShaderDumpDX12",
                "$dx12TestDir\d3d12_log.jsonl",
                "$dx12TestDir\d3d12_log.txt",
                "$dx12TestDir\d3d12_hook.log"
            )
            $dx12CleanupPaths += Get-ChildItem -Path $dx12TestDir -Filter "d3d12_log.before_*.jsonl" -File -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }
            $dx12CleanupPaths += Get-ChildItem -Path $dx12TestDir -Filter "d3d12_log.before_*.txt" -File -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }
            foreach ($cleanupPath in $dx12CleanupPaths) {
                if (Test-Path $cleanupPath) {
                    try {
                        Remove-Item $cleanupPath -Recurse -Force -ErrorAction Stop
                        Write-Host "  Removed old DX12 test artifact: $cleanupPath" -ForegroundColor Gray
                    } catch {
                        Write-Host "  Could not remove old DX12 test artifact: $cleanupPath" -ForegroundColor Yellow
                        Write-Host "  $($_.Exception.Message)" -ForegroundColor Yellow
                    }
                }
            }

            $dx12TargetDll = "$dx12TestDir\d3d12.dll"
            try {
                Copy-Item $dx12Dll $dx12TargetDll -Force -ErrorAction Stop
                Write-Host "  Copied DX12 DLL: $dx12TargetDll" -ForegroundColor Gray
            } catch {
                Write-Host "  Failed to copy DX12 DLL: $dx12TargetDll" -ForegroundColor Red
                Write-Host "  $($_.Exception.Message)" -ForegroundColor Red
                throw
            }
        }
    } elseif (($copyToTargetDirectory -or $extraCopyTargets.Count -gt 0) -and -not (Test-Path $dx12Dll)) {
        Write-Host "  Skipped DX12 DLL copy: DLL not found: $dx12Dll" -ForegroundColor Yellow
    } else {
        Write-Host "  Skipped DX12 DLL copy: CopyToTargetDirectory=false" -ForegroundColor Gray
    }

    if ($launchGame) {
        $startArgs = @{
            FilePath = $dx12Exe
        }
        if ($dx12WorkingDirectory) {
            $startArgs.WorkingDirectory = $dx12WorkingDirectory
        }
        if ($dx12LaunchArguments) {
            $startArgs.ArgumentList = $dx12LaunchArguments
        }
        Start-Process @startArgs
        $workingDirectoryText = if ($dx12WorkingDirectory) { $dx12WorkingDirectory } else { "<empty>" }
        Write-Host "  Launched DX12 target [$selectedGameId]: $dx12Exe $dx12LaunchArguments (WorkingDirectory=$workingDirectoryText)" -ForegroundColor Gray
    } else {
        Write-Host "  Skipped DX12 launch: LaunchGame=false" -ForegroundColor Gray
    }
}

Write-Host "`n===== All projects built successfully =====" -ForegroundColor Green
Write-Host "Output directory: $outDir"
