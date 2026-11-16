[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CandidateBuild,
    [Parameter(Mandatory)][string]$BaselineBuild,
    [string]$ReportPointer = ''
)
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess -or $PSVersionTable.PSVersion.Major -lt 7) { throw 'Use x64 PowerShell 7.' }
. (Join-Path $PSScriptRoot 'process.ps1')
$simulationRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $simulationRoot 'build'
$hostExe = Join-Path $buildRoot 'tools\dxgi_proxy_fault_host.exe'
if (-not (Test-Path -LiteralPath $hostExe)) { throw "Build the test host first: $hostExe" }
$hostHash = (Get-FileHash -LiteralPath $hostExe -Algorithm SHA256).Hash
$hostMtime = (Get-Item -LiteralPath $hostExe).LastWriteTimeUtc.ToString('o')

function Resolve-DxgiBuild([string]$build, [string]$label) {
    $root = (Resolve-Path -LiteralPath $build).Path
    $dll = Join-Path $root 'src\DirectXGI\dxgi.dll'
    if (-not (Test-Path -LiteralPath $dll)) { throw "$label dxgi.dll not found: $dll" }
    return $dll
}

$candidateDll = Resolve-DxgiBuild $CandidateBuild 'candidate'
$baselineDll = Resolve-DxgiBuild $BaselineBuild 'baseline'
$candidateHash = (Get-FileHash -LiteralPath $candidateDll -Algorithm SHA256).Hash
$baselineHash = (Get-FileHash -LiteralPath $baselineDll -Algorithm SHA256).Hash
if ($candidateHash -eq $baselineHash) {
    throw 'Baseline and candidate dxgi.dll are identical; pre-fix red evidence needs a different build.'
}
$candidateMtime = (Get-Item -LiteralPath $candidateDll).LastWriteTimeUtc.ToString('o')
$baselineMtime = (Get-Item -LiteralPath $baselineDll).LastWriteTimeUtc.ToString('o')

