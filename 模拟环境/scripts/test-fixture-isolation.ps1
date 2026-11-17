[CmdletBinding()]
param([Parameter(Mandatory)][string]$Executable)
$ErrorActionPreference='Stop'
$root=Join-Path (Split-Path -Parent $Executable) 'fixture-isolation'
$null=New-Item -ItemType Directory -Path $root
foreach($name in @('dx11_upstream_regression.exe','d3d11.dll','wic-regression.png','clang_rt.asan_dynamic-x86_64.dll')) {
    Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $Executable) $name) -Destination $root
}
$ownedExe=Join-Path $root 'dx11_upstream_regression.exe'
$config=Join-Path $root 'd3dx.ini'
[IO.File]::WriteAllText($config,'F202 caller-owned configuration: must remain byte-identical')
$before=(Get-FileHash -LiteralPath $config).Hash
$children=[Collections.Generic.List[object]]::new()
$observed=[Collections.Generic.List[object]]::new()
function Start-FixtureProcess([string[]]$Arguments,[string]$Label) {
    $info=[Diagnostics.ProcessStartInfo]::new()
    $info.FileName=$ownedExe; $info.WorkingDirectory=$root; $info.UseShellExecute=$false; $info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
    $info.StandardOutputEncoding=[Text.Encoding]::UTF8; $info.StandardErrorEncoding=[Text.Encoding]::UTF8
    foreach($argument in $Arguments){$info.ArgumentList.Add($argument)}
    $process=[Diagnostics.Process]::new(); $process.StartInfo=$info
    if(-not $process.Start()){throw 'F202: failed to start actual regression process'}
    $entry=[pscustomobject]@{process=$process;stdout=$process.StandardOutput.ReadToEndAsync();stderr=$process.StandardError.ReadToEndAsync();label=$Label}
    $children.Add($entry)
    return $entry
}
function Finish-FixtureProcess($Entry,[int]$ExpectedExit) {
    if(-not $Entry.process.WaitForExit(20000)){throw 'F202: actual regression process timed out'}
    if(-not $Entry.stdout.Wait(5000) -or -not $Entry.stderr.Wait(5000)){throw 'F202: child output did not close'}
    $out=$Entry.stdout.GetAwaiter().GetResult(); $err=$Entry.stderr.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $root ($Entry.label+'.stdout.txt')),$out)
    [IO.File]::WriteAllText((Join-Path $root ($Entry.label+'.stderr.txt')),$err)
    $match=[regex]::Match($out,'(?m)^Isolated regression directory: ([^\r\n]+)')
    $path=if($match.Success){$match.Groups[1].Value}else{''}
    $observed.Add([pscustomobject]@{label=$Entry.label;exitCode=$Entry.process.ExitCode;isolatedDirectory=$path;stdout=$out;stderr=$err})
    if($Entry.process.ExitCode -ne $ExpectedExit){throw "F202: $($Entry.label) exit $($Entry.process.ExitCode), expected $ExpectedExit"}
    if(-not $path -or (Test-Path -LiteralPath $path)){throw 'F202: missing isolated directory receipt or incomplete parent cleanup'}
    if((Get-FileHash -LiteralPath $config).Hash -ne $before){throw 'F202: original d3dx.ini was changed'}
    return $path
}
try {
    $one=Start-FixtureProcess @('--audit-item','F-114') 'concurrent-one'
    $two=Start-FixtureProcess @('--audit-item','F-114') 'concurrent-two'
    $onePath=Finish-FixtureProcess $one 0
    $twoPath=Finish-FixtureProcess $two 0
    if($onePath -eq $twoPath){throw 'F202: concurrent invocations shared a fixture directory'}
    foreach($entry in @($one,$two)) {
        if($entry.stdout.GetAwaiter().GetResult() -notmatch 'PASS audit item: F-114'){throw 'F202: actual INI regression did not execute'}
    }
    $failure=Start-FixtureProcess @('--fixture-exit-test') 'explicit-exit'
    $null=Finish-FixtureProcess $failure 37
    if($failure.stderr.GetAwaiter().GetResult() -notmatch 'F202: exit with a live owned d3dx.ini'){throw 'F202: abrupt fixture exit did not execute'}
    Write-Host 'PASS audit item: F-202'
} finally {
    foreach($entry in $children) {
        if(-not $entry.process.HasExited){$entry.process.Kill($true);$null=$entry.process.WaitForExit(5000)}
        $entry.process.Dispose()
    }
    $observed | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'results.json') -Encoding utf8
}
