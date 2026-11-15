[CmdletBinding()]
param([string]$Item='',[switch]$SpecialOnly,[string]$ReportPointer='')
$ErrorActionPreference='Stop'
if(-not [Environment]::Is64BitProcess -or $PSVersionTable.PSVersion.Major -lt 7){throw 'Use x64 PowerShell 7.'}
if($Item -and $Item -ne 'F-152'){throw 'Unknown component specialization.'}
. (Join-Path $PSScriptRoot 'process.ps1')
$simulationRoot=Split-Path -Parent $PSScriptRoot
$buildRoot=Join-Path $simulationRoot 'build'
$candidateBuild=Join-Path $buildRoot 'candidate'
$componentHost=Join-Path $buildRoot 'tools\dx11_proxy_component_host.exe'
$nativeCompiler=Join-Path ([Environment]::SystemDirectory) 'd3dcompiler_47.dll'
$nativeDxgi=Join-Path ([Environment]::SystemDirectory) 'dxgi.dll'
$providerHash=(Get-FileHash -LiteralPath $nativeCompiler).Hash
$runRoot=Join-Path $buildRoot ('c\'+[guid]::NewGuid().ToString('N').Substring(0,12))
New-Item -ItemType Directory -Path $runRoot | Out-Null
$results=[Collections.Generic.List[object]]::new()
$nativePassed=@{}
foreach($kind in @('compiler','dxgi')) {
    $caseRoot=Join-Path $runRoot "native-$kind"
    New-Item -ItemType Directory -Path $caseRoot | Out-Null
    $caseExe=Join-Path $caseRoot 'component-host.exe'
    Copy-Item -LiteralPath $componentHost -Destination $caseExe
    $library=if($kind -eq 'compiler'){$nativeCompiler}else{$nativeDxgi}
    $result=Invoke-IsolatedSimulationProcess -Executable $caseExe -Arguments @($library,$kind) -WorkingDirectory $caseRoot -LogPrefix (Join-Path $caseRoot 'host')
    $nativePassed[$kind]=$result.status -eq 'PASS'
    $results.Add([pscustomobject]@{id="native-$kind";status=$result.status;input=$library;sha256=(Get-FileHash -LiteralPath $library).Hash;detail=$result})
    Write-Host "[$($result.status)] native-$kind"
}
foreach($kind in @('compiler','dxgi')) {
    $inputDll=if($kind -eq 'compiler'){Join-Path $candidateBuild 'src\D3DCompiler_46\d3dcompiler_47.dll'}else{Join-Path $candidateBuild 'src\DirectXGI\dxgi.dll'}
    $inputHash=(Get-FileHash -LiteralPath $inputDll).Hash
    foreach($mode in @('normal','disabled','blocked')) {
        $id="$kind-$mode"
        if(-not $nativePassed[$kind]){$results.Add([pscustomobject]@{id=$id;status='NOT_RUN';detail='Native prerequisite failed.'});continue}
        $caseRoot=Join-Path $runRoot $id
        New-Item -ItemType Directory -Path $caseRoot | Out-Null
        $caseExe=Join-Path $caseRoot 'component-host.exe'
        Copy-Item -LiteralPath $componentHost -Destination $caseExe
        $copiedDll=Join-Path $caseRoot ([IO.Path]::GetFileName($inputDll))
        Copy-Item -LiteralPath $inputDll -Destination $copiedDll
        if((Get-FileHash -LiteralPath $copiedDll).Hash -ne $inputHash){throw 'Component DLL changed during copying.'}
        if($kind -eq 'compiler') {
            $provider=Join-Path $caseRoot 'D3DCompiler_46_org.dll'
            Copy-Item -LiteralPath $nativeCompiler -Destination $provider
            if((Get-FileHash -LiteralPath $provider).Hash -ne $providerHash){throw 'Compatible native provider differs from system source.'}
        }
        $calls=if($mode -eq 'disabled'){0}else{1}
        "[Logging]`ncalls=$calls`nunbuffered=1`ndebug=0`nforce_cpu_affinity=0`n" |
            Set-Content -LiteralPath (Join-Path $caseRoot 'd3dx.ini') -Encoding utf8
        $logName=if($kind -eq 'compiler'){'D3DCompiler_46_log.txt'}else{'dxgi_log.txt'}
        $logPath=Join-Path $caseRoot $logName
        if($mode -eq 'blocked'){New-Item -ItemType Directory -Path $logPath | Out-Null}
        $result=Invoke-IsolatedSimulationProcess -Executable $caseExe -Arguments @($copiedDll,$kind) -WorkingDirectory $caseRoot -LogPrefix (Join-Path $caseRoot 'host')
        if($result.status -eq 'PASS' -and $mode -eq 'blocked' -and -not (Test-Path -LiteralPath $logPath -PathType Container)) {
            $result.status='FAIL';$result.message='Log-open failure fixture was modified.'
        }
        if((Get-FileHash -LiteralPath $copiedDll).Hash -ne $inputHash){$result.status='HARNESS_ERROR';$result.message='DLL changed during testing.'}
        $results.Add([pscustomobject]@{id=$id;status=$result.status;input=$copiedDll;sha256=$inputHash;loggingCalls=$calls;unbuffered=1;detail=$result})
        Write-Host "[$($result.status)] $id"
    }
}
$status=if(@($results | Where-Object status -ne 'PASS').Count){'FAIL'}else{'PASS'}
$report=[ordered]@{item=$Item;status=$status;cases=$results.ToArray();nativeCompilerProvider=$nativeCompiler;providerSha256=$providerHash;providerMode='System D3DCompiler_47 copied under the declared legacy _46_org dependency name; public API compatibility coverage, not original 46/private export certification';scope='Actual DXGI/Compiler DLL public normal/failure business with real native providers and controlled logging modes';bridgeProtocolTested=$false}
$reportPath=Join-Path $runRoot 'results.json'
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding utf8
if($ReportPointer){[IO.File]::WriteAllText($ReportPointer,$reportPath,[Text.UTF8Encoding]::new($false))}
Write-Host "Component report: $reportPath"
if($status -ne 'PASS'){exit 1}
if($SpecialOnly){Write-Host "PASS audit item: $Item"}
