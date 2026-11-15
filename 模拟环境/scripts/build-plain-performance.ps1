[CmdletBinding()]
param([ValidateRange(1,32)][int]$Parallel=6)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$simulationRoot=Split-Path -Parent $PSScriptRoot
$repositoryRoot=Split-Path -Parent $simulationRoot
$buildRoot=Join-Path $simulationRoot 'build'
$businessPath=Join-Path $buildRoot 'business-build-info.json'
$business=Get-Content -LiteralPath $businessPath -Raw | ConvertFrom-Json
$known=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach($source in $business.sources){
    $null=$known.Add($source.path)
    if((Get-FileHash -LiteralPath (Join-Path $repositoryRoot $source.path)).Hash -ne $source.sha256){throw "Run the business build before performance building: $($source.path)"}
}
foreach($file in Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'src'),(Join-Path $repositoryRoot 'cmake'),(Join-Path $simulationRoot 'src'),(Join-Path $simulationRoot 'scripts') -File -Recurse){
    if(-not $known.Contains([IO.Path]::GetRelativePath($repositoryRoot,$file.FullName))){throw 'Business build source inventory is stale.'}
}
$toolchain=Initialize-SimulationToolchain
$plain=Join-Path $buildRoot 'plain-performance'
& $toolchain.cmake -S $repositoryRoot -B $plain -G Ninja "-DCMAKE_MAKE_PROGRAM=$($toolchain.ninja)" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DDX11_AUDIT_ASAN_LOCKS=OFF -DBUNNY_PROXY_ASAN=OFF -DDX12_RELEASE_TIMING=OFF "-DPython3_EXECUTABLE=$($business.python)" "-DCMAKE_PROJECT_INCLUDE=$(Join-Path $simulationRoot 'business-overlay.cmake')" 2>&1 | Tee-Object -FilePath (Join-Path $buildRoot 'plain-performance-configure.log')
if($LASTEXITCODE -ne 0){throw 'Plain performance configure failed.'}
& $toolchain.cmake --build $plain --target dx11_upstream_regression --parallel $Parallel 2>&1 | Tee-Object -FilePath (Join-Path $buildRoot 'plain-performance-build.log')
if($LASTEXITCODE -ne 0){throw 'Plain performance build failed.'}
foreach($source in $business.sources){if((Get-FileHash -LiteralPath (Join-Path $repositoryRoot $source.path)).Hash -ne $source.sha256){throw 'Source changed during plain performance building.'}}
$dumpbin=Join-Path (Split-Path -Parent $toolchain.compiler) 'dumpbin.exe'
$dependencies=& $dumpbin /DEPENDENTS (Join-Path $plain 'src\DirectX11\dx11_upstream_regression.exe')
if($LASTEXITCODE -ne 0 -or ($dependencies -match 'clang_rt|asan')){throw 'Plain executable dependency audit failed.'}
$dependencies | Set-Content -LiteralPath (Join-Path $plain 'runtime-dependencies.txt') -Encoding utf8
$products=@(foreach($name in @('dx11_upstream_regression.exe','d3d11.dll','wic-regression.png')){
    $relative="src\DirectX11\$name"
    [pscustomobject]@{path=$relative;sha256=(Get-FileHash -LiteralPath (Join-Path $plain $relative)).Hash}
})
@{utc=[DateTime]::UtcNow.ToString('o');configuration='Release';standard='C++17';addressSanitizer=$false;basisBusinessManifestSha256=(Get-FileHash -LiteralPath $businessPath).Hash;toolchain=$toolchain;sources=$business.sources;products=$products;runtimeDependencyAuditSha256=(Get-FileHash -LiteralPath (Join-Path $plain 'runtime-dependencies.txt')).Hash} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $buildRoot 'plain-performance-build-info.json') -Encoding utf8
