# Verify that the process controller rejects failures and actually kills timeouts.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'process.ps1')
$simulationRoot = Split-Path -Parent $PSScriptRoot
$runRoot = Join-Path $simulationRoot ('build\runner-self-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runRoot | Out-Null
$shell = (Get-Process -Id $PID).Path
$failure = Invoke-IsolatedSimulationProcess -Executable $shell -Arguments @('-NoProfile','-Command','exit 5') -WorkingDirectory $runRoot -LogPrefix (Join-Path $runRoot 'failure')
if ($failure.status -ne 'FAIL' -or $failure.exitCode -ne 5) { throw 'Controller accepted or misreported a failing child.' }
$timeout = Invoke-IsolatedSimulationProcess -Executable $shell -Arguments @('-NoProfile','-Command','Start-Sleep -Seconds 30') -WorkingDirectory $runRoot -LogPrefix (Join-Path $runRoot 'timeout') -TimeoutMs 500
if ($timeout.status -ne 'TIMEOUT') { throw 'Controller failed to report timeout.' }
$launchFailure = Invoke-IsolatedSimulationProcess -Executable (Join-Path $runRoot 'missing.exe') -Arguments @('unused') -WorkingDirectory $runRoot -LogPrefix (Join-Path $runRoot 'launch-failure')
if ($launchFailure.status -ne 'HARNESS_ERROR') { throw 'Controller failed to report a launch error.' }
@{failure=$failure;timeout=$timeout;launchFailure=$launchFailure} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runRoot 'results.json') -Encoding utf8
Write-Host "PASS: nonzero exit, timeout and launch-error rejection; $runRoot"
