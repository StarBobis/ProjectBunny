param(
    [ValidateSet('Debug','Release')]
    [string]$Configuration = 'Debug',

    [ValidateSet('x64','Win32')]
    [string]$Platform = 'x64',

    [string]$GameId = '',

    # Build the DX12 DLL with detailed hot-path timing instrumentation
    # compiled in (adds -DDX12_RELEASE_TIMING=ON and uses a separate
    # build directory so the clean build cache is not clobbered).
    [switch]$ReleaseTiming
)

$root = Split-Path -Parent $PSScriptRoot

$cleanScript = Join-Path $root "clean_build_result.ps1"
if (-not (Test-Path $cleanScript)) {
    Write-Host "ERROR: Clean script not found: $cleanScript" -ForegroundColor Red
    exit 1
}

Write-Host "===== Cleaning previous build results =====" -ForegroundColor Cyan
& $cleanScript
if (-not $?) {
    Write-Host "FAILED: clean_build_result.ps1" -ForegroundColor Red
    exit 1
}

$localCmake = Join-Path $root ".tools\cmake\bin\cmake.exe"
$localNinja = Join-Path $root ".tools\ninja\ninja.exe"
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmake = if (Test-Path $localCmake) { $localCmake } elseif ($cmakeCommand) { $cmakeCommand.Source } else { $null }

$buildDirSuffix = if ($ReleaseTiming) { '-timing' } else { '' }
$buildDir = "$root\build\cmake-$Platform-$($Configuration.ToLower())$buildDirSuffix"

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

if (-not $cmake -or -not (Test-Path $cmake)) {
    Write-Host "ERROR: CMake not found." -ForegroundColor Red
    Write-Host "Install it in .tools\cmake or ensure cmake is in your system PATH." -ForegroundColor Red
    exit 1
}

$ninjaCommand = Get-Command ninja -ErrorAction SilentlyContinue
$ninja = if (Test-Path $localNinja) { $localNinja } elseif ($ninjaCommand) { $ninjaCommand.Source } else { $null }
if (-not $ninja -or -not (Test-Path $ninja)) {
    Write-Host "ERROR: Ninja not found." -ForegroundColor Red
    Write-Host "Install it in .tools\ninja or ensure ninja is in your system PATH." -ForegroundColor Red
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
"$cmake" -S "$root" -B "$buildDir" -G "Ninja" -DCMAKE_MAKE_PROGRAM="$ninja" -DCMAKE_BUILD_TYPE="$Configuration" -DDX12_RELEASE_TIMING=$(if ($ReleaseTiming) { 'ON' } else { 'OFF' })
if errorlevel 1 exit /b 1
"$cmake" --build "$buildDir" --target DirectX12
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

            # Never delete the previous run's log files: rotate them to a
            # timestamped backup so a build cannot destroy the evidence the
            # user is analyzing. Only the oldest rotated backups are pruned
            # (keep the newest 10 per log kind) to bound disk usage.
            $dx12LogFiles = @(
                "$dx12TestDir\d3d12_log.jsonl",
                "$dx12TestDir\d3d12_log.txt",
                "$dx12TestDir\d3d12_hook.log"
            )
            $dx12RotateStamp = Get-Date -Format "yyyyMMdd-HHmmss"
            foreach ($dx12LogFile in $dx12LogFiles) {
                if (Test-Path $dx12LogFile) {
                    $dx12LogBase = [System.IO.Path]::GetFileNameWithoutExtension($dx12LogFile)
                    $dx12LogExt = [System.IO.Path]::GetExtension($dx12LogFile)
                    $dx12BackupFile = Join-Path $dx12TestDir ("{0}.before_{1}{2}" -f $dx12LogBase, $dx12RotateStamp, $dx12LogExt)
                    try {
                        Move-Item -LiteralPath $dx12LogFile -Destination $dx12BackupFile -Force -ErrorAction Stop
                        Write-Host "  Rotated DX12 test log: $dx12BackupFile" -ForegroundColor Gray
                    } catch {
                        Write-Host "  Could not rotate DX12 test log (leaving it in place): $dx12LogFile" -ForegroundColor Yellow
                        Write-Host "  $($_.Exception.Message)" -ForegroundColor Yellow
                    }
                }
            }
            foreach ($dx12RotatePattern in @("d3d12_log.before_*.jsonl", "d3d12_log.before_*.txt", "d3d12_hook.before_*.log")) {
                $dx12Rotated = @(Get-ChildItem -Path $dx12TestDir -Filter $dx12RotatePattern -File -ErrorAction SilentlyContinue |
                    Sort-Object LastWriteTime -Descending)
                if ($dx12Rotated.Count -gt 10) {
                    foreach ($dx12OldLog in $dx12Rotated | Select-Object -Skip 10) {
                        try {
                            Remove-Item -LiteralPath $dx12OldLog.FullName -Force -ErrorAction Stop
                            Write-Host "  Pruned old DX12 test log: $($dx12OldLog.FullName)" -ForegroundColor Gray
                        } catch {
                            Write-Host "  Could not prune old DX12 test log: $($dx12OldLog.FullName)" -ForegroundColor Yellow
                        }
                    }
                }
            }
            $dx12ShaderDumpDir = "$dx12TestDir\ShaderDumpDX12"
            if (Test-Path $dx12ShaderDumpDir) {
                try {
                    Remove-Item -LiteralPath $dx12ShaderDumpDir -Recurse -Force -ErrorAction Stop
                    Write-Host "  Removed old DX12 shader dump directory: $dx12ShaderDumpDir" -ForegroundColor Gray
                } catch {
                    Write-Host "  Could not remove old DX12 shader dump directory: $dx12ShaderDumpDir" -ForegroundColor Yellow
                    Write-Host "  $($_.Exception.Message)" -ForegroundColor Yellow
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
