function Invoke-IsolatedSimulationProcess {
    param(
        [Parameter(Mandatory)][string]$Executable,
        [Parameter(Mandatory)][string[]]$Arguments,
        [Parameter(Mandatory)][string]$WorkingDirectory,
        [Parameter(Mandatory)][string]$LogPrefix,
        [ValidateRange(100, 60000)][int]$TimeoutMs = 20000
    )
    $processInfo = [Diagnostics.ProcessStartInfo]::new()
    $processInfo.FileName = $Executable
    $processInfo.WorkingDirectory = $WorkingDirectory
    $processInfo.UseShellExecute = $false
    $processInfo.CreateNoWindow = $true
    $processInfo.RedirectStandardOutput = $true
    $processInfo.RedirectStandardError = $true
    $processInfo.StandardOutputEncoding = [Text.Encoding]::UTF8
    $processInfo.StandardErrorEncoding = [Text.Encoding]::UTF8
    foreach ($argument in $Arguments) { $processInfo.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $processInfo
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $status = 'HARNESS_ERROR'; $exitCode = $null; $message = ''; $started = $false
    try {
        if (-not $process.Start()) { throw 'Failed to start child process.' }
        $started = $true
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutMs)) {
            $status = 'TIMEOUT'
            $process.Kill($true)
            if (-not $process.WaitForExit(5000)) { throw 'Timed-out child could not be terminated.' }
        } else {
            $exitCode = $process.ExitCode
            $status = if ($exitCode -eq 0) { 'PASS' } else { 'FAIL' }
        }
        if (-not $stdoutTask.Wait(5000) -or -not $stderrTask.Wait(5000)) { throw 'Child output did not close.' }
        $stdoutTask.GetAwaiter().GetResult() | Set-Content -LiteralPath ($LogPrefix + '.stdout.txt') -Encoding utf8
        $stderrTask.GetAwaiter().GetResult() | Set-Content -LiteralPath ($LogPrefix + '.stderr.txt') -Encoding utf8
    } catch {
        $message = $_.Exception.Message
        $status = 'HARNESS_ERROR'
        if ($started -and -not $process.HasExited) { $process.Kill($true); $null = $process.WaitForExit(5000) }
    } finally {
        $watch.Stop()
        $process.Dispose()
    }
    [pscustomobject]@{status=$status; exitCode=$exitCode; elapsedMs=$watch.ElapsedMilliseconds; message=$message}
}
