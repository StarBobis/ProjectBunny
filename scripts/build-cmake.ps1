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
$ninjaCommand = Get-Command ninja -ErrorAction SilentlyContinue

# Fall back to the CMake/Ninja bundled with Visual Studio (Common7\IDE\
# CommonExtensions\Microsoft\CMake) and to standalone installs, so the build
# works on machines that do not vendor the tools under .tools and do not have
# them on PATH. Same vswhere lookup the vcvars probe below uses.
$vsCmake = $null
$vsNinja = $null
$vswherePath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswherePath) {
    $vsInstallPath = & $vswherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if ($vsInstallPath) {
        $vsCmakeCandidate = Join-Path $vsInstallPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        $vsNinjaCandidate = Join-Path $vsInstallPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
        if (Test-Path $vsCmakeCandidate) { $vsCmake = $vsCmakeCandidate }
        if (Test-Path $vsNinjaCandidate) { $vsNinja = $vsNinjaCandidate }
    }
}
$standaloneCmake = "C:\Program Files\CMake\bin\cmake.exe"
if (-not (Test-Path $standaloneCmake)) { $standaloneCmake = $null }

$cmake = if (Test-Path $localCmake) { $localCmake }
    elseif ($cmakeCommand) { $cmakeCommand.Source }
    elseif ($vsCmake) { $vsCmake }
    elseif ($standaloneCmake) { $standaloneCmake }
    else { $null }
$ninja = if (Test-Path $localNinja) { $localNinja }
    elseif ($ninjaCommand) { $ninjaCommand.Source }
    elseif ($vsNinja) { $vsNinja }
    else { $null }

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
    Write-Host "Install it in .tools\cmake, put it on PATH, or install Visual Studio with the CMake tools (bundled CMake/Ninja are auto-detected)." -ForegroundColor Red
    exit 1
}

if (-not $ninja -or -not (Test-Path $ninja)) {
    Write-Host "ERROR: Ninja not found." -ForegroundColor Red
    Write-Host "Install it in .tools\ninja, put it on PATH, or install Visual Studio with the CMake tools (bundled CMake/Ninja are auto-detected)." -ForegroundColor Red
    exit 1
}

Write-Host "INFO: Using CMake: $cmake" -ForegroundColor Green
Write-Host "INFO: Using Ninja: $ninja" -ForegroundColor Green

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
$tmpConfigureScript = Join-Path $tempDir "cmake_configure_$PID.bat"
$tmpBuildScript = Join-Path $tempDir "cmake_build_$PID.bat"
$configureContent = @"
@echo off
call "$vcvars" > nul
if errorlevel 1 exit /b 1
"$cmake" -S "$root" -B "$buildDir" -G "Ninja" -DCMAKE_MAKE_PROGRAM="$ninja" -DCMAKE_BUILD_TYPE="$Configuration" -DDX12_RELEASE_TIMING=$(if ($ReleaseTiming) { 'ON' } else { 'OFF' })
if errorlevel 1 exit /b 1
"@
$buildContent = @"
@echo off
call "$vcvars" > nul
if errorlevel 1 exit /b 1
"$cmake" --build "$buildDir" --target DirectX12 3Dmigoto-Injector-V2
if errorlevel 1 exit /b 1
"@

Write-Host "===== Configuring CMake ($Configuration|$Platform) =====" -ForegroundColor Cyan
$configureContent | Set-Content -Path $tmpConfigureScript -Encoding ASCII
cmd /c $tmpConfigureScript 2>&1
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    # Transient-failure guard: CMake's compiler detection generates a
    # try_compile mini-project under the build dir and immediately builds it;
    # a real-time AV / file-system race can make freshly generated files (e.g.
    # CMakeFiles\rules.ninja) momentarily invisible to ninja, failing the
    # configure spuriously. Wipe the half-generated build dir and retry once.
    Write-Host "Configure failed; wiping $buildDir and retrying once in case of a transient file-system race..." -ForegroundColor Yellow
    Remove-Item $buildDir -Recurse -Force -ErrorAction SilentlyContinue
    cmd /c $tmpConfigureScript 2>&1
    $exitCode = $LASTEXITCODE
}
Remove-Item $tmpConfigureScript -Force -ErrorAction SilentlyContinue
if ($exitCode -ne 0) {
    Write-Host "FAILED: CMake $($Configuration) $($Platform)" -ForegroundColor Red
    exit $exitCode
}

