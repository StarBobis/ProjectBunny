param([switch]$Hardware)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'build\immutable-upload-test'
New-Item -ItemType Directory -Force $out | Out-Null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools not found' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$build = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /O2 /EHsc /W4 "$PSScriptRoot\immutable_upload_test.cpp" /Fo"$out\test.obj" /Fe"$out\test.exe" /link d3d12.lib dxgi.lib
exit /b %errorlevel%
"@
[System.IO.File]::WriteAllText((Join-Path $out 'build.bat'), $build)
& cmd.exe /c (Join-Path $out 'build.bat')
if ($LASTEXITCODE -ne 0) { throw 'Immutable upload test build failed' }
if ($Hardware) {
    & (Join-Path $out 'test.exe') --hardware
} else {
    & (Join-Path $out 'test.exe')
}
if ($LASTEXITCODE -eq 77 -and $Hardware) {
    Write-Host 'Hardware test skipped: no physical GPU adapter visible; no hardware result claimed.'
} elseif ($LASTEXITCODE -ne 0) {
    throw 'Immutable upload test failed'
}
