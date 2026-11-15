# Dot-source from PowerShell 7. No deployment, source changes or game launch.
function Initialize-SimulationToolchain {
    if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'PowerShell 7 or newer is required.' }
    $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswherePath)) { throw 'vswhere.exe was not found.' }
    $installationPath = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installationPath) { throw 'MSVC x64 tools were not found.' }
    $vcvarsPath = Join-Path $installationPath 'VC\Auxiliary\Build\vcvars64.bat'
    $environmentCommand = 'call "{0}" > nul && set' -f $vcvarsPath
    $compilerEnvironment = & $env:ComSpec /d /c $environmentCommand
    if ($LASTEXITCODE -ne 0) { throw 'Failed to initialize MSVC x64 environment.' }
    foreach ($line in $compilerEnvironment) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }
    $toolDirectories = @(
        (Join-Path $installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'),
        (Join-Path $installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'),
        (Join-Path $env:ProgramFiles 'CMake\bin')
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Container }
    $env:Path = ($toolDirectories -join ';') + ';' + $env:Path
    return [pscustomobject]@{
        cmake = (Get-Command cmake -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
        ninja = (Get-Command ninja -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
        compiler = (Get-Command cl -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    }
}