Write-Host "===== Building CMake ($Configuration|$Platform) =====" -ForegroundColor Cyan
$buildContent | Set-Content -Path $tmpBuildScript -Encoding ASCII
cmd /c $tmpBuildScript 2>&1
$exitCode = $LASTEXITCODE
Remove-Item $tmpBuildScript -Force -ErrorAction SilentlyContinue
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
    "src/DirectXGI/dxgi.dll",
    "src/3Dmigoto-Injector-V2/Run.exe"
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

            # Ship the loader next to the DLL so the deployed folder is
            # launch-ready (Run.exe reads d3dx.ini from its own directory).
            $runExe = "$outDir\Run.exe"
            if (Test-Path $runExe) {
                try {
                    Copy-Item $runExe "$dx12TestDir\Run.exe" -Force -ErrorAction Stop
                    Write-Host "  Copied loader: $dx12TestDir\Run.exe" -ForegroundColor Gray
                } catch {
                    Write-Host "  Failed to copy loader: $dx12TestDir\Run.exe" -ForegroundColor Red
                    Write-Host "  $($_.Exception.Message)" -ForegroundColor Red
                    throw
                }
            }

            # A TIMING build also ships its PDB: the in-process crash report
            # symbolizes our own stack frames against it (the dbghelp guard
            # keeps the symbol server off, so a local PDB is the only source).
            if ($ReleaseTiming) {
                $dx12Pdb = "$buildDir\src\DirectX12\d3d12.pdb"
                if (Test-Path $dx12Pdb) {
                    try {
                        Copy-Item $dx12Pdb "$dx12TestDir\d3d12.pdb" -Force -ErrorAction Stop
                        Write-Host "  Copied DX12 PDB: $dx12TestDir\d3d12.pdb" -ForegroundColor Gray
                    } catch {
                        Write-Host "  Failed to copy DX12 PDB (crash report will log module+offset only): $($_.Exception.Message)" -ForegroundColor Yellow
                    }
                } else {
                    Write-Host "  DX12 PDB not found at $dx12Pdb (crash report will log module+offset only)" -ForegroundColor Yellow
                }
            }

            # Optional DXC backend for [CustomShaderX] runtime HLSL compile.
            # Only the official dxcompiler/dxil pair is deployed, and only
            # from a repo-local drop (third_party\dxc\): the VulkanSDK
            # self-contained dxcompiler fail-fasts (0xC0000409) on the second
            # in-process compile, which would take the game down, and
            # LoadLibrary("dxcompiler.dll") would happily pick it up if we
            # copied it next to d3d12.dll. With no pair deployed the compile
            # degrades to a logged no-op and the custom element is skipped.
            $dxcDrop = "$PSScriptRoot\..\third_party\dxc"
            $dxcCompiler = "$dxcDrop\dxcompiler.dll"
            $dxcDxil = "$dxcDrop\dxil.dll"
            if ((Test-Path $dxcCompiler) -and (Test-Path $dxcDxil)) {
                try {
                    Copy-Item $dxcCompiler "$dx12TestDir\dxcompiler.dll" -Force -ErrorAction Stop
                    Copy-Item $dxcDxil "$dx12TestDir\dxil.dll" -Force -ErrorAction Stop
                    Write-Host "  Copied DXC backend: dxcompiler.dll + dxil.dll" -ForegroundColor Gray
                } catch {
                    Write-Host "  Failed to copy DXC backend (custom shaders will be skipped): $($_.Exception.Message)" -ForegroundColor Yellow
                }
            } else {
                Write-Host "  DXC backend not deployed (third_party\dxc\dxcompiler.dll + dxil.dll missing);" -ForegroundColor DarkYellow
                Write-Host "    [CustomShaderX] elements will log compile_backend_unavailable and be skipped." -ForegroundColor DarkYellow
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
