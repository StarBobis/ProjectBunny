[CmdletBinding()]
param([Parameter(Mandatory)][string]$Item,[Parameter(Mandatory)][string]$FullReport,[string]$RedReport='',[Parameter(Mandatory)][string]$Summary)
$ErrorActionPreference='Stop'
$simulationRoot=Split-Path -Parent $PSScriptRoot
$repositoryRoot=Split-Path -Parent $simulationRoot
$plan=Get-Content -LiteralPath (Join-Path $simulationRoot 'repair-plan.json') -Raw | ConvertFrom-Json
$entry=@($plan.rows | Where-Object id -eq $Item)
if($entry.Count -ne 1 -or $entry[0].group -notin @('A','B','C')){throw 'Item is frozen, out of scope, or unknown.'}
$reportPath=(Resolve-Path -LiteralPath $FullReport).Path
$report=Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
$required=@('special','business','legacy-version','version-generator','actual-dll-integration')
if($report.item -ne $Item -or $report.status -ne 'PASS' -or @($report.checks | Where-Object status -ne 'PASS').Count){throw 'Requested item did not pass its entire validation cycle.'}
foreach($name in $required){if(@($report.checks | Where-Object name -eq $name).Count -ne 1){throw "Missing validation: $name"}}
if(-not $report.actualDllIntegration -or @($report.actualDllIntegration.cases).Count -lt 15 -or @($report.actualDllIntegration.cases | Where-Object status -ne 'PASS').Count){throw 'Actual DLL integration incomplete.'}
$candidate=Join-Path $simulationRoot 'build\candidate\src\DirectX11\d3d11.dll'
if((Get-FileHash -LiteralPath $candidate).Hash -ne $report.candidateSha256){throw 'Report does not describe the current candidate DLL.'}
$info=Get-Content -LiteralPath (Join-Path $simulationRoot 'build\business-build-info.json') -Raw | ConvertFrom-Json
foreach($source in $info.sources){if((Get-FileHash -LiteralPath (Join-Path $repositoryRoot $source.path)).Hash -ne $source.sha256){throw "Source changed since validation build: $($source.path)"}}
$progressPath=Join-Path $simulationRoot 'repair-progress.json'
$progress=Get-Content -LiteralPath $progressPath -Raw | ConvertFrom-Json
if(@($progress.completed | Where-Object id -eq $Item).Count){throw 'Item already recorded; do not overwrite its evidence.'}
$evidence=[ordered]@{id=$Item;status='validated-in-simulator';summary=$Summary;utc=[DateTime]::UtcNow.ToString('o');buildHead=$report.buildHead;candidateSha256=$report.candidateSha256;fullReport=[IO.Path]::GetRelativePath($simulationRoot,$reportPath);fullReportSha256=(Get-FileHash -LiteralPath $reportPath).Hash;checks=$report.checks;integrationCases=@($report.actualDllIntegration.cases | Select-Object id,status);targetGameValidated=$false;commitLookup="git log --fixed-strings --grep=$Item"}
if($RedReport){
    $redPath=(Resolve-Path -LiteralPath $RedReport).Path
    $red=Get-Content -LiteralPath $redPath -Raw | ConvertFrom-Json
    if($red.item -ne $Item -or $red.status -eq 'PASS'){throw 'Invalid pre-fix failure evidence.'}
    $evidence.redReport=[IO.Path]::GetRelativePath($simulationRoot,$redPath)
    $evidence.redReportSha256=(Get-FileHash -LiteralPath $redPath).Hash
    $evidence.redChecks=$red.checks
}
$validationRoot=Join-Path $simulationRoot 'validation'
New-Item -ItemType Directory -Path $validationRoot -Force | Out-Null
$evidence | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $validationRoot "$Item.json") -Encoding utf8
$progress.completed=@($progress.completed)+[pscustomobject]@{id=$Item;status='validated-in-simulator';summary=$Summary;evidence="validation/$Item.json";commitLookup="git log --fixed-strings --grep=$Item"}
$progress | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $progressPath -Encoding utf8
Write-Host "Recorded: $Item; next stage only this item and commit it."
