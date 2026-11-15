[CmdletBinding()]
param([string]$Item = '',[switch]$SpecialOnly,[ValidateRange(1000,60000)][int]$TimeoutMs=60000)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'process.ps1')
$simulationRoot=Split-Path -Parent $PSScriptRoot
$repositoryRoot=Split-Path -Parent $simulationRoot
$buildRoot=Join-Path $simulationRoot 'build'
$candidateBuild=Join-Path $buildRoot 'candidate'
$info=Get-Content -LiteralPath (Join-Path $buildRoot 'business-build-info.json') -Raw | ConvertFrom-Json
foreach($source in $info.sources) {
    if((Get-FileHash -LiteralPath (Join-Path $repositoryRoot $source.path) -Algorithm SHA256).Hash -ne $source.sha256) { throw "Rebuild before testing changed source: $($source.path)" }
}
foreach($product in $info.products) {
    if((Get-FileHash -LiteralPath (Join-Path $candidateBuild $product.path) -Algorithm SHA256).Hash -ne $product.sha256) { throw "Built product changed: $($product.path)" }
}
$label=if($Item){$Item}else{'baseline'}
if($SpecialOnly -and -not $Item){throw 'SpecialOnly requires an item.'}
if($label -notmatch '^(baseline|F-\d{3}|P-\d{2})$'){throw 'Invalid item id.'}
$runRoot=Join-Path $buildRoot ('f\'+[guid]::NewGuid().ToString('N').Substring(0,12))
New-Item -ItemType Directory -Path $runRoot | Out-Null
$checks=[Collections.Generic.List[object]]::new()
$dll=Join-Path $candidateBuild 'src\DirectX11\d3d11.dll'
$regression=Join-Path $candidateBuild 'src\DirectX11\dx11_upstream_regression.exe'
foreach($mode in @('special','business')) {
    if($mode -eq 'special' -and -not $Item){continue}
    if($mode -eq 'business' -and $SpecialOnly){continue}
    $caseRoot=Join-Path $runRoot $mode
    New-Item -ItemType Directory -Path $caseRoot | Out-Null
    foreach($path in @($dll,$regression,(Join-Path $candidateBuild 'src\DirectX11\wic-regression.png'))) {
        Copy-Item -LiteralPath $path -Destination $caseRoot
    }
    $exe=Join-Path $caseRoot 'dx11_upstream_regression.exe'
    $arguments=if($mode -eq 'special'){@('--audit-item',$Item)}else{@('--full')}
    $result=Invoke-IsolatedSimulationProcess -Executable $exe -Arguments $arguments -WorkingDirectory $caseRoot -LogPrefix (Join-Path $caseRoot 'host') -TimeoutMs $TimeoutMs
    if($mode -eq 'special' -and $result.status -eq 'PASS') {
        $outputLines=Get-Content -LiteralPath (Join-Path $caseRoot 'host.stdout.txt')
        if("PASS audit item: $Item" -notin $outputLines) {
            $result.status='FAIL';$result.message='Specialized test did not acknowledge the requested item.'
        }
    }
    $checks.Add([pscustomobject]@{name=$mode;status=$result.status;detail=$result})
    Write-Host "[$($result.status)] $label $mode"
}
if($SpecialOnly) {
    $status=if(@($checks | Where-Object status -ne 'PASS').Count){'FAIL'}else{'PASS'}
    @{item=$label;status=$status;validationLevel='special-only';buildHead=$info.head;candidateSha256=(Get-FileHash -LiteralPath $dll).Hash;checks=$checks.ToArray()} |
        ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'results.json') -Encoding utf8
    Write-Host "Special report: $(Join-Path $runRoot 'results.json')"
    if($status -ne 'PASS'){exit 1}
    exit 0
}
$shaderRoot=Join-Path $runRoot 'business\shader-fixture'
$toolSource=Join-Path $candidateBuild 'src\D3D_Shaders\D3D_Shaders.exe'
$shaderResult=[pscustomobject]@{status='NOT_RUN';exitCode=$null;elapsedMs=0;message='Business suite did not produce the native-validated shader fixture.'}
if(Test-Path -LiteralPath (Join-Path $shaderRoot 'expected.cbo')) {
    $toolCopy=Join-Path $shaderRoot 'D3D_Shaders.exe'
    Copy-Item -LiteralPath $toolSource -Destination $toolCopy
    $shaderResult=Invoke-IsolatedSimulationProcess -Executable $toolCopy -Arguments @('--fixture-run') -WorkingDirectory $shaderRoot -LogPrefix (Join-Path $shaderRoot 'tool') -TimeoutMs $TimeoutMs
    $output=Join-Path $shaderRoot 'ShaderCache\0123456789abcdef-ps.cbo'
    if($shaderResult.status -eq 'PASS' -and (-not (Test-Path -LiteralPath $output) -or (Get-FileHash -LiteralPath $output).Hash -ne (Get-FileHash -LiteralPath (Join-Path $shaderRoot 'expected.cbo')).Hash)) {
        $shaderResult.status='FAIL';$shaderResult.message='CLI did not produce the complete native-validated bytecode.'
    }
}
$checks.Add([pscustomobject]@{name='offline-shader-tool';status=$shaderResult.status;detail=$shaderResult})
Write-Host "[$($shaderResult.status)] $label offline-shader-tool"
$legacyRoot=Join-Path $runRoot 'legacy-version'
New-Item -ItemType Directory -Path $legacyRoot | Out-Null
$legacySource=Join-Path $candidateBuild 'src\DirectX11\Release\dx11_legacy_version_regression.exe'
Copy-Item -LiteralPath $legacySource -Destination $legacyRoot
$legacyResult=Invoke-IsolatedSimulationProcess -Executable (Join-Path $legacyRoot 'dx11_legacy_version_regression.exe') -Arguments @('--full') -WorkingDirectory $legacyRoot -LogPrefix (Join-Path $legacyRoot 'host') -TimeoutMs $TimeoutMs
$checks.Add([pscustomobject]@{name='legacy-version';status=$legacyResult.status;detail=$legacyResult})
Write-Host "[$($legacyResult.status)] $label legacy-version"
$versionRoot=Join-Path $runRoot 'version-source'
New-Item -ItemType Directory -Path (Join-Path $versionRoot 'cmake') | Out-Null
$generatorSource=Join-Path $repositoryRoot 'cmake\GenerateDX11Version.cmake'
$generatorCopy=Join-Path $versionRoot 'cmake\GenerateDX11Version.cmake'
Copy-Item -LiteralPath $generatorSource -Destination $generatorCopy
if((Get-FileHash -LiteralPath $generatorSource).Hash -ne (Get-FileHash -LiteralPath $generatorCopy).Hash){throw 'Version generator snapshot differs.'}
$env:Path=(Split-Path -Parent $info.toolchain.cmake)+';'+$env:Path
$versionResult=Invoke-IsolatedSimulationProcess -Executable $info.python -Arguments @((Join-Path $repositoryRoot 'scripts\test-dx11-version.py'),'--source-root',$versionRoot,'--work-dir',(Join-Path $versionRoot 'build')) -WorkingDirectory $runRoot -LogPrefix (Join-Path $runRoot 'version-generator') -TimeoutMs $TimeoutMs
$checks.Add([pscustomobject]@{name='version-generator';status=$versionResult.status;detail=$versionResult})
Write-Host "[$($versionResult.status)] $label version-generator"
$pointer=Join-Path $runRoot 'integration-report-path.txt'
$shell=(Get-Process -Id $PID).Path
$integrationResult=Invoke-IsolatedSimulationProcess -Executable $shell -Arguments @('-NoProfile','-File',(Join-Path $PSScriptRoot 'run-tests.ps1'),'-CandidateDll',$dll,'-ReportPointer',$pointer) -WorkingDirectory $runRoot -LogPrefix (Join-Path $runRoot 'integration') -TimeoutMs $TimeoutMs
$checks.Add([pscustomobject]@{name='actual-dll-integration';status=$integrationResult.status;detail=$integrationResult})
Write-Host "[$($integrationResult.status)] $label actual-dll-integration"
$integration=$null
if(Test-Path -LiteralPath $pointer) {
    $integrationPath=[IO.File]::ReadAllText($pointer,[Text.Encoding]::UTF8)
    $integration=Get-Content -LiteralPath $integrationPath -Raw | ConvertFrom-Json
}
$status=if(@($checks | Where-Object status -ne 'PASS').Count -eq 0 -and $integration -and @($integration.cases | Where-Object status -ne 'PASS').Count -eq 0){'PASS'}else{'FAIL'}
$report=[ordered]@{item=$label;status=$status;utc=[DateTime]::UtcNow.ToString('o');buildHead=$info.head;candidateSha256=(Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash;checks=$checks.ToArray();actualDllIntegration=$integration;businessSurface='Entire existing production-source DX11 regression suite with real WARP resources; distinct from actual-DLL integration';targetGameValidated=$false}
$reportPath=Join-Path $runRoot 'results.json'
$report | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $reportPath -Encoding utf8
Write-Host "Full report: $reportPath"
if($status -ne 'PASS'){exit 1}