$runRoot = Join-Path $buildRoot ('f\' + [guid]::NewGuid().ToString('N').Substring(0, 12))
New-Item -ItemType Directory -Path $runRoot | Out-Null
$cases = @(
    [pscustomobject]@{id = 'T-0'; mode = 'control'; marker = 'PASS: T-0 normal forwarding'; description = 'control: normal forwarding'},
    [pscustomobject]@{id = 'T-1'; mode = 'loadfail'; marker = 'PASS: T-1 load failure latched'; description = 'LoadLibrary failure is latched'},
    [pscustomobject]@{id = 'T-1b'; mode = 'missing-export'; marker = 'PASS: T-1b missing CreateDXGIFactory1'; description = 'missing single export fails cleanly'},
    [pscustomobject]@{id = 'T-2'; mode = 'race'; marker = 'PASS: T-2 first-initialization race'; description = 'first-init race waits for completion'},
    [pscustomobject]@{id = 'T-3'; mode = 'self'; marker = 'PASS: T-3 self-load refused'; description = 'self-load refused without recursion'}
)
$baselineExpectations = @{
    'T-0' = [pscustomobject]@{kind = 'GREEN'; exitCodes = @(0); allowTimeout = $false}
    'T-1' = [pscustomobject]@{kind = 'RED'; exitCodes = @(-1073741819); allowTimeout = $false}
    'T-1b' = [pscustomobject]@{kind = 'RED'; exitCodes = @(-1073741819); allowTimeout = $false}
    'T-2' = [pscustomobject]@{kind = 'RED'; exitCodes = @(-1073741819); allowTimeout = $false}
    'T-3' = [pscustomobject]@{kind = 'RED'; exitCodes = @(-1073741571); allowTimeout = $true}
}
$results = [Collections.Generic.List[object]]::new()
$provenanceLines = [Collections.Generic.List[string]]::new()
foreach ($run in @(
    [pscustomobject]@{runtime = 'candidate'; dll = $candidateDll; sha = $candidateHash; mtime = $candidateMtime},
    [pscustomobject]@{runtime = 'baseline'; dll = $baselineDll; sha = $baselineHash; mtime = $baselineMtime}
)) {
    foreach ($case in $cases) {
        $id = "$($run.runtime)-$($case.id)"
        $caseRoot = Join-Path $runRoot $id
        New-Item -ItemType Directory -Path $caseRoot | Out-Null
        $caseHost = Join-Path $caseRoot 'dxgi_proxy_fault_host.exe'
        $caseDll = Join-Path $caseRoot 'dxgi.dll'
        Copy-Item -LiteralPath $hostExe -Destination $caseHost
        Copy-Item -LiteralPath $run.dll -Destination $caseDll
        $caseHostHash = (Get-FileHash -LiteralPath $caseHost -Algorithm SHA256).Hash
        if ($caseHostHash -ne $hostHash) { throw "Harness changed during copying: $id" }
        $caseDllHash = (Get-FileHash -LiteralPath $caseDll -Algorithm SHA256).Hash
        if ($caseDllHash -ne $run.sha) { throw "Input DLL changed during copying: $id" }
        "[Logging]`ncalls=1`nunbuffered=1`ndebug=0`nforce_cpu_affinity=0`n" |
            Set-Content -LiteralPath (Join-Path $caseRoot 'd3dx.ini') -Encoding utf8
        $result = Invoke-IsolatedSimulationProcess -Executable $caseHost -Arguments @($caseDll, $case.mode, $run.sha) -WorkingDirectory $caseRoot -LogPrefix (Join-Path $caseRoot 'host') -TimeoutMs 20000
        $stdoutPath = Join-Path $caseRoot 'host.stdout.txt'
        $stdout = if (Test-Path -LiteralPath $stdoutPath) { Get-Content -LiteralPath $stdoutPath -Raw } else { '' }
        $gpaNames = @([regex]::Matches([string]$stdout, 'GPA\[\d+\] name=([^\r\n]+)') | ForEach-Object { $_.Groups[1].Value.Trim() })
        $expected = if ($run.runtime -eq 'candidate') {
            [pscustomobject]@{kind = 'GREEN'; exitCodes = @(0); allowTimeout = $false}
        } else {
            $baselineExpectations[$case.id]
        }
        $ok = $false
        $detail = ''
        if ($expected.kind -eq 'GREEN') {
            $ok = ($result.status -eq 'PASS' -and ([string]$stdout) -match [regex]::Escape($case.marker))
            if (-not $ok) {
                $detail = "expected GREEN marker '$($case.marker)'; process=$($result.status) exit=$($result.exitCode)"
            }
        } else {
            if ($result.status -eq 'TIMEOUT') {
                $ok = [bool]$expected.allowTimeout
            } else {
                $ok = ($result.status -ne 'PASS') -and ($expected.exitCodes -contains $result.exitCode)
            }
            if (-not $ok) {
                $detail = "expected RED exit in [$($expected.exitCodes -join ',')]; process=$($result.status) exit=$($result.exitCode)"
            }
        }
        if ($case.id -eq 'T-2' -and $ok) {
            $targetIndex = [Array]::IndexOf([string[]]$gpaNames, 'CreateDXGIFactory')
            if ($targetIndex -lt 1) {
                $ok = $false
                $detail = 'T-2 must log at least one GPA name before CreateDXGIFactory'
            }
        }
        $provenance = [ordered]@{
            runtime = $run.runtime
            case = $case.id
            mode = $case.mode
            expected = $expected.kind
            status = $(if ($ok) { 'PASS' } else { 'FAIL' })
            processStatus = $result.status
            exitCode = $result.exitCode
            detail = $detail
            harnessSource = $hostExe
            harnessSourceSha256 = $hostHash
            harnessSourceMtimeUtc = $hostMtime
            harnessExe = $caseHost
            harnessExeSha256 = $caseHostHash
            harnessExeMtimeUtc = (Get-Item -LiteralPath $caseHost).LastWriteTimeUtc.ToString('o')
            testedDll = $caseDll
            testedDllSha256 = $caseDllHash
            testedDllMtimeUtc = (Get-Item -LiteralPath $caseDll).LastWriteTimeUtc.ToString('o')
            gpaNames = @($gpaNames)
            stdoutLog = $stdoutPath
            stderrLog = (Join-Path $caseRoot 'host.stderr.txt')
        }
        $provenance | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $caseRoot 'provenance.json') -Encoding utf8
        $provenanceLines.Add("[$($provenance.status)] $id expected=$($expected.kind) process=$($result.status) exit=$($result.exitCode) harness=$caseHost sha256=$caseHostHash mtime=$($provenance.harnessExeMtimeUtc) dll=$caseDll sha256=$caseDllHash mtime=$($provenance.testedDllMtimeUtc) gpa=$($gpaNames -join ',')")
        $results.Add([pscustomobject]$provenance)
        Write-Host "[$($provenance.status)] $id expected=$($expected.kind) exit=$($result.exitCode)"
    }
}
$status = if (@($results | Where-Object status -ne 'PASS').Count) { 'FAIL' } else { 'PASS' }
$provenancePath = Join-Path $runRoot 'provenance.log'
$provenanceLines | Set-Content -LiteralPath $provenancePath -Encoding utf8
$report = [ordered]@{
    item = 'F-271'
    status = $status
    harness = [ordered]@{source = $hostExe; sha256 = $hostHash; mtimeUtc = $hostMtime}
    candidate = [ordered]@{dll = $candidateDll; sha256 = $candidateHash; mtimeUtc = $candidateMtime}
    baseline = [ordered]@{dll = $baselineDll; sha256 = $baselineHash; mtimeUtc = $baselineMtime}
    provenanceLog = $provenancePath
    cases = $results.ToArray()
}
$reportPath = Join-Path $runRoot 'results.json'
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding utf8
if ($ReportPointer) { [IO.File]::WriteAllText($ReportPointer, $reportPath, [Text.UTF8Encoding]::new($false)) }
Write-Host "DXGI fault report: $reportPath"
if ($status -ne 'PASS') { exit 1 }
