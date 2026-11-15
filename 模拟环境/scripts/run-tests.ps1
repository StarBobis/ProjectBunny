[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CandidateDll,
    [string]$BaselineDll = '',
    [string]$ReportPointer = '',
    [string]$SpecialItem = '',
    [ValidateRange(100, 60000)][int]$TimeoutMs = 20000
)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'PowerShell 7 or newer is required.' }
if($SpecialItem -and $SpecialItem -ne 'F-178'){throw 'Unknown actual-DLL specialization.'}
. (Join-Path $PSScriptRoot 'process.ps1')
$simulationRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = Split-Path -Parent $simulationRoot
$hostPath = Join-Path $simulationRoot 'build\tools\dx11_sim_host.exe'
$smokePath = Join-Path $simulationRoot 'build\tools\dx11_existing_smoke.exe'
$faultProviderPath = Join-Path $simulationRoot 'build\tools\dx11_fault_provider.dll'
foreach ($required in @($hostPath, $smokePath, $faultProviderPath, $CandidateDll)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing file: $required" }
}
$buildInfoPath = Join-Path $simulationRoot 'build\host-build-info.json'
if (-not (Test-Path -LiteralPath $buildInfoPath)) { throw 'Run scripts/build.ps1 to record the host build first.' }
$hostBuildInfo = Get-Content -LiteralPath $buildInfoPath -Raw | ConvertFrom-Json
foreach ($source in $hostBuildInfo.sources) {
    if ((Get-FileHash -LiteralPath (Join-Path $repositoryRoot $source.path) -Algorithm SHA256).Hash -ne $source.sha256) {
        throw "Host source changed; run scripts/build.ps1 first: $($source.path)"
    }
}
foreach ($hostRecord in $hostBuildInfo.hosts) {
    if ((Get-FileHash -LiteralPath (Join-Path $simulationRoot "build\tools\$($hostRecord.name)") -Algorithm SHA256).Hash -ne $hostRecord.sha256) {
        throw "Host executable changed; rebuild it: $($hostRecord.name)"
    }
}
$CandidateDll = (Resolve-Path -LiteralPath $CandidateDll).Path
if ($BaselineDll) { $BaselineDll = (Resolve-Path -LiteralPath $BaselineDll).Path }
if (-not [Environment]::Is64BitProcess) { throw 'Use x64 PowerShell 7.' }
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + $PID + '-' + [guid]::NewGuid().ToString('N').Substring(0,8)
$runRoot = Join-Path $simulationRoot "build\runs\$runId"
New-Item -ItemType Directory -Path $runRoot | Out-Null
$results = [Collections.Generic.List[object]]::new()
$artifacts = [Collections.Generic.List[object]]::new()
$scenarios = @('capabilities','buffer','texture','deferred','swapchain','swapchain-qi')
if($SpecialItem){$scenarios=@('swapchain-qi')}
$nativePassed = @{}
$nativeDll = Join-Path ([Environment]::SystemDirectory) 'd3d11.dll'
foreach ($scenario in $scenarios) {
    $caseRoot = Join-Path $runRoot "native-$scenario"
    New-Item -ItemType Directory -Path $caseRoot | Out-Null
    $result = Invoke-IsolatedSimulationProcess -Executable $hostPath -Arguments @($nativeDll,$scenario) -WorkingDirectory $caseRoot -LogPrefix (Join-Path $caseRoot 'host') -TimeoutMs $TimeoutMs
    $results.Add([pscustomobject]@{id="native-$scenario"; runtime='System32'; scenario=$scenario; status=$result.status; detail=$result})
    $nativePassed[$scenario] = $result.status -eq 'PASS'
    Write-Host "[$($result.status)] native-$scenario"
}
$inputs = @([pscustomobject]@{label='candidate';path=$CandidateDll})
if ($BaselineDll) { $inputs = @([pscustomobject]@{label='baseline';path=$BaselineDll}) + $inputs }
foreach ($inputDll in $inputs) {
    $sourceHash = (Get-FileHash -LiteralPath $inputDll.path -Algorithm SHA256).Hash
    $artifacts.Add([pscustomobject]@{label=$inputDll.label;path=$inputDll.path;sha256=$sourceHash;fileVersion=[Diagnostics.FileVersionInfo]::GetVersionInfo($inputDll.path).FileVersion})
    $cases = @($scenarios | ForEach-Object { [pscustomobject]@{name=$_;scenario=$_;exe=$hostPath;mode=$_;config='wrapper.ini'} })
    $cases += @(
        [pscustomobject]@{name='constants-missing-name';scenario='capabilities';exe=$hostPath;mode='constants-missing-name';config='constants-missing-name.ini'},
        [pscustomobject]@{name='injected-device-failure';scenario='capabilities';exe=$hostPath;mode='injected-device-failure';config='device-failure.ini'},
        [pscustomobject]@{name='smoke-wrapper';scenario='capabilities';exe=$smokePath;mode='--child';config='wrapper.ini'},
        [pscustomobject]@{name='smoke-native-control';scenario='capabilities';exe=$smokePath;mode='--child-native-control';config='wrapper.ini'},
        [pscustomobject]@{name='smoke-hook-all';scenario='capabilities';exe=$smokePath;mode='--child-hook-all';config='hook-all.ini'},
        [pscustomobject]@{name='smoke-denied-qi';scenario='capabilities';exe=$smokePath;mode='--child-denied-qi';config='deny-interfaces.ini'}
    )
    if($SpecialItem){$cases=@($cases | Where-Object name -eq 'swapchain-qi')}
    foreach ($case in $cases) {
        $caseId = "$($inputDll.label)-$($case.name)"
        if (-not $nativePassed[$case.scenario]) {
            $results.Add([pscustomobject]@{id=$caseId;runtime=$inputDll.label;scenario=$case.name;status='NOT_RUN';detail='Native prerequisite failed; investigate the environment first.'})
            Write-Host "[NOT_RUN] $caseId"
            continue
        }
        $caseRoot = Join-Path $runRoot $caseId
        New-Item -ItemType Directory -Path $caseRoot | Out-Null
        $copiedDll = Join-Path $caseRoot 'd3d11.dll'
        Copy-Item -LiteralPath $inputDll.path -Destination $copiedDll
        if ((Get-FileHash -LiteralPath $copiedDll -Algorithm SHA256).Hash -ne $sourceHash) { throw 'DLL changed while copying; rebuild must finish before testing.' }
        $configSource = Join-Path $simulationRoot "fixtures\$($case.config)"
        Copy-Item -LiteralPath $configSource -Destination (Join-Path $caseRoot 'd3dx.ini')
        if ($case.name -eq 'injected-device-failure') {
            Copy-Item -LiteralPath $faultProviderPath -Destination (Join-Path $caseRoot 'dx11_fault_provider.dll')
        }
        # DllMain accepts the ordinary game layout (EXE beside DLL). A host in
        # build/tools with DLLs in sibling build/runs is intentionally rejected
        # unless Loader/target is configured. Use the ordinary layout per case.
        $caseExecutable = Join-Path $caseRoot ([IO.Path]::GetFileName($case.exe))
        Copy-Item -LiteralPath $case.exe -Destination $caseExecutable
        $arguments = if ($case.exe -eq $hostPath) { @($copiedDll,$case.mode) } else { @($case.mode,$copiedDll) }
        $result = Invoke-IsolatedSimulationProcess -Executable $caseExecutable -Arguments $arguments -WorkingDirectory $caseRoot -LogPrefix (Join-Path $caseRoot 'host') -TimeoutMs $TimeoutMs
        $afterHash = (Get-FileHash -LiteralPath $copiedDll -Algorithm SHA256).Hash
        if ($afterHash -ne $sourceHash) { $result.status='HARNESS_ERROR'; $result.message='Copied DLL changed during testing.' }
        $results.Add([pscustomobject]@{id=$caseId;runtime=$inputDll.label;scenario=$case.name;status=$result.status;loadedInput=$copiedDll;sha256=$sourceHash;executable=$caseExecutable;executableSha256=(Get-FileHash -LiteralPath $caseExecutable -Algorithm SHA256).Hash;configurationSha256=(Get-FileHash -LiteralPath (Join-Path $caseRoot 'd3dx.ini') -Algorithm SHA256).Hash;detail=$result})
        Write-Host "[$($result.status)] $caseId"
    }
}
$head = & git -C $repositoryRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { $head = 'unavailable' }
$scope = 'Actual x64 DX11 DLL + WARP integration smoke only. No repair-item, real-game, hardware-driver or performance certification.'
if($SpecialItem){$scope='F-178 actual proxy/native WARP SwapChain4 query and relative reference ownership scenario.'}
$report = [ordered]@{
    schemaVersion=1; runId=$runId; utc=[DateTime]::UtcNow.ToString('o'); repositoryHead=$head
    os=[Environment]::OSVersion.VersionString; processArchitecture='x64'; driver='WARP'
    scope=$scope; inputs=$artifacts.ToArray(); nativeDll=$nativeDll
    nativeDllSha256=(Get-FileHash -LiteralPath $nativeDll -Algorithm SHA256).Hash
    hostSha256=(Get-FileHash -LiteralPath $hostPath -Algorithm SHA256).Hash
    existingSmokeSha256=(Get-FileHash -LiteralPath $smokePath -Algorithm SHA256).Hash
    faultProviderSha256=(Get-FileHash -LiteralPath $faultProviderPath -Algorithm SHA256).Hash
    hostBuildInfo=$hostBuildInfo
    runnerSha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
    processControllerSha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'process.ps1') -Algorithm SHA256).Hash
    existingSmokeSourceSha256=(Get-FileHash -LiteralPath (Join-Path $repositoryRoot 'src\DirectX11\tests\proxy_smoke.cpp') -Algorithm SHA256).Hash
    cases=$results.ToArray()
    frozen=@('real game/driver confirmation','DLL unload and loader-lock stress','concurrent hooks and ownership stress','resource/shader/Map failure injection','high UAV slots and CB-window conformance','performance conclusions','DXGI and compiler proxy DLL integration')
}
$reportPath = Join-Path $runRoot 'results.json'
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $reportPath -Encoding utf8
$lines = @('# DX11 DLL 模拟环境测试结果','',"- Run: $runId", "- HEAD: $head", "- 范围：真实 DLL + WARP 基础集成冒烟；不代表修复条目或实机验证通过。",'', '| Case | Status |','|---|---|')
$lines += @($results | ForEach-Object { "| $($_.id) | $($_.status) |" })
$lines += @('', '详细路径、哈希、退出码、耗时和能力记录见同目录 results.json / 各用例日志。', '未支持的能力必须单独补测；本首版没有执行冻结项目，也没有进行性能测量。')
$lines | Set-Content -LiteralPath (Join-Path $runRoot '结果.md') -Encoding utf8
Write-Host "Report: $reportPath"
if ($ReportPointer) {
    $pointerFile = [IO.File]::Open($ReportPointer,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try {
        $pointerBytes=[Text.Encoding]::UTF8.GetBytes($reportPath)
        $pointerFile.Write($pointerBytes,0,$pointerBytes.Length)
    } finally { $pointerFile.Dispose() }
}
if (@($results | Where-Object status -ne 'PASS').Count -gt 0) { exit 1 }
if($SpecialItem){Write-Host "PASS audit item: $SpecialItem"}
