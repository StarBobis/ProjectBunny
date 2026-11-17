[CmdletBinding()]
param([Parameter(Mandatory)][string]$BaselineDirectory,[Parameter(Mandatory)][string]$CandidateDirectory,
    [ValidateRange(3,15)][int]$Pairs=9,[string]$ReportPointer='')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'process.ps1')
$simulationRoot=Split-Path -Parent $PSScriptRoot
$runRoot=Join-Path $simulationRoot ('build\perf\p01-'+[guid]::NewGuid().ToString('N').Substring(0,10))
$null=New-Item -ItemType Directory -Path $runRoot
$snapshots=@{}; $manifests=@{}
foreach($role in @('baseline','candidate')) {
    $directory=if($role -eq 'baseline'){$BaselineDirectory}else{$CandidateDirectory}
    $snapshot=Get-Content -LiteralPath (Join-Path $directory 'snapshot.json') -Raw | ConvertFrom-Json
    if((Get-FileHash -LiteralPath (Join-Path $directory 'build-info.json')).Hash -ne $snapshot.buildInfoSha256){throw 'Changed snapshot manifest.'}
    foreach($file in $snapshot.files){if((Get-FileHash -LiteralPath (Join-Path $directory $file.name)).Hash -ne $file.sha256){throw 'Changed frozen binary.'}}
    $snapshots[$role]=[pscustomobject]@{directory=(Resolve-Path -LiteralPath $directory).Path;info=$snapshot}
    $manifests[$role]=Get-Content -LiteralPath (Join-Path $directory 'build-info.json') -Raw | ConvertFrom-Json
}
foreach($field in @('configuration','standard')){if($manifests.baseline.$field -ne $manifests.candidate.$field){throw 'Incomparable build modes.'}}
if($manifests.baseline.addressSanitizer -ne $false -or $manifests.candidate.addressSanitizer -ne $false){throw 'ASan allocator overhead is not a production performance baseline.'}
if($manifests.baseline.toolchain.compiler -ne $manifests.candidate.toolchain.compiler){throw 'Different compilers.'}
$different=@(foreach($source in $manifests.baseline.sources){
    $other=@($manifests.candidate.sources | Where-Object path -eq $source.path)
    if($other.Count -ne 1 -or $other[0].sha256 -ne $source.sha256){$source.path}
})
if($manifests.baseline.sources.Count -ne $manifests.candidate.sources.Count -or @($different | Where-Object {$_ -ne 'src\DirectX11\IniHandler.cpp'}).Count){throw 'Comparison changes more than the production INI implementation.'}
$trials=[Collections.Generic.List[object]]::new()
$digest=$null; $checksums=@{}
for($pair=0;$pair -lt $Pairs;$pair++) {
    $order=if($pair%2){@('candidate','baseline')}else{@('baseline','candidate')}
    foreach($role in $order) {
        $caseRoot=Join-Path $runRoot "$pair-$role"
        $null=New-Item -ItemType Directory -Path $caseRoot
        foreach($file in $snapshots[$role].info.files){
            Copy-Item -LiteralPath (Join-Path $snapshots[$role].directory $file.name) -Destination $caseRoot
            if((Get-FileHash -LiteralPath (Join-Path $caseRoot $file.name)).Hash -ne $file.sha256){throw 'Benchmark binary changed during staging.'}
        }
        $process=Invoke-IsolatedSimulationProcess -Executable (Join-Path $caseRoot 'dx11_upstream_regression.exe') -Arguments @('--audit-item','P-01','--fixture-isolated') -WorkingDirectory $caseRoot -LogPrefix (Join-Path $caseRoot 'host') -TimeoutMs 60000 -EnvironmentOverrides @{BUNNY_INI_LOOKUP_BENCHMARK='1';ASAN_OPTIONS='halt_on_error=1:alloc_dealloc_mismatch=1:new_delete_type_mismatch=1'}
        if($process.status -ne 'PASS'){throw "Benchmark process failed: $caseRoot"}
        $lines=Get-Content -LiteralPath (Join-Path $caseRoot 'host.stdout.txt')
        if('PASS audit item: P-01' -notin $lines){throw 'Correctness checks did not complete.'}
        if('P01_RUNTIME_SANITIZER=none' -notin $lines){throw 'Plain runtime state was not confirmed.'}
        $behavior=@($lines | Where-Object {$_ -like 'P01_BEHAVIOR_DIGEST=*'})
        if($behavior.Count -ne 1){throw 'Missing diagnostic digest.'}
        if($null -eq $digest){$digest=$behavior[0]}elseif($digest -ne $behavior[0]){throw 'Actual diagnostics differ between runs.'}
        $measurements=@($lines | Where-Object {$_ -like 'P01_BENCHMARK_JSON=*'} | ForEach-Object {$_.Substring(19) | ConvertFrom-Json})
        if($measurements.Count -ne 3){throw 'Incomplete measurements.'}
        foreach($measurement in $measurements){
            if($measurement.iterations -ne 100000 -or $measurement.nsPerCall -le 0){throw 'Invalid measurement workload or clock.'}
            if(-not $checksums.ContainsKey($measurement.name)){$checksums[$measurement.name]=$measurement.checksum}
            elseif($checksums[$measurement.name] -ne $measurement.checksum){throw 'Benchmark results differ.'}
        }
        $trials.Add([pscustomobject]@{pair=$pair;role=$role;process=$process;measurements=$measurements})
    }
    Write-Host "P01 measurement pair $($pair+1)/$Pairs passed"
}
$summary=@(foreach($name in $checksums.Keys){
    $values=@{}
    foreach($role in @('baseline','candidate')){
        $samples=@($trials | Where-Object role -eq $role | ForEach-Object measurements | Where-Object name -eq $name | ForEach-Object nsPerCall | Sort-Object)
        $middle=[int][Math]::Floor($samples.Count/2)
        $median=if($samples.Count%2){$samples[$middle]}else{($samples[$middle-1]+$samples[$middle])/2}
        $values[$role]=[pscustomobject]@{samplesNs=$samples;medianNs=$median;minNs=$samples[0];maxNs=$samples[-1]}
    }
    [pscustomobject]@{name=$name;baseline=$values.baseline;candidate=$values.candidate;medianReductionPercent=100*($values.baseline.medianNs-$values.candidate.medianNs)/$values.baseline.medianNs}
})
$report=[ordered]@{item='P-01';status='MEASURED';scope='Same-host plain Release/C++17 production getter microbenchmarks, no ASan allocator; synthetic missing-heavy and numeric-dense options. Not whole reload, game FPS or statistical certification.';pairs=$Pairs;order='Alternating AB/BA';baselineBuildInfoSha256=$snapshots.baseline.info.buildInfoSha256;candidateBuildInfoSha256=$snapshots.candidate.info.buildInfoSha256;candidateBusinessBuildInfoSha256=$manifests.candidate.basisBusinessManifestSha256;baselineSnapshot=$snapshots.baseline;candidateSnapshot=$snapshots.candidate;sourceDifferences=$different;diagnosticDigest=$digest;summary=$summary;trials=$trials.ToArray()}
$reportPath=Join-Path $runRoot 'results.json'
$report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $reportPath -Encoding utf8
if($ReportPointer){[IO.File]::WriteAllText($ReportPointer,$reportPath)}
$summary | Select-Object name,medianReductionPercent | Format-Table
Write-Host "Measurement report: $reportPath"
