[CmdletBinding()]
param([switch]$BuildCandidate, [switch]$BuildBusiness, [ValidateRange(1, 32)][int]$Parallel = 6)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$simulationRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = Split-Path -Parent $simulationRoot
$buildRoot = Join-Path $simulationRoot 'build'
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
$toolchain = Initialize-SimulationToolchain
$toolchain | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $buildRoot 'toolchain.json') -Encoding utf8
$hostBuild = Join-Path $buildRoot 'tools'
$hostSources = @('模拟环境\CMakeLists.txt','模拟环境\src\dx11_sim_host.cpp','模拟环境\src\dx11_fault_provider.cpp','模拟环境\src\dx11_fault_provider.def','src\DirectX11\tests\proxy_smoke.cpp','src\DirectX11\DX11Version.h')
$hostSourceHashes = @($hostSources | ForEach-Object {
    [pscustomobject]@{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $repositoryRoot $_) -Algorithm SHA256).Hash}
})
& $toolchain.cmake -S $simulationRoot -B $hostBuild -G Ninja "-DCMAKE_MAKE_PROGRAM=$($toolchain.ninja)" -DCMAKE_BUILD_TYPE=Release 2>&1 |
    Tee-Object -FilePath (Join-Path $buildRoot 'host-configure.log')
if ($LASTEXITCODE -ne 0) { throw "Host configuration failed: $LASTEXITCODE" }
& $toolchain.cmake --build $hostBuild --parallel $Parallel 2>&1 | Tee-Object -FilePath (Join-Path $buildRoot 'host-build.log')
if ($LASTEXITCODE -ne 0) { throw "Host build failed: $LASTEXITCODE" }
foreach ($source in $hostSourceHashes) {
    if ((Get-FileHash -LiteralPath (Join-Path $repositoryRoot $source.path) -Algorithm SHA256).Hash -ne $source.sha256) {
        throw "Source changed during build; rebuild the test host: $($source.path)"
    }
}
$hosts = @('dx11_sim_host.exe','dx11_existing_smoke.exe','dx11_fault_provider.dll') | ForEach-Object {
    [pscustomobject]@{name=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $hostBuild $_) -Algorithm SHA256).Hash}
}
@{utc=[DateTime]::UtcNow.ToString('o');configuration='Release';standard='C++17';toolchain=$toolchain;sources=$hostSourceHashes;hosts=@($hosts)} |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $buildRoot 'host-build-info.json') -Encoding utf8
if ($BuildCandidate -and -not $BuildBusiness) {
    # Use the project's isolated DX11 Release build script, which does not deploy.
    & (Join-Path $repositoryRoot 'scripts\build_release_dx11_x64.ps1') -BuildDirectory (Join-Path $buildRoot 'candidate') -Parallel $Parallel 2>&1 |
        Tee-Object -FilePath (Join-Path $buildRoot 'candidate-build.log')
    if ($LASTEXITCODE -ne 0) { throw "Candidate build failed: $LASTEXITCODE" }
}
if ($BuildBusiness) {
    $candidateBuild = Join-Path $buildRoot 'candidate'
    $pythonPath = & py -3.11 -c 'import sys; print(sys.executable)'
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $pythonPath)) { throw 'Python 3.11 is required for the existing version-generator regression.' }
    $sourceFiles = @(Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'src'),(Join-Path $repositoryRoot 'cmake') -Recurse -File)
    $sourceFiles += Get-ChildItem -LiteralPath (Join-Path $simulationRoot 'src'),(Join-Path $simulationRoot 'scripts') -Recurse -File
    $sourceFiles += Get-Item -LiteralPath (Join-Path $repositoryRoot 'CMakeLists.txt'),(Join-Path $repositoryRoot 'scripts\build_release_dx11_x64.ps1'),(Join-Path $repositoryRoot 'scripts\test-dx11-version.py'),(Join-Path $simulationRoot 'CMakeLists.txt'),(Join-Path $simulationRoot 'business-overlay.cmake')
    $sourceSnapshot = @($sourceFiles | Sort-Object FullName | ForEach-Object {
        [pscustomobject]@{path=[IO.Path]::GetRelativePath($repositoryRoot,$_.FullName);sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
    })
    $asanRuntime=Join-Path (Split-Path -Parent $toolchain.compiler) 'clang_rt.asan_dynamic-x86_64.dll'
    if(-not (Test-Path -LiteralPath $asanRuntime)){throw 'MSVC x64 AddressSanitizer runtime is required for lock cleanup validation.'}
    & $toolchain.cmake -S $repositoryRoot -B $candidateBuild -G Ninja "-DCMAKE_MAKE_PROGRAM=$($toolchain.ninja)" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DDX11_AUDIT_ASAN_LOCKS=ON -DDX12_RELEASE_TIMING=OFF "-DPython3_EXECUTABLE=$pythonPath" "-DCMAKE_PROJECT_INCLUDE=$(Join-Path $simulationRoot 'business-overlay.cmake')" 2>&1 |
        Tee-Object -FilePath (Join-Path $buildRoot 'business-configure.log')
    if ($LASTEXITCODE -ne 0) { throw "Business configuration failed: $LASTEXITCODE" }
    & $toolchain.cmake --build $candidateBuild --target dx11_upstream_regression D3D_Shaders --parallel $Parallel 2>&1 |
        Tee-Object -FilePath (Join-Path $buildRoot 'business-build.log')
    if ($LASTEXITCODE -ne 0) { throw "Business build failed: $LASTEXITCODE" }
    Copy-Item -LiteralPath $asanRuntime -Destination (Join-Path $candidateBuild 'src\DirectX11\clang_rt.asan_dynamic-x86_64.dll') -Force
    foreach ($source in $sourceSnapshot) {
        if ((Get-FileHash -LiteralPath (Join-Path $repositoryRoot $source.path) -Algorithm SHA256).Hash -ne $source.sha256) { throw "Source changed during business build: $($source.path)" }
    }
    $head = & git -C $repositoryRoot rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot record build HEAD.' }
    $products = @('src\DirectX11\d3d11.dll','src\DirectX11\dx11_upstream_regression.exe','src\DirectX11\Release\dx11_legacy_version_regression.exe','src\D3D_Shaders\D3D_Shaders.exe','src\DirectX11\clang_rt.asan_dynamic-x86_64.dll') | ForEach-Object {
        [pscustomobject]@{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $candidateBuild $_) -Algorithm SHA256).Hash}
    }
    @{utc=[DateTime]::UtcNow.ToString('o');head=$head;configuration='Release';standard='C++17';lockCleanupAddressSanitizer=$true;python=$pythonPath;toolchain=$toolchain;sources=$sourceSnapshot;products=@($products)} |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $buildRoot 'business-build-info.json') -Encoding utf8
}
Write-Host "Test hosts: $hostBuild"
if ($BuildCandidate) { Write-Host "Candidate: $(Join-Path $buildRoot 'candidate\src\DirectX11\d3d11.dll')" }
